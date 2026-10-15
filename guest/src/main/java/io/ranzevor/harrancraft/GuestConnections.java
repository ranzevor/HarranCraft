package io.ranzevor.harrancraft;

import io.ranzevor.harrancraft.bridge.Frame;
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
    private static final AtomicLong EPOCHS = new AtomicLong(new SecureRandom().nextLong(1, Long.MAX_VALUE / 2));

    private GuestConnections() {}

    public static boolean allowed(MinecraftServer server) {
        return server.isSingleplayer() && !server.isDedicated() && !server.isRemote()
                && !server.isLoading() && !server.isStopping() && !server.isStopped();
    }

    public static Session attach(MinecraftServer server, InputStream input, OutputStream output) {
        requireThread(server);
        if (!allowed(server)) throw new IllegalStateException("Bridge requires a private integrated server");
        Objects.requireNonNull(input);
        Objects.requireNonNull(output);
        detach(server);
        long epoch = EPOCHS.incrementAndGet();
        if (epoch <= 0) throw new IllegalStateException("Session epoch space exhausted");
        Session session = new Session(epoch, new MinecraftBlocks(server), new PipeLink(input, output), System.nanoTime());
        SESSIONS.put(server, session);
        return session;
    }

    public static void tick(MinecraftServer server) {
        requireThread(server);
        Session session = SESSIONS.get(server);
        if (session != null) {
            session.tick(System.nanoTime(), allowed(server));
            if (!session.isOpen()) SESSIONS.remove(server, session);
        }
    }

    public static void detach(MinecraftServer server) {
        requireThread(server);
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
        }
    }

    static void requireThread(MinecraftServer server) {
        if (!server.isOnThread()) throw new IllegalStateException("Minecraft world access requires its server thread");
    }
}
