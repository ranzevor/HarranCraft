package io.ranzevor.harrancraft.bridge;

import java.io.ByteArrayInputStream;
import java.io.IOException;
import java.util.ArrayDeque;
import java.util.Arrays;
import java.util.HashMap;
import java.util.HexFormat;
import java.util.Map;
import java.util.concurrent.atomic.AtomicReference;

public final class CoreTests {
    private static int checks;
    private static final Frame.Key KEY = new Frame.Key("minecraft:overworld", 0, 80, 0);

    private static void check(boolean condition, String message) {
        ++checks;
        if (!condition) throw new AssertionError(message);
    }
    private static void fails(Runnable action) {
        try { action.run(); } catch (IllegalArgumentException e) { ++checks; return; }
        throw new AssertionError("Malformed frame accepted");
    }
    private static Frame command(int kind, long id, String expected, String state) {
        return new Frame(kind, 7, id, Frame.NATIVE, Frame.OK, KEY, expected, state, 0);
    }
    private static Frame golden() {
        return new Frame(Frame.SET, 0x0102030405060708L, 42, Frame.NATIVE, Frame.OK,
                new Frame.Key("minecraft:overworld", -17, 64, 33), "minecraft:air",
                "minecraft:oak_log[axis=x]", 0);
    }
    private static final class MemoryLink implements Link {
        final ArrayDeque<Frame> incoming = new ArrayDeque<>(), outgoing = new ArrayDeque<>();
        boolean open = true;
        int capacity = 256;
        @Override public Frame poll() { return incoming.poll(); }
        @Override public boolean send(Frame frame) {
            if (!open || outgoing.size() >= capacity) { close(); return false; }
            outgoing.add(frame); return true;
        }
        @Override public int outputCapacity() { return capacity - outgoing.size(); }
        @Override public boolean isOpen() { return open; }
        @Override public void close() { open = false; incoming.clear(); }
    }
    private static final class World implements BlockWorld {
        final Map<Frame.Key, String> blocks = new HashMap<>();
        Session session;
        int writes;
        @Override public Outcome read(Frame.Key key) { return new Outcome(Frame.OK, blocks.getOrDefault(key, "minecraft:air")); }
        @Override public Outcome compareAndSet(Frame.Key key, String expected, String state) {
            String before = read(key).state();
            if (!before.equals(expected)) return new Outcome(Frame.CONFLICT, before);
            if (!before.equals(state)) {
                ++writes;
                blocks.put(key, state);
                session.changed(key, before, state);
            }
            return new Outcome(Frame.OK, state);
        }
    }
    private static final class Setup {
        final MemoryLink link = new MemoryLink();
        final World world = new World();
        final Session session = new Session(7, world, link, 0);
        Setup() {
            world.session = session;
            link.incoming.add(Frame.control(Frame.HELLO, 0, 0, Frame.NATIVE));
            session.tick(1, true);
            check(link.outgoing.remove().kind() == Frame.WELCOME, "Handshake failed");
        }
        Frame send(Frame request) {
            link.incoming.add(request);
            session.tick(10, true);
            Frame result = null;
            while (!link.outgoing.isEmpty()) {
                Frame f = link.outgoing.remove();
                if (f.kind() != Frame.EVENT) result = f;
            }
            return result;
        }
    }
    public static void main(String[] args) throws Exception {
        if (args.length == 1 && args[0].equals("--golden")) {
            System.out.println(HexFormat.of().formatHex(golden().encode())); return;
        }
        check(Frame.decode(golden().encode()).equals(golden()), "Frame round trip");
        for (int offset : new int[]{0, 4, 50, 60, 127, 319, 511}) {
            byte[] b = golden().encode(); b[offset] ^= 0x7f; fails(() -> Frame.decode(b));
        }
        for (int length = 0; length < Frame.BYTES; ++length) {
            byte[] b = Arrays.copyOf(golden().encode(), length); fails(() -> Frame.decode(b));
        }
        for (int offset : new int[]{44, 46, 48}) {
            byte[] b = golden().encode(); b[offset] = (byte) 255; b[offset + 1] = (byte) 255;
            fails(() -> Frame.decode(b));
        }
        check(Frame.read(new ByteArrayInputStream(new byte[0])) == null, "Empty pipe is EOF");
        try { Frame.read(new ByteArrayInputStream(new byte[10])); throw new AssertionError(); }
        catch (IOException expected) { ++checks; }
        Setup s = new Setup();
        Frame place = command(Frame.SET, 1, "minecraft:air", "minecraft:stone");
        Frame receipt = s.send(place);
        check(receipt.status() == Frame.OK && s.world.writes == 1, "Actual adapter write");
        check(s.send(place).equals(receipt) && s.world.writes == 1, "Duplicate write");
        check(s.send(command(Frame.SET, 1, "minecraft:air", "minecraft:gold_block")).status() == Frame.CONFLICT,
                "Changed duplicate");
        check(s.send(command(Frame.SET, 2, "minecraft:air", "minecraft:gold_block")).status() == Frame.CONFLICT,
                "CAS rejection");
        check(s.world.writes == 1, "Rejected request mutated state");
        Frame old = new Frame(Frame.SET, 6, 3, Frame.NATIVE, Frame.OK, KEY, "minecraft:stone", "minecraft:air", 0);
        check(s.send(old).status() == Frame.STALE_EPOCH && s.world.writes == 1, "Old epoch");
        for (int id = 3; id < 520; ++id) {
            check(s.send(command(Frame.GET, id, "", "")).status() == Frame.OK, "Receipt eviction read");
        }
        check(s.send(place).status() == Frame.RETIRED_ID && s.world.writes == 1, "Evicted receipt replay");
        s.world.blocks.put(KEY, "minecraft:gold_block");
        s.session.changed(KEY, "minecraft:stone", "minecraft:gold_block");
        Frame external = s.link.outgoing.remove();
        check(external.origin() == Frame.GUEST && external.cause() == 0, "External event origin");
        s.session.close();
        check(s.session.watchedCount() == 0 && !s.session.isOpen(), "Disconnect cleanup");
        check(s.world.blocks.get(KEY).equals("minecraft:gold_block"), "Disconnect rewrote owned Minecraft state");
        Setup denied = new Setup();
        denied.link.incoming.add(place);
        denied.session.tick(2, false);
        check(denied.world.writes == 0 && !denied.session.isOpen(), "Mode refusal");
        Setup full = new Setup();
        full.link.capacity = 128;
        full.link.incoming.add(place);
        full.session.tick(2, true);
        check(full.world.writes == 0 && !full.session.isOpen(), "Backpressure before mutation");
        Setup timeout = new Setup();
        timeout.link.incoming.add(place);
        timeout.session.tick(Session.TIMEOUT_NANOS + 2, true);
        check(timeout.world.writes == 0 && !timeout.session.isOpen(), "Heartbeat deadline");
        Setup bounded = new Setup();
        for (int id = 1; id <= 40; ++id) bounded.link.incoming.add(command(Frame.GET, id, "", ""));
        bounded.session.tick(2, true);
        check(bounded.link.incoming.size() == 8, "Per-tick bound");
        AtomicReference<Throwable> error = new AtomicReference<>();
        Thread wrong = new Thread(() -> { try { bounded.session.tick(3, true); } catch (Throwable t) { error.set(t); } });
        wrong.start(); wrong.join();
        check(error.get() instanceof IllegalStateException, "Owner-thread refusal");
        Setup limited = new Setup();
        for (int x = 0; x <= Session.MAX_WATCHED; ++x) {
            Frame query = new Frame(Frame.GET, 7, x + 1, Frame.NATIVE, Frame.OK,
                    new Frame.Key("minecraft:overworld", x, 80, 0), "", "", 0);
            int expected = x == Session.MAX_WATCHED ? Frame.CAPACITY : Frame.OK;
            check(limited.send(query).status() == expected, "Subscription bound");
        }
        System.out.println("bridge-core: passed " + checks + " assertions");
    }
}
