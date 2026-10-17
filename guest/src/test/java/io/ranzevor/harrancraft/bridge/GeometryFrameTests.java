package io.ranzevor.harrancraft.bridge;

import java.io.ByteArrayInputStream;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HexFormat;
import java.util.List;

public final class GeometryFrameTests {
    private static int checks;

    private static void require(boolean good, String detail) {
        ++checks;
        if (!good) throw new AssertionError(detail);
    }

    private static void rejected(Runnable action) {
        try { action.run(); } catch (IllegalArgumentException error) { ++checks; return; }
        throw new AssertionError("Invalid geometry record was accepted");
    }

    private static GeometryFrame golden() {
        return GeometryFrame.snapshot(0x0102030405060708L, 42,
                new Frame.Key("minecraft:overworld", -17, 64, 33),
                "minecraft:oak_slab[type=bottom,waterlogged=false]",
                List.of(new GeometryFrame.Box(0, 0, 0, 1, 0.5, 1)));
    }

    public static void main(String[] args) throws Exception {
        if (args.length == 1 && args[0].equals("--golden")) {
            System.out.println(HexFormat.of().formatHex(golden().encode()));
            return;
        }
        require(args.length == 0, "Unknown geometry test argument");
        require(GeometryFrame.decode(golden().encode()).equals(golden()), "Round trip");
        var retire = GeometryFrame.retire(7, 5, golden().key());
        require(GeometryFrame.decode(retire.encode()).equals(retire), "Retirement round trip");
        var air = GeometryFrame.snapshot(7, 6, golden().key(), "minecraft:air", List.of());
        require(GeometryFrame.decode(air.encode()).equals(air), "Empty geometry round trip");

        var boxes = new ArrayList<GeometryFrame.Box>();
        for (int i = 0; i < GeometryFrame.MAX_BOXES; ++i) {
            boxes.add(new GeometryFrame.Box(0, 0, 0, 1, 1, 1));
        }
        var max = GeometryFrame.snapshot(7, 7, golden().key(), "minecraft:stone", boxes);
        require(max.encode().length == GeometryFrame.MAX_BYTES, "Maximum frame size");
        require(GeometryFrame.decode(max.encode()).equals(max), "Maximum frame round trip");
        boxes.add(new GeometryFrame.Box(0, 0, 0, 1, 1, 1));
        rejected(() -> GeometryFrame.snapshot(7, 7, golden().key(), "minecraft:stone", boxes));
        rejected(() -> GeometryFrame.retire(0, 7, golden().key()));
        rejected(() -> new GeometryFrame(GeometryFrame.RETIRE, 7, 8, golden().key(),
                "minecraft:air", List.of()));
        rejected(() -> new GeometryFrame(GeometryFrame.SNAPSHOT, 7, 8, golden().key(),
                "", List.of()));
        rejected(() -> new GeometryFrame(GeometryFrame.SNAPSHOT, 7, 8, golden().key(),
                "minecraft:stone", List.of(new GeometryFrame.Box(0, 0, 0, 0, 1, 1))));

        byte[] valid = golden().encode();
        for (int length = 0; length < valid.length; ++length) {
            byte[] truncated = Arrays.copyOf(valid, length);
            rejected(() -> GeometryFrame.decode(truncated));
        }
        for (int at : new int[]{0, 4, 6, 8, 12, 44, 46, 48, 50, 52, 56, 127, 319}) {
            byte[] invalid = valid.clone();
            invalid[at] ^= (byte) 0xff;
            rejected(() -> GeometryFrame.decode(invalid));
        }
        for (int index = 0; index < 6; ++index) {
            byte[] invalid = valid.clone();
            ByteBuffer.wrap(invalid).order(ByteOrder.LITTLE_ENDIAN)
                    .putDouble(GeometryFrame.HEADER_BYTES + index * 8, Double.NaN);
            rejected(() -> GeometryFrame.decode(invalid));
        }
        require(GeometryFrame.read(new ByteArrayInputStream(new byte[0])) == null,
                "Empty stream is EOF");
        try {
            GeometryFrame.read(new ByteArrayInputStream(Arrays.copyOf(valid, 20)));
            throw new AssertionError("Truncated header accepted");
        } catch (IOException expected) { ++checks; }
        try {
            GeometryFrame.read(new ByteArrayInputStream(Arrays.copyOf(valid, valid.length - 1)));
            throw new AssertionError("Truncated boxes accepted");
        } catch (IOException expected) { ++checks; }
        require(GeometryFrame.read(new ByteArrayInputStream(valid)).equals(golden()),
                "Stream round trip");
        System.out.println("geometry-java: passed " + checks + " assertions");
    }
}
