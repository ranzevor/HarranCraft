package io.ranzevor.harrancraft.bridge;

import java.io.IOException;
import java.io.InputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;

/** Versioned, bounded transport for client-baked quads and their capture identities. */
public record RenderWire(int kind, RenderFrame.Identity identity, Frame.Key key,
                         String state, RenderFrame.CameraPose camera,
                         List<RenderFrame.Quad> quads) {
    public static final int MAGIC = 0x31524348;
    public static final int VERSION = 1;
    public static final int SNAPSHOT = 1;
    public static final int RETIRE = 2;
    public static final int HEADER_BYTES = 384;
    public static final int QUAD_BYTES = 400;
    public static final int MAX_QUADS = RenderFrame.MAX_QUADS;
    public static final int MAX_BYTES = HEADER_BYTES + MAX_QUADS * QUAD_BYTES;
    private static final RenderFrame.CameraPose ZERO_CAMERA =
            new RenderFrame.CameraPose(0, 0, 0, 0, 0);

    public RenderWire {
        if (kind != SNAPSHOT && kind != RETIRE) {
            throw new IllegalArgumentException("Unknown render record kind");
        }
        Objects.requireNonNull(identity);
        Objects.requireNonNull(key);
        Objects.requireNonNull(camera);
        Objects.requireNonNull(state);
        quads = List.copyOf(quads);
        if (key.dimension() == null || quads.size() > MAX_QUADS) {
            throw new IllegalArgumentException("Invalid render payload");
        }
        checkText(key.dimension(), 64, false);
        checkText(state, 192, kind == RETIRE);
        if (kind == RETIRE && (!state.isEmpty() || !quads.isEmpty()
                || !camera.equals(ZERO_CAMERA))) {
            throw new IllegalArgumentException("Retirement contains model data");
        }
    }

    private static void checkText(String value, int max, boolean empty) {
        if (value.length() > max || (!empty && value.isEmpty())) {
            throw new IllegalArgumentException("Invalid render string length");
        }
        for (int i = 0; i < value.length(); ++i) {
            char c = value.charAt(i);
            if (c < 33 || c > 126) {
                throw new IllegalArgumentException("Invalid render ASCII string");
            }
        }
    }

    public static RenderWire snapshot(RenderFrame captured) {
        Objects.requireNonNull(captured);
        return new RenderWire(SNAPSHOT, captured.identity(), captured.key(),
                captured.state(), captured.camera(), captured.quads());
    }

    public static RenderWire retire(RenderFrame.Identity identity, Frame.Key key) {
        return new RenderWire(RETIRE, identity, key, "", ZERO_CAMERA, List.of());
    }

    private static void writeText(ByteBuffer data, int start, String value) {
        byte[] ascii = value.getBytes(StandardCharsets.US_ASCII);
        for (int index = 0; index < ascii.length; ++index) {
            data.put(start + index, ascii[index]);
        }
    }

    private static String readText(ByteBuffer data, int start, int length, int capacity) {
        if (length > capacity) throw new IllegalArgumentException("Render string exceeds capacity");
        for (int index = 0; index < capacity; ++index) {
            int byteValue = Byte.toUnsignedInt(data.get(start + index));
            if (index < length ? byteValue < 33 || byteValue > 126 : byteValue != 0) {
                throw new IllegalArgumentException("Invalid render string padding");
            }
        }
        return new String(data.array(), start, length, StandardCharsets.US_ASCII);
    }

    public byte[] encode() {
        int total = HEADER_BYTES + QUAD_BYTES * quads.size();
        ByteBuffer data = ByteBuffer.allocate(total).order(ByteOrder.LITTLE_ENDIAN);
        data.putInt(0, MAGIC);
        data.putShort(4, (short) VERSION);
        data.putShort(6, (short) kind);
        data.putInt(8, total);
        data.putLong(16, identity.epoch());
        data.putLong(24, identity.world());
        data.putLong(32, identity.camera());
        data.putLong(40, identity.frame());
        data.putLong(48, identity.capture());
        data.putInt(56, key.x());
        data.putInt(60, key.y());
        data.putInt(64, key.z());
        data.putShort(68, (short) quads.size());
        data.putShort(70, (short) key.dimension().length());
        data.putShort(72, (short) state.length());
        data.putDouble(76, camera.x());
        data.putDouble(84, camera.y());
        data.putDouble(92, camera.z());
        data.putFloat(100, camera.yaw());
        data.putFloat(104, camera.pitch());
        writeText(data, 112, key.dimension());
        writeText(data, 176, state);
        for (int index = 0; index < quads.size(); ++index) {
            RenderFrame.Quad quad = quads.get(index);
            int offset = HEADER_BYTES + QUAD_BYTES * index;
            data.putShort(offset, (short) quad.cullFace());
            data.putShort(offset + 2, (short) quad.face());
            data.putShort(offset + 4, (short) quad.tintIndex());
            data.put(offset + 6, (byte) (quad.shade() ? 1 : 0));
            data.put(offset + 7, (byte) quad.lightEmission());
            data.putShort(offset + 8, (short) quad.atlas().length());
            data.putShort(offset + 10, (short) quad.sprite().length());
            writeText(data, offset + 16, quad.atlas());
            writeText(data, offset + 144, quad.sprite());
            for (int vertexIndex = 0; vertexIndex < 4; ++vertexIndex) {
                RenderFrame.Vertex vertex = quad.vertices().get(vertexIndex);
                int at = offset + 272 + 32 * vertexIndex;
                data.putFloat(at, vertex.x());
                data.putFloat(at + 4, vertex.y());
                data.putFloat(at + 8, vertex.z());
                data.putFloat(at + 12, vertex.u());
                data.putFloat(at + 16, vertex.v());
                data.putInt(at + 20, vertex.packedColor());
                data.putInt(at + 24, vertex.packedLight());
                data.putInt(at + 28, vertex.packedNormal());
            }
        }
        return data.array();
    }

    public static RenderWire decode(byte[] bytes) {
        if (bytes.length < HEADER_BYTES || bytes.length > MAX_BYTES) {
            throw new IllegalArgumentException("Invalid render record size");
        }
        ByteBuffer data = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN);
        if (data.getInt(0) != MAGIC || Short.toUnsignedInt(data.getShort(4)) != VERSION
                || data.getInt(8) != bytes.length || data.getInt(12) != 0
                || data.getShort(74) != 0 || data.getInt(108) != 0) {
            throw new IllegalArgumentException("Invalid render record header");
        }
        for (int at = 368; at < HEADER_BYTES; ++at) {
            if (data.get(at) != 0) throw new IllegalArgumentException("Nonzero render header padding");
        }
        int count = Short.toUnsignedInt(data.getShort(68));
        if (count > MAX_QUADS || bytes.length != HEADER_BYTES + count * QUAD_BYTES) {
            throw new IllegalArgumentException("Invalid render quad count");
        }
        String dimension = readText(data, 112, Short.toUnsignedInt(data.getShort(70)), 64);
        String state = readText(data, 176, Short.toUnsignedInt(data.getShort(72)), 192);
        List<RenderFrame.Quad> quads = new ArrayList<>(count);
        for (int index = 0; index < count; ++index) {
            int offset = HEADER_BYTES + QUAD_BYTES * index;
            if (data.getInt(offset + 12) != 0 || Byte.toUnsignedInt(data.get(offset + 6)) > 1) {
                throw new IllegalArgumentException("Invalid render quad header");
            }
            int atlasLength = Short.toUnsignedInt(data.getShort(offset + 8));
            int spriteLength = Short.toUnsignedInt(data.getShort(offset + 10));
            String atlas = readText(data, offset + 16, atlasLength, 128);
            String sprite = readText(data, offset + 144, spriteLength, 128);
            List<RenderFrame.Vertex> vertices = new ArrayList<>(4);
            for (int i = 0; i < 4; ++i) {
                int at = offset + 272 + 32 * i;
                vertices.add(new RenderFrame.Vertex(
                        data.getFloat(at), data.getFloat(at + 4), data.getFloat(at + 8),
                        data.getFloat(at + 12), data.getFloat(at + 16),
                        data.getInt(at + 20), data.getInt(at + 24), data.getInt(at + 28)));
            }
            quads.add(new RenderFrame.Quad(data.getShort(offset),
                    Short.toUnsignedInt(data.getShort(offset + 2)), data.getShort(offset + 4),
                    data.get(offset + 6) == 1, Byte.toUnsignedInt(data.get(offset + 7)),
                    atlas, sprite, vertices));
        }
        return new RenderWire(Short.toUnsignedInt(data.getShort(6)),
                new RenderFrame.Identity(data.getLong(16), data.getLong(24), data.getLong(32),
                        data.getLong(40), data.getLong(48)),
                new Frame.Key(dimension, data.getInt(56), data.getInt(60), data.getInt(64)),
                state, new RenderFrame.CameraPose(data.getDouble(76), data.getDouble(84),
                        data.getDouble(92), data.getFloat(100), data.getFloat(104)), quads);
    }

    public static RenderWire read(InputStream input) throws IOException {
        byte[] header = input.readNBytes(HEADER_BYTES);
        if (header.length == 0) return null;
        if (header.length != HEADER_BYTES) throw new IOException("Truncated render header");
        int total = ByteBuffer.wrap(header).order(ByteOrder.LITTLE_ENDIAN).getInt(8);
        if (total < HEADER_BYTES || total > MAX_BYTES
                || (total - HEADER_BYTES) % QUAD_BYTES != 0) {
            throw new IOException("Invalid render record length");
        }
        byte[] bytes = new byte[total];
        System.arraycopy(header, 0, bytes, 0, HEADER_BYTES);
        byte[] rest = input.readNBytes(total - HEADER_BYTES);
        if (rest.length != total - HEADER_BYTES) throw new IOException("Truncated render model");
        System.arraycopy(rest, 0, bytes, HEADER_BYTES, rest.length);
        try {
            return decode(bytes);
        } catch (IllegalArgumentException error) {
            throw new IOException("Invalid render record", error);
        }
    }
}
