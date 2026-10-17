package io.ranzevor.harrancraft.bridge;

import java.io.ByteArrayInputStream;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HexFormat;
import java.util.List;

public final class TextureWireTests {
    private static int checks;
    private static final RenderFrame.Identity ID =
            new RenderFrame.Identity(0x0102030405060708L, 7, 9, 42, 43);

    private static void check(boolean condition, String detail) {
        ++checks;
        if (!condition) throw new AssertionError(detail);
    }

    private static void rejected(Runnable task) {
        try { task.run(); }
        catch (IllegalArgumentException expected) { ++checks; return; }
        throw new AssertionError("Invalid texture record accepted");
    }

    static TextureWire golden() {
        return new TextureWire(TextureWire.SNAPSHOT, ID,
                "minecraft:textures/atlas/blocks.png", "minecraft:block/stone",
                16, 32, 2, 2, 0.125f, 0.25f, 0.375f, 0.5f,
                List.of(0xff112233, 0x80123456, 0x00000000, 0xffffffff));
    }

    public static void main(String[] args) throws Exception {
        if (args.length == 1 && args[0].equals("--golden")) {
            System.out.println(HexFormat.of().formatHex(golden().encode()));
            return;
        }
        check(args.length == 0, "Unknown texture test argument");
        TextureWire first = golden();
        byte[] bytes = first.encode();
        check(bytes.length == TextureWire.HEADER_BYTES + 16, "Golden pixel size changed");
        check(TextureWire.decode(bytes).equals(first), "Texture ARGB roundtrip failed");
        TextureWire retire = TextureWire.retire(ID, first.atlas(), first.sprite());
        check(TextureWire.decode(retire.encode()).equals(retire), "Retire roundtrip failed");

        var maximumPixels = new ArrayList<Integer>();
        for (int i = 0; i < TextureWire.MAX_PIXELS; ++i) {
            maximumPixels.add(i | 0xff000000);
        }
        TextureWire maximum = new TextureWire(TextureWire.SNAPSHOT, ID,
                first.atlas(), first.sprite(), 0, 0,
                TextureWire.MAX_EDGE, TextureWire.MAX_EDGE,
                0, 1, 0, 1, maximumPixels);
        check(maximum.encode().length == TextureWire.MAX_BYTES, "Maximum texture record size");
        check(TextureWire.decode(maximum.encode()).equals(maximum),
                "Max pixel image roundtrip failed");
        maximumPixels.clear();
        check(maximum.argb().size() == TextureWire.MAX_PIXELS,
                "Pixel collection was not immutable");

        rejected(() -> new TextureWire(TextureWire.SNAPSHOT, ID, first.atlas(), first.sprite(),
                0, 0, 65, 1, 0, 1, 0, 1, List.of()));
        rejected(() -> new TextureWire(TextureWire.SNAPSHOT, ID, first.atlas(), first.sprite(),
                0, 0, 2, 2, 0, 1, 0, 1, List.of(1)));
        rejected(() -> new TextureWire(TextureWire.SNAPSHOT, ID, first.atlas(), first.sprite(),
                0, 0, 2, 2, Float.NaN, 1, 0, 1, first.argb()));
        rejected(() -> new TextureWire(TextureWire.SNAPSHOT, ID, first.atlas(), first.sprite(),
                0, 0, 2, 2, 0.5f, 0.5f, 0, 1, first.argb()));
        rejected(() -> new TextureWire(TextureWire.RETIRE, ID, first.atlas(), first.sprite(),
                0, 0, 0, 0, -0.0f, 0, 0, 0, List.of()));
        rejected(() -> new TextureWire(TextureWire.RETIRE, ID, first.atlas(), first.sprite(),
                0, 0, 0, 0, 0, 0, 0, 0, List.of(1)));
        rejected(() -> new TextureWire(TextureWire.SNAPSHOT, ID, "../atlas", "bad\nsprite",
                0, 0, 1, 1, 0, 1, 0, 1, List.of(1)));

        for (int length = 0; length < bytes.length; ++length) {
            byte[] truncated = Arrays.copyOf(bytes, length);
            rejected(() -> TextureWire.decode(truncated));
        }
        for (int at : new int[]{0, 4, 6, 8, 12, 59, 63, 64, 66, 68, 88,
                                 90, 92, 96, 191, 192, 319}) {
            byte[] invalid = bytes.clone();
            invalid[at] ^= (byte) 0xff;
            rejected(() -> TextureWire.decode(invalid));
        }
        for (int at : new int[]{72, 76, 80, 84}) {
            byte[] invalid = bytes.clone();
            ByteBuffer.wrap(invalid).order(ByteOrder.LITTLE_ENDIAN)
                    .putFloat(at, Float.NaN);
            rejected(() -> TextureWire.decode(invalid));
        }
        check(TextureWire.read(new ByteArrayInputStream(new byte[0])) == null,
                "Empty source stream was not EOF");
        try {
            TextureWire.read(new ByteArrayInputStream(Arrays.copyOf(bytes, 10)));
            throw new AssertionError("Truncated header was accepted");
        } catch (IOException expected) { ++checks; }
        try {
            TextureWire.read(new ByteArrayInputStream(Arrays.copyOf(bytes, bytes.length - 1)));
            throw new AssertionError("Truncated image pixels were accepted");
        } catch (IOException expected) { ++checks; }
        check(TextureWire.read(new ByteArrayInputStream(bytes)).equals(first),
                "Stream decoding lost image bytes");
        System.out.println("texture-wire: passed " + checks + " assertions");
    }
}
