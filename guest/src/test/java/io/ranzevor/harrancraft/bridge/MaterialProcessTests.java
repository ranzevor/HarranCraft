package io.ranzevor.harrancraft.bridge;

import java.io.ByteArrayOutputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.List;
import java.util.concurrent.TimeUnit;

public final class MaterialProcessTests {
    private static int checks;
    private static final Frame.Key KEY = new Frame.Key("minecraft:overworld", 0, 80, 0);

    private static void require(boolean value, String message) {
        ++checks;
        if (!value) throw new AssertionError(message);
    }

    private static RenderFrame model() {
        return MaterialPipelineTests.model(new RenderFrame.Identity(17, 7, 9, 10, 1), KEY);
    }

    private static byte[] stream(RenderFrame model, List<TextureWire> textures) {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        bytes.writeBytes(RenderWire.snapshot(model).encode());
        for (TextureWire texture : textures) bytes.writeBytes(texture.encode());
        return bytes.toByteArray();
    }

    private record Result(int exit, String out, String err) {}

    private static Result exchange(String executable, byte[] input) throws Exception {
        Process process = new ProcessBuilder(executable, "--materials").start();
        try {
            try (var stdin = process.getOutputStream()) {
                stdin.write(input);
            }
            require(process.waitFor(10, TimeUnit.SECONDS),
                    "Native material receiver did not terminate");
            return new Result(process.exitValue(),
                    new String(process.getInputStream().readAllBytes(), StandardCharsets.UTF_8),
                    new String(process.getErrorStream().readAllBytes(), StandardCharsets.UTF_8));
        } finally {
            process.destroyForcibly();
            require(process.waitFor(5, TimeUnit.SECONDS),
                    "Native material receiver child survived cleanup");
        }
    }

    private static void accepted(String executable, byte[] data, int expected) throws Exception {
        Result result = exchange(executable, data);
        require(result.exit() == 0,
                "Native material receiver rejected valid source: " + result.err());
        require(result.out().strip().equals(
                "{\"expected\":" + expected + ",\"received\":" + expected + "}"),
                "Native complete-material count disagreed");
    }

    private static void refused(String executable, byte[] data) throws Exception {
        Result result = exchange(executable, data);
        require(result.exit() != 0, "Incomplete or conflicting material was accepted");
    }

    public static void main(String[] args) throws Exception {
        require(args.length == 1, "Expected native material receiver");
        RenderFrame model = model();
        List<TextureWire> sprites = MaterialPipelineTests.sprites(model);
        accepted(args[0], stream(model, sprites), 2);
        accepted(args[0], stream(model(), List.of(sprites.get(1), sprites.get(0))), 2);

        var air = new RenderFrame(model.identity(), KEY, "minecraft:air",
                model.camera(), List.of());
        accepted(args[0], stream(air, List.of()), 0);

        var retired = TextureWire.retire(model.identity(), sprites.getFirst().atlas(),
                sprites.getFirst().sprite());
        Result retirement = exchange(args[0], stream(model, List.of(retired)));
        require(retirement.exit() == 0
                        && retirement.out().strip().equals("{\"expected\":0,\"received\":0}"),
                "Material retirement did not invalidate staged pixels");

        refused(args[0], stream(model, List.of()));
        refused(args[0], stream(model, List.of(sprites.getFirst())));
        refused(args[0], stream(model,
                List.of(sprites.getFirst(), sprites.getFirst())));
        var wrong = new TextureWire(TextureWire.SNAPSHOT, model.identity(),
                sprites.getFirst().atlas(), "minecraft:block/unexpected",
                0, 0, 1, 1, 0, 1, 0, 1, List.of(0xff000000));
        refused(args[0], stream(model, List.of(wrong, sprites.get(1))));

        var old = new TextureWire(TextureWire.SNAPSHOT,
                new RenderFrame.Identity(16, 7, 9, 10, 1),
                sprites.getFirst().atlas(), sprites.getFirst().sprite(),
                sprites.getFirst().atlasX(), sprites.getFirst().atlasY(),
                sprites.getFirst().width(), sprites.getFirst().height(),
                sprites.getFirst().minU(), sprites.getFirst().maxU(),
                sprites.getFirst().minV(), sprites.getFirst().maxV(),
                sprites.getFirst().argb());
        refused(args[0], stream(model, List.of(old, sprites.get(1))));

        byte[] bad = stream(model, sprites);
        byte[] version = bad.clone();
        version[4] ^= 1;
        refused(args[0], version);
        byte[] truncated = Arrays.copyOf(bad, bad.length - 1);
        refused(args[0], truncated);
        byte[] over = bad.clone();
        ByteBuffer.wrap(over).order(ByteOrder.LITTLE_ENDIAN).putInt(8, RenderWire.MAX_BYTES + 1);
        refused(args[0], over);

        System.out.println("material-process: passed " + checks + " assertions");
    }
}
