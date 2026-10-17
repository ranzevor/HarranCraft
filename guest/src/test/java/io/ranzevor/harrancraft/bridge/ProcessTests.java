package io.ranzevor.harrancraft.bridge;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicReference;

/** Runs the real native pipe peer against a controlled protocol-test world. */
public final class ProcessTests {
    private static long execute(String executable, String scenario, long epoch, long old) throws Exception {
        Process process = old == 0 ? new ProcessBuilder(executable, scenario).start()
                : new ProcessBuilder(executable, scenario, Long.toString(old)).start();
        AtomicBoolean placed = new AtomicBoolean(), passed = new AtomicBoolean();
        AtomicReference<String> diagnostic = new AtomicReference<>("");
        Thread evidence = Thread.ofPlatform().daemon().start(() -> {
            try (BufferedReader lines = new BufferedReader(new InputStreamReader(process.getErrorStream(), StandardCharsets.UTF_8))) {
                String line;
                while ((line = lines.readLine()) != null) {
                    if (line.contains("\"stage\":\"placed\"")) placed.set(true);
                    if (line.contains("\"stage\":\"passed\"")) passed.set(true);
                    diagnostic.set(line);
                }
            } catch (Exception e) { diagnostic.set(e.getClass().getSimpleName()); }
        });
        Frame.Key key = new Frame.Key("minecraft:overworld", 0, 80, 0);
        class World implements BlockWorld {
            String state = "minecraft:air";
            Session session;
            @Override public Outcome read(Frame.Key at) { return new Outcome(Frame.OK, state); }
            @Override public Outcome compareAndSet(Frame.Key at, String expected, String target) {
                if (!state.equals(expected)) return new Outcome(Frame.CONFLICT, state);
                if (target.startsWith("minecraft:chest")) return new Outcome(Frame.UNSUPPORTED_STATE, state);
                String previous = state;
                state = target;
                session.changed(at, previous, state);
                return new Outcome(Frame.OK, state);
            }
        }
        World world = new World();
        PipeLink link = new PipeLink(process.getInputStream(), process.getOutputStream(), () -> {
            try {
                if (!process.waitFor(100, TimeUnit.MILLISECONDS)) process.toHandle().destroyForcibly();
            } catch (InterruptedException error) {
                process.toHandle().destroyForcibly();
                Thread.currentThread().interrupt();
            }
        });
        Session session = new Session(epoch, world, link, System.nanoTime());
        world.session = session;
        long deadline = System.nanoTime() + 15_000_000_000L;
        boolean external = false;
        try {
            while (process.isAlive() && System.nanoTime() < deadline) {
                session.tick(System.nanoTime(), true);
                if (placed.get() && !external) {
                    external = true;
                    world.state = "minecraft:gold_block";
                    session.changed(key, "minecraft:diamond_block", world.state);
                }
                Thread.sleep(1);
            }
            if (process.isAlive()) throw new AssertionError("Native peer timed out");
            evidence.join(2000);
            if (evidence.isAlive()) throw new AssertionError("Native evidence stream did not close");
            if (process.exitValue() != 0 || !passed.get() || !world.state.equals("minecraft:air")) {
                throw new AssertionError("Native peer failed: " + diagnostic.get());
            }
        } finally {
            session.close();
            process.destroyForcibly();
            if (!process.waitFor(5, TimeUnit.SECONDS)) {
                throw new AssertionError("Native peer did not terminate");
            }
            evidence.join(2000);
            if (evidence.isAlive()) throw new AssertionError("Native evidence thread did not terminate");
            long cleanupDeadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(5);
            while (!link.isTerminated() && System.nanoTime() < cleanupDeadline) Thread.sleep(1);
            if (!link.isTerminated() || link.cleanupFailure() != null) {
                throw new AssertionError("Native pipe cleanup failed", link.cleanupFailure());
            }
        }
        return epoch;
    }
    public static void main(String[] args) throws Exception {
        if (args.length != 1) throw new IllegalArgumentException("Expected native peer executable");
        long previous = execute(args[0], "cycle", 7, 0);
        execute(args[0], "stale", 8, previous);
        execute(args[0], "invalid-version", 9, 0);
        System.out.println("native-java-pipes: 3 scenarios passed");
    }
}
