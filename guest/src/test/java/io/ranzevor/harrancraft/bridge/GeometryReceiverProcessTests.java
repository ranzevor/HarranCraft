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

public final class GeometryReceiverProcessTests {
    private static int checks;
    private static final Frame.Key KEY = new Frame.Key("minecraft:overworld", 0, 80, 0);

    private static void require(boolean value, String message) {
        ++checks;
        if (!value) throw new AssertionError(message);
    }

    private static GeometryFrame frame(long epoch, long revision, String state, double height) {
        return GeometryFrame.snapshot(epoch, revision, KEY, state,
                List.of(new GeometryFrame.Box(0, 0, 0, 1, height, 1)));
    }

    private static byte[] joined(GeometryFrame... frames) {
        var output = new ByteArrayOutputStream();
        for (var frame : frames) output.writeBytes(frame.encode());
        return output.toByteArray();
    }

    private record Result(int exit, String stdout, String stderr) {}

    private static Result finish(Process process) throws Exception {
        if (!process.waitFor(10, TimeUnit.SECONDS)) {
            throw new AssertionError("Native receiver exceeded process deadline");
        }
        return new Result(process.exitValue(),
                new String(process.getInputStream().readAllBytes(), StandardCharsets.UTF_8),
                new String(process.getErrorStream().readAllBytes(), StandardCharsets.UTF_8));
    }

    private static Result raw(String executable, byte[] bytes) throws Exception {
        Process process = new ProcessBuilder(executable, "--receive").start();
        try {
            try (var output = process.getOutputStream()) {
                output.write(bytes);
            }
            return finish(process);
        } finally {
            process.destroyForcibly();
            require(process.waitFor(5, TimeUnit.SECONDS),
                    "Native receiver retained a process after raw exchange");
        }
    }

    private static void valid(String executable, byte[] bytes, int records, int staged)
            throws Exception {
        Result result = raw(executable, bytes);
        require(result.exit() == 0, "Native receiver refused valid records: " + result.stderr());
        require(result.stdout().strip().equals(
                "{\"records\":" + records + ",\"staged_before_close\":" + staged + "}"),
                "Native receiver stage/cleanup counts differ");
    }

    private static void invalid(String executable, byte[] bytes) throws Exception {
        Result result = raw(executable, bytes);
        require(result.exit() != 0, "Native receiver accepted invalid records");
    }

    private static void async(String executable, GeometryFrame... frames) throws Exception {
        Process process = new ProcessBuilder(executable, "--receive").start();
        GeometryPipe pipe = null;
        try {
            AtomicInteger flushed = new AtomicInteger();
            OutputStream counted = new FilterOutputStream(process.getOutputStream()) {
                @Override public void flush() throws IOException {
                    super.flush();
                    flushed.incrementAndGet();
                }
            };
            pipe = new GeometryPipe(17, counted, () -> {});
            for (var frame : frames) {
                require(pipe.offer(frame), "Async geometry pipe refused a valid frame");
            }
            long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(5);
            while (flushed.get() < frames.length && pipe.isOpen()
                    && System.nanoTime() < deadline) {
                Thread.sleep(2);
            }
            require(flushed.get() == frames.length && pipe.isOpen(),
                    "Async pipe did not flush all records");
            pipe.close();
            deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(5);
            while (!pipe.isTerminated() && System.nanoTime() < deadline) Thread.sleep(2);
            require(pipe.isTerminated() && pipe.ioFailure() == null
                            && pipe.cleanupFailure() == null,
                    "Async geometry workers failed to retire");
            Result result = finish(process);
            require(result.exit() == 0,
                    "Native receiver rejected asynchronously written records: " + result.stderr());
            require(result.stdout().strip().equals(
                    "{\"records\":" + frames.length + ",\"staged_before_close\":0}"),
                    "Async Java/C++ geometry state disagreed");
        } finally {
            if (pipe != null) pipe.close();
            process.destroyForcibly();
            require(process.waitFor(5, TimeUnit.SECONDS),
                    "Native receiver process was not retired");
        }
    }

    public static void main(String[] args) throws Exception {
        require(args.length == 1, "Expected native receiver executable");
        String executable = args[0];
        var first = frame(17, 1, "minecraft:stone", 1);
        var second = frame(17, 2, "minecraft:oak_slab[type=bottom,waterlogged=false]", 0.5);
        var air = GeometryFrame.snapshot(17, 3, KEY, "minecraft:air", List.of());
        var retire = GeometryFrame.retire(17, 4, KEY);
        valid(executable, joined(first, second, air, retire), 4, 0);
        valid(executable, first.encode(), 1, 1);
        valid(executable, new byte[0], 0, 0);
        async(executable, first, second, air, retire);

        var malformed = first.encode().clone();
        malformed[0] ^= 0x40;
        invalid(executable, malformed);

        var oversized = Arrays.copyOf(first.encode(), 12);
        ByteBuffer.wrap(oversized).order(ByteOrder.LITTLE_ENDIAN)
                .putInt(8, GeometryFrame.MAX_BYTES + 1);
        invalid(executable, oversized);
        invalid(executable, Arrays.copyOf(first.encode(), 11));
        invalid(executable, Arrays.copyOf(first.encode(), first.encode().length - 1));
        invalid(executable, joined(first, frame(17, 3, "minecraft:stone", 1)));
        invalid(executable, joined(first, frame(18, 2, "minecraft:stone", 1)));
        invalid(executable, joined(first, first));
        invalid(executable, joined(first,
                GeometryFrame.snapshot(17, 2,
                        new Frame.Key("minecraft:the_nether", 0, 80, 0),
                        "minecraft:stone",
                        List.of(new GeometryFrame.Box(0, 0, 0, 1, 1, 1)))));
        System.out.println("geometry-receiver-process: passed " + checks + " assertions");
    }
}
