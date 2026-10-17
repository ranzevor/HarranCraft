package io.ranzevor.harrancraft.bridge;

import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayDeque;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

public final class LifecycleTests {
    private static final Frame.Key KEY = new Frame.Key("minecraft:overworld", 0, 80, 0);
    private static int checks;
    private static int failures;

    private LifecycleTests() {}

    private static void check(boolean condition, String message) {
        ++checks;
        if (!condition) throw new AssertionError(message);
    }

    private static Frame command(int kind, long id) {
        return new Frame(kind, 7, id, Frame.NATIVE, Frame.OK, KEY,
                kind == Frame.SET ? "minecraft:air" : "",
                kind == Frame.SET ? "minecraft:stone" : "", 0);
    }

    private static final class ControlledLink implements Link {
        final ArrayDeque<Frame> incoming = new ArrayDeque<>();
        boolean open = true;
        int refusedKind;
        int sendsAfterClose;
        @Override public Frame poll() { return incoming.poll(); }
        @Override public boolean send(Frame frame) {
            if (!open) ++sendsAfterClose;
            return open && frame.kind() != refusedKind;
        }
        @Override public int outputCapacity() { return 256; }
        @Override public boolean isOpen() { return open; }
        @Override public void close() { open = false; incoming.clear(); }
    }

    private static final class ControlledWorld implements BlockWorld {
        Session session;
        boolean failRead;
        boolean failWrite;
        int writes;
        @Override public Outcome read(Frame.Key key) {
            if (failRead) throw new IllegalStateException("read failure");
            return new Outcome(Frame.OK, "minecraft:air");
        }
        @Override public Outcome compareAndSet(Frame.Key key, String expected, String state) {
            ++writes;
            session.changed(key, expected, state);
            if (failWrite) throw new IllegalStateException("write failure");
            return new Outcome(Frame.OK, state);
        }
    }

    private static final class Setup {
        final ControlledLink link = new ControlledLink();
        final ControlledWorld world = new ControlledWorld();
        final Session session = new Session(7, world, link, 0);
        Setup(boolean welcome) {
            world.session = session;
            if (welcome) send(Frame.control(Frame.HELLO, 0, 0, Frame.NATIVE));
        }
        void send(Frame request) {
            link.incoming.add(request);
            session.tick(1, true);
        }
        void closed() {
            check(!session.isOpen() && !link.isOpen(), "Failed link retained an open session");
            check(session.watchedCount() == 0, "Closed session retained subscriptions");
        }
    }

    private static void welcomeFailure() {
        Setup setup = new Setup(false);
        setup.link.refusedKind = Frame.WELCOME;
        setup.send(Frame.control(Frame.HELLO, 0, 0, Frame.NATIVE));
        setup.closed();
    }

    private static void duplicateFailure() {
        Setup setup = new Setup(true);
        Frame request = command(Frame.GET, 1);
        setup.send(request);
        setup.link.refusedKind = Frame.RESULT;
        setup.send(request);
        setup.closed();
    }

    private static void adapterFailure(boolean mutation) {
        Setup setup = new Setup(true);
        setup.world.failRead = !mutation;
        setup.world.failWrite = mutation;
        try {
            setup.send(command(mutation ? Frame.SET : Frame.GET, 1));
            throw new AssertionError("Adapter failure was suppressed");
        } catch (IllegalStateException error) {
            check(error.getMessage().equals(mutation ? "write failure" : "read failure"),
                    "Original adapter failure was lost");
        }
        setup.closed();
        setup.session.tick(2, true);
        check(setup.world.writes == (mutation ? 1 : 0), "Failed mutation was replayed");
    }

    private static void eventFailure() {
        Setup setup = new Setup(true);
        setup.link.refusedKind = Frame.EVENT;
        setup.send(command(Frame.SET, 1));
        setup.closed();
        check(setup.world.writes == 1, "Mutation evidence was lost");
        check(setup.link.sendsAfterClose == 0, "Receipt sent after event invalidated the session");
    }

    private static void modeTransition() {
        Setup setup = new Setup(true);
        setup.send(command(Frame.GET, 1));
        setup.link.incoming.add(command(Frame.SET, 2));
        setup.session.tick(2, false);
        setup.closed();
        check(setup.world.writes == 0, "Queued mutation survived mode rejection");
    }

