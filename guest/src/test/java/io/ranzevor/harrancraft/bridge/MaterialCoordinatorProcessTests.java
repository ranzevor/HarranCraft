package io.ranzevor.harrancraft.bridge;

import java.io.BufferedInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import java.nio.file.attribute.PosixFilePermission;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.HexFormat;
import java.util.List;
import java.util.Set;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicLong;
import java.util.concurrent.atomic.AtomicReference;
import java.util.function.Predicate;

/** Exchanges model and texture records over two independently scheduled OS pipes. */
public final class MaterialCoordinatorProcessTests {
    private static final String DIMENSION = "minecraft:overworld";
    private static final int NORMAL_TIMEOUT_MS = 5000;
    private static final long PRESSURE_EPOCH = 190;
    private static int checks;
    private static int scenarios;
    private static String executable;

    private MaterialCoordinatorProcessTests() {}

    private static void require(boolean condition, String message) {
        ++checks;
        if (!condition) throw new AssertionError(message);
    }

    private static RenderFrame model(long epoch, long capture, int x) {
        return MaterialPipelineTests.model(new RenderFrame.Identity(epoch, 7, 9,
                9 + capture, capture), new Frame.Key(DIMENSION, x, 80, 0));
    }

    private static byte[] encoded(RenderFrame frame) {
        return RenderWire.snapshot(frame).encode();
    }

    private static String materialToken(RenderFrame frame) {
        HexFormat hex = HexFormat.of();
        StringBuilder result = new StringBuilder(hex.formatHex(encoded(frame)));
        List<TextureWire> sprites = new ArrayList<>(MaterialPipelineTests.sprites(frame));
        sprites.sort(Comparator.comparing(TextureWire::atlas).thenComparing(TextureWire::sprite));
        for (TextureWire sprite : sprites) result.append(':').append(hex.formatHex(sprite.encode()));
        return result.toString();
    }

    private record Snapshot(long sequence, String reason, long modelBytes, long textureBytes,
                            String state, long epoch, long models, long textures, long pending,
                            long ready, long retainedBytes, long retainedRecords, long buffered,
                            List<String> materials) {
        static Snapshot parse(String line) {
            String[] fields = line.split(" ", -1);
            require(fields.length == 17 && fields[0].equals("STATE"),
                    "Malformed coordinator peer response");
            return new Snapshot(Long.parseLong(fields[1]), fields[2], Long.parseLong(fields[3]),
                    Long.parseLong(fields[4]), fields[5], Long.parseLong(fields[6]),
                    Long.parseLong(fields[7]), Long.parseLong(fields[8]), Long.parseLong(fields[9]),
                    Long.parseLong(fields[10]), Long.parseLong(fields[11]), Long.parseLong(fields[12]),
                    Long.parseLong(fields[13]), List.of(fields[14], fields[15], fields[16]));
        }

        String summary() {
            return state + ": models=" + models + ", textures=" + textures + ", pending="
                    + pending + ", ready=" + ready + ", read=" + modelBytes + '/' + textureBytes;
        }

        void noReady() {
            require(state.equals("active") && ready == 0 && materials.stream().allMatch("-"::equals),
                    "Incomplete or superseded material became usable: " + summary());
        }

        void ready(RenderFrame expected) {
            require(state.equals("active") && epoch == expected.identity().epoch()
                            && materials.get(expected.key().x()).equals(materialToken(expected)),
                    "Native model, identity, manifest or source pixels changed: " + summary());
        }

        void cleared(String expected) {
            require(state.equals(expected) && epoch == 0 && pending == 0 && ready == 0
                            && retainedBytes == 0 && retainedRecords == 0 && buffered == 0
                            && materials.stream().allMatch("-"::equals),
                    "Terminal material state retained owned data: " + summary());
        }
    }

    private static final class Peer implements AutoCloseable {
        private final Process process;
        private final InputStream responses;
        private final OutputStream models;
        private final ArrayBlockingQueue<String> lines = new ArrayBlockingQueue<>(256);
        private final AtomicReference<Throwable> readerFailure = new AtomicReference<>();
        private final AtomicBoolean readerDone = new AtomicBoolean();
        private final Thread reader;
        private OutputStream textures;
        private Path pipe;
        private boolean windowsPipe;
        private boolean endpointVerified;
        private boolean modelClosed;
        private boolean textureClosed;
        private boolean closed;
        private long modelSent;
        private long textureSent;
        private Snapshot latest;

