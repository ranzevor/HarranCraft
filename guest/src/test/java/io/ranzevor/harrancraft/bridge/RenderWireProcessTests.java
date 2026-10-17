package io.ranzevor.harrancraft.bridge;

import java.io.ByteArrayOutputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.List;
import java.util.concurrent.TimeUnit;

public final class RenderWireProcessTests {
    private static void require(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }

    private static byte[] payload() {
        var first = RenderWireTests.golden();
        var empty = new RenderWire(RenderWire.SNAPSHOT, first.identity(), first.key(),
                "minecraft:air", first.camera(), List.of());
        var retire = RenderWire.retire(first.identity(), first.key());
        var data = new ByteArrayOutputStream();
        for (var frame : List.of(first, empty, retire)) data.writeBytes(frame.encode());
        return data.toByteArray();
    }

    private static void exchange(String executable, byte[] bytes, boolean valid)
            throws Exception {
        Process process = new ProcessBuilder(executable, "--echo").start();
        try {
            try (var stdin = process.getOutputStream()) {
                stdin.write(bytes);
            }
            if (!process.waitFor(10, TimeUnit.SECONDS)) {
                throw new AssertionError("Native render peer timed out");
            }
            byte[] output = process.getInputStream().readAllBytes();
            String stderr = new String(process.getErrorStream().readAllBytes(),
                    StandardCharsets.UTF_8);
            if (valid) {
                require(process.exitValue() == 0,
                        "Native render peer failed: " + stderr);
                require(Arrays.equals(bytes, output), "Cross-language render data changed");
                require(stderr.contains("passed 3 frames"),
                        "Render record count changed");
            } else {
                require(process.exitValue() != 0, "Invalid render data was accepted");
            }
        } finally {
            process.destroyForcibly();
            require(process.waitFor(5, TimeUnit.SECONDS),
                    "Native render peer did not terminate");
        }
    }

    public static void main(String[] args) throws Exception {
        require(args.length == 1, "Expected native render peer executable");
        byte[] bytes = payload();
        exchange(args[0], bytes, true);
        byte[] badVersion = bytes.clone();
        badVersion[4] ^= 1;
        exchange(args[0], badVersion, false);
        byte[] invalidSize = Arrays.copyOf(bytes, 12);
        ByteBuffer.wrap(invalidSize).order(ByteOrder.LITTLE_ENDIAN)
                .putInt(8, RenderWire.MAX_BYTES + 1);
        exchange(args[0], invalidSize, false);
        exchange(args[0], Arrays.copyOf(bytes, RenderWire.HEADER_BYTES - 1), false);
        exchange(args[0], Arrays.copyOf(bytes, bytes.length - 1), false);
        System.out.println("render-process: passed");
    }
}
