package io.ranzevor.harrancraft.test;

import io.ranzevor.harrancraft.GuestConnections;
import io.ranzevor.harrancraft.bridge.Session;
import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.HashSet;
import java.util.concurrent.ConcurrentLinkedQueue;
import net.fabricmc.fabric.api.client.gametest.v1.FabricClientGameTest;
import net.fabricmc.fabric.api.client.gametest.v1.context.ClientGameTestContext;
import net.fabricmc.fabric.api.client.gametest.v1.context.TestSingleplayerContext;
import net.minecraft.block.Blocks;
import net.minecraft.util.math.BlockPos;

public final class ClientTests implements FabricClientGameTest {
    private static final BlockPos POS = new BlockPos(0, 80, 0);

    private static final class Peer implements AutoCloseable {
        final Process process;
        final ConcurrentLinkedQueue<String> messages = new ConcurrentLinkedQueue<>();
        final HashSet<String> seen = new HashSet<>();
        final Thread evidence;
        Peer(String scenario, long oldEpoch) throws Exception {
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
            seen.addAll(messages);
            seen.stream().filter(line -> line.contains("\"stage\":\"passed\""))
                    .forEach(System.out::println);
        }
        @Override public void close() {
            process.destroyForcibly();
            try {
                if (!process.waitFor(5, java.util.concurrent.TimeUnit.SECONDS)) throw new AssertionError("Native peer did not stop");
                evidence.join(2000);
                if (evidence.isAlive()) throw new AssertionError("Evidence reader did not stop");
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                throw new IllegalStateException("Interrupted while stopping native peer", e);
            }
        }
    }

    @Override public void runTest(ClientGameTestContext context) {
        try (TestSingleplayerContext singleplayer = context.worldBuilder().create()) {
            singleplayer.getServer().runCommand("gamemode spectator @a");
            singleplayer.getServer().runCommand("tp @a 0.5 79 -4 0 0");
            singleplayer.getServer().runCommand("setblock 0 80 0 minecraft:air");
            singleplayer.getClientWorld().waitForChunksRender();
            context.takeScreenshot("harrancraft-h2a-empty");
            long prior;
            try (Peer peer = new Peer("cycle", 0)) {
                Session session = singleplayer.getServer().computeOnServer(server ->
                        GuestConnections.attach(server, peer.process.getInputStream(), peer.process.getOutputStream()));
                prior = session.epoch();
                peer.stage(context, "placed");
                context.waitFor(client -> client.world != null && client.world.getBlockState(POS).isOf(Blocks.DIAMOND_BLOCK));
                singleplayer.getClientWorld().waitForChunksRender();
                context.takeScreenshot("harrancraft-h2a-placed");
                singleplayer.getServer().runOnServer(server -> {
                    if (!server.getOverworld().getBlockState(POS).isOf(Blocks.DIAMOND_BLOCK))
                        throw new AssertionError("Placement was not in the authoritative world");
                });
                singleplayer.getServer().runCommand("setblock 0 80 0 minecraft:gold_block");
                peer.finished(context);
                context.waitFor(client -> client.world != null && client.world.getBlockState(POS).isAir());
                singleplayer.getClientWorld().waitForChunksRender();
                context.takeScreenshot("harrancraft-h2a-removed");
                singleplayer.getServer().runOnServer(server -> {
                    if (!server.getOverworld().getBlockState(POS).isAir()) throw new AssertionError("Removal failed");
                    GuestConnections.tick(server);
                    if (session.isOpen() || session.watchedCount() != 0) throw new AssertionError("Disconnect retained state");
                });
            }
            try (Peer peer = new Peer("stale", prior)) {
                singleplayer.getServer().runOnServer(server ->
                        GuestConnections.attach(server, peer.process.getInputStream(), peer.process.getOutputStream()));
                peer.finished(context);
            }
            try (Peer peer = new Peer("invalid-version", 0)) {
                singleplayer.getServer().runOnServer(server ->
                        GuestConnections.attach(server, peer.process.getInputStream(), peer.process.getOutputStream()));
                peer.finished(context);
            }
            singleplayer.getServer().runOnServer(server -> {
                GuestConnections.detach(server);
                if (!server.getOverworld().getBlockState(POS).isAir()) throw new AssertionError("Rejected command changed state");
            });
        } catch (Exception e) {
            throw new AssertionError("Client bridge integration failed", e);
        }
    }
}