        Peer(long epoch) throws Exception { this(epoch, NORMAL_TIMEOUT_MS); }

        Peer(long epoch, int timeoutMs) throws Exception {
            process = new ProcessBuilder(executable, "--coordinate", Long.toString(epoch),
                    Integer.toString(timeoutMs)).redirectErrorStream(true).start();
            responses = process.getInputStream();
            models = process.getOutputStream();
            reader = Thread.ofPlatform().daemon().name("hc-material-peer-responses").start(this::read);
            try {
                String[] greeting = line().split(" ", 3);
                require(greeting.length == 3 && greeting[0].equals("READY")
                                && Long.parseLong(greeting[1]) == process.pid(),
                        "Native material peer did not identify its second pipe");
                windowsPipe = greeting[2].startsWith("\\\\.\\pipe\\HarranCraft-material-");
                pipe = Path.of(greeting[2]);
                if (!windowsPipe) {
                    require(pipe.isAbsolute() && pipe.getParent().getParent().equals(Path.of("/tmp"))
                                    && pipe.getParent().getFileName().toString()
                                        .startsWith("harrancraft-material-")
                                    && pipe.getFileName().toString().equals("textures"),
                            "Native peer returned an unexpected texture endpoint");
                    require(Files.getPosixFilePermissions(pipe.getParent(), LinkOption.NOFOLLOW_LINKS)
                                    .equals(Set.of(PosixFilePermission.OWNER_READ,
                                            PosixFilePermission.OWNER_WRITE,
                                            PosixFilePermission.OWNER_EXECUTE))
                                    && Files.getPosixFilePermissions(pipe, LinkOption.NOFOLLOW_LINKS)
                                        .equals(Set.of(PosixFilePermission.OWNER_READ,
                                                PosixFilePermission.OWNER_WRITE))
                                    && ((int) Files.getAttribute(pipe, "unix:mode",
                                            LinkOption.NOFOLLOW_LINKS) & 0170000) == 0010000,
                            "Native texture endpoint is not a private FIFO");
                }
                endpointVerified = true;
                // WRITE opens the existing endpoint without creating or truncating a file.
                textures = Files.newOutputStream(pipe, StandardOpenOption.WRITE);
                await(value -> value.state().equals("active") && value.epoch() == epoch,
                        "initial session");
            } catch (Exception | Error error) {
                try { close(); }
                catch (IOException cleanup) { error.addSuppressed(cleanup); }
                throw error;
            }
        }

        private void read() {
            try (InputStream input = new BufferedInputStream(responses)) {
                StringBuilder current = new StringBuilder();
                int total = 0;
                for (int value; (value = input.read()) != -1;) {
                    if (++total > 4 * 1024 * 1024 || current.length() >= 16 * 1024) {
                        throw new IOException("Native material peer exceeded its output bound");
                    }
                    if (value == '\n') {
                        if (!lines.offer(current.toString())) {
                            throw new IOException("Native material response queue overflowed");
                        }
                        current.setLength(0);
                    } else if (value != '\r') {
                        if (value < 32 || value > 126) {
                            throw new IOException("Native material peer returned non-ASCII output");
                        }
                        current.append((char) value);
                    }
                }
                if (!current.isEmpty()) throw new IOException("Native material peer truncated a response");
            } catch (Throwable error) {
                readerFailure.set(error);
            } finally {
                readerDone.set(true);
            }
        }

        private String line() throws Exception {
            long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(8);
            while (System.nanoTime() < deadline) {
                Throwable error = readerFailure.get();
                if (error != null) throw new AssertionError("Native material response reader failed", error);
                String value = lines.poll(50, TimeUnit.MILLISECONDS);
                if (value != null) return value;
                require(!readerDone.get(), "Native material peer ended before its expected response");
            }
            throw new AssertionError("Native material peer response timed out");
        }

