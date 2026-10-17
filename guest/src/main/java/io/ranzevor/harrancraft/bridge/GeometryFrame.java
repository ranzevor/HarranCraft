package io.ranzevor.harrancraft.bridge;

import java.io.IOException;
import java.io.InputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

/** Geometry side-channel v1. Does not change the 512-byte block protocol. */
public record GeometryFrame(int kind, long epoch, long revision, Frame.Key key,
                            String state, List<Box> boxes) {
    public static final int MAGIC = 0x31474348;
    public static final int VERSION = 1;
    public static final int SNAPSHOT = 1, RETIRE = 2;
    public static final int HEADER_BYTES = 320;
    public static final int MAX_BOXES = 128;
    public static final int BOX_BYTES = 48;
    public static final int MAX_BYTES = HEADER_BYTES + MAX_BOXES * BOX_BYTES;

    public record Box(double minX, double minY, double minZ,
                      double maxX, double maxY, double maxZ) {
        public Box {
            if (!Double.isFinite(minX) || !Double.isFinite(minY) || !Double.isFinite(minZ)
                    || !Double.isFinite(maxX) || !Double.isFinite(maxY) || !Double.isFinite(maxZ)
                    || !(minX < maxX && minY < maxY && minZ < maxZ)) {
                throw new IllegalArgumentException("Invalid geometry bounds");
            }
        }
    }

    public GeometryFrame {
        if ((kind != SNAPSHOT && kind != RETIRE) || epoch <= 0 || revision <= 0
                || key == null || key.dimension() == null || state == null || boxes == null
                || boxes.size() > MAX_BOXES) {
            throw new IllegalArgumentException("Invalid geometry identity");
        }
        validateText(key.dimension(), 64, false);
        validateText(state, 192, kind == RETIRE);
        boxes = List.copyOf(boxes);
        if (kind == RETIRE && (!state.isEmpty() || !boxes.isEmpty())) {
            throw new IllegalArgumentException("Retirement has unexpected body");
        }
    }

    private static void validateText(String text, int capacity, boolean allowEmpty) {
        if (text.length() > capacity || (!allowEmpty && text.isEmpty())) {
            throw new IllegalArgumentException("Invalid geometry text length");
        }
        for (int i = 0; i < text.length(); ++i) {
            char value = text.charAt(i);
            if (value < 32 || value > 126) {
                throw new IllegalArgumentException("Invalid geometry text");
            }
        }
    }

    public static GeometryFrame snapshot(long epoch, long revision, Frame.Key key,
                                         String state, List<Box> boxes) {
        return new GeometryFrame(SNAPSHOT, epoch, revision, key, state, boxes);
    }

    public static GeometryFrame retire(long epoch, long revision, Frame.Key key) {
        return new GeometryFrame(RETIRE, epoch, revision, key, "", List.of());
    }

    public byte[] encode() {
        int length = HEADER_BYTES + boxes.size() * BOX_BYTES;
        ByteBuffer buffer = ByteBuffer.allocate(length).order(ByteOrder.LITTLE_ENDIAN);
        buffer.putInt(0, MAGIC);
        buffer.putShort(4, (short) VERSION);
        buffer.putShort(6, (short) kind);
        buffer.putInt(8, length);
        buffer.putLong(16, epoch);
        buffer.putLong(24, revision);
        buffer.putInt(32, key.x());
        buffer.putInt(36, key.y());
        buffer.putInt(40, key.z());
        buffer.putShort(44, (short) boxes.size());
        putText(buffer, 64, 46, key.dimension());
        putText(buffer, 128, 48, state);
        for (int i = 0; i < boxes.size(); ++i) {
            Box box = boxes.get(i);
            int at = HEADER_BYTES + i * BOX_BYTES;
            buffer.putDouble(at, box.minX());
            buffer.putDouble(at + 8, box.minY());
            buffer.putDouble(at + 16, box.minZ());
            buffer.putDouble(at + 24, box.maxX());
            buffer.putDouble(at + 32, box.maxY());
            buffer.putDouble(at + 40, box.maxZ());
        }
        return buffer.array();
    }

    private static void putText(ByteBuffer buffer, int at, int sizeAt, String value) {
        buffer.putShort(sizeAt, (short) value.length());
        byte[] bytes = value.getBytes(StandardCharsets.US_ASCII);
        for (int i = 0; i < bytes.length; ++i) buffer.put(at + i, bytes[i]);
    }

    private static String getText(ByteBuffer buffer, int at, int sizeAt, int capacity) {
        int length = Short.toUnsignedInt(buffer.getShort(sizeAt));
        if (length > capacity) throw new IllegalArgumentException("Geometry text exceeds limit");
        for (int i = 0; i < capacity; ++i) {
            int value = Byte.toUnsignedInt(buffer.get(at + i));
            if (i < length ? value < 32 || value > 126 : value != 0) {
                throw new IllegalArgumentException("Invalid geometry text/padding");
            }
        }
        return new String(buffer.array(), at, length, StandardCharsets.US_ASCII);
    }

    public static GeometryFrame decode(byte[] data) {
        if (data.length < HEADER_BYTES || data.length > MAX_BYTES) {
            throw new IllegalArgumentException("Invalid geometry byte count");
        }
        ByteBuffer buffer = ByteBuffer.wrap(data).order(ByteOrder.LITTLE_ENDIAN);
        if (buffer.getInt(0) != MAGIC || Short.toUnsignedInt(buffer.getShort(4)) != VERSION
                || buffer.getInt(8) != data.length || buffer.getInt(12) != 0
                || buffer.getShort(50) != 0 || buffer.getInt(52) != 0
                || buffer.getLong(56) != 0) {
            throw new IllegalArgumentException("Invalid geometry header");
        }
        int count = Short.toUnsignedInt(buffer.getShort(44));
        if (count > MAX_BOXES || data.length != HEADER_BYTES + count * BOX_BYTES) {
            throw new IllegalArgumentException("Invalid geometry box count");
        }
        String dimension = getText(buffer, 64, 46, 64);
        String state = getText(buffer, 128, 48, 192);
        List<Box> boxes = new ArrayList<>(count);
        for (int i = 0; i < count; ++i) {
            int at = HEADER_BYTES + i * BOX_BYTES;
            boxes.add(new Box(buffer.getDouble(at), buffer.getDouble(at + 8),
                    buffer.getDouble(at + 16), buffer.getDouble(at + 24),
                    buffer.getDouble(at + 32), buffer.getDouble(at + 40)));
        }
        return new GeometryFrame(Short.toUnsignedInt(buffer.getShort(6)),
                buffer.getLong(16), buffer.getLong(24),
                new Frame.Key(dimension, buffer.getInt(32), buffer.getInt(36),
                        buffer.getInt(40)), state, boxes);
    }

    public static GeometryFrame read(InputStream input) throws IOException {
        byte[] header = input.readNBytes(HEADER_BYTES);
        if (header.length == 0) return null;
        if (header.length != HEADER_BYTES) throw new IOException("Truncated geometry header");
        ByteBuffer view = ByteBuffer.wrap(header).order(ByteOrder.LITTLE_ENDIAN);
        int total = view.getInt(8);
        if (total < HEADER_BYTES || total > MAX_BYTES) {
            throw new IOException("Invalid geometry frame length");
        }
        byte[] bytes = new byte[total];
        System.arraycopy(header, 0, bytes, 0, HEADER_BYTES);
        byte[] body = input.readNBytes(total - HEADER_BYTES);
        if (body.length != total - HEADER_BYTES) {
            throw new IOException("Truncated geometry payload");
        }
        System.arraycopy(body, 0, bytes, HEADER_BYTES, body.length);
        try {
            return decode(bytes);
        } catch (IllegalArgumentException error) {
            throw new IOException("Invalid geometry record", error);
        }
    }
}
