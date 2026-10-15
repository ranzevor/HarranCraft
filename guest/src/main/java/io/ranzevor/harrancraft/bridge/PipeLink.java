package io.ranzevor.harrancraft.bridge;

import java.io.Closeable;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.Objects;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;

/** Blocking I/O, cancellation and stream closure never run on the Minecraft tick thread. */
public final class PipeLink implements Link {
    public static final int QUEUE_CAPACITY = 256;
    public enum StopReason { CLOSED, INPUT_ENDED, INPUT_FAILED, OUTPUT_FAILED,
                             INPUT_SATURATED, OUTPUT_SATURATED, INTERRUPTED }
    private final InputStream input;
    private final OutputStream output;
    private final ArrayBlockingQueue<Frame> incoming = new ArrayBlockingQueue<>(QUEUE_CAPACITY);
    private final ArrayBlockingQueue<Frame> outgoing = new ArrayBlockingQueue<>(QUEUE_CAPACITY);
    private final Object handoff = new Object();
    private final AtomicReference<StopReason> stopped = new AtomicReference<>();
    private final AtomicReference<Throwable> ioFailure = new AtomicReference<>();
    private final AtomicReference<Throwable> cleanupFailure = new AtomicReference<>();
    private final Thread reader;
    private final Thread writer;
    private final Thread cancel;
    private final Thread closeInput;
    private final Thread closeOutput;

    /** Both streams must support releasing blocked I/O through close alone. */
    public PipeLink(InputStream input, OutputStream output) {
        this(input, output, () -> {});
    }

    /** cancelIo must release this link's I/O using only resources explicitly owned by the caller. */
    public PipeLink(InputStream input, OutputStream output, Runnable cancelIo) {
        this.input = Objects.requireNonNull(input);
        this.output = Objects.requireNonNull(output);
        Objects.requireNonNull(cancelIo);
        reader = worker("read", this::readLoop);
        writer = worker("write", this::writeLoop);
        closeInput = worker("close-input", () -> closeStream(input));
        closeOutput = worker("close-output", () -> closeStream(output));
        cancel = worker("cancel", () -> {
            try { cancelIo.run(); }
            catch (RuntimeException error) { record(cleanupFailure, error); }
        });
        reader.start();
        writer.start();
    }

    private static Thread worker(String suffix, Runnable action) {
        return Thread.ofPlatform().daemon().name("harrancraft-pipe-" + suffix).unstarted(action);
    }

    private void readLoop() {
        try {
            while (isOpen()) {
                Frame frame = Frame.read(input);
                if (frame == null) { stop(StopReason.INPUT_ENDED); return; }
                synchronized (handoff) {
                    if (!isOpen()) return;
                    if (incoming.offer(frame)) continue;
                }
                stop(StopReason.INPUT_SATURATED);
                return;
            }
        } catch (IOException | RuntimeException error) {
            if (isOpen()) record(ioFailure, error);
            stop(StopReason.INPUT_FAILED);
        } finally {
            close();
        }
    }

    private void writeLoop() {
        try {
            while (isOpen()) {
                Frame frame = outgoing.poll(250, TimeUnit.MILLISECONDS);
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

    @Override public Frame poll() {
        synchronized (handoff) { return isOpen() ? incoming.poll() : null; }
    }
    @Override public int outputCapacity() { return outgoing.remainingCapacity(); }
    @Override public boolean isOpen() { return stopped.get() == null; }
    public StopReason stopReason() { return stopped.get(); }
    public Throwable ioFailure() { return ioFailure.get(); }
    public Throwable cleanupFailure() { return cleanupFailure.get(); }

    /** Worker termination is distinct from successful resource cleanup. */
    public boolean isTerminated() {
        return !isOpen() && ended(reader) && ended(writer) && ended(cancel)
                && ended(closeInput) && ended(closeOutput);
    }

    private static boolean ended(Thread thread) {
        return thread.getState() == Thread.State.TERMINATED;
    }

    @Override public boolean send(Frame frame) {
        Objects.requireNonNull(frame);
        synchronized (handoff) {
            if (!isOpen()) return false;
            if (outgoing.offer(frame)) return true;
        }
        stop(StopReason.OUTPUT_SATURATED);
        return false;
    }

    @Override public void close() { stop(StopReason.CLOSED); }

    private void stop(StopReason reason) {
        synchronized (handoff) {
            if (!stopped.compareAndSet(null, reason)) return;
            incoming.clear();
            outgoing.clear();
        }
        reader.interrupt();
        writer.interrupt();
        // Cancellation and both closes are independent: any one may wait on a stream monitor.
        cancel.start();
        closeInput.start();
        closeOutput.start();
    }

    private void closeStream(Closeable stream) {
        try { stream.close(); }
        catch (IOException | RuntimeException error) { record(cleanupFailure, error); }
    }

    private static void record(AtomicReference<Throwable> target, Throwable error) {
        if (!target.compareAndSet(null, error)) {
            Throwable first = target.get();
            if (first != error) first.addSuppressed(error);
        }
    }
}