        Snapshot await(Predicate<Snapshot> condition, String expected) throws Exception {
            if (latest != null && condition.test(latest)) return latest;
            while (true) {
                if (latest != null && !latest.state().equals("active")) {
                    throw new AssertionError("Expected " + expected + "; observed " + latest.summary());
                }
                Snapshot next = Snapshot.parse(line());
                require(latest == null || next.sequence() == latest.sequence() + 1,
                        "Native material observations were lost or reordered");
                latest = next;
                if (condition.test(next)) return next;
            }
        }

        Snapshot model(RenderFrame frame) throws Exception { return model(encoded(frame)); }
        Snapshot model(byte[] bytes) throws Exception { return send(true, bytes, 0, bytes.length); }
        Snapshot texture(TextureWire sprite) throws Exception { return texture(sprite.encode()); }
        Snapshot texture(byte[] bytes) throws Exception { return send(false, bytes, 0, bytes.length); }

        Snapshot send(boolean model, byte[] bytes, int at, int count) throws Exception {
            OutputStream output = model ? models : textures;
            output.write(bytes, at, count);
            output.flush();
            if (model) modelSent += count;
            else textureSent += count;
            return await(value -> value.modelBytes() == modelSent && value.textureBytes() == textureSent,
                    "native consumption of both independent streams");
        }

        Snapshot sprites(RenderFrame frame) throws Exception {
            Snapshot result = null;
            for (TextureWire sprite : MaterialPipelineTests.sprites(frame)) result = texture(sprite);
            return result;
        }

        private void closeChannel(boolean model) throws IOException {
            if (model && !modelClosed) {
                modelClosed = true;
                models.close();
            } else if (!model && !textureClosed && textures != null) {
                textureClosed = true;
                textures.close();
            }
        }

        void eof(boolean model, String expected) throws Exception {
            closeChannel(model);
            terminal(expected);
        }

        void terminal(String expected) throws Exception {
            Snapshot value = await(next -> next.state().equals(expected), expected);
            value.cleared(expected);
            require(process.waitFor(5, TimeUnit.SECONDS) && process.exitValue() == 0,
                    "Native material peer failed or survived a terminal state");
            require(windowsPipe || (!Files.exists(pipe, LinkOption.NOFOLLOW_LINKS)
                            && !Files.exists(pipe.getParent(), LinkOption.NOFOLLOW_LINKS)),
                    "Native material peer retained its private FIFO or directory");
        }

        @Override public void close() throws IOException {
            if (closed) return;
            closed = true;
            IOException failure = null;
            boolean interrupted = false;
            for (boolean channel : new boolean[]{true, false}) {
                try { closeChannel(channel); }
                catch (IOException error) {
                    failure = retain(failure, error);
                }
            }
            for (int attempt = 0; attempt < 2 && process.isAlive(); ++attempt) {
                try {
                    if (process.waitFor(3, TimeUnit.SECONDS)) break;
                    process.destroyForcibly();
                    failure = retain(failure, new IOException("Native material peer required forced cleanup"));
                } catch (InterruptedException error) {
                    interrupted = true;
                    process.destroyForcibly();
                    failure = retain(failure, new IOException("Material process cleanup interrupted", error));
                }
            }
            if (process.isAlive()) {
                failure = retain(failure, new IOException("Native material peer survived forced cleanup"));
            }
            for (int attempt = 0; attempt < 2 && reader.isAlive(); ++attempt) {
                try { reader.join(TimeUnit.SECONDS.toMillis(2)); }
                catch (InterruptedException error) {
                    interrupted = true;
                    failure = retain(failure, new IOException("Material reader cleanup interrupted", error));
                }
                if (reader.isAlive()) {
                    try { responses.close(); }
                    catch (IOException error) { failure = retain(failure, error); }
                }
            }
            if (reader.isAlive()) {
                failure = retain(failure, new IOException("Native material response reader survived cleanup"));
            }
            if (readerFailure.get() != null) {
                failure = retain(failure, new IOException("Native material response reader failed", readerFailure.get()));
            }
            if (!process.isAlive() && process.exitValue() != 0) {
                failure = retain(failure, new IOException("Native material peer exit was unsuccessful"));
            }
            try { responses.close(); }
            catch (IOException error) { failure = retain(failure, error); }
            if (!process.isAlive() && !windowsPipe && endpointVerified) {
                try {
                    if (Files.exists(pipe.getParent(), LinkOption.NOFOLLOW_LINKS)) {
                        failure = retain(failure, new IOException("Native material peer retained its private directory"));
                    }
                    // Failed tests remove their child's verified endpoint after recording any leak.
                    Files.deleteIfExists(pipe);
                    Files.deleteIfExists(pipe.getParent());
                } catch (IOException error) {
                    failure = retain(failure, error);
                }
            }
            if (interrupted) Thread.currentThread().interrupt();
            if (failure != null) throw failure;
        }

