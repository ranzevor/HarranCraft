package io.ranzevor.harrancraft.bridge;

import java.io.BufferedReader;
import java.io.ByteArrayInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Collection;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;
import java.util.function.BooleanSupplier;

public final class PipeTests {
    private static String executable;
    private static int scenarios;
    private static int assertions;
    private static final Frame HELLO = Frame.control(Frame.HELLO, 0, 0, Frame.NATIVE);

    private PipeTests() {}

    private static void check(boolean condition, String message) {
        ++assertions;
        if (!condition) throw new AssertionError(message);
    }

    private static void until(BooleanSupplier condition, String message) throws InterruptedException {
        long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(5);
        while (!condition.getAsBoolean() && System.nanoTime() < deadline) Thread.sleep(1);
        check(condition.getAsBoolean(), message);
    }

    private static Frame receive(PipeLink link) throws InterruptedException {
        AtomicReference<Frame> received = new AtomicReference<>();
        until(() -> {
            if (received.get() == null) received.set(link.poll());
            return received.get() != null;
        }, "Complete frame did not arrive");
        return received.get();
    }

    private static final class Peer implements AutoCloseable {
        final Process process;
        final PipeLink link;
        final Thread evidence;
        final CountDownLatch ready = new CountDownLatch(1);
        final AtomicInteger cancellations = new AtomicInteger();
        final AtomicReference<Throwable> evidenceFailure = new AtomicReference<>();

        Peer(String scenario) throws IOException {
            process = new ProcessBuilder(executable, scenario).start();
            evidence = Thread.ofPlatform().daemon().name("harrancraft-pipe-test-evidence").start(() -> {
                try (BufferedReader lines = new BufferedReader(new InputStreamReader(
                        process.getErrorStream(), StandardCharsets.UTF_8))) {
                    String line;
                    int count = 0;
                    while ((line = lines.readLine()) != null) {
                        if (++count > 8 || line.length() > 256) throw new IOException("Unexpected peer diagnostic volume");
                        if (line.equals("ready")) ready.countDown();
                        else throw new IOException("Unexpected peer diagnostic: " + line);
                    }
                } catch (IOException error) {
                    if (process.isAlive()) evidenceFailure.set(error);
                }
            });
            link = new PipeLink(process.getInputStream(), process.getOutputStream(), () -> {
                cancellations.incrementAndGet();
                process.toHandle().destroyForcibly();
            });
        }

        void awaitReady() throws InterruptedException {
            check(ready.await(5, TimeUnit.SECONDS), "Native pipe peer did not initialize");
        }

        void terminated() throws InterruptedException {
            until(link::isTerminated, "Pipe workers did not terminate");
            check(process.waitFor(5, TimeUnit.SECONDS), "Cancelled child did not terminate");
            check(!link.isOpen(), "Terminated link remained open");
            check(link.poll() == null, "Closed link returned a queued frame");
            check(!link.send(HELLO), "Closed link accepted a new frame");
            check(link.outputCapacity() == PipeLink.QUEUE_CAPACITY, "Closed link retained queued output");
            check(cancellations.get() == 1, "Cancellation did not run exactly once");
            evidence.join(5000);
            check(!evidence.isAlive(), "Peer evidence thread survived teardown");
            check(evidenceFailure.get() == null, "Peer evidence failed");
        }

        @Override public void close() {
            link.close();
            try { terminated(); }
            catch (InterruptedException error) {
                Thread.currentThread().interrupt();
                throw new AssertionError("Interrupted during pipe teardown", error);
            } finally {
                process.toHandle().destroyForcibly();
                boolean interrupted = Thread.interrupted();
                try {
                    if (!process.waitFor(5, TimeUnit.SECONDS)) throw new AssertionError("Child survived fallback cleanup");
                    evidence.join(5000);
                    if (evidence.isAlive()) throw new AssertionError("Evidence survived fallback cleanup");
                } catch (InterruptedException error) {
                    interrupted = true;
                    throw new AssertionError("Fallback cleanup interrupted", error);
                } finally {
                    if (interrupted) Thread.currentThread().interrupt();
                }
            }
        }
    }

    private static void fragment() throws Exception {
        try (Peer peer = new Peer("fragment")) {
            peer.awaitReady();
            check(receive(peer.link).equals(HELLO), "Fragmented HELLO changed");
            Frame message = new Frame(Frame.SET, 73, 19, Frame.NATIVE, Frame.OK,
                    new Frame.Key("minecraft:overworld", -17, 80, 33),
                    "minecraft:air", "minecraft:oak_log[axis=x]", 0);
            check(peer.link.send(message), "Echo request refused");
            check(receive(peer.link).equals(message), "Fragmented echo changed wire fields");
        }
    }

