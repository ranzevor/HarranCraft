package io.ranzevor.harrancraft.bridge;

import java.io.ByteArrayOutputStream;
import java.io.FilterOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.List;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;

public final class RenderReceiverProcessTests {
    private static int checks;
    private static final Frame.Key KEY = new Frame.Key("minecraft:overworld", -17, 80, 33);

    private static void require(boolean valid, String message) {
        ++checks;
        if (!valid) throw new AssertionError(message);
    }

    private static RenderWire render(long epoch, long world, long camera,
                                     long frame, long capture, String state) {
        var golden = RenderWireTests.golden();
        return new RenderWire(RenderWire.SNAPSHOT,
                new RenderFrame.Identity(epoch, world, camera, frame, capture),
                KEY, state, golden.camera(), golden.quads());
    }

    private static byte[] join(RenderWire... frames) {
        ByteArrayOutputStream data = new ByteArrayOutputStream();
        for (RenderWire record : frames) data.writeBytes(record.encode());
        return data.toByteArray();
    }

    private record Result(int exit, String stdout, String stderr) {}

    private static Result finish(Process process) throws Exception {
        require(process.waitFor(10, TimeUnit.SECONDS),
                "Native render receiver did not terminate");
        return new Result(process.exitValue(),
                new String(process.getInputStream().readAllBytes(), StandardCharsets.UTF_8),
                new String(process.getErrorStream().readAllBytes(), StandardCharsets.UTF_8));
    }

    private static Result raw(String executable, byte[] bytes) throws Exception {
        Process process = new ProcessBuilder(executable, "--receive").start();
        try {
            try (OutputStream output = process.getOutputStream()) {
                output.write(bytes);
            }
            return finish(process);
        } finally {
            process.destroyForcibly();
            require(process.waitFor(5, TimeUnit.SECONDS), "Native render process survived cleanup");
        }
    }

    private static void accepted(String executable, byte[] bytes, int records, int staged)
            throws Exception {
        Result result = raw(executable, bytes);
        require(result.exit() == 0, "Native render receiver rejected valid input: "
                + result.stderr());
        require(result.stdout().strip().equals(
                "{\"records\":" + records + ",\"staged_before_close\":" + staged + "}"),
                "Native render receiving or retirement counts changed");
    }

    private static void rejected(String executable, byte[] bytes) throws Exception {
        require(raw(executable, bytes).exit() != 0, "Invalid render input was accepted");
    }

    private static void async(String executable, RenderWire... frames) throws Exception {
        Process process = new ProcessBuilder(executable, "--receive").start();
        RenderPipe pipe = null;
        try {
            AtomicInteger flushed = new AtomicInteger();
            OutputStream output = new FilterOutputStream(process.getOutputStream()) {
                @Override public void flush() throws IOException {
                    super.flush();
                    flushed.incrementAndGet();
                }
            };
            pipe = new RenderPipe(17, 7, 9, output, () -> {});
            for (RenderWire frame : frames) {
                require(pipe.offer(frame), "Valid asynchronous render record refused");
            }
            long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(5);
            while (flushed.get() < frames.length && pipe.isOpen()
                    && System.nanoTime() < deadline) Thread.sleep(2);
            require(flushed.get() == frames.length && pipe.isOpen(),
                    "Render pipe did not flush all records");
            pipe.close();
            deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(5);
            while (!pipe.isTerminated() && System.nanoTime() < deadline) Thread.sleep(2);
            require(pipe.isTerminated() && pipe.ioFailure() == null
                            && pipe.cleanupFailure() == null,
                    "Render sender did not retire its workers");
            Result result = finish(process);
            require(result.exit() == 0, "Native rejected asynchronous render data: "
                    + result.stderr());
            require(result.stdout().strip().equals(
                    "{\"records\":" + frames.length + ",\"staged_before_close\":0}"),
                    "Native render models survived clean retirement");
        } finally {
            if (pipe != null) pipe.close();
            process.destroyForcibly();
            require(process.waitFor(5, TimeUnit.SECONDS),
                    "Native receiver process was not retired");
        }
    }

    public static void main(String[] args) throws Exception {
        require(args.length == 1, "Expected native render receiver executable");
        String executable = args[0];
        RenderWire first = render(17, 7, 9, 10, 1, "minecraft:stone");
        RenderWire second = render(17, 7, 9, 12, 2, "minecraft:oak_slab");
        RenderWire air = new RenderWire(RenderWire.SNAPSHOT,
                new RenderFrame.Identity(17, 7, 9, 20, 3),
                KEY, "minecraft:air", first.camera(), List.of());
        RenderWire retire = RenderWire.retire(
                new RenderFrame.Identity(17, 7, 9, 22, 4), KEY);
        accepted(executable, join(first, second, air, retire), 4, 0);
        accepted(executable, first.encode(), 1, 1);
        accepted(executable, new byte[0], 0, 0);
        async(executable, first, second, air, retire);

        byte[] invalidVersion = first.encode().clone();
        invalidVersion[4] ^= 1;
        rejected(executable, invalidVersion);

        byte[] oversized = Arrays.copyOf(first.encode(), 12);
        ByteBuffer.wrap(oversized).order(ByteOrder.LITTLE_ENDIAN)
                .putInt(8, RenderWire.MAX_BYTES + 1);
        rejected(executable, oversized);
        rejected(executable, Arrays.copyOf(first.encode(), 11));
        rejected(executable, Arrays.copyOf(first.encode(), first.encode().length - 1));
        rejected(executable, join(first, render(17, 7, 9, 11, 1, "minecraft:stone")));
        rejected(executable, join(first, render(17, 7, 9, 12, 3, "minecraft:stone")));
        rejected(executable, join(first, render(17, 7, 10, 12, 2, "minecraft:stone")));
        rejected(executable, join(first, render(16, 7, 9, 12, 2, "minecraft:stone")));

        System.out.println("render-receiver-process: passed " + checks + " assertions");
    }
}