    private static void guestAdmission() {
        // Bits describe singleplayer, dedicated, remote, ready, stopping and stopped.
        // Only the private ready combination may process the queued command.
        for (int flags = 0; flags < 64; ++flags) {
            boolean allowed = GuestAccess.allowed((flags & 1) != 0, (flags & 2) != 0,
                    (flags & 4) != 0, (flags & 8) != 0, (flags & 16) != 0, (flags & 32) != 0);
            check(allowed == (flags == 9), "Guest admission accepted the wrong server state");
            Setup initial = new Setup(false);
            initial.link.incoming.add(Frame.control(Frame.HELLO, 0, 0, Frame.NATIVE));
            initial.session.tick(1, allowed);
            if (flags == 9) {
                check(initial.session.isWelcomed(), "Ready private server refused its handshake");
            } else {
                initial.closed();
                check(!initial.session.isWelcomed(), "Refused server completed a handshake");
            }
            Setup active = new Setup(true);
            active.send(command(Frame.GET, 1));
            active.link.incoming.add(command(Frame.SET, 2));
            active.session.tick(2, allowed);
            if (flags == 9) {
                check(active.session.isOpen() && active.world.writes == 1,
                        "Ready private server rejected its queued mutation");
            } else {
                active.closed();
                check(active.world.writes == 0, "Disallowed server executed a queued mutation");
            }
            initial.session.close();
            active.session.close();
        }
    }

    private static void invalidText() {
        for (String state : new String[]{"x".repeat(193), "minecraft:stone\n", "方块"}) {
            try {
                new Frame(Frame.SET, 7, 1, Frame.NATIVE, Frame.OK, KEY, "minecraft:air", state, 0);
                throw new AssertionError("Invalid in-memory frame was accepted");
            } catch (IllegalArgumentException expected) {
                ++checks;
            }
        }
    }

    private static void await(CountDownLatch latch) {
        boolean interrupted = false;
        for (;;) {
            try { latch.await(); break; }
            catch (InterruptedException error) { interrupted = true; }
        }
        if (interrupted) Thread.currentThread().interrupt();
    }

    private static void independentPipeClose() throws Exception {
        CountDownLatch inputClosed = new CountDownLatch(1);
        CountDownLatch outputClosing = new CountDownLatch(1);
        CountDownLatch releaseOutput = new CountDownLatch(1);
        CountDownLatch outputClosed = new CountDownLatch(1);
        InputStream input = new InputStream() {
            @Override public int read() { await(inputClosed); return -1; }
            @Override public void close() { inputClosed.countDown(); }
        };
        OutputStream output = new OutputStream() {
            @Override public void write(int value) {}
            @Override public void close() {
                outputClosing.countDown();
                await(releaseOutput);
                outputClosed.countDown();
            }
        };
        PipeLink link = new PipeLink(input, output);
        try {
            link.close();
            check(!link.isOpen(), "Close did not invalidate the pipe immediately");
            check(outputClosing.await(2, TimeUnit.SECONDS), "Output close did not start");
            check(inputClosed.await(2, TimeUnit.SECONDS), "Output close blocked input cleanup");
            check(!link.send(Frame.control(Frame.PING, 7, 1, Frame.GUEST)), "Closed pipe accepted a frame");
        } finally {
            releaseOutput.countDown();
            inputClosed.countDown();
            check(outputClosed.await(2, TimeUnit.SECONDS), "Output close worker leaked");
            link.close();
        }
    }

    @FunctionalInterface private interface Test { void run() throws Exception; }

    private static void run(String name, Test test) {
        try { test.run(); }
        catch (Exception | AssertionError error) {
            ++failures;
            System.err.println(name + ": " + error);
        }
    }

    public static void main(String[] args) {
        run("welcome-send-failure", LifecycleTests::welcomeFailure);
        run("duplicate-send-failure", LifecycleTests::duplicateFailure);
        run("adapter-read-failure", () -> adapterFailure(false));
        run("adapter-write-failure", () -> adapterFailure(true));
        run("event-send-failure", LifecycleTests::eventFailure);
        run("mode-transition", LifecycleTests::modeTransition);
        run("guest-admission", LifecycleTests::guestAdmission);
        run("frame-text-validation", LifecycleTests::invalidText);
        run("independent-pipe-close", LifecycleTests::independentPipeClose);
        if (failures != 0) throw new AssertionError(failures + " lifecycle scenarios failed");
        System.out.println("bridge-lifecycle: 9 scenarios passed, " + checks + " assertions");
    }
}
