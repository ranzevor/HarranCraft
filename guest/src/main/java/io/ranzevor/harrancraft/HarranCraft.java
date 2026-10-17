package io.ranzevor.harrancraft;

import net.fabricmc.api.ModInitializer;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerLifecycleEvents;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerTickEvents;

public final class HarranCraft implements ModInitializer {
    @Override public void onInitialize() {
        ServerTickEvents.END_SERVER_TICK.register(GuestConnections::tick);
        ServerLifecycleEvents.SERVER_STOPPING.register(GuestConnections::detach);
    }
}
