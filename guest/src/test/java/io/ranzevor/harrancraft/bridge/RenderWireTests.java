package io.ranzevor.harrancraft.bridge;

import java.io.ByteArrayInputStream;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HexFormat;
import java.util.List;

public final class RenderWireTests {
    private static int checks;
    private static final Frame.Key KEY =
            new Frame.Key("minecraft:overworld", -17, 80, 33);
    private static final RenderFrame.Identity ID =
            new RenderFrame.Identity(0x0102030405060708L, 7, 9, 42, 43);

    private static void require(boolean okay, String message) {
        ++checks;
        if (!okay) throw new AssertionError(message);
    }

    private static void rejected(Runnable operation) {
        try { operation.run(); }
        catch (IllegalArgumentException error) { ++checks; return; }
        throw new AssertionError("Invalid render data accepted");
    }

    static RenderWire golden() {
        List<RenderFrame.Vertex> vertices = new ArrayList<>();
        for (int index = 0; index < 4; ++index) {
            vertices.add(new RenderFrame.Vertex(
                    index == 1 || index == 2 ? 1f : 0f,
                    index >= 2 ? 1f : 0f, 0.5f,
                    index / 4f, index / 8f,
                    0xff00aa55, 0x00f000f0, 0x007f0000));
        }
        var quad = new RenderFrame.Quad(-1, 2, -1, true, 0,
                "minecraft:textures/atlas/blocks.png", "minecraft:block/stone", vertices);
        return RenderWire.snapshot(new RenderFrame(ID, KEY,
                "minecraft:oak_slab[type=bottom,waterlogged=false]",
                new RenderFrame.CameraPose(0.5, 82, -4, 90, -10),
                List.of(quad)));
    }

    public static void main(String[] args) throws Exception {
        if (args.length == 1 && args[0].equals("--golden")) {
            System.out.println(HexFormat.of().formatHex(golden().encode()));
            return;
        }
        require(args.length == 0, "Unexpected render test argument");
        var first = golden();
        byte[] bytes = first.encode();
        require(bytes.length == RenderWire.HEADER_BYTES + RenderWire.QUAD_BYTES,
                "Render record length changed");
        require(RenderWire.decode(bytes).equals(first), "Golden record changed");
        var air = new RenderWire(RenderWire.SNAPSHOT, ID, KEY, "minecraft:air",
                first.camera(), List.of());
        require(RenderWire.decode(air.encode()).equals(air),
                "Empty model was not preserved");
        var retired = RenderWire.retire(ID, KEY);
        require(RenderWire.decode(retired.encode()).equals(retired),
                "Retirement round trip failed");
        var quads = new ArrayList<RenderFrame.Quad>();
        for (int index = 0; index < RenderWire.MAX_QUADS; ++index) {
            quads.add(first.quads().getFirst());
        }
        var maximum = new RenderWire(RenderWire.SNAPSHOT, ID, KEY, first.state(),
                first.camera(), quads);
        require(maximum.encode().length == RenderWire.MAX_BYTES,
                "Maximum render record bytes changed");
        require(RenderWire.decode(maximum.encode()).equals(maximum),
                "Maximum render payload failed to decode");
        quads.add(first.quads().getFirst());
        rejected(() -> new RenderWire(RenderWire.SNAPSHOT, ID, KEY, first.state(),
                first.camera(), quads));
        rejected(() -> new RenderWire(RenderWire.RETIRE, ID, KEY, "minecraft:stone",
                retired.camera(), List.of()));
        rejected(() -> new RenderWire(RenderWire.RETIRE, ID, KEY, "",
                first.camera(), List.of()));
        rejected(() -> new RenderWire(RenderWire.SNAPSHOT, ID, KEY, "",
                first.camera(), List.of()));

        for (int length = 0; length < bytes.length; ++length) {
            byte[] damaged = Arrays.copyOf(bytes, length);
            rejected(() -> RenderWire.decode(damaged));
        }
        for (int at : new int[]{0, 4, 6, 8, 12, 68, 70, 72, 74, 108, 127,
                367, 368, 383, 390, 396, 400, 527, 655}) {
            byte[] damaged = bytes.clone();
            damaged[at] ^= (byte) 0xff;
            rejected(() -> RenderWire.decode(damaged));
        }
        for (int at : new int[]{76, 84, 92}) {
            byte[] damaged = bytes.clone();
            ByteBuffer.wrap(damaged).order(ByteOrder.LITTLE_ENDIAN)
                    .putDouble(at, Double.NaN);
            rejected(() -> RenderWire.decode(damaged));
        }
        for (int at : new int[]{100, 104}) {
            byte[] damaged = bytes.clone();
            ByteBuffer.wrap(damaged).order(ByteOrder.LITTLE_ENDIAN)
                    .putFloat(at, Float.POSITIVE_INFINITY);
            rejected(() -> RenderWire.decode(damaged));
        }
        for (int at : new int[]{0, 4, 8, 12, 16}) {
            byte[] damaged = bytes.clone();
            ByteBuffer.wrap(damaged).order(ByteOrder.LITTLE_ENDIAN)
                    .putFloat(RenderWire.HEADER_BYTES + 272 + at, Float.NaN);
            rejected(() -> RenderWire.decode(damaged));
        }
        require(RenderWire.read(new ByteArrayInputStream(new byte[0])) == null,
                "Empty record stream is not EOF");
        try {
            RenderWire.read(new ByteArrayInputStream(Arrays.copyOf(bytes, 100)));
            throw new AssertionError("Partial record header accepted");
        } catch (IOException expected) { ++checks; }
        try {
            RenderWire.read(new ByteArrayInputStream(
                    Arrays.copyOf(bytes, bytes.length - 1)));
            throw new AssertionError("Partial render payload accepted");
        } catch (IOException expected) { ++checks; }
        require(RenderWire.read(new ByteArrayInputStream(bytes)).equals(first),
                "Render stream read did not preserve data");
        System.out.println("render-wire: passed " + checks + " assertions");
    }
}