    private static void inputFailure(String scenario, PipeLink.StopReason expected) throws Exception {
        try (Peer peer = new Peer(scenario)) {
            peer.awaitReady();
            until(() -> !peer.link.isOpen(), "Input failure did not invalidate transport");
            check(peer.link.stopReason() == expected, "Incorrect input failure reason");
            if (scenario.equals("partial")) {
                check(peer.link.ioFailure() instanceof IOException, "Partial frame lost its exception");
                check(peer.link.ioFailure().getMessage().equals("Truncated wire frame"), "Truncation was misclassified");
            }
            peer.terminated();
        }
    }

    private static void outputFailure(boolean saturation) throws Exception {
        try (Peer peer = new Peer(saturation ? "idle" : "broken")) {
            peer.awaitReady();
            if (saturation) {
                int submitted = 0;
                while (submitted < 4096 && peer.link.send(HELLO)) ++submitted;
                check(submitted < 4096, "Non-reading peer did not trigger bounded backpressure");
                check(peer.link.stopReason() == PipeLink.StopReason.OUTPUT_SATURATED,
                        "Output saturation was misclassified");
            } else {
                check(peer.link.send(HELLO), "Initial write was not queued");
                until(() -> !peer.link.isOpen(), "Broken output was not detected");
                check(peer.link.stopReason() == PipeLink.StopReason.OUTPUT_FAILED, "Broken pipe was misclassified");
                check(peer.link.ioFailure() instanceof IOException, "Broken pipe lost its original exception");
            }
            peer.terminated();
        }
    }

    private static void idleClose() throws Exception {
        try (Peer peer = new Peer("idle")) {
            peer.awaitReady();
            check(receive(peer.link).equals(HELLO), "Idle peer handshake missing");
            long start = System.nanoTime();
            peer.link.close();
            long elapsed = System.nanoTime() - start;
            check(elapsed < TimeUnit.SECONDS.toNanos(1), "Close blocked on native I/O");
            check(peer.link.stopReason() == PipeLink.StopReason.CLOSED, "Explicit close reason changed");
            peer.terminated();
            check(peer.link.cleanupFailure() == null, "Idle stream cleanup failed");
        }
    }

    private static void partialTimeout() throws Exception {
        try (Peer peer = new Peer("trickle")) {
            peer.awaitReady();
            BlockWorld untouched = new BlockWorld() {
                @Override public Outcome read(Frame.Key key) { throw new AssertionError("Partial frame accessed the world"); }
                @Override public Outcome compareAndSet(Frame.Key key, String expected, String state) {
                    throw new AssertionError("Partial frame mutated the world");
                }
            };
            Session session = new Session(73, untouched, peer.link, 0);
            session.tick(1, true);
            check(session.isOpen() && !session.isWelcomed(), "Partial frame became a handshake");
            session.tick(Session.TIMEOUT_NANOS + 1, true);
            check(!session.isOpen() && session.watchedCount() == 0, "Timeout retained session state");
            peer.terminated();
        }
    }

    private static void concurrentClose() throws Exception {
        try (Peer peer = new Peer("idle")) {
            peer.awaitReady();
            CountDownLatch start = new CountDownLatch(1);
            AtomicReference<Throwable> failed = new AtomicReference<>();
            ArrayList<Thread> producers = new ArrayList<>();
            for (int i = 0; i < 4; ++i) {
                Thread thread = Thread.ofPlatform().daemon().start(() -> {
                    try {
                        start.await();
                        for (int n = 0; n < 1024 && peer.link.send(HELLO); ++n) { }
                    } catch (Throwable error) { failed.compareAndSet(null, error); }
                });
                producers.add(thread);
            }
            start.countDown();
            peer.link.close();
            for (Thread thread : producers) {
                thread.join(5000);
                check(!thread.isAlive(), "Producer survived close");
            }
            check(failed.get() == null, "Concurrent send failed unexpectedly");
            peer.terminated();
        }
    }

    private static void cleanupFailure() throws Exception {
        AtomicInteger cancellations = new AtomicInteger();
        OutputStream output = new OutputStream() {
            @Override public void write(int value) { }
            @Override public void close() throws IOException { throw new IOException("close failure"); }
        };
        PipeLink link = new PipeLink(new ByteArrayInputStream(new byte[0]), output, () -> {
            cancellations.incrementAndGet();
            throw new IllegalStateException("cancel failure");
        });
        until(link::isTerminated, "Failing cleanup workers did not terminate");
        link.close();
        Throwable failure = link.cleanupFailure();
        check(failure != null && failure.getSuppressed().length == 1, "Cleanup failures were discarded");
        check(cancellations.get() == 1, "Failure retried cancellation");
        check(link.stopReason() == PipeLink.StopReason.INPUT_ENDED, "Cleanup overwrote the first stop reason");
    }

