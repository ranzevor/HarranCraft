package io.ranzevor.harrancraft.test;

import io.ranzevor.harrancraft.GuestConnections;
import io.ranzevor.harrancraft.MinecraftBlocks;
import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import net.fabricmc.fabric.api.gametest.v1.FabricGameTest;
import net.minecraft.block.Blocks;
import net.minecraft.test.GameTest;
import net.minecraft.test.TestContext;

public final class ServerTests implements FabricGameTest {
    @GameTest(templateName = EMPTY_STRUCTURE)
    public void canonicalState(TestContext context) {
        String state = "minecraft:oak_log[axis=x]";
        var block = MinecraftBlocks.parse(state);
        context.setBlockState(1, 1, 1, block);
        context.expectBlock(Blocks.OAK_LOG, 1, 1, 1);
        if (!MinecraftBlocks.describe(block).equals(state)) throw new AssertionError("Block properties lost");
        for (String invalid : new String[]{"minecraft:missing_block", "minecraft:oak_log[axis=q]",
                "minecraft:oak_log[axis=x,axis=z]", "minecraft:oak_log", "#minecraft:logs"}) {
            try { MinecraftBlocks.parse(invalid); throw new AssertionError("Invalid block state accepted"); }
            catch (IllegalArgumentException expected) { }
        }
        context.complete();
    }

    @GameTest(templateName = EMPTY_STRUCTURE)
    public void isolatedServerRefusal(TestContext context) {
        var server = context.getWorld().getServer();
        if (GuestConnections.allowed(server)) throw new AssertionError("GameTest server classified as integrated");
        try {
            GuestConnections.attach(server, new ByteArrayInputStream(new byte[0]), new ByteArrayOutputStream());
            throw new AssertionError("Unapproved server accepted a bridge");
        } catch (IllegalStateException expected) { }
        context.complete();
    }
}
