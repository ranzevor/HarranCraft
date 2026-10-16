package io.ranzevor.harrancraft.test;

import io.ranzevor.harrancraft.GuestConnections;
import io.ranzevor.harrancraft.MinecraftBlocks;
import io.ranzevor.harrancraft.bridge.Frame;
import io.ranzevor.harrancraft.bridge.Link;
import io.ranzevor.harrancraft.bridge.PipeLink;
import io.ranzevor.harrancraft.bridge.Session;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.ServerSocket;
import java.util.ArrayDeque;
import java.util.HashSet;
import net.fabricmc.fabric.api.client.gametest.v1.FabricClientGameTest;
import net.fabricmc.fabric.api.client.gametest.v1.context.ClientGameTestContext;
import net.fabricmc.fabric.api.client.gametest.v1.context.TestSingleplayerContext;
import net.fabricmc.fabric.api.client.gametest.v1.world.TestWorldSave;
import net.minecraft.block.Block;
import net.minecraft.block.Blocks;
import net.minecraft.block.entity.ChestBlockEntity;
import net.minecraft.client.network.LanServerPinger;
import net.minecraft.item.ItemStack;
import net.minecraft.item.Items;
import net.minecraft.server.MinecraftServer;
import net.minecraft.util.math.BlockPos;
import net.minecraft.world.GameMode;

public final class WorldLifecycleTests implements FabricClientGameTest {
    private static final BlockPos POS = new BlockPos(0, 80, 0);
    private static final Frame.Key KEY = new Frame.Key("minecraft:overworld", 0, 80, 0);
    private final HashSet<Long> epochs = new HashSet<>();

    private static void require(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }

    private static void passed(String scenario) {
        System.out.println("HARRANCRAFT_SCENARIO " + scenario);
    }

    private static void position(ClientGameTestContext context, TestSingleplayerContext world) {
        world.getServer().runCommand("gamemode spectator @a");
        world.getServer().runCommand("tp @a 0.5 79 -4 0 0");
        for (int tick = 0; tick < 200; ++tick) {
            if (world.getServer().computeOnServer(server -> server.getOverworld().isChunkLoaded(POS))) return;
            context.waitTick();
        }
        throw new AssertionError("Test position did not load");
    }

    private static void prepare(ClientGameTestContext context, TestSingleplayerContext world) {
        position(context, world);
        world.getServer().runCommand("setblock 0 80 0 minecraft:air");
        world.getServer().runOnServer(server -> {
            require(GuestConnections.allowed(server), "Created world is not a private integrated server");
            require(server.getOverworld().isChunkLoaded(POS), "Test position is not loaded");
            require(server.getOverworld().getBlockState(POS).isAir(), "Test position is not air");
        });
    }

    private Session attach(TestSingleplayerContext world, GamePeer peer) {
        Session session = world.getServer().computeOnServer(peer::attach);
        require(epochs.add(session.epoch()), "A connection reused a prior epoch");
        return session;
    }

    private static void cleared(ClientGameTestContext context, TestSingleplayerContext world, Session session) {
        long deadline = System.nanoTime() + java.util.concurrent.TimeUnit.SECONDS.toNanos(5);
        for (int tick = 0; tick < 200 && System.nanoTime() < deadline; ++tick) {
            boolean closed = world.getServer().computeOnServer(server -> !session.isOpen() && session.watchedCount() == 0);
            if (closed && System.nanoTime() < deadline) return;
            context.waitTick();
        }
        throw new AssertionError("Automatic tick cleanup retained subscriptions");
    }

    private long cycle(ClientGameTestContext context, TestSingleplayerContext world) throws Exception {
        try (GamePeer peer = new GamePeer("cycle", 0)) {
            Session session = attach(world, peer);
            peer.stage(context, "placed");
            world.getServer().runOnServer(server -> require(
                    server.getOverworld().getBlockState(POS).isOf(Blocks.DIAMOND_BLOCK),
                    "Reconnected placement did not reach the world"));
            world.getServer().runCommand("setblock 0 80 0 minecraft:gold_block");
            peer.finished(context);
            cleared(context, world, session);
            world.getServer().runOnServer(server -> require(
                    server.getOverworld().getBlockState(POS).isAir(), "Reconnected removal failed"));
            return session.epoch();
        }
    }

    private void stale(ClientGameTestContext context, TestSingleplayerContext world, long old) throws Exception {
        try (GamePeer peer = new GamePeer("stale", old)) {
            Session session = attach(world, peer);
            peer.finished(context);
            cleared(context, world, session);
            world.getServer().runOnServer(server -> require(
                    server.getOverworld().getBlockState(POS).isAir(), "Old epoch changed the new session"));
        }
    }