    private static void lateRead() throws Exception {
        CountDownLatch reading = new CountDownLatch(1);
        CountDownLatch release = new CountDownLatch(1);
        InputStream input = new InputStream() {
            @Override public int read() { return -1; }
            @Override public byte[] readNBytes(int size) {
                reading.countDown();
                boolean interrupted = false;
                for (;;) {
                    try { release.await(); break; }
                    catch (InterruptedException error) { interrupted = true; }
                }
                if (interrupted) Thread.currentThread().interrupt();
                return HELLO.encode();
            }
        };
        PipeLink link = new PipeLink(input, OutputStream.nullOutputStream());
        try {
            check(reading.await(5, TimeUnit.SECONDS), "Controlled read did not start");
            link.close();
            check(!link.isTerminated(), "Blocked read was reported terminated");
        } finally {
            release.countDown();
            link.close();
        }
        until(link::isTerminated, "Released read did not terminate");
        var field = PipeLink.class.getDeclaredField("incoming");
        field.setAccessible(true);
        check(((Collection<?>) field.get(link)).isEmpty(), "Late read retained a frame after invalidation");
    }

    private static void pendingCancellation() throws Exception {
        CountDownLatch cancelling = new CountDownLatch(1);
        CountDownLatch release = new CountDownLatch(1);
        PipeLink link = new PipeLink(new ByteArrayInputStream(new byte[0]), OutputStream.nullOutputStream(), () -> {
            cancelling.countDown();
            try { release.await(); }
            catch (InterruptedException error) { Thread.currentThread().interrupt(); }
        });
        try {
            check(cancelling.await(5, TimeUnit.SECONDS), "Cancellation did not start");
            check(!link.isOpen() && !link.isTerminated(), "Pending cancellation was reported complete");
        } finally { release.countDown(); link.close(); }
        until(link::isTerminated, "Released cancellation did not terminate");
    }

    private static long descriptors() throws IOException {
        Path path = Path.of("/proc/self/fd");
        if (!Files.isDirectory(path)) return -1;
        try (var files = Files.list(path)) { return files.count(); }
    }

    private static long pipeThreads() {
        return Thread.getAllStackTraces().keySet().stream()
                .filter(thread -> thread.isAlive() && thread.getName().startsWith("harrancraft-pipe-")).count();
    }

    private static void reconnects() throws Exception {
        idleClose();
        long descriptorsBefore = descriptors();
        long threadsBefore = pipeThreads();
        PipeLink retired = null;
        for (int i = 0; i < 32; ++i) {
            try (Peer peer = new Peer("idle")) {
                peer.awaitReady();
                check(receive(peer.link).equals(HELLO), "New pipe handshake failed");
                if (retired != null) {
                    check(retired.isTerminated() && !retired.send(HELLO), "Retired pipe accepted a new-session frame");
                    check(retired.poll() == null, "Retired pipe replayed a frame");
                }
                retired = peer.link;
            }
        }
        long descriptorsAfter = descriptors();
        long threadsAfter = pipeThreads();
        check(threadsBefore == 0 && threadsAfter == 0, "Pipe thread count grew across reconnects");
        if (descriptorsBefore >= 0) {
            check(descriptorsAfter == descriptorsBefore, "File descriptors grew across reconnects");
            System.out.println("pipe-resources: cycles=32 fd_before=" + descriptorsBefore
                    + " fd_after=" + descriptorsAfter + " workers_after=" + threadsAfter);
        } else {
            System.out.println("pipe-resources: cycles=32 workers_after=" + threadsAfter + " fd_measurement=not_available");
        }
    }

    @FunctionalInterface private interface Scenario { void run() throws Exception; }
    private static void run(String name, Scenario scenario) throws Exception {
        long before = descriptors();
        scenario.run();
        check(pipeThreads() == 0, "Pipe workers survived scenario teardown");
        if (before >= 0) check(descriptors() == before, "Scenario leaked a file descriptor");
        ++scenarios;
        System.out.println("pipe-scenario: " + name + " passed");
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 1) throw new IllegalArgumentException("Expected native pipe peer executable");
        executable = args[0];
        idleClose();
        run("fragmented-frames", PipeTests::fragment);
        run("partial-eof", () -> inputFailure("partial", PipeLink.StopReason.INPUT_FAILED));
        run("empty-eof", () -> inputFailure("eof", PipeLink.StopReason.INPUT_ENDED));
        run("input-saturation", () -> inputFailure("flood", PipeLink.StopReason.INPUT_SATURATED));
        run("output-saturation", () -> outputFailure(true));
        run("broken-output", () -> outputFailure(false));
        run("idle-read-cancellation", PipeTests::idleClose);
        run("partial-frame-timeout", PipeTests::partialTimeout);
        run("concurrent-send-close", PipeTests::concurrentClose);
        run("cleanup-failure-reporting", PipeTests::cleanupFailure);
        run("late-read-after-close", PipeTests::lateRead);
        run("pending-cancellation", PipeTests::pendingCancellation);
        run("repeated-pipe-retirement", PipeTests::reconnects);
        System.out.println("native-pipe-faults: " + scenarios + " scenarios passed, " + assertions + " assertions");
    }
}