        private static IOException retain(IOException failure, IOException next) {
            if (failure == null) return next;
            failure.addSuppressed(next);
            return failure;
        }
    }

    private static void modelFirst() throws Exception {
        RenderFrame first = model(101, 1, 0);
        try (Peer peer = new Peer(101)) {
            Snapshot pending = peer.model(first);
            pending.noReady();
            require(pending.pending() == 1 && pending.models() == 1 && pending.textures() == 0,
                    "Model-first capture was not retained");
            peer.texture(MaterialPipelineTests.sprites(first).getFirst()).noReady();
            Snapshot ready = peer.texture(MaterialPipelineTests.sprites(first).getLast());
            ready.ready(first);
            require(ready.pending() == 0 && ready.ready() == 1 && ready.retainedRecords() == 3,
                    "Complete material did not retire its pending association");
            peer.eof(true, "ended");
        }
    }

    private static void textureFirst() throws Exception {
        RenderFrame first = model(102, 1, 0);
        try (Peer peer = new Peer(102)) {
            peer.texture(MaterialPipelineTests.sprites(first).getLast()).noReady();
            Snapshot pending = peer.texture(MaterialPipelineTests.sprites(first).getFirst());
            pending.noReady();
            require(pending.models() == 0 && pending.textures() == 2 && pending.pending() == 1,
                    "Texture-first batch was not observed before its model");
            peer.model(first).ready(first);
            peer.eof(false, "ended");
        }
    }

    private static void interleaved() throws Exception {
        RenderFrame first = model(103, 1, 0);
        byte[] model = encoded(first), texture = MaterialPipelineTests.sprites(first).getFirst().encode();
        try (Peer peer = new Peer(103)) {
            peer.send(true, model, 0, 17).noReady();
            Snapshot partial = peer.send(false, texture, 0, 11);
            partial.noReady();
            require(partial.buffered() == 28 && partial.models() == 0 && partial.textures() == 0,
                    "Independent parsers did not retain their own partial records");
            peer.send(true, model, 17, model.length - 17).noReady();
            peer.send(false, texture, 11, texture.length - 11).noReady();
            peer.texture(MaterialPipelineTests.sprites(first).getLast()).ready(first);
            peer.eof(true, "ended");
        }
    }

    private static void aheadCaptures() throws Exception {
        RenderFrame first = model(104, 1, 0), second = model(104, 2, 1);
        try (Peer peer = new Peer(104)) {
            peer.sprites(first).noReady();
            Snapshot ahead = peer.sprites(second);
            ahead.noReady();
            require(ahead.pending() == 2 && ahead.textures() == 4 && ahead.models() == 0,
                    "Multiple texture-first captures were not independently retained");
            peer.model(first).ready(first);
            Snapshot ready = peer.model(second);
            ready.ready(first);
            ready.ready(second);
            require(ready.ready() == 2 && ready.pending() == 0,
                    "Independent model positions did not become complete");
            peer.eof(false, "ended");
        }
    }

    private static void superseded(boolean completeFirst) throws Exception {
        long epoch = completeFirst ? 105 : 106;
        RenderFrame first = model(epoch, 1, 0), second = model(epoch, 2, 0);
        try (Peer peer = new Peer(epoch)) {
            peer.model(first).noReady();
            if (completeFirst) peer.sprites(first).ready(first);
            peer.model(second).noReady();
            if (!completeFirst) {
                Snapshot drained = peer.sprites(first);
                drained.noReady();
                require(drained.models() == 2 && drained.textures() == 2 && drained.pending() == 1,
                        "Superseded in-flight textures were not drained exactly once");
            }
            peer.sprites(second).ready(second);
            peer.eof(true, "ended");
        }
    }

