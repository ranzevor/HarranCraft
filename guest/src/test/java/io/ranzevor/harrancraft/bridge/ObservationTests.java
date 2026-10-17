package io.ranzevor.harrancraft.bridge;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.concurrent.ConcurrentLinkedQueue;
import java.util.concurrent.TimeUnit;
import java.util.function.BooleanSupplier;

/** Real native observation peers; supplied guest frames do not instantiate Minecraft. */
public final class ObservationTests {
    private static final Frame.Key KEY = new Frame.Key("minecraft:overworld", 0, 80, 0);
    private static int assertions;

    private ObservationTests() {}

    private static void check(boolean value, String message) {
        ++assertions;
        if (!value) throw new AssertionError(message);
    }

    private static void await(BooleanSupplier ready, String message) throws InterruptedException {
        long end = System.nanoTime() + TimeUnit.SECONDS.toNanos(5);
        while (!ready.getAsBoolean() && System.nanoTime() < end) Thread.sleep(1);
        check(ready.getAsBoolean(), message);
    }

    private static final class Peer implements AutoCloseable {
        final Process process;
        final PipeLink link;
        final ConcurrentLinkedQueue<String> messages = new ConcurrentLinkedQueue<>();
        final Thread evidence;

        Peer(String executable) throws Exception {
            process = new ProcessBuilder(executable, "observe").start();
            link = new PipeLink(process.getInputStream(), process.getOutputStream(), () -> {
                try {
                    if (!process.waitFor(100, TimeUnit.MILLISECONDS)) process.toHandle().destroyForcibly();
                } catch (InterruptedException error) {
                    process.toHandle().destroyForcibly();
                    Thread.currentThread().interrupt();
                }
            });
            evidence = Thread.ofPlatform().daemon().name("harrancraft-observation-evidence").start(() -> {
                try (BufferedReader reader = new BufferedReader(new InputStreamReader(process.getErrorStream(), StandardCharsets.UTF_8))) {
                    for (String line; (line = reader.readLine()) != null;) {
                        if (messages.size() >= 32) throw new IllegalStateException("Evidence limit exceeded");
                        messages.add(line);
                    }
                } catch (Exception error) { messages.add("error:" + error); }
            });
        }

        Frame receive() throws Exception {
            long end = System.nanoTime() + TimeUnit.SECONDS.toNanos(5);
            while (System.nanoTime() < end && link.isOpen()) {
                Frame frame = link.poll();
                if (frame != null) return frame;
                Thread.sleep(1);
            }
            throw new AssertionError("Observer did not send expected frame: " + messages);
        }

        boolean has(String stage) {
            return messages.stream().anyMatch(line -> line.contains("\"stage\":\"" + stage + "\""));
        }

        void welcome() throws Exception {
            Frame hello = receive();
            check(hello.equals(Frame.control(Frame.HELLO, 0, 0, Frame.NATIVE)), "Observer handshake mismatch");
            check(link.send(Frame.control(Frame.WELCOME, 7, 0, Frame.GUEST)), "Welcome send failed");
            Frame get = receive();
            check(get.kind() == Frame.GET && get.key().equals(KEY) && get.epoch() == 7 && get.id() == 1,
                    "Observer did not request the loaded test position");
            check(link.send(new Frame(Frame.RESULT, 7, 1, Frame.GUEST, Frame.OK, KEY, "", "minecraft:air", 0)),
                    "Initial state send failed");
            await(() -> has("observing"), "Observer did not acknowledge subscription");
        }

        void finished(boolean success) throws Exception {
            check(process.waitFor(5, TimeUnit.SECONDS), "Observer did not exit");
            evidence.join(2000);
            check(!evidence.isAlive(), "Observer evidence worker did not end");
            check((process.exitValue() == 0) == success, "Unexpected observer exit: " + messages);
            check(has("passed") == success, "Incorrect terminal observer evidence");
            await(link::isTerminated, "Observer link workers did not end");
            check(link.cleanupFailure() == null, "Observer cleanup failed");
        }

