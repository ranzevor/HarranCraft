package io.ranzevor.harrancraft.mixin;

import io.ranzevor.harrancraft.GuestConnections;
import net.minecraft.block.BlockState;
import net.minecraft.server.world.ServerWorld;
import net.minecraft.util.math.BlockPos;
import net.minecraft.world.World;
import net.minecraft.world.chunk.WorldChunk;
import org.spongepowered.asm.mixin.Final;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Mixin(WorldChunk.class)
abstract class WorldChunkMixin {
    @Shadow @Final private World world;

    @Inject(method = "setBlockState", at = @At("RETURN"))
    private void harrancraft$changed(BlockPos pos, BlockState requested, boolean moved,
                                     CallbackInfoReturnable<BlockState> result) {
        BlockState previous = result.getReturnValue();
        if (previous != null && world instanceof ServerWorld serverWorld) {
            GuestConnections.changed(serverWorld, (WorldChunk) (Object) this, pos, previous);
        }
    }
}