    private static void missing(int kind) throws Exception {
        long epoch = 110 + kind;
        RenderFrame first = model(epoch, 1, 0);
        try (Peer peer = new Peer(epoch, 350)) {
            if (kind == 0) peer.sprites(first).noReady();
            else if (kind == 1) {
                peer.model(first).noReady();
                peer.texture(MaterialPipelineTests.sprites(first).getFirst()).noReady();
            } else peer.send(true, encoded(first), 0, 17).noReady();
            peer.terminal("timed_out");
        }
    }

    private static void duplicate(int kind) throws Exception {
        long epoch = 120 + kind;
        RenderFrame first = model(epoch, 1, 0);
        try (Peer peer = new Peer(epoch)) {
            if (kind != 0) peer.model(first).noReady();
            if (kind == 2) peer.model(first);
            else {
                peer.texture(MaterialPipelineTests.sprites(first).getFirst()).noReady();
                peer.texture(MaterialPipelineTests.sprites(first).getFirst());
            }
            peer.terminal("rejected");
        }
    }

    private static void identityMismatch(int kind) throws Exception {
        long epoch = 130 + kind;
        RenderFrame first = model(epoch, 1, 0);
        var id = new RenderFrame.Identity(kind == 0 ? epoch + 1 : epoch,
                kind == 1 ? 8 : 7, kind == 2 ? 10 : 9, kind == 3 ? 11 : 10, 1);
        RenderFrame foreign = MaterialPipelineTests.model(id, first.key());
        try (Peer peer = new Peer(epoch)) {
            peer.model(first).noReady();
            peer.texture(MaterialPipelineTests.sprites(foreign).getFirst());
            peer.terminal("rejected");
        }
    }

    private static void incompleteBatchAdvance() throws Exception {
        RenderFrame first = model(140, 1, 0), second = model(140, 2, 1);
        try (Peer peer = new Peer(140)) {
            peer.model(first).noReady();
            peer.texture(MaterialPipelineTests.sprites(first).getFirst()).noReady();
            peer.texture(MaterialPipelineTests.sprites(second).getFirst());
            peer.terminal("incomplete");
        }
    }

    private static void eof(boolean models, int kind) throws Exception {
        long epoch = 150 + (models ? 0 : 10) + kind;
        RenderFrame first = model(epoch, 1, 0);
        try (Peer peer = new Peer(epoch)) {
            if (kind == 1 || kind == 3) peer.model(first).noReady();
            if (kind == 2) {
                byte[] bytes = models ? encoded(first) : MaterialPipelineTests.sprites(first).getFirst().encode();
                peer.send(models, bytes, 0, 17).noReady();
            } else if (kind == 3) peer.sprites(first).ready(first);
            else if (kind == 4) {
                byte[] bytes = models ? MaterialPipelineTests.sprites(first).getFirst().encode() : encoded(first);
                peer.send(!models, bytes, 0, 17).noReady();
            }
            peer.eof(models, kind == 1 || kind == 4 ? "incomplete" : kind == 2 ? "truncated" : "ended");
        }
    }

    private static void retired(boolean texture) throws Exception {
        long epoch = texture ? 170 : 171;
        RenderFrame first = model(epoch, 1, 0);
        try (Peer peer = new Peer(epoch)) {
            peer.model(first).noReady();
            peer.sprites(first).ready(first);
            if (texture) {
                TextureWire sprite = MaterialPipelineTests.sprites(first).getFirst();
                peer.texture(TextureWire.retire(first.identity(), sprite.atlas(), sprite.sprite()));
                peer.terminal("revoked");
            } else {
                peer.model(RenderWire.retire(model(epoch, 2, 0).identity(), first.key()).encode()).noReady();
                peer.eof(true, "ended");
            }
        }
    }

    private static final class Gated extends OutputStream {
        private final OutputStream output;
        private final CountDownLatch entered = new CountDownLatch(1);
        private final CountDownLatch released = new CountDownLatch(1);
        private final AtomicBoolean closed = new AtomicBoolean();

        Gated(OutputStream output) { this.output = output; }

