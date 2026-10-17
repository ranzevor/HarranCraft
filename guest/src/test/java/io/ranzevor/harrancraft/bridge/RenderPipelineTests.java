package io.ranzevor.harrancraft.bridge;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;

public final class RenderPipelineTests {
    private static int checks;
    private static final Frame.Key KEY = new Frame.Key("minecraft:overworld", 0, 80, 0);
    private static final RenderFrame.CameraPose CAMERA =
            new RenderFrame.CameraPose(0.5, 82, -4, 90, -10);

    private static void require(boolean condition, String message) {
        ++checks;
        if (!condition) throw new AssertionError(message);
    }

    private static RenderWire sample(long epoch, long world, long camera,
                                     long frame, long capture) {
        return new RenderWire(RenderWire.SNAPSHOT,
                new RenderFrame.Identity(epoch, world, camera, frame, capture),
                KEY, "minecraft:stone", CAMERA, RenderWireTests.golden().quads());
    }

    private static void retired(RenderPipe pipe) throws InterruptedException {
        long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(5);
        while (!pipe.isTerminated() && System.nanoTime() < deadline) Thread.sleep(2);
        require(pipe.isTerminated(), "Render pipe workers survived shutdown");
    }

    private static final class Memory extends ByteArrayOutputStream {
        final AtomicInteger flushed = new AtomicInteger();

        @Override public synchronized void flush() throws IOException {
            super.flush();
            flushed.incrementAndGet();
        }
    }

    private static void drain(Memory stream, int count) throws InterruptedException {
        long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(5);
        while (stream.flushed.get() < count && System.nanoTime() < deadline) Thread.sleep(2);
        require(stream.flushed.get() == count, "Render stream did not flush all records");
    }

    private static final class Stalled extends OutputStream {
        final CountDownLatch entered = new CountDownLatch(1);
        final CountDownLatch release = new CountDownLatch(1);
        final AtomicBoolean closed = new AtomicBoolean();

        @Override public void write(int value) throws IOException {
            write(new byte[]{(byte) value}, 0, 1);
        }

        @Override public void write(byte[] bytes, int at, int count) throws IOException {
            entered.countDown();
            try { release.await(); }
            catch (InterruptedException error) {
                Thread.currentThread().interrupt();
                throw new IOException("Render write interrupted", error);
            }
            if (closed.get()) throw new IOException("Render stream closed");
        }

        @Override public void close() {
            closed.set(true);
            release.countDown();
        }
    }

    private static final class Faulty extends OutputStream {
        @Override public void write(int value) throws IOException {
            throw new IOException("Render stream write failed");
        }
        @Override public void write(byte[] bytes, int at, int length) throws IOException {
            throw new IOException("Render stream write failed");
        }
    }