    private static void stopped(MinecraftServer server, Session session) throws InterruptedException {
        server.getThread().join(5000);
        require(!server.getThread().isAlive() && server.isStopped(), "Generated server did not stop");
        // Joining the owner publishes its final session state; no calls are queued to a stopped server.
        require(!session.isOpen(), "Stopping the world left its bridge open");
    }

    private static void blockEntity(TestSingleplayerContext world) {
        world.getServer().runOnServer(server -> {
            var level = server.getOverworld();
            require(level.setBlockState(POS, Blocks.CHEST.getDefaultState(), Block.NOTIFY_ALL),
                    "Chest fixture was not placed");
            require(level.getBlockEntity(POS) instanceof ChestBlockEntity, "Chest entity is missing");
            ChestBlockEntity chest = (ChestBlockEntity) level.getBlockEntity(POS);
            chest.setStack(0, new ItemStack(Items.DIAMOND, 7));
            chest.markDirty();
            MinecraftBlocks blocks = new MinecraftBlocks(server);
            var before = blocks.read(KEY);
            require(before.status() == Frame.OK, "Chest state cannot be read");
            var refused = blocks.compareAndSet(KEY, before.state(), "minecraft:air");
            require(refused.status() == Frame.UNSUPPORTED_STATE, "Existing block entity was overwritten");
            require(level.getBlockEntity(POS) == chest && blocks.read(KEY).state().equals(before.state()),
                    "Rejected replacement changed the chest identity or state");
            require(chest.getStack(0).isOf(Items.DIAMOND) && chest.getStack(0).getCount() == 7,
                    "Rejected replacement changed inventory data");
            level.setBlockState(POS, Blocks.AIR.getDefaultState(), Block.NOTIFY_ALL);
        });
        passed("block-entity-preservation");
    }

    private static void wrongThread(TestSingleplayerContext world) throws Exception {
        MinecraftServer server = world.getServer().computeOnServer(value -> value);
        try (GamePeer peer = new GamePeer("observe", 0)) {
            PipeLink offered = peer.connection();
            try {
                GuestConnections.attach(server, offered);
                throw new AssertionError("Game-test thread obtained server mutation ownership");
            } catch (IllegalStateException expected) {
                require(offered.isOpen(), "Rejected attachment consumed caller-owned transport");
            }
            world.getServer().runOnServer(value -> require(
                    value.getOverworld().getBlockState(POS).isAir(), "Rejected attachment changed the world"));
        }
        passed("thread-ownership");
    }

    @Override public void runTest(ClientGameTestContext context) {
        try {
            TestWorldSave save;
            MinecraftServer first;
            Session ending;
            long last;
            try (GamePeer observer = new GamePeer("observe", 0)) {
                try (TestSingleplayerContext world = context.worldBuilder().create()) {
                    prepare(context, world);
                    for (int round = 0; round < 3; ++round) cycle(context, world);
                    passed("reconnect");
                    blockEntity(world);
                    wrongThread(world);
                    ending = attach(world, observer);
                    observer.stage(context, "observing");
                    world.getServer().runOnServer(server -> require(
                            ending.isWelcomed() && ending.watchedCount() == 1, "Observer was not live before world exit"));
                    world.getServer().runCommand("setblock 0 80 0 minecraft:gold_block");
                    observer.stage(context, "observed");
                    first = world.getServer().computeOnServer(server -> server);
                    save = world.getWorldSave();
                }
                stopped(first, ending);
                observer.finished(context);
                last = ending.epoch();
            }
            passed("world-stop");
            try (TestSingleplayerContext reopened = save.open()) {
                position(context, reopened);
                reopened.getServer().runOnServer(server -> {
                    require(server != first && GuestConnections.allowed(server), "Reopened server identity is invalid");
                    require(server.getOverworld().getBlockState(POS).isOf(Blocks.GOLD_BLOCK),
                            "Generated save lost its authoritative block");
                });
                prepare(context, reopened);
                stale(context, reopened, last);
                last = cycle(context, reopened);
            }
            passed("world-reopen");
            try (TestSingleplayerContext replacement = context.worldBuilder().create()) {
                require(!replacement.getWorldSave().getSaveDirectory().equals(save.getSaveDirectory()),
                        "Replacement world reused the prior save");
                prepare(context, replacement);
                stale(context, replacement, last);
                cycle(context, replacement);
                require(!ending.isOpen(), "New world reactivated an old session");
            }
            passed("world-replacement");
            if (Boolean.getBoolean("harrancraft.test.lan")) lan(context);
        } catch (Exception error) {
            throw new AssertionError("World lifecycle integration failed", error);
        }
    }