        @Override public void write(int value) throws IOException { write(new byte[]{(byte) value}, 0, 1); }

        @Override public void write(byte[] bytes, int at, int count) throws IOException {
            entered.countDown();
            try {
                if (!released.await(5, TimeUnit.SECONDS)) throw new IOException("Model writer gate timed out");
            } catch (InterruptedException error) {
                Thread.currentThread().interrupt();
                throw new IOException("Model writer gate interrupted", error);
            }
            if (closed.get()) throw new IOException("Model writer gate closed");
            output.write(bytes, at, count);
        }

        @Override public void flush() throws IOException { output.flush(); }
        @Override public void close() throws IOException {
            released.countDown();
            if (closed.compareAndSet(false, true)) output.close();
        }
    }

    private static void publisher() throws Exception {
        RenderFrame first = model(180, 1, 0);
        try (Peer peer = new Peer(180)) {
            Gated gated = new Gated(peer.models);
            var source = new RenderCaptureSession(180, 7, 9, DIMENSION, MaterialPipelineTests::model);
            MaterialPublisher publisher = new MaterialPublisher(source, MaterialPipelineTests::sprites,
                    180, 7, 9, gated, gated.released::countDown, peer.textures, () -> {});
            try {
                require(publisher.publish(10, 1, first.key()), "Actual material publisher refused the fixture");
                require(gated.entered.await(5, TimeUnit.SECONDS), "Actual model writer did not reach its gate");
                Snapshot ahead = peer.await(value -> value.textures() == 2, "texture writer arrival");
                ahead.noReady();
                require(ahead.models() == 0 && ahead.modelBytes() == 0,
                        "Publisher test did not observe texture-first arrival at the native process");
                gated.released.countDown();
                peer.await(value -> value.ready() == 1, "model writer arrival").ready(first);
            } finally {
                publisher.close();
                retired(publisher);
            }
            peer.terminal("ended");
        }
    }

    private static void retired(MaterialPublisher publisher) throws InterruptedException {
        long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(5);
        while ((!publisher.modelTransport().isTerminated() || !publisher.textureTransport().isTerminated())
                && System.nanoTime() < deadline) Thread.sleep(2);
        require(publisher.modelTransport().isTerminated() && publisher.textureTransport().isTerminated()
                        && publisher.modelTransport().cleanupFailure() == null
                        && publisher.textureTransport().cleanupFailure() == null,
                "Actual publisher retained workers or failed to release its independent endpoints");
    }

    private static final class StalledAfterReady extends OutputStream {
        private final OutputStream output;
        private final AtomicBoolean armed = new AtomicBoolean();
        private final AtomicBoolean cancelled = new AtomicBoolean();
        private final AtomicBoolean closed = new AtomicBoolean();
        private final CountDownLatch entered = new CountDownLatch(1);
        private final CountDownLatch released = new CountDownLatch(1);
        private final AtomicInteger cancellations = new AtomicInteger();
        private final AtomicLong forwarded = new AtomicLong();

        StalledAfterReady(OutputStream output) { this.output = output; }

        void cancel() {
            cancellations.incrementAndGet();
            cancelled.set(true);
            released.countDown();
        }

        @Override public void write(int value) throws IOException { write(new byte[]{(byte) value}, 0, 1); }

        @Override public void write(byte[] bytes, int at, int count) throws IOException {
            if (armed.get()) {
                entered.countDown();
                try {
                    if (!released.await(5, TimeUnit.SECONDS)) {
                        throw new IOException("Material backpressure gate timed out");
                    }
                } catch (InterruptedException error) {
                    Thread.currentThread().interrupt();
                    throw new IOException("Material backpressure gate interrupted", error);
                }
            }
            if (cancelled.get() || closed.get()) throw new IOException("Material backpressure endpoint cancelled");
            output.write(bytes, at, count);
            forwarded.addAndGet(count);
        }

        @Override public void flush() throws IOException { output.flush(); }

        @Override public void close() throws IOException {
            cancelled.set(true);
            released.countDown();
            if (closed.compareAndSet(false, true)) output.close();
        }
    }

