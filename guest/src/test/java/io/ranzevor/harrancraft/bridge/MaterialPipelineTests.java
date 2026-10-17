package io.ranzevor.harrancraft.bridge;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;

public final class MaterialPipelineTests {
    private static int checks;
    private static final Frame.Key KEY = new Frame.Key("minecraft:overworld", 0, 80, 0);
    private static final RenderFrame.CameraPose POSE =
            new RenderFrame.CameraPose(0.5, 82, -4, 90, -10);

    private static void require(boolean value, String message) {
        ++checks;
        if (!value) throw new AssertionError(message);
    }

    static RenderFrame model(RenderFrame.Identity id, Frame.Key key) {
        var first = RenderWireTests.golden().quads().getFirst();
        var second = new RenderFrame.Quad(-1, 2, -1, true, 0,
                first.atlas(), "minecraft:block/ore", first.vertices());
        return new RenderFrame(id, key, "minecraft:stone", POSE, List.of(first, second));
    }

    static List<TextureWire> sprites(RenderFrame render) {
        TextureWire fixture = TextureWireTests.golden();
        RenderFrame.Identity id = render.identity();
        return List.of(
                new TextureWire(TextureWire.SNAPSHOT, id, fixture.atlas(), fixture.sprite(),
                        fixture.atlasX(), fixture.atlasY(), fixture.width(), fixture.height(),
                        fixture.minU(), fixture.maxU(), fixture.minV(), fixture.maxV(),
                        fixture.argb()),
                new TextureWire(TextureWire.SNAPSHOT, id, fixture.atlas(), "minecraft:block/ore",
                        18, 32, fixture.width(), fixture.height(), fixture.minU(),
                        fixture.maxU(), fixture.minV(), fixture.maxV(), fixture.argb()));
    }

    private static RenderCaptureSession source(long epoch) {
        return new RenderCaptureSession(epoch, 7, 9, KEY.dimension(), MaterialPipelineTests::model);
    }

    private static final class Memory extends ByteArrayOutputStream {
        final AtomicInteger flushed = new AtomicInteger();
        @Override public synchronized void flush() throws IOException {
            super.flush();
            flushed.incrementAndGet();
        }
    }

    private static void complete(int count, Memory output) throws InterruptedException {
        long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(5);
        while (output.flushed.get() < count && System.nanoTime() < deadline) {
            Thread.sleep(2);
        }
        require(output.flushed.get() == count, "Material output did not flush completed batches");
    }

    private static void retired(RenderPipe model, TexturePipe texture) throws InterruptedException {
        long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(5);
        while ((!model.isTerminated() || !texture.isTerminated())
                && System.nanoTime() < deadline) Thread.sleep(2);
        require(model.isTerminated() && texture.isTerminated(),
                "Model or material transport workers survived shutdown");
        require(model.cleanupFailure() == null && texture.cleanupFailure() == null,
                "Material transport failed to close owned endpoints");
    }

    private static final class Stalled extends OutputStream {
        final CountDownLatch entered = new CountDownLatch(1);
        final CountDownLatch released = new CountDownLatch(1);
        final AtomicBoolean closed = new AtomicBoolean();
        @Override public void write(int value) throws IOException {
            write(new byte[]{(byte) value}, 0, 1);
        }
        @Override public void write(byte[] bytes, int at, int count) throws IOException {
            entered.countDown();
            try { released.await(); }
            catch (InterruptedException error) {
                Thread.currentThread().interrupt();
                throw new IOException("Source sprite write interrupted", error);
            }
            if (closed.get()) throw new IOException("Source sprite output closed");
        }
        @Override public void close() {
            closed.set(true);
            released.countDown();
        }
    }

    private static final class Broken extends OutputStream {
        @Override public void write(int value) throws IOException {
            throw new IOException("Source sprite output failed");
        }
        @Override public void write(byte[] bytes, int at, int count) throws IOException {
            throw new IOException("Source sprite output failed");
        }
    }