    /** A controlled queue paired with the real Minecraft adapter and the actual post-LAN mode. */
    private static final class QueuedLink implements Link {
        final ArrayDeque<Frame> incoming = new ArrayDeque<>();
        final ArrayDeque<Frame> outgoing = new ArrayDeque<>();
        boolean open = true;
        @Override public Frame poll() { return open ? incoming.poll() : null; }
        @Override public boolean send(Frame frame) { return open && outgoing.size() < 256 && outgoing.offer(frame); }
        @Override public boolean isOpen() { return open; }
        @Override public int outputCapacity() { return 256 - outgoing.size(); }
        @Override public void close() { open = false; incoming.clear(); outgoing.clear(); }
    }

    private static boolean lanPingerAlive() {
        return Thread.getAllStackTraces().keySet().stream().anyMatch(thread -> thread.isAlive()
                && thread instanceof LanServerPinger);
    }

    private void lan(ClientGameTestContext context) throws Exception {
        int port;
        try (ServerSocket reserved = new ServerSocket(0)) { port = reserved.getLocalPort(); }
        MinecraftServer opened;
        Session active;
        try (GamePeer observer = new GamePeer("observe", 0)) {
            try (TestSingleplayerContext world = context.worldBuilder().create()) {
                prepare(context, world);
                active = attach(world, observer);
                observer.stage(context, "observing");
                opened = world.getServer().computeOnServer(server -> server);
                world.getServer().runOnServer(server -> {
                    require(active.isWelcomed(), "Observer was not live before LAN transition");
                    QueuedLink queue = new QueuedLink();
                    Session queued = new Session(1, new MinecraftBlocks(server), queue, System.nanoTime());
                    try {
                        queue.incoming.add(Frame.control(Frame.HELLO, 0, 0, Frame.NATIVE));
                        queue.incoming.add(new Frame(Frame.GET, 1, 1, Frame.NATIVE, Frame.OK, KEY, "", "", 0));
                        queued.tick(System.nanoTime(), GuestConnections.allowed(server));
                        require(queued.isWelcomed() && queued.watchedCount() == 1, "Queued session was not established");
                        queue.incoming.add(new Frame(Frame.SET, 1, 2, Frame.NATIVE, Frame.OK,
                                KEY, "minecraft:air", "minecraft:diamond_block", 0));
                        require(server.openToLan(GameMode.SPECTATOR, false, port), "Opening isolated world to LAN failed");
                        require(server.isRemote() && !GuestConnections.allowed(server), "LAN mode was not rejected");
                        queued.tick(System.nanoTime(), GuestConnections.allowed(server));
                        require(!queued.isOpen() && queued.watchedCount() == 0 && queue.incoming.isEmpty(),
                                "LAN transition retained queued mutation or subscriptions");
                        require(server.getOverworld().getBlockState(POS).isAir(), "Queued mutation ran after LAN transition");
                        MinecraftBlocks blocks = new MinecraftBlocks(server);
                        require(blocks.read(KEY).status() == Frame.FORBIDDEN, "LAN read was not refused");
                        require(blocks.compareAndSet(KEY, "minecraft:air", "minecraft:stone").status() == Frame.FORBIDDEN,
                                "LAN adapter mutation was not refused");
                    } finally { queued.close(); }
                });
                // The registered end-tick callback must revoke the real pipe without a manual bridge tick.
                cleared(context, world, active);
                observer.finished(context);
                try (GamePeer refused = new GamePeer("observe", 0)) {
                    PipeLink offered = refused.connection();
                    world.getServer().runOnServer(server -> {
                        try {
                            GuestConnections.attach(server, offered);
                            throw new AssertionError("LAN server accepted a new bridge");
                        } catch (IllegalStateException expected) {
                            require(offered.isOpen(), "LAN refusal consumed caller-owned transport");
                        }
                        require(server.getOverworld().getBlockState(POS).isAir(), "LAN refusal changed the world");
                    });
                }
            }
            stopped(opened, active);
        }
        try (ServerSocket released = new ServerSocket()) {
            released.setReuseAddress(false);
            released.bind(new InetSocketAddress(InetAddress.getLoopbackAddress(), port));
        }
        long deadline = System.nanoTime() + java.util.concurrent.TimeUnit.SECONDS.toNanos(5);
        while (lanPingerAlive() && System.nanoTime() < deadline) Thread.sleep(1);
        require(!lanPingerAlive(), "Generated LAN discovery worker did not stop");
        passed("lan-revocation");
        try (TestSingleplayerContext privateWorld = context.worldBuilder().create()) {
            prepare(context, privateWorld);
            stale(context, privateWorld, active.epoch());
            cycle(context, privateWorld);
        }
        passed("lan-recovery");
    }
}