    private static void publisherBackpressure() throws Exception {
        RenderFrame first = model(PRESSURE_EPOCH, 1, 0);
        try (Peer peer = new Peer(PRESSURE_EPOCH)) {
            StalledAfterReady models = new StalledAfterReady(peer.models);
            StalledAfterReady textures = new StalledAfterReady(peer.textures);
            var source = new RenderCaptureSession(PRESSURE_EPOCH, 7, 9, DIMENSION, MaterialPipelineTests::model);
            MaterialPublisher publisher = new MaterialPublisher(source, MaterialPipelineTests::sprites,
                    PRESSURE_EPOCH, 7, 9, models, models::cancel, textures, textures::cancel);
            try {
                MaterialReloadRegistry.track(publisher);
                require(publisher.publish(10, 1, first.key()), "Backpressure fixture was refused");
                peer.await(value -> value.ready() == 1, "material before backpressure").ready(first);
                models.armed.set(true);
                textures.armed.set(true);
                require(publisher.publish(11, 2, first.key()), "Blocked capture was refused before saturation");
                require(models.entered.await(5, TimeUnit.SECONDS)
                                && textures.entered.await(5, TimeUnit.SECONDS),
                        "Both independent material writers did not enter their gates");
                for (int queued = 0; queued < TexturePipe.QUEUE_CAPACITY; ++queued) {
                    long capture = 3L + queued;
                    require(publisher.publish(9 + capture, capture, first.key()),
                            "Material queue saturated before its declared capacity");
                }
                long overflow = 3L + TexturePipe.QUEUE_CAPACITY;
                require(!publisher.publish(9 + overflow, overflow, first.key()) && !publisher.isOpen(),
                        "Material publisher accepted a batch beyond its bounded queue");
                require(publisher.textureTransport().stopReason() == TexturePipe.StopReason.SATURATED
                                && publisher.modelTransport().stopReason() == RenderPipe.StopReason.CLOSED,
                        "Texture backpressure did not revoke both output channels");
                retired(publisher);
                long expectedTextures = MaterialPipelineTests.sprites(first).stream()
                        .mapToLong(sprite -> sprite.encode().length).sum();
                require(models.cancellations.get() == 1 && textures.cancellations.get() == 1
                                && models.closed.get() && textures.closed.get()
                                && models.forwarded.get() == encoded(first).length
                                && textures.forwarded.get() == expectedTextures
                                && publisher.modelTransport().ioFailure() == null
                                && publisher.textureTransport().ioFailure() == null,
                        "Backpressure forwarded queued payloads, repeated cancellation or lost endpoint ownership");
                peer.terminal("ended");
                require(peer.latest.models() == 1 && peer.latest.textures() == 2
                                && peer.latest.modelBytes() == encoded(first).length
                                && peer.latest.textureBytes() == expectedTextures,
                        "Native peer received a capture after both writers were stalled");
            } finally {
                publisher.close();
                retired(publisher);
            }
        }
    }

    private static final class RecoveredPeers implements AutoCloseable {
        private final List<Peer> peers = new ArrayList<>();
        private final List<MaterialPublisher> publishers = new ArrayList<>();

        void add(long epoch) throws Exception {
            Peer peer = new Peer(epoch);
            peers.add(peer);
            var source = new RenderCaptureSession(epoch, 7, 9, DIMENSION, MaterialPipelineTests::model);
            MaterialPublisher publisher = new MaterialPublisher(source, MaterialPipelineTests::sprites,
                    epoch, 7, 9, peer.models, () -> {}, peer.textures, () -> {});
            publishers.add(publisher);
            MaterialReloadRegistry.track(publisher);
            RenderFrame recovered = model(epoch, 1, 0);
            require(publisher.publish(10, 1, recovered.key()), "Fresh-epoch material publisher did not recover");
            Snapshot ready = peer.await(value -> value.ready() == 1, "fresh-epoch native material");
            ready.ready(recovered);
            require(epoch > PRESSURE_EPOCH && ready.models() == 1 && ready.textures() == 2,
                    "Recovered native session retained previous capture counters or identity");
        }

