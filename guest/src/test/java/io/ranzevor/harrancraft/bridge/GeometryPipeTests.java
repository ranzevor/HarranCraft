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

public final class GeometryPipeTests {
    private static int assertions;

    private static void require(boolean condition, String message) {
        ++assertions;
        if (!condition) throw new AssertionError(message);
    }

    private static GeometryFrame sample(long epoch, long revision) {
        return GeometryFrame.snapshot(epoch, revision,
                new Frame.Key("minecraft:overworld", 0, 80, 0), "minecraft:stone",
                List.of(new GeometryFrame.Box(0, 0, 0, 1, 1, 1)));
    }

    private static void awaitClosed(GeometryPipe pipe) throws InterruptedException {
        long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(5);
        while (!pipe.isTerminated() && System.nanoTime() < deadline) {
            Thread.sleep(2);
        }
        require(pipe.isTerminated(), "Geometry workers did not terminate");
        require(pipe.cleanupFailure() == null, "Geometry cleanup failed");
    }

    private static void awaitBytes(ByteArrayOutputStream output, int target)
            throws InterruptedException {
        long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(5);
        while (output.size() < target && System.nanoTime() < deadline) {
            Thread.sleep(2);
        }
        require(output.size() == target, "Geometry frames were not fully written");
    }

    private static final class Stalled extends OutputStream {
        final CountDownLatch entered = new CountDownLatch(1);
        final CountDownLatch release = new CountDownLatch(1);
        final AtomicBoolean closed = new AtomicBoolean();

        @Override public void write(int value) throws IOException {
            write(new byte[]{(byte) value}, 0, 1);
        }

        @Override public void write(byte[] value, int off, int length) throws IOException {
            entered.countDown();
            try {
                release.await();
            } catch (InterruptedException error) {
                Thread.currentThread().interrupt();
                throw new IOException("Geometry write interrupted", error);
            }
            if (closed.get()) throw new IOException("Geometry endpoint closed");
        }

        @Override public void close() {
            closed.set(true);
            release.countDown();
        }
    }

    private static final class Faulty extends OutputStream {
        @Override public void write(int value) throws IOException {
            throw new IOException("Geometry output failure");
        }

        @Override public void write(byte[] bytes, int start, int length) throws IOException {
            throw new IOException("Geometry output failure");
        }
    }

    public static void main(String[] args) throws Exception {
        require(args.length == 0, "Unknown geometry pipe argument");
        ByteArrayOutputStream memory = new ByteArrayOutputStream();
        AtomicInteger cancellations = new AtomicInteger();
        GeometryPipe link = new GeometryPipe(17, memory, cancellations::incrementAndGet);
        require(link.nextRevision() == 1, "Unexpected initial revision");
        require(link.offer(sample(17, 1)), "First geometry frame rejected");
        require(link.offer(sample(17, 2)), "Second geometry frame rejected");
        awaitBytes(memory, sample(17, 1).encode().length * 2);
        link.close();
        link.close();
        awaitClosed(link);
        require(cancellations.get() == 1, "Cancellation did not run exactly once");
        try (var input = new ByteArrayInputStream(memory.toByteArray())) {
            require(sample(17, 1).equals(GeometryFrame.read(input)), "First output frame changed");
            require(sample(17, 2).equals(GeometryFrame.read(input)), "Second output frame changed");
            require(GeometryFrame.read(input) == null, "Unexpected extra geometry data");
        }

        GeometryPipe wrong = new GeometryPipe(18, new ByteArrayOutputStream(), () -> {});
        require(!wrong.offer(sample(17, 1)), "Wrong epoch was accepted");
        awaitClosed(wrong);
        require(wrong.stopReason() == GeometryPipe.StopReason.WRONG_EPOCH,
                "Wrong epoch failure was lost");

        GeometryPipe gap = new GeometryPipe(19, new ByteArrayOutputStream(), () -> {});
        require(!gap.offer(sample(19, 2)), "Missing revision was accepted");
        awaitClosed(gap);
        require(gap.stopReason() == GeometryPipe.StopReason.REVISION_INVALID,
                "Revision sequence failure was lost");

        GeometryPipe owner = new GeometryPipe(20, new ByteArrayOutputStream(), () -> {});
        AtomicReference<Throwable> crossThread = new AtomicReference<>();
        Thread foreign = Thread.ofPlatform().start(() -> {
            try { owner.offer(sample(20, 1)); }
            catch (Throwable error) { crossThread.set(error); }
        });
        foreign.join();
        require(crossThread.get() instanceof IllegalStateException,
                "Cross-thread publishing was permitted");
        owner.close();
        awaitClosed(owner);

        Stalled blocked = new Stalled();
        AtomicInteger blockedCancels = new AtomicInteger();
        GeometryPipe full = new GeometryPipe(21, blocked, () -> {
            blockedCancels.incrementAndGet();
            blocked.close();
        });
        require(full.offer(sample(21, 1)), "Failed first blocked geometry send");
        require(blocked.entered.await(5, TimeUnit.SECONDS), "Geometry writer never entered output");
        for (int i = 2; i <= GeometryPipe.QUEUE_CAPACITY + 1; ++i) {
            require(full.offer(sample(21, i)), "Queue filled before its capacity");
        }
        require(!full.offer(sample(21, GeometryPipe.QUEUE_CAPACITY + 2)),
                "Geometry queue overflow was accepted");
        awaitClosed(full);
        require(full.stopReason() == GeometryPipe.StopReason.SATURATED,
                "Queue saturation reason changed");
        require(blockedCancels.get() == 1, "Saturation did not cancel output once");

        GeometryPipe faulty = new GeometryPipe(22, new Faulty(), () -> {});
        require(faulty.offer(sample(22, 1)), "First faulty geometry send was rejected early");
        long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(5);
        while (faulty.isOpen() && System.nanoTime() < deadline) Thread.sleep(2);
        require(!faulty.isOpen(), "Failed stream kept geometry open");
        awaitClosed(faulty);
        require(faulty.stopReason() == GeometryPipe.StopReason.OUTPUT_FAILED,
                "Output failure reason changed");
        require(faulty.ioFailure() instanceof IOException, "Output error was discarded");

        System.out.println("geometry-pipe: passed " + assertions + " assertions");
    }
}
