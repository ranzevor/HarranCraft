package io.ranzevor.harrancraft.bridge;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.concurrent.atomic.AtomicReference;

public final class RenderFrameTests {
    private static int checks;
    private static final Frame.Key KEY =
            new Frame.Key("minecraft:overworld", 0, 80, 0);
    private static final RenderFrame.CameraPose CAMERA =
            new RenderFrame.CameraPose(0.5, 82.0, -4.0, 90, -10);

    private static void check(boolean condition, String message) {
        ++checks;
        if (!condition) throw new AssertionError(message);
    }

    private static void rejected(Runnable action) {
        try { action.run(); }
        catch (IllegalArgumentException error) { ++checks; return; }
        throw new AssertionError("Invalid render geometry accepted");
    }

    private static int[] packed() {
        int[] raw = new int[32];
        for (int index = 0; index < 4; ++index) {
            int at = index * 8;
            raw[at] = Float.floatToRawIntBits(index == 1 || index == 2 ? 1.0f : 0.0f);
            raw[at + 1] = Float.floatToRawIntBits(index >= 2 ? 1.0f : 0.0f);
            raw[at + 2] = Float.floatToRawIntBits(0.5f);
            raw[at + 3] = 0xff00aa55;
            raw[at + 4] = Float.floatToRawIntBits(index / 4.0f);
            raw[at + 5] = Float.floatToRawIntBits(index / 8.0f);
            raw[at + 6] = 0xf000f0;
            raw[at + 7] = 0x007f0000;
        }
        return raw;
    }

    private static RenderFrame sample(RenderFrame.Identity key) {
        var quad = new RenderFrame.Quad(-1, 2, -1, true, 0,
                "minecraft:textures/atlas/blocks.png", "minecraft:block/stone",
                RenderFrame.unpackVertices(packed()));
        return new RenderFrame(key, KEY, "minecraft:stone", CAMERA, List.of(quad));
    }

    public static void main(String[] args) throws Exception {
        check(args.length == 0, "Unexpected render test arguments");
        RenderFrame.Identity first = new RenderFrame.Identity(7, 13, 3, 1, 1);
        RenderFrame result = sample(first);
        check(result.quads().size() == 1 && result.quads().getFirst().vertices().size() == 4,
                "Baked model vertex count changed");
        check(result.quads().getFirst().vertices().get(2).y() == 1.0f,
                "Packed vertex position was not decoded");
        check(result.quads().getFirst().vertices().get(2).z() == 0.5f,
                "Render quad vertex Z changed");
        check(result.quads().getFirst().vertices().get(2).packedColor() == 0xff00aa55,
                "Vertex packed color was lost");
        check(result.quads().getFirst().vertices().get(2).packedLight() == 0xf000f0,
                "Vertex packed light was lost");
        var source = new ArrayList<>(result.quads());
        source.clear();
        check(result.quads().size() == 1, "Render frame mutable backing list");
        var vertices = new ArrayList<>(result.quads().getFirst().vertices());
        vertices.clear();
        check(result.quads().getFirst().vertices().size() == 4,
                "Render quad mutable vertex list");
        rejected(() -> new RenderFrame.Identity(0, 13, 3, 1, 1));
        rejected(() -> new RenderFrame.Identity(7, 0, 3, 1, 1));
        rejected(() -> new RenderFrame.CameraPose(Double.NaN, 0, 0, 0, 0));
        rejected(() -> new RenderFrame.CameraPose(0, 0, 0, 0, Float.POSITIVE_INFINITY));
        rejected(() -> RenderFrame.unpackVertices(new int[31]));
        rejected(() -> RenderFrame.unpackVertices(null));
        int[] nonfinite = packed();
        nonfinite[1] = Float.floatToRawIntBits(Float.NaN);
        rejected(() -> RenderFrame.unpackVertices(nonfinite));
        rejected(() -> new RenderFrame.Quad(6, 2, -1, true, 0,
                "minecraft:atlas", "minecraft:sprite", RenderFrame.unpackVertices(packed())));
        rejected(() -> new RenderFrame.Quad(-1, 2, -1, true, 0,
                "minecraft:atlas", "minecraft:sprite", List.of()));
        rejected(() -> new RenderFrame.Quad(-1, 2, -1, true, 0,
                "minecraft:atlas", "../bad\nname", RenderFrame.unpackVertices(packed())));
        rejected(() -> new RenderFrame(first, KEY, "minecraft:stone", CAMERA,
                Arrays.asList(new RenderFrame.Quad[RenderFrame.MAX_QUADS + 1])));

        RenderCaptureSession session = new RenderCaptureSession(7, 13, 3,
                "minecraft:overworld", RenderFrameTests::sampleFromSource);
        check(session.capture(1, 1, KEY).identity().equals(first), "Frame stamp changed");
        check(session.capture(2, 2, KEY).identity().frame() == 2, "Next frame not captured");
        rejected(() -> session.capture(2, 3, KEY));
        check(!session.isActive(), "Duplicate logical frame failed to revoke session");

        RenderCaptureSession wrongWorld = new RenderCaptureSession(7, 13, 3,
                "minecraft:overworld", RenderFrameTests::sampleFromSource);
        rejected(() -> wrongWorld.capture(1, 1,
                new Frame.Key("minecraft:the_nether", 0, 80, 0)));
        check(!wrongWorld.isActive(), "Foreign dimension kept session alive");
        var mismatch = new RenderCaptureSession(7, 13, 3, "minecraft:overworld",
                (id, key) -> sample(new RenderFrame.Identity(
                        id.epoch(), id.world(), id.camera(), id.frame() + 1, id.capture())));
        rejected(() -> mismatch.capture(1, 1, KEY));
        check(!mismatch.isActive(), "Source capture identity mismatch kept session");
        var failure = new RenderCaptureSession(7, 13, 3, "minecraft:overworld",
                (id, key) -> { throw new IllegalStateException("World no longer available"); });
        boolean stopped = false;
        try { failure.capture(1, 1, KEY); }
        catch (IllegalStateException expected) { stopped = true; }
        check(stopped && !failure.isActive(), "World invalidation did not stop");
        var owned = new RenderCaptureSession(7, 13, 3, "minecraft:overworld",
                RenderFrameTests::sampleFromSource);
        AtomicReference<Throwable> other = new AtomicReference<>();
        Thread worker = Thread.ofPlatform().start(() -> {
            try { owned.capture(1, 1, KEY); }
            catch (Throwable error) { other.set(error); }
        });
        worker.join();
        check(other.get() instanceof IllegalStateException,
                "Cross-thread render capture was accepted");
        check(owned.isActive(), "Cross-thread refusal stopped valid owner session");
        owned.stop();
        owned.stop();
        check(!owned.isActive(), "Explicit stop is not idempotent");
        System.out.println("render-frame: passed " + checks + " assertions");
    }

    private static RenderFrame sampleFromSource(RenderFrame.Identity stamp, Frame.Key key) {
        return sample(stamp);
    }
}
