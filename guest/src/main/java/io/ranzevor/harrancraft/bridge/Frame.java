package io.ranzevor.harrancraft.bridge;

import java.io.IOException;
import java.io.InputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;

/** The Java endpoint for the contract in include/harrancraft/wire.hpp. */
public record Frame(int kind, long epoch, long id, int origin, int status,
                    Key key, String expected, String state, long cause) {
    public static final int BYTES = 512;
    public static final int MAGIC = 0x31435248;
    public static final int VERSION = 1;
    public static final int HELLO = 1, WELCOME = 2, GET = 3, SET = 4, RESULT = 5;
    public static final int EVENT = 6, PING = 7, PONG = 8, CLOSE = 9;
    public static final int NATIVE = 1, GUEST = 2;
    public static final int OK = 0, STALE_EPOCH = 1, INVALID = 2, UNAVAILABLE = 3;
    public static final int CONFLICT = 4, FORBIDDEN = 5, RETIRED_ID = 6, CAPACITY = 7;
    public static final int UNSUPPORTED_STATE = 8;
    public static final Key EMPTY_KEY = new Key("", 0, 0, 0);

    public record Key(String dimension, int x, int y, int z) {}

    public Frame {
        if (kind < HELLO || kind > CLOSE || epoch < 0 || id < 0 || cause < 0
                || (origin != NATIVE && origin != GUEST) || status < OK || status > UNSUPPORTED_STATE
                || key == null || key.dimension() == null || expected == null || state == null) {
            throw new IllegalArgumentException("Invalid wire fields");
        }
        validateText(key.dimension(), 64);
        validateText(expected, 192);
        validateText(state, 192);
    }

    private static void validateText(String value, int capacity) {
        if (value.length() > capacity) throw new IllegalArgumentException("Wire text exceeds capacity");
        for (int i = 0; i < value.length(); ++i) {
            char c = value.charAt(i);
            if (c < 32 || c > 126) throw new IllegalArgumentException("Invalid wire text");
        }
    }

    public static Frame control(int kind, long epoch, long id, int origin) {
        return new Frame(kind, epoch, id, origin, OK, EMPTY_KEY, "", "", 0);
    }

    public boolean hasEmptyBody() {
        return key.equals(EMPTY_KEY) && expected.isEmpty() && state.isEmpty() && cause == 0;
    }

    public byte[] encode() {
        ByteBuffer b = ByteBuffer.allocate(BYTES).order(ByteOrder.LITTLE_ENDIAN);
        b.putInt(0, MAGIC).putShort(4, (short) VERSION).putShort(6, (short) kind);
        b.putLong(8, epoch).putLong(16, id).putInt(24, origin).putInt(28, status);
        b.putInt(32, key.x()).putInt(36, key.y()).putInt(40, key.z()).putLong(52, cause);
        putText(b, 64, 44, key.dimension());
        putText(b, 128, 46, expected);
        putText(b, 320, 48, state);
        return b.array();
    }

    public static Frame decode(byte[] bytes) {
        if (bytes.length != BYTES) throw new IllegalArgumentException("Invalid wire size");
        ByteBuffer b = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN);
        if (b.getInt(0) != MAGIC || b.getShort(4) != VERSION || b.getShort(50) != 0
                || b.getInt(60) != 0) throw new IllegalArgumentException("Invalid wire header");
        return new Frame(Short.toUnsignedInt(b.getShort(6)), b.getLong(8), b.getLong(16),
                b.getInt(24), b.getInt(28),
                new Key(readText(b, 64, 44, 64), b.getInt(32), b.getInt(36), b.getInt(40)),
                readText(b, 128, 46, 192), readText(b, 320, 48, 192), b.getLong(52));
    }

    public static Frame read(InputStream input) throws IOException {
        byte[] bytes = input.readNBytes(BYTES);
        if (bytes.length == 0) return null;
        if (bytes.length != BYTES) throw new IOException("Truncated wire frame");
        try {
            return decode(bytes);
        } catch (IllegalArgumentException e) {
            throw new IOException("Invalid wire frame", e);
        }
    }

    private static void putText(ByteBuffer b, int at, int lengthAt, String value) {
        for (int i = 0; i < value.length(); ++i) b.put(at + i, (byte) value.charAt(i));
        b.putShort(lengthAt, (short) value.length());
    }

    private static String readText(ByteBuffer b, int at, int lengthAt, int capacity) {
        int length = Short.toUnsignedInt(b.getShort(lengthAt));
        if (length > capacity) throw new IllegalArgumentException("Wire text exceeds capacity");
        for (int i = 0; i < capacity; ++i) {
            int c = Byte.toUnsignedInt(b.get(at + i));
            if (i < length ? c < 32 || c > 126 : c != 0) {
                throw new IllegalArgumentException("Invalid wire text or padding");
            }
        }
        return new String(b.array(), at, length, StandardCharsets.US_ASCII);
    }
}
