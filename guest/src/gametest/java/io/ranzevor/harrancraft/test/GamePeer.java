package io.ranzevor.harrancraft.test;

import io.ranzevor.harrancraft.GuestConnections;
import io.ranzevor.harrancraft.bridge.PipeLink;
import io.ranzevor.harrancraft.bridge.Session;
import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.HashSet;
import java.util.concurrent.ConcurrentLinkedQueue;
import net.fabricmc.fabric.api.client.gametest.v1.context.ClientGameTestContext;
import net.minecraft.server.MinecraftServer;

final class GamePeer implements AutoCloseable {
    final Process process;
    final ConcurrentLinkedQueue<String> messages = new ConcurrentLinkedQueue<>();
    final HashSet<String> seen = new HashSet<>();
    final Thread evidence;
    PipeLink link;
    GamePeer(String scenario, long oldEpoch) throws Exception {
        String executable = System.getProperty("harrancraft.test.peer");
        if (executable == null || !Files.isRegularFile(Path.of(executable))) {
            throw new IllegalStateException("Native peer executable was not supplied");
        }
        ProcessBuilder builder = oldEpoch == 0 ? new ProcessBuilder(executable, scenario)
                : new ProcessBuilder(executable, scenario, Long.toString(oldEpoch));
        process = builder.start();
        evidence = Thread.ofPlatform().daemon().name("harrancraft-test-evidence").start(() -> {
            try (BufferedReader lines = new BufferedReader(new InputStreamReader(process.getErrorStream(), StandardCharsets.UTF_8))) {
                String line;
                int count = 0;
                while ((line = lines.readLine()) != null && ++count <= 32) messages.add(line);
            } catch (Exception e) { messages.add("Evidence pipe failed: " + e.getClass().getSimpleName()); }
        });
    }
    PipeLink connection() {
        if (link != null) return link;
        link = new PipeLink(process.getInputStream(), process.getOutputStream(), () -> {
            try {
                if (!process.waitFor(100, java.util.concurrent.TimeUnit.MILLISECONDS))
                    process.toHandle().destroyForcibly();
            } catch (InterruptedException error) {
                process.toHandle().destroyForcibly();
                Thread.currentThread().interrupt();
            }
        });
        return link;
    }
    Session attach(MinecraftServer server) {
        if (link != null) throw new IllegalStateException("Peer already attached");
        connection();
        try { return GuestConnections.attach(server, link); }
        catch (RuntimeException error) { link.close(); throw error; }
    }
    void stage(ClientGameTestContext context, String stage) {
        for (int ticks = 0; ticks < 600; ++ticks) {
            seen.addAll(messages);
            if (seen.stream().anyMatch(line -> line.contains("\"stage\":\"" + stage + "\""))) return;
            if (!process.isAlive()) {
                try { evidence.join(2000); }
                catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                    throw new IllegalStateException("Interrupted while collecting peer evidence", e);
                }
                seen.addAll(messages);
                if (seen.stream().anyMatch(line -> line.contains("\"stage\":\"" + stage + "\""))) return;
                throw new AssertionError("Native peer stopped before " + stage + ": " + seen);
            }
            context.waitTick();
        }
        throw new AssertionError("Native peer stage timed out: " + stage);
    }
    void finished(ClientGameTestContext context) throws Exception {
        stage(context, "passed");
        for (int ticks = 0; ticks < 200 && process.isAlive(); ++ticks) context.waitTick();
        if (process.isAlive() || process.exitValue() != 0) throw new AssertionError("Native peer failed");
        evidence.join(2000);
        if (evidence.isAlive()) throw new AssertionError("Evidence reader did not stop");
        for (int ticks = 0; ticks < 200 && !link.isTerminated(); ++ticks) context.waitTick();
        if (!link.isTerminated() || link.cleanupFailure() != null)
            throw new AssertionError("Native pipe cleanup failed", link.cleanupFailure());
        seen.addAll(messages);
        seen.stream().filter(line -> line.contains("\"stage\":\"passed\""))
                .forEach(System.out::println);
    }
    @Override public void close() {
        if (link != null) link.close();
        process.toHandle().destroyForcibly();
        try {
            if (!process.waitFor(5, java.util.concurrent.TimeUnit.SECONDS)) throw new AssertionError("Native peer did not stop");
            evidence.join(2000);
            if (evidence.isAlive()) throw new AssertionError("Evidence reader did not stop");
            long deadline = System.nanoTime() + java.util.concurrent.TimeUnit.SECONDS.toNanos(5);
            while (link != null && !link.isTerminated() && System.nanoTime() < deadline) Thread.sleep(1);
            if (link != null && (!link.isTerminated() || link.cleanupFailure() != null))
                throw new AssertionError("Native pipe cleanup failed", link.cleanupFailure());
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("Interrupted while stopping native peer", e);
        }
    }
}
