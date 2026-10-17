package io.ranzevor.harrancraft.bridge;

import java.io.OutputStream;
import java.util.Objects;

/** Links an approved client-thread model source to an owned asynchronous render pipe. */
public final class RenderPublisher implements AutoCloseable {
    private final RenderCaptureSession capture;
    private final RenderPipe pipe;
    private final Thread owner = Thread.currentThread();
    private boolean stopped;

    public RenderPublisher(RenderCaptureSession capture, long epoch, long world, long camera,
                           OutputStream output, Runnable cancelIo) {
        this.capture = Objects.requireNonNull(capture);
        pipe = new RenderPipe(epoch, world, camera, output, cancelIo);
    }

    public boolean publish(long logicalFrame, long captureId, Frame.Key key) {
        requireOwner();
        if (!isOpen()) return false;
        try {
            RenderFrame sampled = capture.capture(logicalFrame, captureId, key);
            if (!pipe.offer(RenderWire.snapshot(sampled))) {
                close();
                return false;
            }
            return true;
        } catch (RuntimeException error) {
            close();
            throw error;
        }
    }

    public boolean isOpen() {
        requireOwner();
        if (!stopped && (!pipe.isOpen() || !capture.isActive())) close();
        return !stopped;
    }

    public RenderPipe transport() {
        requireOwner();
        return pipe;
    }

    @Override public void close() {
        requireOwner();
        if (stopped) return;
        stopped = true;
        capture.stop();
        pipe.close();
    }

    private void requireOwner() {
        if (owner != Thread.currentThread()) {
            throw new IllegalStateException("Render publication requires the client owner thread");
        }
    }
}
