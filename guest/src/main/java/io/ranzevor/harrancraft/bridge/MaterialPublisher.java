package io.ranzevor.harrancraft.bridge;

import java.io.OutputStream;
import java.util.HashSet;
import java.util.List;
import java.util.Objects;
import java.util.Set;

/** Captures a model and all required source sprites before enqueueing either channel. */
public final class MaterialPublisher implements AutoCloseable {
    @FunctionalInterface
    public interface SpriteSource {
        List<TextureWire> read(RenderFrame frame);
    }

    private record SpriteId(String atlas, String sprite) {}

    private final RenderCaptureSession capture;
    private final SpriteSource sprites;
    private final RenderPipe models;
    private final TexturePipe textures;
    private final Thread owner = Thread.currentThread();
    private boolean stopped;

    public MaterialPublisher(RenderCaptureSession capture, SpriteSource sprites,
                             long epoch, long world, long camera,
                             OutputStream modelOutput, Runnable cancelModel,
                             OutputStream textureOutput, Runnable cancelTexture) {
        this.capture = Objects.requireNonNull(capture);
        this.sprites = Objects.requireNonNull(sprites);
        Objects.requireNonNull(modelOutput);
        Objects.requireNonNull(cancelModel);
        Objects.requireNonNull(textureOutput);
        Objects.requireNonNull(cancelTexture);
        models = new RenderPipe(epoch, world, camera, modelOutput, cancelModel);
        textures = new TexturePipe(epoch, world, camera, textureOutput, cancelTexture);
    }

    public boolean publish(long frame, long captureId, Frame.Key key) {
        requireOwner();
        if (!isOpen()) return false;
        try {
            RenderFrame model = capture.capture(frame, captureId, key);
            List<TextureWire> batch = List.copyOf(
                    Objects.requireNonNull(sprites.read(model)));
            validate(model, batch);
            if (!models.offer(RenderWire.snapshot(model))
                    || !textures.offerBatch(model.identity(), batch)) {
                close();
                return false;
            }
            return true;
        } catch (RuntimeException error) {
            close();
            throw error;
        }
    }

    private static void validate(RenderFrame model, List<TextureWire> batch) {
        if (batch.size() > TexturePipe.MAX_SPRITES) {
            throw new IllegalArgumentException("Too many material sprites");
        }
        Set<SpriteId> expected = new HashSet<>();
        for (RenderFrame.Quad quad : model.quads()) {
            expected.add(new SpriteId(quad.atlas(), quad.sprite()));
            if (expected.size() > TexturePipe.MAX_SPRITES) {
                throw new IllegalArgumentException("Model exceeds sprite capacity");
            }
        }
        Set<SpriteId> actual = new HashSet<>();
        for (TextureWire texture : batch) {
            if (texture == null || texture.kind() != TextureWire.SNAPSHOT
                    || !texture.identity().equals(model.identity())
                    || !actual.add(new SpriteId(texture.atlas(), texture.sprite()))) {
                throw new IllegalArgumentException("Duplicate or foreign material sprite");
            }
        }
        if (!expected.equals(actual)) {
            throw new IllegalArgumentException("Incomplete or unknown material manifest");
        }
    }

    public boolean isOpen() {
        requireOwner();
        if (!stopped && (!capture.isActive() || !models.isOpen() || !textures.isOpen())) close();
        return !stopped;
    }

    public RenderPipe modelTransport() {
        requireOwner();
        return models;
    }

    public TexturePipe textureTransport() {
        requireOwner();
        return textures;
    }

    // May be called by a resource-reload listener without entering game callbacks.
    public void invalidateTransports() {
        models.close();
        textures.close();
    }

    @Override public void close() {
        requireOwner();
        if (stopped) return;
        stopped = true;
        capture.stop();
        invalidateTransports();
        MaterialReloadRegistry.untrack(this);
    }

    private void requireOwner() {
        if (Thread.currentThread() != owner) {
            throw new IllegalStateException("Material publisher accessed from a foreign thread");
        }
    }
}
