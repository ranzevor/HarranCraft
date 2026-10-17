package io.ranzevor.harrancraft.bridge;

import java.io.ByteArrayOutputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.List;
import java.util.concurrent.TimeUnit;

public final class TextureProcessTests {
    private static void require(boolean valid, String message) {
        if (!valid) throw new AssertionError(message);
    }

    private static byte[] payload() {
        var first = TextureWireTests.golden();
        var next = new TextureWire(TextureWire.SNAPSHOT,
                new RenderFrame.Identity(first.identity().epoch(), first.identity().world(),
                        first.identity().camera(), 44, 45),
                first.atlas(), first.sprite(), 16, 32, 2, 2, 0.125f, 0.25f,
                0.375f, 0.5f,
                List.of(0xff000000, 0x00112233, 0xaa8899cc, 0xffffffff));
        var retired = TextureWire.retire(
                new RenderFrame.Identity(first.identity().epoch(), first.identity().world(),
                        first.identity().camera(), 46, 47),
                first.atlas(), first.sprite());
        var stream = new ByteArrayOutputStream();
        for (var frame : List.of(first, next, retired)) stream.writeBytes(frame.encode());
        return stream.toByteArray();
    }

    private static void exchange(String executable, byte[] data, boolean success)
            throws Exception {
        Process process = new ProcessBuilder(executable, "--echo").start();
        try {
            try (var input = process.getOutputStream()) {
                input.write(data);
            }
            require(process.waitFor(10, TimeUnit.SECONDS), "Native texture peer timed out");
            byte[] actual = process.getInputStream().readAllBytes();
            String errors = new String(process.getErrorStream().readAllBytes(),
                    StandardCharsets.UTF_8);
            if (success) {
                require(process.exitValue() == 0,
                        "Native texture peer rejected valid source image: " + errors);
                require(Arrays.equals(data, actual), "Source pixels changed across Java/C++");
                require(errors.contains("passed 3 frames"), "Sprite record count changed");
            } else {
                require(process.exitValue() != 0, "Malformed source pixel stream was accepted");
            }
        } finally {
            process.destroyForcibly();
            require(process.waitFor(5, TimeUnit.SECONDS),
                    "Native texture peer survived cancellation");
        }
    }

    public static void main(String[] args) throws Exception {
        require(args.length == 1, "Expected native texture peer");
        byte[] valid = payload();
        exchange(args[0], valid, true);
        byte[] version = valid.clone();
        version[4] ^= 1;
        exchange(args[0], version, false);
        byte[] oversized = Arrays.copyOf(valid, 12);
        ByteBuffer.wrap(oversized).order(ByteOrder.LITTLE_ENDIAN)
                .putInt(8, TextureWire.MAX_BYTES + 1);
        exchange(args[0], oversized, false);
        exchange(args[0], Arrays.copyOf(valid, TextureWire.HEADER_BYTES - 1), false);
        exchange(args[0], Arrays.copyOf(valid, valid.length - 1), false);
        System.out.println("texture-process: passed");
    }
}
