package io.ranzevor.harrancraft;

import io.ranzevor.harrancraft.bridge.Frame;
import io.ranzevor.harrancraft.bridge.RenderCaptureSession;
import io.ranzevor.harrancraft.bridge.RenderFrame;
import io.ranzevor.harrancraft.bridge.RenderPublisher;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.List;
import net.fabricmc.api.EnvType;
import net.fabricmc.api.Environment;
import net.minecraft.block.BlockRenderType;
import net.minecraft.block.BlockState;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.render.Camera;
import net.minecraft.client.render.model.BakedModel;
import net.minecraft.client.render.model.BakedQuad;
import net.minecraft.client.world.ClientWorld;
import net.minecraft.entity.Entity;
import net.minecraft.server.integrated.IntegratedServer;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.Direction;
import net.minecraft.util.math.random.Random;

/** Captures loaded client baked-model quads, not rasterized scene geometry. */
@Environment(EnvType.CLIENT)
public final class MinecraftRenderModels {
    private MinecraftRenderModels() {}

    public static RenderCaptureSession begin(
            MinecraftClient client, long epoch, long worldId, long cameraId) {
        requireClientThread(client);
        IntegratedServer server = client.getServer();
        if (client.world == null || client.player == null || server == null
                || !GuestConnections.allowed(server)) {
            throw new IllegalStateException("Private integrated client world unavailable");
        }
        ClientWorld world = client.world;
        Camera camera = client.gameRenderer.getCamera();
        if (camera == null || !camera.isReady()) {
            throw new IllegalStateException("Minecraft camera not ready");
        }
        String dimension = world.getRegistryKey().getValue().toString();
        Entity focused = camera.getFocusedEntity();
        return new RenderCaptureSession(epoch, worldId, cameraId, dimension,
                (stamp, key) -> capture(client, server, world, camera, focused, stamp, key));
    }

    /** Attaches caller-owned render output after verifying the client world. */
    public static RenderPublisher beginPublisher(
            MinecraftClient client, long epoch, long worldId, long cameraId,
            OutputStream output, Runnable cancelIo) {
        RenderCaptureSession capture = begin(client, epoch, worldId, cameraId);
        return new RenderPublisher(capture, epoch, worldId, cameraId, output, cancelIo);
    }

    private static void requireClientThread(MinecraftClient client) {
        if (client == null || !client.isOnThread()) {
            throw new IllegalStateException("Minecraft render sampling requires client thread");
        }
    }

    private static RenderFrame capture(
            MinecraftClient client, IntegratedServer server, ClientWorld world,
            Camera camera, Entity focused, RenderFrame.Identity stamp, Frame.Key key) {
        requireClientThread(client);
        if (client.world != world || client.getServer() != server || client.player == null
                || client.gameRenderer.getCamera() != camera || !camera.isReady()
                || camera.getFocusedEntity() != focused || !GuestConnections.allowed(server)) {
            throw new IllegalStateException("Client world or camera changed");
        }
        if (!key.dimension().equals(world.getRegistryKey().getValue().toString())) {
            throw new IllegalStateException("Mismatched client dimension");
        }
        BlockPos pos = new BlockPos(key.x(), key.y(), key.z());
        if (world.isOutOfHeightLimit(pos) || !world.getWorldBorder().contains(pos)
                || !world.getChunkManager().isChunkLoaded(pos.getX() >> 4, pos.getZ() >> 4)) {
            throw new IllegalStateException("Client model position unavailable");
        }

        BlockState state = world.getBlockState(pos);
        if (state.hasBlockEntity() || !state.getFluidState().isEmpty()) {
            throw new UnsupportedOperationException("Dynamic block or fluid renderer required");
        }
        String canonical = MinecraftBlocks.describe(state);
        var position = camera.getPos();
        RenderFrame.CameraPose view = new RenderFrame.CameraPose(
                position.x, position.y, position.z, camera.getYaw(), camera.getPitch());

        if (state.getRenderType() == BlockRenderType.INVISIBLE) {
            return new RenderFrame(stamp, key, canonical, view, List.of());
        }
        if (state.getRenderType() != BlockRenderType.MODEL) {
            throw new UnsupportedOperationException("Block requires an entity renderer");
        }
        BakedModel model = client.getBlockRenderManager().getModel(state);
        ArrayList<RenderFrame.Quad> quads = new ArrayList<>();
        long seed = state.getRenderingSeed(pos);
        append(quads, model, state, null, seed);
        for (Direction face : Direction.values()) {
            append(quads, model, state, face, seed);
        }
        return new RenderFrame(stamp, key, canonical, view, quads);
    }

    private static void append(ArrayList<RenderFrame.Quad> target, BakedModel model,
                               BlockState state, Direction cull, long seed) {
        Random random = Random.create(seed);
        for (BakedQuad quad : model.getQuads(state, cull, random)) {
            if (target.size() >= RenderFrame.MAX_QUADS) {
                throw new IllegalStateException("Baked model exceeds quad capacity");
            }
            var sprite = quad.getSprite();
            if (sprite == null || quad.getFace() == null) {
                throw new IllegalStateException("Missing baked quad metadata");
            }
            target.add(new RenderFrame.Quad(
                    cull == null ? -1 : cull.getId(),
                    quad.getFace().getId(),
                    quad.getTintIndex(), quad.hasShade(), quad.getLightEmission(),
                    sprite.getAtlasId().toString(), sprite.getContents().getId().toString(),
                    RenderFrame.unpackVertices(quad.getVertexData())));
        }
    }
}