        @Override public void close() {
            link.close();
            process.toHandle().destroyForcibly();
            try {
                check(process.waitFor(5, TimeUnit.SECONDS), "Owned observer survived cleanup");
                evidence.join(2000);
                check(!evidence.isAlive(), "Owned evidence reader survived cleanup");
                await(link::isTerminated, "Owned pipe survived cleanup");
                check(link.cleanupFailure() == null, "Owned pipe cleanup failed");
            } catch (InterruptedException error) {
                Thread.currentThread().interrupt();
                throw new AssertionError("Observer cleanup interrupted", error);
            }
        }
    }

    private static void complete(String executable, boolean change) throws Exception {
        try (Peer peer = new Peer(executable)) {
            peer.welcome();
            if (change) {
                check(peer.link.send(new Frame(Frame.EVENT, 7, 1, Frame.GUEST, Frame.OK,
                        KEY, "minecraft:air", "minecraft:gold_block", 0)), "Change send failed");
                await(() -> peer.has("observed"), "Observer did not receive independent change");
            }
            peer.link.close();
            peer.finished(true);
            check(peer.messages.stream().anyMatch(line -> line.contains("\"events\":" + (change ? 1 : 0))),
                    "Observer event count mismatch");
        }
    }

    private static void invalid(String executable, Frame frame) throws Exception {
        try (Peer peer = new Peer(executable)) {
            peer.welcome();
            check(peer.link.send(frame), "Invalid-frame fixture was not sent");
            peer.finished(false);
        }
    }

    private static long descriptors() throws Exception {
        Path path = Path.of("/proc/self/fd");
        if (!Files.isDirectory(path)) return -1;
        try (var entries = Files.list(path)) { return entries.count(); }
    }

    public static void main(String[] arguments) throws Exception {
        if (arguments.length != 1) throw new IllegalArgumentException("Expected native peer executable");
        String executable = arguments[0];
        complete(executable, false);
        complete(executable, true);
        invalid(executable, new Frame(Frame.EVENT, 8, 1, Frame.GUEST, Frame.OK, KEY, "minecraft:air", "minecraft:stone", 0));
        invalid(executable, new Frame(Frame.EVENT, 7, 2, Frame.GUEST, Frame.OK, KEY, "minecraft:air", "minecraft:stone", 0));
        invalid(executable, new Frame(Frame.EVENT, 7, 1, Frame.NATIVE, Frame.OK, KEY, "minecraft:air", "minecraft:stone", 2));
        invalid(executable, new Frame(Frame.RESULT, 7, 1, Frame.GUEST, Frame.OK, KEY, "", "minecraft:air", 0));
        invalid(executable, new Frame(Frame.EVENT, 7, 1, Frame.GUEST, Frame.OK,
                new Frame.Key("minecraft:overworld", 1, 80, 0), "minecraft:air", "minecraft:stone", 0));
        invalid(executable, new Frame(Frame.EVENT, 7, 1, Frame.GUEST, Frame.OK, KEY, "minecraft:stone", "minecraft:gold_block", 0));
        try (Peer peer = new Peer(executable)) {
            peer.welcome();
            peer.process.getOutputStream().write(new byte[17]);
            peer.process.getOutputStream().close();
            peer.finished(false);
        }
        long before = descriptors();
        for (int round = 0; round < 8; ++round) complete(executable, true);
        long after = descriptors();
        if (before >= 0) check(before == after, "Observer descriptor count increased");
        check(Thread.getAllStackTraces().keySet().stream().noneMatch(thread -> thread.isAlive()
                && (thread.getName().startsWith("harrancraft-pipe-")
                || thread.getName().equals("harrancraft-observation-evidence"))), "Observer worker leaked");
        System.out.println("native-observation: 9 scenarios and 8 retirement cycles passed, " + assertions
                + " assertions, descriptors " + before + " -> " + after);
    }
}
