package io.ranzevor.harrancraft.bridge;

import java.io.Closeable;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.Objects;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;

/** Blocking pipe I/O is confined to workers, never the Minecraft tick thread. */
public final class PipeLink implements Link {
    public static final int QUEUE_CAPACITY = 256;
    private final InputStream input;
    private final OutputStream output;
    private final ArrayBlockingQueue<Frame> incoming = new ArrayBlockingQueue<>(QUEUE_CAPACITY);
    private final ArrayBlockingQueue<Frame> outgoing = new ArrayBlockingQueue<>(QUEUE_CAPACITY);
    private final AtomicBoolean open = new AtomicBoolean(true);
    private final Thread reader;
    private final Thread writer;

    public PipeLink(InputStream input, OutputStream output) {
        this.input = Objects.requireNonNull(input);
        this.output = Objects.requireNonNull(output);
        reader = Thread.ofPlatform().daemon().name("harrancraft-pipe-read").unstarted(this::readLoop);
        writer = Thread.ofPlatform().daemon().name("harrancraft-pipe-write").unstarted(this::writeLoop);
        reader.start();
        writer.start();
    }

    private void readLoop() {
        try {
            while (open.get()) {
                Frame frame = Frame.read(input);
                if (frame == null || !incoming.offer(frame)) break;
            }
        } catch (IOException ignored) {
            // The epoch becomes unusable on any incomplete or invalid stream.
        } finally {
            close();
        }
    }

    private void writeLoop() {
        try {
            while (open.get()) {
                Frame frame = outgoing.poll(250, TimeUnit.MILLISECONDS);
                if (frame != null) {
                    output.write(frame.encode());
                    output.flush();
                }
            }
        } catch (IOException | InterruptedException | IllegalArgumentException ignored) {
            // A failed send requires state reconciliation through a new session.
        } finally {
            close();
        }
    }

    @Override public Frame poll() { return open.get() ? incoming.poll() : null; }
    @Override public int outputCapacity() { return outgoing.remainingCapacity(); }
    @Override public boolean isOpen() { return open.get(); }
    @Override public boolean send(Frame frame) {
        if (!open.get() || !outgoing.offer(frame)) {
            close();
            return false;
        }
        if (!open.get()) {
            outgoing.clear();
            return false;
        }
        return true;
    }
    @Override public void close() {
        if (!open.compareAndSet(true, false)) return;
        incoming.clear();
        outgoing.clear();
        reader.interrupt();
        writer.interrupt();
        // Some pipe streams hold a monitor during writes; closing one direction must not block the other.
        closeStream(input, "harrancraft-pipe-close-input");
        closeStream(output, "harrancraft-pipe-close-output");
    }

    private static void closeStream(Closeable stream, String name) {
        Thread.ofPlatform().daemon().name(name).start(() -> {
            try { stream.close(); } catch (IOException ignored) { }
        });
    }
}
