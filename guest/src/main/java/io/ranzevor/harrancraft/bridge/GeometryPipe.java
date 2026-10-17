package io.ranzevor.harrancraft.bridge;

import java.io.IOException;
import java.io.OutputStream;
import java.util.Objects;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;

/** One owned geometry output endpoint; no blocking writes on the Minecraft server thread. */
public final class GeometryPipe implements AutoCloseable {
    public static final int QUEUE_CAPACITY = 64;

    public enum StopReason {
        CLOSED, OUTPUT_FAILED, SATURATED, WRONG_EPOCH, REVISION_INVALID, INTERRUPTED
    }

    private final long epoch;
    private final OutputStream output;
    private final ArrayBlockingQueue<GeometryFrame> queued = new ArrayBlockingQueue<>(QUEUE_CAPACITY);
    private final Object handoff = new Object();
    private final Thread owner = Thread.currentThread();
    private final Thread writer;
    private final Thread cancel;
    private final Thread closeOutput;
    private final AtomicReference<StopReason> stopped = new AtomicReference<>();
    private final AtomicReference<Throwable> ioFailure = new AtomicReference<>();
    private final AtomicReference<Throwable> cleanupFailure = new AtomicReference<>();
    private long revision;

    public GeometryPipe(long epoch, OutputStream output, Runnable cancelIo) {
        if (epoch <= 0) throw new IllegalArgumentException("Geometry epoch must be positive");
        this.epoch = epoch;
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

    private static Thread worker(String suffix, Runnable task) {
        return Thread.ofPlatform().daemon().name("harrancraft-geometry-" + suffix).unstarted(task);
    }

    public long epoch() { return epoch; }
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
            throw new IllegalStateException("Geometry publishing requires the owner thread");
        }
    }

    public long nextRevision() {
        requireOwner();
        if (!isOpen() || revision == Long.MAX_VALUE) {
            close();
            throw new IllegalStateException("Geometry revision unavailable");
        }
        return revision + 1;
    }

    public boolean offer(GeometryFrame frame) {
        requireOwner();
        Objects.requireNonNull(frame);
        StopReason reason;
        synchronized (handoff) {
            if (!isOpen()) return false;
            if (frame.epoch() != epoch) {
                reason = StopReason.WRONG_EPOCH;
            } else if (revision == Long.MAX_VALUE || frame.revision() != revision + 1) {
                reason = StopReason.REVISION_INVALID;
            } else if (queued.offer(frame)) {
                revision = frame.revision();
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
                GeometryFrame frame = queued.poll(250, TimeUnit.MILLISECONDS);
                if (frame != null && isOpen()) {
                    output.write(frame.encode());
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
    }

    private static void record(AtomicReference<Throwable> slot, Throwable error) {
        if (!slot.compareAndSet(null, error)) {
            Throwable previous = slot.get();
            if (previous != error) previous.addSuppressed(error);
        }
    }
}
