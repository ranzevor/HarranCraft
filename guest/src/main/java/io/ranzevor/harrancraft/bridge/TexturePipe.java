package io.ranzevor.harrancraft.bridge;

import java.io.IOException;
import java.io.OutputStream;
import java.util.HashSet;
import java.util.List;
import java.util.Objects;
import java.util.Set;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicReference;

/** Asynchronously transfers one complete, bounded source-sprite batch per capture. */
public final class TexturePipe implements AutoCloseable {
    public static final int QUEUE_CAPACITY = 2;
    public static final int MAX_SPRITES = 16;

    public enum StopReason {
        CLOSED, OUTPUT_FAILED, SATURATED, WRONG_IDENTITY, STALE_CAPTURE, INVALID_BATCH, INTERRUPTED
    }

    private record SpriteId(String atlas, String sprite) {}

    private final long epoch;
    private final long world;
    private final long camera;
    private final OutputStream output;
    private final ArrayBlockingQueue<List<TextureWire>> batches =
            new ArrayBlockingQueue<>(QUEUE_CAPACITY);
    private final Object handoff = new Object();
    private final Thread owner = Thread.currentThread();
    private final Thread writer;
    private final Thread cancel;
    private final Thread closeOutput;
    private final AtomicReference<StopReason> stopped = new AtomicReference<>();
    private final AtomicReference<Throwable> ioFailure = new AtomicReference<>();
    private final AtomicReference<Throwable> cleanupFailure = new AtomicReference<>();
    private final AtomicReference<Runnable> stopListener = new AtomicReference<>();
    private final AtomicBoolean stopNotified = new AtomicBoolean();
    private long lastFrame;
    private long lastCapture;

    public TexturePipe(long epoch, long world, long camera, OutputStream output, Runnable cancelIo) {
        new RenderFrame.Identity(epoch, world, camera, 1, 1);
        this.epoch = epoch;
        this.world = world;
        this.camera = camera;
        this.output = Objects.requireNonNull(output);
        Objects.requireNonNull(cancelIo);
        writer = worker("write", this::writeLoop);
        cancel = worker("cancel", () -> {
            try { cancelIo.run(); }
            catch (RuntimeException error) { record(cleanupFailure, error); }
        });
        closeOutput = worker("close", () -> {
            try { output.close(); }
            catch (IOException | RuntimeException error) { record(cleanupFailure, error); }
        });
        writer.start();
    }

    private static Thread worker(String name, Runnable action) {
        return Thread.ofPlatform().daemon().name("harrancraft-texture-" + name).unstarted(action);
    }

    public boolean isOpen() { return stopped.get() == null; }
    public StopReason stopReason() { return stopped.get(); }
    public Throwable ioFailure() { return ioFailure.get(); }
    public Throwable cleanupFailure() { return cleanupFailure.get(); }
    public boolean isTerminated() {
        return !isOpen() && ended(writer) && ended(cancel) && ended(closeOutput);
    }

    private static boolean ended(Thread thread) {
        return thread.getState() == Thread.State.TERMINATED;
    }

    private void requireOwner() {
        if (Thread.currentThread() != owner) {
            throw new IllegalStateException("Material publishing requires the capture owner thread");
        }
    }

    void onStop(Runnable listener) {
        requireOwner();
        if (!stopListener.compareAndSet(null, Objects.requireNonNull(listener))) {
            throw new IllegalStateException("Texture stop listener is already registered");
        }
        notifyStopped();
    }

    private void notifyStopped() {
        Runnable listener = stopListener.get();
        if (isOpen() || listener == null || !stopNotified.compareAndSet(false, true)) return;
        try { listener.run(); }
        catch (RuntimeException error) { record(cleanupFailure, error); }
    }

    public boolean offerBatch(RenderFrame.Identity identity, List<TextureWire> sprites) {
        requireOwner();
        Objects.requireNonNull(identity);
        Objects.requireNonNull(sprites);
        StopReason reason;
        synchronized (handoff) {
            if (!isOpen()) return false;
            if (identity.epoch() != epoch || identity.world() != world
                    || identity.camera() != camera) {
                reason = StopReason.WRONG_IDENTITY;
            } else if (lastCapture == Long.MAX_VALUE || identity.capture() != lastCapture + 1
                    || identity.frame() <= lastFrame) {
                reason = StopReason.STALE_CAPTURE;
            } else if (!validSprites(identity, sprites)) {
                reason = StopReason.INVALID_BATCH;
            } else if (batches.offer(List.copyOf(sprites))) {
                lastFrame = identity.frame();
                lastCapture = identity.capture();
                return true;
            } else {
                reason = StopReason.SATURATED;
            }
        }
        stop(reason);
        return false;
    }

    private static boolean validSprites(RenderFrame.Identity identity, List<TextureWire> sprites) {
        if (sprites.size() > MAX_SPRITES) return false;
        Set<SpriteId> identities = new HashSet<>();
        for (TextureWire sprite : sprites) {
            if (sprite == null || sprite.kind() != TextureWire.SNAPSHOT
                    || !sprite.identity().equals(identity)
                    || !identities.add(new SpriteId(sprite.atlas(), sprite.sprite()))) {
                return false;
            }
        }
        return true;
    }

    private void writeLoop() {
        try {
            while (isOpen()) {
                List<TextureWire> batch = batches.poll(250, TimeUnit.MILLISECONDS);
                if (batch != null && isOpen()) {
                    for (TextureWire sprite : batch) {
                        if (!isOpen()) break;
                        output.write(sprite.encode());
                    }
                    if (isOpen()) output.flush();
                }
            }
        } catch (InterruptedException error) {
            Thread.currentThread().interrupt();
            stop(StopReason.INTERRUPTED);
        } catch (IOException | RuntimeException error) {
            if (isOpen()) record(ioFailure, error);
            stop(StopReason.OUTPUT_FAILED);
        } finally {
            close();
        }
    }

    @Override public void close() {
        stop(StopReason.CLOSED);
    }

    private void stop(StopReason reason) {
        synchronized (handoff) {
            if (!stopped.compareAndSet(null, reason)) return;
            batches.clear();
        }
        writer.interrupt();
        cancel.start();
        closeOutput.start();
        notifyStopped();
    }

    private static void record(AtomicReference<Throwable> slot, Throwable error) {
        if (!slot.compareAndSet(null, error)) {
            Throwable first = slot.get();
            if (first != error) first.addSuppressed(error);
        }
    }
}
