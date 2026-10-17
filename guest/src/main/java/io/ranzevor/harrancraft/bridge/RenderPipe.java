package io.ranzevor.harrancraft.bridge;

import java.io.IOException;
import java.io.OutputStream;
import java.util.Objects;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicReference;

/** Bounded client-owned render output with no stream writes on the capture thread. */
public final class RenderPipe implements AutoCloseable {
    public static final int QUEUE_CAPACITY = 8;

    public enum StopReason {
        CLOSED, OUTPUT_FAILED, SATURATED, WRONG_IDENTITY, STALE_CAPTURE, INTERRUPTED
    }

    private final long epoch;
    private final long world;
    private final long camera;
    private final OutputStream output;
    private final ArrayBlockingQueue<RenderWire> queued = new ArrayBlockingQueue<>(QUEUE_CAPACITY);
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
    private long frame;
    private long capture;

    public RenderPipe(long epoch, long world, long camera, OutputStream output, Runnable cancelIo) {
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
        return Thread.ofPlatform().daemon().name("harrancraft-render-" + name).unstarted(action);
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
        if (owner != Thread.currentThread()) {
            throw new IllegalStateException("Render output requires the capture owner thread");
        }
    }

    void onStop(Runnable listener) {
        requireOwner();
        if (!stopListener.compareAndSet(null, Objects.requireNonNull(listener))) {
            throw new IllegalStateException("Render stop listener is already registered");
        }
        notifyStopped();
    }

    private void notifyStopped() {
        Runnable listener = stopListener.get();
        if (isOpen() || listener == null || !stopNotified.compareAndSet(false, true)) return;
        try { listener.run(); }
        catch (RuntimeException error) { record(cleanupFailure, error); }
    }

    public boolean offer(RenderWire data) {
        requireOwner();
        Objects.requireNonNull(data);
        StopReason reason;
        synchronized (handoff) {
            if (!isOpen()) return false;
            RenderFrame.Identity id = data.identity();
            if (id.epoch() != epoch || id.world() != world || id.camera() != camera) {
                reason = StopReason.WRONG_IDENTITY;
            } else if (capture == Long.MAX_VALUE || id.capture() != capture + 1
                    || id.frame() <= frame) {
                reason = StopReason.STALE_CAPTURE;
            } else if (queued.offer(data)) {
                frame = id.frame();
                capture = id.capture();
                return true;
            } else {
                reason = StopReason.SATURATED;
            }
        }
        stop(reason);
        return false;
    }

    private void writeLoop() {
        try {
            while (isOpen()) {
                RenderWire record = queued.poll(250, TimeUnit.MILLISECONDS);
                if (record != null && isOpen()) {
                    output.write(record.encode());
                    output.flush();
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
            queued.clear();
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
