package io.ranzevor.harrancraft;

import io.ranzevor.harrancraft.bridge.Frame;
import io.ranzevor.harrancraft.bridge.RenderFrame;
import io.ranzevor.harrancraft.bridge.TextureWire;
import io.ranzevor.harrancraft.mixin.SpriteContentsAccessor;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Objects;
import net.fabricmc.api.EnvType;
import net.fabricmc.api.Environment;
import net.minecraft.block.BlockRenderType;
import net.minecraft.block.BlockState;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.render.model.BakedModel;
import net.minecraft.client.render.model.BakedQuad;
import net.minecraft.client.texture.NativeImage;
import net.minecraft.client.texture.Sprite;
import net.minecraft.client.world.ClientWorld;
import net.minecraft.server.integrated.IntegratedServer;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.Direction;
import net.minecraft.util.math.random.Random;

/** Samples the source images of quads from the same already-loaded client model. */
@Environment(EnvType.CLIENT)
public final class MinecraftSpriteTextures {
    public static final int MAX_SPRITES = 16;

    private record SpriteKey(String atlas, String sprite) {}

    private MinecraftSpriteTextures() {}

    public static List<TextureWire> capture(MinecraftClient client, RenderFrame render) {
        Objects.requireNonNull(render);
        if (client == null || !client.isOnThread()) {
            throw new IllegalStateException("Sprite sampling requires Minecraft client thread");
        }
        IntegratedServer server = client.getServer();
        ClientWorld world = client.world;
        if (world == null || client.player == null || server == null
                || !GuestConnections.allowed(server) || !client.gameRenderer.getCamera().isReady()
                || !render.key().dimension().equals(world.getRegistryKey().getValue().toString())) {
            throw new IllegalStateException("Source Minecraft world is unavailable");
        }
        var view = client.gameRenderer.getCamera();
        var position = view.getPos();
        RenderFrame.CameraPose current = new RenderFrame.CameraPose(
                position.x, position.y, position.z, view.getYaw(), view.getPitch());
        if (!current.equals(render.camera())) {
            throw new IllegalStateException("Render source camera changed");
        }
        Frame.Key key = render.key();
        BlockPos block = new BlockPos(key.x(), key.y(), key.z());
        if (world.isOutOfHeightLimit(block) || !world.getWorldBorder().contains(block)
                || !world.getChunkManager().isChunkLoaded(block.getX() >> 4, block.getZ() >> 4)) {
            throw new IllegalStateException("Sprite source block not loaded");
        }
        BlockState state = world.getBlockState(block);
        if (!MinecraftBlocks.describe(state).equals(render.state())
                || state.hasBlockEntity() || !state.getFluidState().isEmpty()) {
            throw new IllegalStateException("Block changed or unsupported dynamic renderer");
        }
        if (state.getRenderType() == BlockRenderType.INVISIBLE) {
            if (!render.quads().isEmpty()) throw new IllegalStateException("Invisible model has quads");
            return List.of();
        }
        if (state.getRenderType() != BlockRenderType.MODEL) {
            throw new UnsupportedOperationException("Block is not a baked model");
        }
        BakedModel model = client.getBlockRenderManager().getModel(state);
        LinkedHashMap<SpriteKey, TextureWire> textures = new LinkedHashMap<>();
        int matched = 0;
        long seed = state.getRenderingSeed(block);
        matched = append(model, state, null, seed, render, matched, textures);
        for (Direction face : Direction.values()) {
            matched = append(model, state, face, seed, render, matched, textures);
        }
        if (matched != render.quads().size()) {
            throw new IllegalStateException("Baked model changed during texture sampling");
        }
        return List.copyOf(textures.values());
    }

    private static int append(BakedModel model, BlockState state, Direction face, long seed,
                              RenderFrame capture, int index,
                              LinkedHashMap<SpriteKey, TextureWire> target) {
        Random random = Random.create(seed);
        for (BakedQuad quad : model.getQuads(state, face, random)) {
            if (index >= capture.quads().size()) {
                throw new IllegalStateException("Baked model quad count changed");
            }
            Sprite sprite = Objects.requireNonNull(quad.getSprite());
            var expected = capture.quads().get(index++);
            String atlas = sprite.getAtlasId().toString();
            String name = sprite.getContents().getId().toString();
            if (expected.cullFace() != (face == null ? -1 : face.getId())
                    || expected.face() != quad.getFace().getId()
                    || expected.tintIndex() != quad.getTintIndex()
                    || expected.shade() != quad.hasShade()
                    || expected.lightEmission() != quad.getLightEmission()
                    || !expected.atlas().equals(atlas) || !expected.sprite().equals(name)
                    || !expected.vertices().equals(RenderFrame.unpackVertices(quad.getVertexData()))) {
                throw new IllegalStateException("Baked quad changed since render capture");
            }
            SpriteKey id = new SpriteKey(atlas, name);
            if (!target.containsKey(id)) {
                if (target.size() == MAX_SPRITES) {
                    throw new IllegalStateException("Sprite count exceeds capture capacity");
                }
                target.put(id, read(capture.identity(), sprite));
            }
        }
        return index;
    }

    private static TextureWire read(RenderFrame.Identity id, Sprite sprite) {
        var contents = sprite.getContents();
        NativeImage image = ((SpriteContentsAccessor) contents).harrancraft$getImage();
        int width = contents.getWidth();
        int height = contents.getHeight();
        if (width < 1 || width > TextureWire.MAX_EDGE
                || height < 1 || height > TextureWire.MAX_EDGE
                || image == null || image.getWidth() != width || image.getHeight() != height) {
            throw new UnsupportedOperationException("Animated or oversized sprite source");
        }
        ArrayList<Integer> pixels = new ArrayList<>(width * height);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                pixels.add(image.getColorArgb(x, y));
            }
        }
        return new TextureWire(TextureWire.SNAPSHOT, id, sprite.getAtlasId().toString(),
                contents.getId().toString(), sprite.getX(), sprite.getY(), width, height,
                sprite.getMinU(), sprite.getMaxU(), sprite.getMinV(), sprite.getMaxV(), pixels);
    }
}
