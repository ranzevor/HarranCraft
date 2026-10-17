package io.ranzevor.harrancraft;

import io.ranzevor.harrancraft.bridge.MaterialReloadRegistry;
import net.fabricmc.api.ClientModInitializer;
import net.fabricmc.fabric.api.resource.ResourceManagerHelper;
import net.fabricmc.fabric.api.resource.SimpleSynchronousResourceReloadListener;
import net.minecraft.resource.ResourceManager;
import net.minecraft.resource.ResourceType;
import net.minecraft.util.Identifier;

/** Retires client-owned material streams whenever resource packs are reloaded. */
public final class HarranCraftClient implements ClientModInitializer {
    @Override public void onInitializeClient() {
        ResourceManagerHelper.get(ResourceType.CLIENT_RESOURCES).registerReloadListener(
                new SimpleSynchronousResourceReloadListener() {
                    @Override public Identifier getFabricId() {
                        return Identifier.of("harrancraft", "material_sources");
                    }

                    @Override public void reload(ResourceManager manager) {
                        MaterialReloadRegistry.onReload();
                    }
                });
    }
}
