package io.ranzevor.harrancraft.bridge;

import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.Objects;
import java.util.Set;

/** Single-thread-owned command ordering, receipts and subscriptions. */
public final class Session {
    public static final int MAX_WATCHED = 128;
    public static final int MAX_RECEIPTS = 512;
    public static final int COMMANDS_PER_TICK = 32;
    public static final long TIMEOUT_NANOS = 30_000_000_000L;
    private final long epoch;
    private final BlockWorld world;
    private final Link link;
    private final Set<Frame.Key> watched = new HashSet<>();
    private final LinkedHashMap<Long, Receipt> receipts = new LinkedHashMap<>();
    private final Thread owner = Thread.currentThread();
    private long lastActivity;
    private long highWater;
    private long eventId;
    private long activeCause;
    private boolean welcomed;
    private boolean closed;
    private record Receipt(Frame request, Frame response) {}

    public Session(long epoch, BlockWorld world, Link link, long now) {
        if (epoch <= 0) throw new IllegalArgumentException("A positive session epoch is required");
        this.epoch = epoch;
        this.world = Objects.requireNonNull(world);
        this.link = Objects.requireNonNull(link);
        lastActivity = now;
    }

    public long epoch() { return epoch; }
    public boolean isOpen() { return !closed && link.isOpen(); }
    public boolean isWelcomed() { return welcomed && isOpen(); }
    public boolean watches(Frame.Key key) { requireOwner(); return isWelcomed() && watched.contains(key); }
    public int watchedCount() { requireOwner(); return watched.size(); }

    public void tick(long now, boolean allowed) {
        requireOwner();
        try {
            advance(now, allowed);
        } catch (RuntimeException error) {
            close();
            throw error;
        } finally {
            if (!isOpen()) close();
        }
    }

    private void advance(long now, boolean allowed) {
        if (!isOpen() || !allowed || now - lastActivity > TIMEOUT_NANOS) {
            close();
            return;
        }
        for (int count = 0; count < COMMANDS_PER_TICK && isOpen(); ++count) {
            Frame request = link.poll();
            if (request == null) break;
            if (request.origin() != Frame.NATIVE || request.status() != Frame.OK || request.cause() != 0) {
                close();
                return;
            }
            if (!welcomed) {
                if (request.kind() != Frame.HELLO || request.epoch() != 0 || request.id() != 0
                        || !request.hasEmptyBody()) {
                    close();
                    return;
                }
                if (!link.send(Frame.control(Frame.WELCOME, epoch, 0, Frame.GUEST))) {
                    close();
                    return;
                }
                welcomed = true;
                lastActivity = now;
                continue;
            }
            if (request.epoch() != epoch) {
                respond(request, Frame.STALE_EPOCH, "");
                continue;
            }
            if (request.id() == 0 || (request.kind() != Frame.GET && request.kind() != Frame.SET
                    && request.kind() != Frame.PING && request.kind() != Frame.CLOSE)) {
                close();
                return;
            }
            lastActivity = now;
            Receipt prior = receipts.get(request.id());
            if (prior != null) {
                if (prior.request().equals(request)) {
                    if (!link.send(prior.response())) close();
                } else {
                    respond(request, Frame.CONFLICT, "");
                }
                continue;
            }
            if (request.id() <= highWater) {
                respond(request, Frame.RETIRED_ID, "");
                continue;
            }
            highWater = request.id();
            if (request.kind() == Frame.CLOSE) {
                close();
                return;
            }
            // Reserve a bounded event burst; overflow invalidates the epoch and requires a new snapshot.
            if (link.outputCapacity() < MAX_WATCHED + 1) {
                close();
                return;
            }
            Frame response;
            if (request.kind() == Frame.PING) {
                if (!request.hasEmptyBody()) {
                    close();
                    return;
                }
                response = Frame.control(Frame.PONG, epoch, request.id(), Frame.GUEST);
            } else {
                BlockWorld.Outcome outcome = execute(request);
                if (!isOpen()) {
                    close();
                    return;
                }
                response = result(request, outcome.status(), outcome.state());
            }
            if (!link.send(response)) {
                close();
                return;
            }
            receipts.put(request.id(), new Receipt(request, response));
            if (receipts.size() > MAX_RECEIPTS) receipts.remove(receipts.keySet().iterator().next());
        }
    }

    private BlockWorld.Outcome execute(Frame request) {
        if (request.key().dimension().isEmpty() || (request.kind() == Frame.GET
                && (!request.expected().isEmpty() || !request.state().isEmpty()))
                || (request.kind() == Frame.SET && (request.expected().isEmpty() || request.state().isEmpty()))) {
            return new BlockWorld.Outcome(Frame.INVALID, "");
        }
        if (!watched.contains(request.key()) && watched.size() == MAX_WATCHED) {
            return new BlockWorld.Outcome(Frame.CAPACITY, "");
        }
        BlockWorld.Outcome current = world.read(request.key());
        if (current.status() != Frame.OK) return current;
        watched.add(request.key());
        if (request.kind() == Frame.GET) return current;
        activeCause = request.id();
        try {
            return world.compareAndSet(request.key(), request.expected(), request.state());
        } finally {
            activeCause = 0;
        }
    }

    public void changed(Frame.Key key, String previous, String current) {
        requireOwner();
        if (!isWelcomed() || !watched.contains(key) || previous.equals(current)) return;
        if (eventId == Long.MAX_VALUE) {
            close();
            return;
        }
        try {
            Frame event = new Frame(Frame.EVENT, epoch, ++eventId,
                    activeCause == 0 ? Frame.GUEST : Frame.NATIVE, Frame.OK, key, previous, current, activeCause);
            if (!link.send(event)) close();
        } catch (IllegalArgumentException e) {
            close();
        }
    }

    private Frame result(Frame request, int status, String state) {
        return new Frame(Frame.RESULT, epoch, request.id(), Frame.GUEST, status,
                request.key(), request.expected(), state, 0);
    }

    private void respond(Frame request, int status, String state) {
        if (!link.send(result(request, status, state))) close();
    }

    public void close() {
        requireOwner();
        if (closed) return;
        closed = true;
        welcomed = false;
        watched.clear();
        receipts.clear();
        activeCause = 0;
        link.close();
    }

    private void requireOwner() {
        if (Thread.currentThread() != owner) throw new IllegalStateException("Wrong bridge owner thread");
    }
}
