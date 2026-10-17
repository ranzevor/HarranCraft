package io.ranzevor.harrancraft.bridge;

import java.io.ByteArrayInputStream;
import java.io.IOException;
import java.io.InputStream;
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
    private static final int DIAGNOSTIC_CLOSE_FAILURE = 86;
    private static final Frame HELLO = Frame.control(Frame.HELLO, 0, 0, Frame.NATIVE);

    private PipeTests() {}

    private static void check(boolean condition, String message) {
        ++assertions;
        if (!condition) throw new AssertionError(message);
    }

    private static void requireNoFailure(Throwable failure, String message) {
        ++assertions;
        if (failure != null) throw new AssertionError(message, failure);
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

    private static final class Evidence {
        private static final int MAX_LINE_BYTES = 256;
        private static final int MAX_LINES = 8;
        final CountDownLatch ready = new CountDownLatch(1);
        final CountDownLatch finished = new CountDownLatch(1);
        final AtomicReference<Throwable> failure = new AtomicReference<>();
        final boolean flood;
        volatile boolean floodOutputClosed;
        volatile boolean terminal;
        volatile boolean eof;

        Evidence(boolean flood) { this.flood = flood; }

        private void record(Throwable error) {
            if (!failure.compareAndSet(null, error) && failure.get() != error) failure.get().addSuppressed(error);
        }

        private void line(String value) {
            if (value.equals("ready") && ready.getCount() == 1 && !terminal) {
                ready.countDown();
            } else if (value.equals("flood-output-closed") && flood && ready.getCount() == 0
                    && !floodOutputClosed && !terminal) {
                floodOutputClosed = true;
            } else if (value.equals("complete") && ready.getCount() == 0 && !terminal
                    && (!flood || floodOutputClosed)) {
                terminal = true;
            } else {
                record(new IOException("Unexpected peer diagnostic: " + value));
            }
        }

        void read(InputStream input) {
            try (input) {
                byte[] buffer = new byte[MAX_LINE_BYTES];
                int size = 0;
                int lines = 0;
                for (int value; (value = input.read()) != -1;) {
                    if (lines == MAX_LINES) throw new IOException("Peer diagnostic exceeded 8 lines");
                    if (value == '\n') {
                        line(new String(buffer, 0, size, StandardCharsets.US_ASCII));
                        ++lines;
                        size = 0;
                    } else {
                        if (size == buffer.length) throw new IOException("Peer diagnostic exceeded 256 bytes");
                        buffer[size++] = (byte) value;
                    }
                }
                eof = true;
                if (size != 0) throw new IOException("Truncated peer diagnostic");
                if (!terminal) throw new IOException("Missing terminal peer diagnostic");
            } catch (IOException | RuntimeException error) {
                record(error);
            } finally {
                finished.countDown();
            }
        }

        void requireComplete() {
            requireNoFailure(failure.get(), "Peer evidence failed");
            check(terminal && eof, "Peer evidence did not finish with terminal and EOF");
            check(!flood || floodOutputClosed, "Flood did not acknowledge closed output");
        }
    }

    private static final class EofGate {
        final CountDownLatch captured = new CountDownLatch(1);
        final CountDownLatch release = new CountDownLatch(1);

        InputStream wrap(InputStream input) {
            return new InputStream() {
                @Override public int read() throws IOException {
                    int value = input.read();
                    if (value == -1) {
                        captured.countDown();
                        try {
                            if (!release.await(5, TimeUnit.SECONDS)) throw new IOException("Controlled EOF was not released");
                        } catch (InterruptedException error) {
                            Thread.currentThread().interrupt();
                            throw new IOException("Interrupted while holding controlled EOF", error);
                        }
                    }
                    return value;
                }
                @Override public void close() throws IOException { input.close(); }
            };
        }
    }

    private static final class RecordedOutput extends OutputStream {
        final OutputStream delegate;
        final AtomicReference<Throwable> ioFailure = new AtomicReference<>();
        final AtomicReference<Throwable> closeFailure = new AtomicReference<>();

        RecordedOutput(OutputStream delegate) { this.delegate = delegate; }

        @Override public void write(int value) throws IOException {
            try { delegate.write(value); }
            catch (IOException | RuntimeException error) { ioFailure.compareAndSet(null, error); throw error; }
        }
        @Override public void write(byte[] bytes, int offset, int length) throws IOException {
            try { delegate.write(bytes, offset, length); }
            catch (IOException | RuntimeException error) { ioFailure.compareAndSet(null, error); throw error; }
        }
        @Override public void flush() throws IOException {
            try { delegate.flush(); }
            catch (IOException | RuntimeException error) { ioFailure.compareAndSet(null, error); throw error; }
        }
        @Override public void close() throws IOException {
            try { delegate.close(); }
            catch (IOException | RuntimeException error) { closeFailure.compareAndSet(null, error); throw error; }
        }
    }

    private static final class Peer implements AutoCloseable {
        final Process process;
        final PipeLink link;
        final Thread evidence;
        final AtomicInteger cancellations = new AtomicInteger();
        final AtomicInteger evidenceBeforeCancellation = new AtomicInteger();
        final AtomicReference<Throwable> exitFailure = new AtomicReference<>();
        final Evidence diagnostics;
        final RecordedOutput brokenOutput;

        Peer(String scenario) throws IOException { this(scenario, null); }

        Peer(String scenario, EofGate gate) throws IOException {
            process = new ProcessBuilder(executable, scenario).start();
            boolean closeFailure = scenario.equals("flood-close-failure");
            diagnostics = new Evidence(scenario.equals("flood") || scenario.equals("flood-diagnostic") || closeFailure);
            InputStream input = gate == null ? process.getErrorStream() : gate.wrap(process.getErrorStream());
            evidence = Thread.ofPlatform().daemon().name("harrancraft-pipe-test-evidence")
                    .start(() -> diagnostics.read(input));
            brokenOutput = scenario.equals("broken") ? new RecordedOutput(process.getOutputStream()) : null;
            OutputStream output = brokenOutput == null ? process.getOutputStream() : brokenOutput;
            link = new PipeLink(process.getInputStream(), output, () -> {
                cancellations.incrementAndGet();
                try {
                    // Flood must observe closeInput before its terminal evidence can precede the kill.
                    if (!diagnostics.finished.await(3, TimeUnit.SECONDS))
                        throw new IllegalStateException("Peer evidence did not finish before cancellation");
                    if (diagnostics.terminal && diagnostics.eof) evidenceBeforeCancellation.incrementAndGet();
                    if (closeFailure && !process.waitFor(1, TimeUnit.SECONDS))
                        throw new IllegalStateException("Diagnostic close-failure fixture did not exit");
                    if (!process.isAlive()) recordExit(process.exitValue());
                } catch (InterruptedException error) {
                    Thread.currentThread().interrupt();
                    throw new IllegalStateException("Interrupted while awaiting peer evidence", error);
                } finally {
                    process.toHandle().destroyForcibly();
                }
            });
        }

        private void recordExit(int value) {
            String message = value == DIAGNOSTIC_CLOSE_FAILURE
                    ? "Native diagnostic close failed (exit=86)"
                    : "Native peer exited before cancellation (exit=" + value + ")";
            exitFailure.compareAndSet(null, new IOException(message));
        }

        private boolean expectedBrokenClose(Throwable error) {
            // Closing this deliberately broken buffered output can retry its failed flush.
            return brokenOutput != null && error instanceof IOException && error == brokenOutput.closeFailure.get()
                    && error.getSuppressed().length == 0 && link.stopReason() == PipeLink.StopReason.OUTPUT_FAILED
                    && link.ioFailure() instanceof IOException && link.ioFailure() == brokenOutput.ioFailure.get();
        }

        private void requireHealthy() {
            Throwable cleanup = link.cleanupFailure();
            Throwable[] failures = {diagnostics.failure.get(), expectedBrokenClose(cleanup) ? null : cleanup, exitFailure.get()};
            String[] messages = {"Peer evidence failed", "Pipe resource cleanup failed", "Native peer failed"};
            AssertionError combined = null;
            for (int i = 0; i < failures.length; ++i) {
                if (failures[i] == null) continue;
                AssertionError detail = new AssertionError(messages[i], failures[i]);
                if (combined == null) combined = detail;
                else combined.addSuppressed(detail);
            }
            ++assertions;
            if (combined != null) throw combined;
            diagnostics.requireComplete();
        }

        void awaitReady() throws InterruptedException {
            check(diagnostics.ready.await(5, TimeUnit.SECONDS), "Native pipe peer did not initialize");
        }

        void terminated() throws InterruptedException {
            until(link::isTerminated, "Pipe workers did not terminate");
            check(process.waitFor(5, TimeUnit.SECONDS), "Cancelled child did not terminate");
            if (process.exitValue() == DIAGNOSTIC_CLOSE_FAILURE) recordExit(process.exitValue());
            check(!link.isOpen(), "Terminated link remained open");
            check(link.poll() == null, "Closed link returned a queued frame");
            check(!link.send(HELLO), "Closed link accepted a new frame");
            check(link.outputCapacity() == PipeLink.QUEUE_CAPACITY, "Closed link retained queued output");
            check(cancellations.get() == 1, "Cancellation did not run exactly once");
            evidence.join(5000);
            check(!evidence.isAlive(), "Peer evidence thread survived teardown");
            requireHealthy();
            check(evidenceBeforeCancellation.get() == 1, "Peer was cancelled before terminal evidence and EOF");
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
            } else {
                requireNoFailure(peer.link.ioFailure(), "Expected input shutdown recorded an I/O failure");
            }
            peer.terminated();
        }
    }

    private static void unexpectedFloodDiagnostic() throws Exception {
        Peer peer = new Peer("flood-diagnostic");
        AssertionError rejected = null;
        try (peer) {
            peer.awaitReady();
            until(() -> !peer.link.isOpen(), "Diagnostic flood did not saturate input");
            check(peer.link.stopReason() == PipeLink.StopReason.INPUT_SATURATED,
                    "Diagnostic flood changed the cancellation reason");
            peer.terminated();
        } catch (AssertionError error) {
            rejected = error;
        }
        check(rejected != null && rejected.getMessage().equals("Peer evidence failed"),
                "Unexpected flood diagnostic did not fail evidence validation");
        Throwable original = peer.diagnostics.failure.get();
        check(original instanceof IOException
                        && original.getMessage().equals("Unexpected peer diagnostic: unexpected-flood-diagnostic"),
                "Unexpected flood diagnostic lost its original text");
        check(rejected.getCause() == original, "Unexpected peer diagnostic lost its exception cause");
        for (Throwable suppressed : rejected.getSuppressed()) {
            check(suppressed instanceof AssertionError && suppressed.getCause() == original,
                    "An unrelated teardown failure was hidden by expected evidence rejection");
        }
        check(peer.diagnostics.terminal && peer.diagnostics.eof && peer.diagnostics.floodOutputClosed,
                "Rejected diagnostics did not drain the explicit flood cancellation acknowledgement");
        check(peer.evidenceBeforeCancellation.get() == 1, "Invalid diagnostic bypassed terminal evidence draining");
        check(!peer.process.isAlive() && peer.link.isTerminated(), "Rejected evidence retained its process or workers");
        requireNoFailure(peer.link.cleanupFailure(), "Rejected evidence prevented successful resource cleanup");
    }

    private static void diagnosticCloseFailure() throws Exception {
        Peer peer = new Peer("flood-close-failure");
        AssertionError rejected = null;
        try (peer) {
            peer.awaitReady();
            until(() -> !peer.link.isOpen(), "Close-failure flood did not saturate input");
            check(peer.link.stopReason() == PipeLink.StopReason.INPUT_SATURATED,
                    "Close-failure flood changed the stop reason");
            peer.terminated();
        } catch (AssertionError error) {
            rejected = error;
        }
        check(rejected != null && rejected.getMessage().equals("Native peer failed"),
                "Terminal evidence hid a native diagnostic-close failure");
        Throwable original = peer.exitFailure.get();
        check(original instanceof IOException && original.getMessage().equals("Native diagnostic close failed (exit=86)"),
                "Native diagnostic-close failure lost its exit status");
        check(rejected.getCause() == original, "Native close failure lost its exception cause");
        for (Throwable suppressed : rejected.getSuppressed()) {
            check(suppressed instanceof AssertionError && suppressed.getCause() == original,
                    "An unrelated teardown failure was hidden by native close rejection");
        }
        peer.diagnostics.requireComplete();
        check(peer.evidenceBeforeCancellation.get() == 1 && peer.process.exitValue() == DIAGNOSTIC_CLOSE_FAILURE,
                "Close-failure fixture was killed before its real exit evidence");
        check(!peer.process.isAlive() && peer.link.isTerminated(), "Native close failure retained its process or workers");
        requireNoFailure(peer.link.cleanupFailure(), "Native close failure prevented resource cleanup");
    }

    private static void evidenceCompletionTimeout() throws Exception {
        EofGate gate = new EofGate();
        Peer peer = new Peer("idle", gate);
        AssertionError rejected = null;
        try (peer) {
            try {
                peer.awaitReady();
                check(receive(peer.link).equals(HELLO), "EOF-gated peer handshake changed");
                check(gate.captured.await(5, TimeUnit.SECONDS), "Native diagnostic EOF was not captured");
                check(peer.diagnostics.terminal && !peer.diagnostics.eof && peer.process.isAlive(),
                        "EOF gate did not hold delivery while the native peer remained alive");
                peer.link.close();
                until(() -> !peer.process.isAlive(), "Evidence timeout did not forcibly terminate the owned peer");
                until(peer.link::isTerminated, "Evidence timeout retained pipe workers");
                check(peer.diagnostics.finished.getCount() == 1 && peer.evidenceBeforeCancellation.get() == 0,
                        "Withheld EOF was reported as completed cancellation evidence");
            } finally {
                gate.release.countDown();
            }
            peer.terminated();
        } catch (AssertionError error) {
            rejected = error;
        }
        check(rejected != null && rejected.getMessage().equals("Pipe resource cleanup failed"),
                "Evidence completion timeout was not retained as cleanup failure");
        Throwable original = peer.link.cleanupFailure();
        check(original instanceof IllegalStateException
                        && original.getMessage().equals("Peer evidence did not finish before cancellation"),
                "Evidence timeout lost its precise failure cause");
        check(rejected.getCause() == original && original.getSuppressed().length == 0,
                "Evidence timeout was replaced or hid another cleanup failure");
        for (Throwable suppressed : rejected.getSuppressed()) {
            check(suppressed instanceof AssertionError && suppressed.getCause() == original,
                    "An unrelated failure was hidden by the expected evidence timeout");
        }
        peer.diagnostics.requireComplete();
        check(peer.cancellations.get() == 1 && peer.evidenceBeforeCancellation.get() == 0,
                "Evidence timeout retried cancellation or fabricated an EOF acknowledgement");
        check(!peer.process.isAlive() && peer.link.isTerminated() && !peer.evidence.isAlive(),
                "Timed-out evidence retained the process or a worker");
        requireNoFailure(peer.exitFailure.get(), "Evidence timeout masked an unexpected native exit");
    }

    private static Throwable rejectedEvidence(Evidence evidence) {
        AssertionError rejected = null;
        try { evidence.requireComplete(); }
        catch (AssertionError error) { rejected = error; }
        check(rejected != null && rejected.getMessage().equals("Peer evidence failed"),
                "Invalid diagnostic evidence was accepted");
        check(rejected.getCause() == evidence.failure.get(), "Evidence validation discarded its original cause");
        check(evidence.finished.getCount() == 0, "Evidence parsing did not release its completion latch");
        return rejected.getCause();
    }

    private static void evidenceFailures() {
        Evidence early = new Evidence(false);
        early.read(new ByteArrayInputStream("ready\n".getBytes(StandardCharsets.US_ASCII)));
        check(rejectedEvidence(early).getMessage().equals("Missing terminal peer diagnostic"),
                "Early diagnostic EOF was misclassified");
        check(early.eof && !early.terminal, "Early EOF was marked terminal");

        Evidence oversized = new Evidence(false);
        oversized.read(new ByteArrayInputStream("x".repeat(257).getBytes(StandardCharsets.US_ASCII)));
        check(rejectedEvidence(oversized).getMessage().equals("Peer diagnostic exceeded 256 bytes"),
                "Oversized diagnostic was not rejected at the byte bound");
        check(!oversized.eof && !oversized.terminal, "Rejected oversize was marked complete");

        Evidence excessLines = new Evidence(false);
        excessLines.read(new ByteArrayInputStream(("ready\ncomplete\n" + "extra\n".repeat(7))
                .getBytes(StandardCharsets.US_ASCII)));
        Throwable extra = rejectedEvidence(excessLines);
        check(extra.getMessage().equals("Unexpected peer diagnostic: extra"),
                "Post-terminal diagnostic was ignored");
        Throwable[] suppressed = extra.getSuppressed();
        check(suppressed.length == 6
                        && suppressed[5].getMessage().equals("Peer diagnostic exceeded 8 lines"),
                "Diagnostic line count was not bounded or an earlier error was discarded");
        check(!excessLines.eof, "Diagnostic volume rejection was mistaken for EOF");

        Evidence missingFlood = new Evidence(true);
        missingFlood.read(new ByteArrayInputStream("ready\ncomplete\n".getBytes(StandardCharsets.US_ASCII)));
        check(rejectedEvidence(missingFlood).getMessage().equals("Unexpected peer diagnostic: complete"),
                "Terminal diagnostic bypassed the flood output-close acknowledgement");

        IOException readFailure = new IOException("Injected evidence read failure");
        IOException closeFailure = new IOException("Injected evidence close failure");
        Evidence failedIo = new Evidence(false);
        failedIo.read(new InputStream() {
            @Override public int read() throws IOException { throw readFailure; }
            @Override public void close() throws IOException { throw closeFailure; }
        });
        check(rejectedEvidence(failedIo) == readFailure, "Diagnostic read failure was replaced");
        check(readFailure.getSuppressed().length == 1 && readFailure.getSuppressed()[0] == closeFailure,
                "Diagnostic stream close failure was lost");
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
            if (!saturation) {
                check(peer.link.ioFailure() == peer.brokenOutput.ioFailure.get(),
                        "Broken output discarded the actual write or flush exception");
                check(peer.link.cleanupFailure() == peer.brokenOutput.closeFailure.get(),
                        "Broken output discarded or misattributed its retained close exception");
                check(peer.link.cleanupFailure() == null || peer.expectedBrokenClose(peer.link.cleanupFailure()),
                        "Broken output hid an unrelated cleanup failure");
            }
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
        run("unexpected-flood-diagnostic", PipeTests::unexpectedFloodDiagnostic);
        run("diagnostic-close-failure", PipeTests::diagnosticCloseFailure);
        run("controlled-eof-delivery-timeout", PipeTests::evidenceCompletionTimeout);
        run("bounded-evidence-failures", PipeTests::evidenceFailures);
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
