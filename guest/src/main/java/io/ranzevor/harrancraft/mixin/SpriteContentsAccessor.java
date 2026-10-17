package io.ranzevor.harrancraft.mixin;

import net.minecraft.client.texture.NativeImage;
import net.minecraft.client.texture.SpriteContents;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;

/** Reads already-loaded client sprite source pixels without GPU readback. */
@Mixin(SpriteContents.class)
public interface SpriteContentsAccessor {
    @Accessor("image")
    NativeImage harrancraft$getImage();
}
