package io.ranzevor.harrancraft.bridge;

import java.io.IOException;
import java.io.InputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;

/** Versioned source-sprite pixels, distinct from GPU atlas and baked quad records. */
public record TextureWire(int kind, RenderFrame.Identity identity, String atlas,
                          String sprite, int atlasX, int atlasY, int width, int height,
                          float minU, float maxU, float minV, float maxV,
                          List<Integer> argb) {
    public static final int MAGIC = 0x31544348;
    public static final int VERSION = 1;
    public static final int SNAPSHOT = 1, RETIRE = 2;
    public static final int HEADER_BYTES = 320;
    public static final int MAX_EDGE = 64;
    public static final int MAX_PIXELS = MAX_EDGE * MAX_EDGE;
    public static final int MAX_BYTES = HEADER_BYTES + MAX_PIXELS * 4;

    public TextureWire {
        if (kind != SNAPSHOT && kind != RETIRE) throw new IllegalArgumentException("Unknown texture record kind");
        Objects.requireNonNull(identity);
        validateText(atlas, 96);
        validateText(sprite, 128);
        argb = List.copyOf(argb);
        if (kind == SNAPSHOT) {
            if (atlasX < 0 || atlasY < 0 || width < 1 || height < 1
                    || width > MAX_EDGE || height > MAX_EDGE
                    || argb.size() != width * height || !Float.isFinite(minU)
                    || !Float.isFinite(maxU) || !Float.isFinite(minV) || !Float.isFinite(maxV)
                    || minU < 0 || maxU > 1 || minV < 0 || maxV > 1
                    || !(minU < maxU && minV < maxV)) {
                throw new IllegalArgumentException("Invalid source texture bounds");
            }
        } else if (atlasX != 0 || atlasY != 0 || width != 0 || height != 0
                || Float.floatToRawIntBits(minU) != 0 || Float.floatToRawIntBits(maxU) != 0
                || Float.floatToRawIntBits(minV) != 0 || Float.floatToRawIntBits(maxV) != 0
                || !argb.isEmpty()) {
            throw new IllegalArgumentException("Texture retirement contains pixels");
        }
    }

    private static void validateText(String value, int limit) {
        if (value == null || value.isEmpty() || value.length() > limit) {
            throw new IllegalArgumentException("Invalid texture identity length");
        }
        for (int i = 0; i < value.length(); ++i) {
            char c = value.charAt(i);
            if (c < 33 || c > 126) throw new IllegalArgumentException("Invalid texture identity text");
        }
    }

    public static TextureWire retire(RenderFrame.Identity id, String atlas, String sprite) {
        return new TextureWire(RETIRE, id, atlas, sprite, 0, 0, 0, 0,
                0, 0, 0, 0, List.of());
    }

    public byte[] encode() {
        int length = HEADER_BYTES + argb.size() * 4;
        ByteBuffer bytes = ByteBuffer.allocate(length).order(ByteOrder.LITTLE_ENDIAN);
        bytes.putInt(0, MAGIC);
        bytes.putShort(4, (short) VERSION);
        bytes.putShort(6, (short) kind);
        bytes.putInt(8, length);
        bytes.putLong(16, identity.epoch());
        bytes.putLong(24, identity.world());
        bytes.putLong(32, identity.camera());
        bytes.putLong(40, identity.frame());
        bytes.putLong(48, identity.capture());
        bytes.putInt(56, atlasX);
        bytes.putInt(60, atlasY);
        bytes.putShort(64, (short) width);
        bytes.putShort(66, (short) height);
        bytes.putFloat(72, minU);
        bytes.putFloat(76, maxU);
        bytes.putFloat(80, minV);
        bytes.putFloat(84, maxV);
        bytes.putShort(88, (short) atlas.length());
        bytes.putShort(90, (short) sprite.length());
        putText(bytes, 96, atlas);
        putText(bytes, 192, sprite);
        for (int i = 0; i < argb.size(); ++i) bytes.putInt(HEADER_BYTES + i * 4, argb.get(i));
        return bytes.array();
    }

    private static void putText(ByteBuffer buffer, int at, String value) {
        byte[] ascii = value.getBytes(StandardCharsets.US_ASCII);
        for (int i = 0; i < ascii.length; ++i) buffer.put(at + i, ascii[i]);
    }

    private static String readText(ByteBuffer buffer, int at, int length, int capacity) {
        if (length > capacity) throw new IllegalArgumentException("Texture text overflow");
        for (int i = 0; i < capacity; ++i) {
            int c = Byte.toUnsignedInt(buffer.get(at + i));
            if (i < length ? c < 33 || c > 126 : c != 0) {
                throw new IllegalArgumentException("Invalid texture text or padding");
            }
        }
        return new String(buffer.array(), at, length, StandardCharsets.US_ASCII);
    }

    public static TextureWire decode(byte[] input) {
        if (input.length < HEADER_BYTES || input.length > MAX_BYTES) {
            throw new IllegalArgumentException("Invalid texture record length");
        }
        ByteBuffer bytes = ByteBuffer.wrap(input).order(ByteOrder.LITTLE_ENDIAN);
        if (bytes.getInt(0) != MAGIC || Short.toUnsignedInt(bytes.getShort(4)) != VERSION
                || bytes.getInt(8) != input.length || bytes.getInt(12) != 0
                || bytes.getInt(68) != 0 || bytes.getInt(92) != 0) {
            throw new IllegalArgumentException("Invalid texture header");
        }
        int width = Short.toUnsignedInt(bytes.getShort(64));
        int height = Short.toUnsignedInt(bytes.getShort(66));
        int kind = Short.toUnsignedInt(bytes.getShort(6));
        int pixels = kind == RETIRE ? 0 : width * height;
        if (width > MAX_EDGE || height > MAX_EDGE
                || input.length != HEADER_BYTES + pixels * 4) {
            throw new IllegalArgumentException("Invalid texture dimensions");
        }
        String atlas = readText(bytes, 96, Short.toUnsignedInt(bytes.getShort(88)), 96);
        String sprite = readText(bytes, 192, Short.toUnsignedInt(bytes.getShort(90)), 128);
        List<Integer> data = new ArrayList<>(pixels);
        for (int i = 0; i < pixels; ++i) data.add(bytes.getInt(HEADER_BYTES + i * 4));
        return new TextureWire(kind,
                new RenderFrame.Identity(bytes.getLong(16), bytes.getLong(24), bytes.getLong(32),
                        bytes.getLong(40), bytes.getLong(48)),
                atlas, sprite, bytes.getInt(56), bytes.getInt(60), width, height,
                bytes.getFloat(72), bytes.getFloat(76), bytes.getFloat(80), bytes.getFloat(84), data);
    }

    public static TextureWire read(InputStream input) throws IOException {
        byte[] header = input.readNBytes(HEADER_BYTES);
        if (header.length == 0) return null;
        if (header.length != HEADER_BYTES) throw new IOException("Truncated texture header");
        ByteBuffer buffer = ByteBuffer.wrap(header).order(ByteOrder.LITTLE_ENDIAN);
        int length = buffer.getInt(8);
        if (length < HEADER_BYTES || length > MAX_BYTES || (length - HEADER_BYTES) % 4 != 0) {
            throw new IOException("Invalid texture declared length");
        }
        byte[] record = new byte[length];
        System.arraycopy(header, 0, record, 0, HEADER_BYTES);
        byte[] tail = input.readNBytes(length - HEADER_BYTES);
        if (tail.length != length - HEADER_BYTES) throw new IOException("Truncated texture pixels");
        System.arraycopy(tail, 0, record, HEADER_BYTES, tail.length);
        try {
            return decode(record);
        } catch (IllegalArgumentException error) {
            throw new IOException("Invalid texture record", error);
        }
    }
}