    public static void main(String[] args) throws Exception {
        require(args.length == 0, "Unknown material pipeline argument");
        Memory models = new Memory(), textures = new Memory();
        AtomicInteger cancelModel = new AtomicInteger(), cancelTexture = new AtomicInteger();
        MaterialPublisher publisher = new MaterialPublisher(source(17), MaterialPipelineTests::sprites,
                17, 7, 9, models, cancelModel::incrementAndGet,
                textures, cancelTexture::incrementAndGet);
        require(publisher.publish(10, 1, KEY), "First material batch refused");
        require(publisher.publish(12, 2, KEY), "Second material batch refused");
        complete(2, models);
        complete(2, textures);
        publisher.close();
        publisher.close();
        retired(publisher.modelTransport(), publisher.textureTransport());
        require(cancelModel.get() == 1 && cancelTexture.get() == 1,
                "Publisher did not cancel each endpoint exactly once");
        try (var modelInput = new ByteArrayInputStream(models.toByteArray());
             var textureInput = new ByteArrayInputStream(textures.toByteArray())) {
            for (int capture = 1; capture <= 2; ++capture) {
                RenderWire next = RenderWire.read(modelInput);
                require(next.identity().capture() == capture && next.quads().size() == 2,
                        "Render/model frame association changed");
                for (int index = 0; index < 2; ++index) {
                    TextureWire pixel = TextureWire.read(textureInput);
                    require(pixel.identity().capture() == capture && pixel.argb().size() == 4,
                            "Texture source pixels or capture IDs changed");
                }
            }
            require(RenderWire.read(modelInput) == null && TextureWire.read(textureInput) == null,
                    "Unexpected material channel bytes");
        }

        MaterialPublisher incomplete = new MaterialPublisher(source(18),
                render -> List.of(sprites(render).getFirst()),
                18, 7, 9, new ByteArrayOutputStream(), () -> {},
                new ByteArrayOutputStream(), () -> {});
        boolean rejected = false;
        try { incomplete.publish(1, 1, KEY); }
        catch (IllegalArgumentException expected) { rejected = true; }
        require(rejected && !incomplete.isOpen(), "Incomplete material set was sent");
        retired(incomplete.modelTransport(), incomplete.textureTransport());

        MaterialPublisher duplicate = new MaterialPublisher(source(19),
                render -> List.of(sprites(render).getFirst(), sprites(render).getFirst()),
                19, 7, 9, new ByteArrayOutputStream(), () -> {},
                new ByteArrayOutputStream(), () -> {});
        rejected = false;
        try { duplicate.publish(1, 1, KEY); }
        catch (IllegalArgumentException expected) { rejected = true; }
        require(rejected && !duplicate.isOpen(), "Duplicate texture ID was accepted");
        retired(duplicate.modelTransport(), duplicate.textureTransport());

        MaterialPublisher foreign = new MaterialPublisher(source(20), MaterialPipelineTests::sprites,
                20, 7, 9, new ByteArrayOutputStream(), () -> {},
                new ByteArrayOutputStream(), () -> {});
        AtomicReference<Throwable> crossThread = new AtomicReference<>();
        Thread other = Thread.ofPlatform().start(() -> {
            try { foreign.publish(1, 1, KEY); }
            catch (Throwable error) { crossThread.set(error); }
        });
        other.join();
        require(crossThread.get() instanceof IllegalStateException && foreign.isOpen(),
                "Foreign publish thread changed valid session");
        foreign.close();
        retired(foreign.modelTransport(), foreign.textureTransport());

        MaterialPublisher reload = new MaterialPublisher(source(21), MaterialPipelineTests::sprites,
                21, 7, 9, new ByteArrayOutputStream(), () -> {},
                new ByteArrayOutputStream(), () -> {});
        MaterialReloadRegistry.track(reload);
        Thread resources = Thread.ofPlatform().start(MaterialReloadRegistry::onReload);
        resources.join();
        require(!reload.isOpen() && !reload.publish(1, 1, KEY),
                "Client resource reload retained active source material session");
        retired(reload.modelTransport(), reload.textureTransport());

        List<MaterialPublisher> active = new ArrayList<>();
        for (int i = 0; i < MaterialReloadRegistry.MAX_ACTIVE; ++i) {
            MaterialPublisher output = new MaterialPublisher(source(30 + i),
                    MaterialPipelineTests::sprites, 30 + i, 7, 9,
                    new ByteArrayOutputStream(), () -> {},
                    new ByteArrayOutputStream(), () -> {});
            MaterialReloadRegistry.track(output);
            active.add(output);
        }
        MaterialPublisher excess = new MaterialPublisher(source(39),
                MaterialPipelineTests::sprites, 39, 7, 9,
                new ByteArrayOutputStream(), () -> {},
                new ByteArrayOutputStream(), () -> {});
        boolean limited = false;
        try { MaterialReloadRegistry.track(excess); }
        catch (IllegalStateException expected) { limited = true; }
        require(limited, "Resource reload registry exceeded its active-owner bound");
        excess.close();
        retired(excess.modelTransport(), excess.textureTransport());
        MaterialReloadRegistry.onReload();
        for (MaterialPublisher output : active) {
            require(!output.isOpen(), "Resource reload retained an owned publisher");
            retired(output.modelTransport(), output.textureTransport());
        }

        Stalled stalled = new Stalled();
        AtomicInteger cancelled = new AtomicInteger();
        TexturePipe full = new TexturePipe(22, 7, 9, stalled, () -> {
            cancelled.incrementAndGet();
            stalled.close();
        });
        for (int batch = 1; batch <= TexturePipe.QUEUE_CAPACITY + 1; ++batch) {
            var id = new RenderFrame.Identity(22, 7, 9, batch, batch);
            if (batch == 1) {
                require(full.offerBatch(id, sprites(model(id, KEY))),
                        "Failed to prime blocked texture output");
                require(stalled.entered.await(5, TimeUnit.SECONDS),
                        "Texture worker failed to start writing");
            } else {
                require(full.offerBatch(id, sprites(model(id, KEY))),
                        "Texture queue filled prematurely");
            }
        }
        var overflow = new RenderFrame.Identity(22, 7, 9,
                TexturePipe.QUEUE_CAPACITY + 2, TexturePipe.QUEUE_CAPACITY + 2);
        require(!full.offerBatch(overflow, sprites(model(overflow, KEY))),
                "Texture backpressure was bypassed");
        long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(5);
        while (!full.isTerminated() && System.nanoTime() < deadline) Thread.sleep(2);
        require(full.isTerminated() && full.stopReason() == TexturePipe.StopReason.SATURATED
                    && cancelled.get() == 1, "Texture backpressure failed to retire output");

        TexturePipe broken = new TexturePipe(23, 7, 9, new Broken(), () -> {});
        var id = new RenderFrame.Identity(23, 7, 9, 1, 1);
        require(broken.offerBatch(id, sprites(model(id, KEY))),
                "Initial broken stream batch was rejected early");
        deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(5);
        while (broken.isOpen() && System.nanoTime() < deadline) Thread.sleep(2);
        while (!broken.isTerminated() && System.nanoTime() < deadline) Thread.sleep(2);
        require(broken.isTerminated() && broken.stopReason() == TexturePipe.StopReason.OUTPUT_FAILED
                    && broken.ioFailure() instanceof IOException,
                "Broken material output failure was not preserved");

        System.out.println("material-pipeline: passed " + checks + " assertions");
    }
}
