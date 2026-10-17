package io.ranzevor.harrancraft.bridge;

import java.util.ArrayList;
import java.util.Collections;
import java.util.Set;
import java.util.IdentityHashMap;

/** Revokes owned output endpoints when client resources are reloaded. */
public final class MaterialReloadRegistry {
    public static final int MAX_ACTIVE = 4;
    private static final Set<MaterialPublisher> ACTIVE =
            Collections.newSetFromMap(new IdentityHashMap<>());

    private MaterialReloadRegistry() {}

    public static synchronized void track(MaterialPublisher publisher) {
        if (publisher == null || !publisher.isOpen()) {
            throw new IllegalArgumentException("Inactive material publisher");
        }
        if (!ACTIVE.contains(publisher) && ACTIVE.size() >= MAX_ACTIVE) {
            throw new IllegalStateException("Too many owned material output sessions");
        }
        ACTIVE.add(publisher);
    }

    public static synchronized void untrack(MaterialPublisher publisher) {
        ACTIVE.remove(publisher);
    }

    public static synchronized void onReload() {
        for (MaterialPublisher publisher : new ArrayList<>(ACTIVE)) {
            publisher.invalidateTransports();
        }
        ACTIVE.clear();
    }
}
