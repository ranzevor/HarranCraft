package io.ranzevor.harrancraft.test;

import io.ranzevor.harrancraft.GuestConnections;
import net.fabricmc.fabric.api.client.gametest.v1.context.ClientGameTestContext;
import net.fabricmc.fabric.api.client.gametest.v1.context.TestSingleplayerContext;

final class GameWorld {
    private GameWorld() {}

    static void awaitReady(ClientGameTestContext context, TestSingleplayerContext world) {
        for (int tick = 0; tick <= 200; ++tick) {
            boolean ready = world.getServer().computeOnServer(server -> {
                if (!server.isSingleplayer() || server.isDedicated() || server.isRemote()
                        || server.isStopping() || server.isStopped()) {
                    throw new AssertionError("Test world left private integrated mode");
                }
                boolean started = server.isLoading();
                if (GuestConnections.allowed(server) != started) {
                    throw new AssertionError("Private server admission disagrees with readiness");
                }
                return started;
            });
            if (ready) return;
            if (tick < 200) context.waitTick();
        }
        throw new AssertionError("Private integrated server did not become ready within 200 ticks");
    }
}