    public static void main(String[] args) throws Exception {
        require(args.length == 0, "Unexpected render pipeline arguments");

        Memory stream = new Memory();
        AtomicInteger cancellations = new AtomicInteger();
        RenderCaptureSession capture = new RenderCaptureSession(17, 7, 9,
                "minecraft:overworld", (identity, key) -> new RenderFrame(identity,
                        key, "minecraft:stone", CAMERA, RenderWireTests.golden().quads()));
        RenderPublisher publisher = new RenderPublisher(capture, 17, 7, 9, stream,
                cancellations::incrementAndGet);
        require(publisher.publish(5, 1, KEY), "First render capture was rejected");
        require(publisher.publish(7, 2, KEY), "Second render capture was rejected");
        drain(stream, 2);
        publisher.close();
        publisher.close();
        RenderPipe link = publisher.transport();
        retired(link);
        require(cancellations.get() == 1 && link.cleanupFailure() == null,
                "Render transport cancellation or cleanup incorrect");
        try (var input = new ByteArrayInputStream(stream.toByteArray())) {
            RenderWire first = RenderWire.read(input);
            RenderWire second = RenderWire.read(input);
            require(first.identity().frame() == 5 && first.identity().capture() == 1,
                    "First captured frame identity changed");
            require(second.identity().frame() == 7 && second.identity().capture() == 2,
                    "Second captured frame identity changed");
            require(RenderWire.read(input) == null, "Unexpected render stream bytes");
        }
        require(!publisher.publish(8, 3, KEY), "Stopped publisher accepted a frame");

        RenderPipe wrong = new RenderPipe(18, 7, 9, new ByteArrayOutputStream(), () -> {});
        require(!wrong.offer(sample(18, 7, 10, 1, 1)),
                "Different camera identity was accepted");
        retired(wrong);
        require(wrong.stopReason() == RenderPipe.StopReason.WRONG_IDENTITY,
                "Wrong camera refusal changed");

        RenderPipe skipped = new RenderPipe(19, 7, 9, new ByteArrayOutputStream(), () -> {});
        require(!skipped.offer(sample(19, 7, 9, 2, 2)), "Missing capture was accepted");
        retired(skipped);
        require(skipped.stopReason() == RenderPipe.StopReason.STALE_CAPTURE,
                "Wrong capture refusal changed");

        RenderPipe repeated = new RenderPipe(20, 7, 9, new ByteArrayOutputStream(), () -> {});
        require(repeated.offer(sample(20, 7, 9, 4, 1)), "First capture failed");
        require(!repeated.offer(sample(20, 7, 9, 4, 2)),
                "Duplicate logical frame was accepted");
        retired(repeated);

        RenderPipe owned = new RenderPipe(21, 7, 9, new ByteArrayOutputStream(), () -> {});
        AtomicReference<Throwable> other = new AtomicReference<>();
        Thread thread = Thread.ofPlatform().start(() -> {
            try { owned.offer(sample(21, 7, 9, 1, 1)); }
            catch (Throwable error) { other.set(error); }
        });
        thread.join();
        require(other.get() instanceof IllegalStateException && owned.isOpen(),
                "Non-owner thread changed render pipe state");
        owned.close();
        retired(owned);

        Stalled blocked = new Stalled();
        AtomicInteger blockedCancels = new AtomicInteger();
        RenderPipe full = new RenderPipe(22, 7, 9, blocked, () -> {
            blockedCancels.incrementAndGet();
            blocked.close();
        });
        require(full.offer(sample(22, 7, 9, 1, 1)), "First stalled capture failed");
        require(blocked.entered.await(5, TimeUnit.SECONDS), "Render writer never started");
        for (int i = 2; i <= RenderPipe.QUEUE_CAPACITY + 1; ++i) {
            require(full.offer(sample(22, 7, 9, i, i)), "Render queue filled too early");
        }
        require(!full.offer(sample(22, 7, 9,
                RenderPipe.QUEUE_CAPACITY + 2, RenderPipe.QUEUE_CAPACITY + 2)),
                "Render queue overflow was accepted");
        retired(full);
        require(full.stopReason() == RenderPipe.StopReason.SATURATED
                        && blockedCancels.get() == 1 && full.cleanupFailure() == null,
                "Saturation did not retire render output");

        RenderPipe broken = new RenderPipe(23, 7, 9, new Faulty(), () -> {});
        require(broken.offer(sample(23, 7, 9, 1, 1)), "First faulty record refused");
        long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(5);
        while (broken.isOpen() && System.nanoTime() < deadline) Thread.sleep(2);
        retired(broken);
        require(broken.stopReason() == RenderPipe.StopReason.OUTPUT_FAILED
                        && broken.ioFailure() instanceof IOException,
                "Render output failure was not preserved");

        RenderCaptureSession brokenSource = new RenderCaptureSession(24, 7, 9,
                "minecraft:overworld", (id, key) -> {
                    throw new IllegalStateException("Client camera changed");
                });
        RenderPublisher rejected = new RenderPublisher(brokenSource, 24, 7, 9,
                new ByteArrayOutputStream(), () -> {});
        boolean failed = false;
        try { rejected.publish(1, 1, KEY); }
        catch (IllegalStateException error) { failed = true; }
        require(failed && !rejected.isOpen(), "Failed source left publisher running");
        retired(rejected.transport());

        System.out.println("render-pipeline: passed " + checks + " assertions");
    }
}
