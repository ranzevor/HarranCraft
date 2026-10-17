package io.ranzevor.harrancraft.bridge;

import java.util.Objects;

/** One client-thread-owned stream of model captures, invalidated on source failure. */
public final class RenderCaptureSession {
    @FunctionalInterface
    public interface Source {
        RenderFrame read(RenderFrame.Identity identity, Frame.Key key);
    }

    private final long epoch;
    private final long world;
    private final long camera;
    private final String dimension;
    private final Source source;
    private final Thread owner = Thread.currentThread();
    private long lastFrame;
    private long lastCapture;
    private boolean active = true;

    public RenderCaptureSession(long epoch, long world, long camera,
                                String dimension, Source source) {
        new RenderFrame.Identity(epoch, world, camera, 1, 1);
        this.epoch = epoch;
        this.world = world;
        this.camera = camera;
        this.dimension = Objects.requireNonNull(dimension);
        if (dimension.isEmpty() || dimension.length() > 64) {
            throw new IllegalArgumentException("Invalid source dimension");
        }
        this.source = Objects.requireNonNull(source);
    }

    public RenderFrame capture(long frame, long capture, Frame.Key key) {
        requireOwner();
        if (!active) throw new IllegalStateException("Render session is inactive");
        if (frame <= lastFrame || capture <= lastCapture || key == null
                || !dimension.equals(key.dimension())) {
            stop();
            throw new IllegalArgumentException("Stale render frame or world");
        }
        try {
            RenderFrame.Identity identity =
                    new RenderFrame.Identity(epoch, world, camera, frame, capture);
            RenderFrame result = Objects.requireNonNull(source.read(identity, key));
            if (!result.identity().equals(identity) || !result.key().equals(key)) {
                throw new IllegalArgumentException("Render source identity changed");
            }
            lastFrame = frame;
            lastCapture = capture;
            return result;
        } catch (RuntimeException error) {
            stop();
            throw error;
        }
    }

    public boolean isActive() {
        requireOwner();
        return active;
    }

    public void stop() {
        requireOwner();
        active = false;
    }

    private void requireOwner() {
        if (owner != Thread.currentThread()) {
            throw new IllegalStateException("Render session accessed from another thread");
        }
    }
}
