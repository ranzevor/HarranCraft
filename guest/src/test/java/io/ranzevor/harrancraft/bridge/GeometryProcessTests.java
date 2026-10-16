package io.ranzevor.harrancraft.bridge;

import java.io.ByteArrayOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.List;
import java.util.concurrent.TimeUnit;

public final class GeometryProcessTests {
    private static void require(boolean good, String message) {
        if (!good) throw new AssertionError(message);
    }

    private static byte[] payload() {
        var key = new Frame.Key("minecraft:overworld", 0, 80, 0);
        var one = GeometryFrame.snapshot(17, 1, key, "minecraft:stone",
                List.of(new GeometryFrame.Box(0, 0, 0, 1, 1, 1)));
        var two = GeometryFrame.snapshot(17, 2, key,
                "minecraft:oak_slab[type=bottom,waterlogged=false]",
                List.of(new GeometryFrame.Box(0, 0, 0, 1, 0.5, 1)));
        var three = GeometryFrame.snapshot(17, 3, key, "minecraft:air", List.of());
        var four = GeometryFrame.retire(17, 4, key);
        var stream = new ByteArrayOutputStream();
        for (var frame : List.of(one, two, three, four)) {
            stream.writeBytes(frame.encode());
        }
        return stream.toByteArray();
    }

    private static void roundtrip(String executable, byte[] payload, boolean valid)
            throws Exception {
        Process process = new ProcessBuilder(executable, "--echo").start();
        try {
            try (var output = process.getOutputStream()) {
                output.write(payload);
            }
            if (!process.waitFor(8, TimeUnit.SECONDS)) {
                throw new AssertionError("Native geometry peer did not terminate");
            }
            byte[] result = process.getInputStream().readAllBytes();
            byte[] errors = process.getErrorStream().readAllBytes();
            if (valid) {
                require(process.exitValue() == 0,
                        "Native geometry peer failed: " + new String(errors, StandardCharsets.UTF_8));
                require(Arrays.equals(payload, result), "Native geometry bytes changed");
                require(new String(errors, StandardCharsets.UTF_8).contains("passed 4 frames"),
                        "Native frame count incorrect");
            } else {
                require(process.exitValue() != 0, "Invalid geometry was accepted");
            }
        } finally {
            process.destroyForcibly();
            if (!process.waitFor(5, TimeUnit.SECONDS)) {
                throw new AssertionError("Native geometry peer was not retired");
            }
        }
    }

    public static void main(String[] args) throws Exception {
        require(args.length == 1, "Expected native geometry peer executable");
        var payload = payload();
        roundtrip(args[0], payload, true);
        var corrupt = payload.clone();
        corrupt[4] ^= 1;
        roundtrip(args[0], corrupt, false);
        roundtrip(args[0], Arrays.copyOf(payload, GeometryFrame.HEADER_BYTES - 1), false);
        System.out.println("geometry-process: passed");
    }
}
