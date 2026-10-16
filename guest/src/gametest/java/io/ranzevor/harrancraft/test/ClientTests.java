package io.ranzevor.harrancraft.test;

import io.ranzevor.harrancraft.GuestConnections;
import io.ranzevor.harrancraft.MinecraftCollisionShapes;
import io.ranzevor.harrancraft.bridge.Frame;
import io.ranzevor.harrancraft.bridge.Session;
import net.fabricmc.fabric.api.client.gametest.v1.FabricClientGameTest;
import net.fabricmc.fabric.api.client.gametest.v1.context.ClientGameTestContext;
import net.fabricmc.fabric.api.client.gametest.v1.context.TestSingleplayerContext;
import net.minecraft.block.Blocks;
import net.minecraft.util.math.BlockPos;

public final class ClientTests implements FabricClientGameTest {
    private static final BlockPos POS = new BlockPos(0, 80, 0);

    @Override public void runTest(ClientGameTestContext context) {
        try (TestSingleplayerContext singleplayer = context.worldBuilder().create()) {
            singleplayer.getServer().runCommand("gamemode spectator @a");
            singleplayer.getServer().runCommand("tp @a 0.5 79 -4 0 0");
            for (int tick = 0; tick < 200; ++tick) {
                if (singleplayer.getServer().computeOnServer(server -> server.getOverworld().getChunkManager().isChunkLoaded(POS.getX() >> 4, POS.getZ() >> 4))) break;
                context.waitTick();
            }
            singleplayer.getServer().runOnServer(server -> {
                if (!server.getOverworld().getChunkManager().isChunkLoaded(POS.getX() >> 4, POS.getZ() >> 4))
                    throw new AssertionError("Test position did not load");
            });
            singleplayer.getServer().runCommand("setblock 0 80 0 minecraft:air");
            singleplayer.getClientWorld().waitForChunksRender();
            context.takeScreenshot("harrancraft-h2a-empty");
            long prior;
            try (GamePeer peer = new GamePeer("cycle", 0)) {
                Session session = singleplayer.getServer().computeOnServer(server ->
                        peer.attach(server));
                prior = session.epoch();
                peer.stage(context, "placed");
                context.waitFor(client -> client.world != null && client.world.getBlockState(POS).isOf(Blocks.DIAMOND_BLOCK));
                singleplayer.getClientWorld().waitForChunksRender();
                context.takeScreenshot("harrancraft-h2a-placed");
                singleplayer.getServer().runOnServer(server -> {
                    if (!server.getOverworld().getBlockState(POS).isOf(Blocks.DIAMOND_BLOCK))
                        throw new AssertionError("Placement was not in the authoritative world");
                    var geometry = GuestConnections.collisionSnapshot(server, session.epoch(),
                            new Frame.Key("minecraft:overworld", POS.getX(), POS.getY(), POS.getZ()));
                    if (geometry.status() != Frame.OK || geometry.snapshot() == null
                            || !geometry.snapshot().canonicalState().equals("minecraft:diamond_block")
                            || geometry.snapshot().boxes().size() != 1
                            || !geometry.snapshot().boxes().getFirst().equals(
                                    new MinecraftCollisionShapes.Bounds(0, 0, 0, 1, 1, 1))) {
                        throw new AssertionError("Authoritative collision snapshot was not captured");
                    }
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
                    var oldShape = GuestConnections.collisionSnapshot(server, session.epoch(),
                            new Frame.Key("minecraft:overworld", POS.getX(), POS.getY(), POS.getZ()));
                    if (oldShape.status() == Frame.OK) {
                        throw new AssertionError("Retired collision snapshot remained accessible");
                    }
                });
            }
            try (GamePeer peer = new GamePeer("stale", prior)) {
                singleplayer.getServer().runOnServer(server ->
                        peer.attach(server));
                peer.finished(context);
            }
            try (GamePeer peer = new GamePeer("invalid-version", 0)) {
                singleplayer.getServer().runOnServer(server ->
                        peer.attach(server));
                peer.finished(context);
            }
            singleplayer.getServer().runOnServer(server -> {
                GuestConnections.detach(server);
                if (!server.getOverworld().getBlockState(POS).isAir()) throw new AssertionError("Rejected command changed state");
            });
            System.out.println("HARRANCRAFT_SCENARIO block-roundtrip");
        } catch (Exception e) {
            throw new AssertionError("Client bridge integration failed", e);
        }
    }
}
