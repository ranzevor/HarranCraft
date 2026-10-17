package io.ranzevor.harrancraft;

import io.ranzevor.harrancraft.bridge.Frame;
import io.ranzevor.harrancraft.bridge.GeometryPipe;
import io.ranzevor.harrancraft.bridge.GuestAccess;
import io.ranzevor.harrancraft.bridge.PipeLink;
import io.ranzevor.harrancraft.bridge.Session;
import java.io.InputStream;
import java.io.OutputStream;
import java.security.SecureRandom;
import java.util.Objects;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicLong;
import net.minecraft.block.BlockState;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.world.ServerWorld;
import net.minecraft.util.math.BlockPos;
import net.minecraft.world.chunk.WorldChunk;

/** An embedding API; attaching requires explicitly supplied, already-owned private pipes. */
public final class GuestConnections {
    private static final ConcurrentHashMap<MinecraftServer, Session> SESSIONS = new ConcurrentHashMap<>();
    private static final ConcurrentHashMap<MinecraftServer, GeometryPipe> GEOMETRY = new ConcurrentHashMap<>();
    private static final AtomicLong EPOCHS = new AtomicLong(new SecureRandom().nextLong(1, Long.MAX_VALUE / 2));

    private GuestConnections() {}

    public static boolean allowed(MinecraftServer server) {
        // Yarn 1.21.4 names Mojang's isReady() getter isLoading(); true means ready.
        return GuestAccess.allowed(server.isSingleplayer(), server.isDedicated(), server.isRemote(),
                server.isLoading(), server.isStopping(), server.isStopped());
    }

    public static Session attach(MinecraftServer server, InputStream input, OutputStream output) {
        requireThread(server);
        if (!allowed(server)) throw new IllegalStateException("Bridge requires a private integrated server");
        Objects.requireNonNull(input);
        Objects.requireNonNull(output);
        PipeLink link = new PipeLink(input, output);
        try { return attach(server, link); }
        catch (RuntimeException error) { link.close(); throw error; }
    }

    /** The caller retains the link handle for cancellation and termination verification. */
    public static Session attach(MinecraftServer server, PipeLink link) {
        requireThread(server);
        if (!allowed(server)) throw new IllegalStateException("Bridge requires a private integrated server");
        Objects.requireNonNull(link);
        detach(server);
        long epoch = EPOCHS.incrementAndGet();
        if (epoch <= 0) throw new IllegalStateException("Session epoch space exhausted");
        Session session = new Session(epoch, new MinecraftBlocks(server), link, System.nanoTime(),
                key -> {
                    if (GEOMETRY.containsKey(server) && !publishCollision(server, epoch, key)) {
                        throw new IllegalStateException("Geometry snapshot could not be queued");
                    }
                });
        SESSIONS.put(server, session);
        return session;
    }

    public static void tick(MinecraftServer server) {
        requireThread(server);
        Session session = SESSIONS.get(server);
        if (session != null) {
            session.tick(System.nanoTime(), allowed(server));
            if (!session.isOpen()) {
                detach(server);
                return;
            }
        }
        GeometryPipe geometry = GEOMETRY.get(server);
        if (geometry != null && (session == null || !geometry.isOpen()
                || geometry.epoch() != session.epoch())) {
            detach(server);
        }
    }

    public static GeometryPipe attachGeometry(
            MinecraftServer server, OutputStream output, Runnable cancelIo) {
        requireThread(server);
        if (!allowed(server)) throw new IllegalStateException("Geometry requires single-player");
        Session session = SESSIONS.get(server);
        if (session == null || !session.isWelcomed()) {
            throw new IllegalStateException("Geometry requires a welcomed bridge session");
        }
        if (GEOMETRY.containsKey(server)) {
            throw new IllegalStateException("Geometry already attached");
        }
        GeometryPipe geometry = new GeometryPipe(session.epoch(), output, cancelIo);
        GEOMETRY.put(server, geometry);
        return geometry;
    }

    public static boolean publishCollision(MinecraftServer server, long epoch, Frame.Key key) {
        requireThread(server);
        GeometryPipe geometry = GEOMETRY.get(server);
        if (geometry == null) return false;
        try {
            var result = collisionSnapshot(server, epoch, key);
            if (result.status() != Frame.OK
                    || !geometry.offer(result.snapshot().geometryFrame(geometry.nextRevision()))) {
                detach(server);
                return false;
            }
            return true;
        } catch (RuntimeException error) {
            detach(server);
            throw error;
        }
    }

    public static MinecraftCollisionShapes.Result collisionSnapshot(
            MinecraftServer server, long epoch, Frame.Key key) {
        requireThread(server);
        if (key == null || key.dimension() == null) {
            return MinecraftCollisionShapes.Result.error(Frame.INVALID);
        }
        if (!allowed(server)) {
            detach(server);
            return MinecraftCollisionShapes.Result.error(Frame.FORBIDDEN);
        }
        Session session = SESSIONS.get(server);
        if (session == null || session.epoch() != epoch) {
            return MinecraftCollisionShapes.Result.error(Frame.STALE_EPOCH);
        }
        if (!session.watches(key)) {
            return MinecraftCollisionShapes.Result.error(Frame.FORBIDDEN);
        }
        try {
            return MinecraftCollisionShapes.read(server, epoch, key);
        } catch (RuntimeException error) {
            detach(server);
            throw error;
        }
    }

    public static void detach(MinecraftServer server) {
        requireThread(server);
        GeometryPipe geometry = GEOMETRY.remove(server);
        if (geometry != null) geometry.close();
        Session session = SESSIONS.remove(server);
        if (session != null) session.close();
    }

    public static void changed(ServerWorld world, WorldChunk chunk, BlockPos pos, BlockState previous) {
        MinecraftServer server = world.getServer();
        Session session = SESSIONS.get(server);
        if (session == null) return;
        requireThread(server);
        if (!allowed(server)) { detach(server); return; }
        Frame.Key key = new Frame.Key(world.getRegistryKey().getValue().toString(), pos.getX(), pos.getY(), pos.getZ());
        if (session.watches(key)) {
            BlockState current = chunk.getBlockState(pos);
            session.changed(key, MinecraftBlocks.describe(previous), MinecraftBlocks.describe(current));
            if (session.isOpen() && GEOMETRY.containsKey(server)
                    && !publishCollision(server, session.epoch(), key)) {
                detach(server);
            }
        }
    }

    static void requireThread(MinecraftServer server) {
        if (!server.isOnThread()) throw new IllegalStateException("Minecraft world access requires its server thread");
    }
}
