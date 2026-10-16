package io.ranzevor.harrancraft;

import io.ranzevor.harrancraft.bridge.BlockWorld;
import io.ranzevor.harrancraft.bridge.Frame;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.world.ServerWorld;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.shape.VoxelShape;

/** Reads actual Minecraft collision shapes; never creates native host physics bodies. */
public final class MinecraftCollisionShapes {
    public static final int MAX_BOXES = 128;

    private MinecraftCollisionShapes() {}

    public record Bounds(double minX, double minY, double minZ,
                         double maxX, double maxY, double maxZ) {
        public Bounds {
            if (!Double.isFinite(minX) || !Double.isFinite(minY) || !Double.isFinite(minZ)
                    || !Double.isFinite(maxX) || !Double.isFinite(maxY) || !Double.isFinite(maxZ)
                    || minX >= maxX || minY >= maxY || minZ >= maxZ) {
                throw new IllegalArgumentException("Invalid collision volume");
            }
        }
    }

    public record Snapshot(long epoch, Frame.Key key, String canonicalState, List<Bounds> boxes) {
        public Snapshot {
            if (epoch <= 0 || key == null || canonicalState == null || boxes == null) {
                throw new IllegalArgumentException("Invalid collision snapshot identity");
            }
            boxes = List.copyOf(boxes);
        }
    }

    public record Result(int status, Snapshot snapshot) {
        public Result {
            if ((status == Frame.OK) != (snapshot != null)) {
                throw new IllegalArgumentException("Collision result status and data disagree");
            }
        }
        public static Result error(int status) {
            if (status == Frame.OK) throw new IllegalArgumentException("Missing snapshot");
            return new Result(status, null);
        }
    }

    private static final class CapacityExceeded extends RuntimeException {
        private static final long serialVersionUID = 1L;
    }

    public static List<Bounds> fromShape(VoxelShape shape) {
        Objects.requireNonNull(shape);
        ArrayList<Bounds> boxes = new ArrayList<>();
        shape.forEachBox((minX, minY, minZ, maxX, maxY, maxZ) -> {
            if (boxes.size() == MAX_BOXES) throw new CapacityExceeded();
            boxes.add(new Bounds(minX, minY, minZ, maxX, maxY, maxZ));
        });
        return List.copyOf(boxes);
    }

    static Result read(MinecraftServer server, long epoch, Frame.Key key) {
        GuestConnections.requireThread(server);
        MinecraftBlocks adapter = new MinecraftBlocks(server);
        BlockWorld.Outcome state = adapter.read(key);
        if (state.status() != Frame.OK) return Result.error(state.status());
        ServerWorld world = adapter.resolve(key);
        if (world == null) return Result.error(Frame.UNAVAILABLE);
        BlockPos pos = new BlockPos(key.x(), key.y(), key.z());
        try {
            return new Result(Frame.OK, new Snapshot(epoch, key, state.state(),
                    fromShape(world.getBlockState(pos).getCollisionShape(world, pos))));
        } catch (CapacityExceeded error) {
            return Result.error(Frame.CAPACITY);
        }
    }
}