        @Override public void close() throws IOException {
            IOException failure = null;
            boolean interrupted = false;
            for (MaterialPublisher publisher : publishers) publisher.close();
            for (MaterialPublisher publisher : publishers) {
                try { retired(publisher); }
                catch (InterruptedException error) {
                    interrupted = true;
                    failure = Peer.retain(failure, new IOException("Recovered publisher cleanup interrupted", error));
                } catch (RuntimeException | AssertionError error) {
                    failure = Peer.retain(failure, new IOException("Recovered publisher cleanup failed", error));
                }
            }
            for (Peer peer : peers) {
                try { peer.terminal("ended"); }
                catch (InterruptedException error) {
                    interrupted = true;
                    failure = Peer.retain(failure, new IOException("Recovered peer cleanup interrupted", error));
                } catch (Exception | AssertionError error) {
                    failure = Peer.retain(failure, new IOException("Recovered peer did not clear its material", error));
                }
                try { peer.close(); }
                catch (IOException error) { failure = Peer.retain(failure, error); }
            }
            if (interrupted) Thread.currentThread().interrupt();
            if (failure != null) throw failure;
        }
    }

    private static void publisherRecovery() throws Exception {
        try (RecoveredPeers recovered = new RecoveredPeers()) {
            // Keeping every new slot live proves the saturated publisher released its registry entry.
            for (int index = 0; index < MaterialReloadRegistry.MAX_ACTIVE; ++index) {
                recovered.add(PRESSURE_EPOCH + 1 + index);
            }
        }
    }

    private static void oldEpochAfterBackpressure(boolean models) throws Exception {
        long epoch = PRESSURE_EPOCH + MaterialReloadRegistry.MAX_ACTIVE + 1;
        try (Peer peer = new Peer(epoch)) {
            RenderFrame old = model(PRESSURE_EPOCH, 1, 0);
            if (models) peer.model(old);
            else peer.texture(MaterialPipelineTests.sprites(old).getFirst());
            peer.terminal("rejected");
            require(peer.latest.models() == 0 && peer.latest.textures() == 0,
                    "Old-epoch data was accepted by a replacement native process");
        }
    }

    @FunctionalInterface private interface Scenario { void run() throws Exception; }

    private static void run(String name, Scenario scenario) throws Exception {
        try { scenario.run(); }
        catch (Exception | Error error) { throw new AssertionError("Material process scenario failed: " + name, error); }
        ++scenarios;
    }

    public static void main(String[] args) throws Exception {
        require(args.length == 1, "Expected native material coordinator peer path");
        executable = args[0];
        run("model-first", MaterialCoordinatorProcessTests::modelFirst);
        run("texture-first and reversed sprites", MaterialCoordinatorProcessTests::textureFirst);
        run("partial interleaving", MaterialCoordinatorProcessTests::interleaved);
        run("multiple texture-first captures", MaterialCoordinatorProcessTests::aheadCaptures);
        run("superseded pending capture", () -> superseded(false));
        run("superseded ready capture", () -> superseded(true));
        for (int kind = 0; kind < 3; ++kind) {
            final int test = kind;
            run("missing input " + kind, () -> missing(test));
            run("duplicate record " + kind, () -> duplicate(test));
        }
        for (int kind = 0; kind < 4; ++kind) {
            final int test = kind;
            run("identity mismatch " + kind, () -> identityMismatch(test));
        }
        run("incomplete batch advance", MaterialCoordinatorProcessTests::incompleteBatchAdvance);
        for (boolean models : new boolean[]{true, false}) {
            for (int kind = 0; kind < 5; ++kind) {
                final int test = kind;
                run((models ? "model" : "texture") + " EOF " + kind, () -> eof(models, test));
            }
        }
        run("texture revocation", () -> retired(true));
        run("model retirement", () -> retired(false));
        run("actual publisher texture-first arrival", MaterialCoordinatorProcessTests::publisher);
        run("actual publisher backpressure and native revocation", MaterialCoordinatorProcessTests::publisherBackpressure);
        run("fresh processes and publishers recover registry capacity", MaterialCoordinatorProcessTests::publisherRecovery);
        run("old model epoch after backpressure", () -> oldEpochAfterBackpressure(true));
        run("old texture epoch after backpressure", () -> oldEpochAfterBackpressure(false));
        System.out.println("material-coordinator-process: passed " + checks + " assertions in "
                + scenarios + " scenarios");
    }
}
