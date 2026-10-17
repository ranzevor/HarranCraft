package io.ranzevor.harrancraft.bridge;

/** Admission for a ready, private integrated server before each world operation. */
public final class GuestAccess {
    private GuestAccess() {}

    public static boolean allowed(boolean singleplayer, boolean dedicated, boolean remote,
            boolean ready, boolean stopping, boolean stopped) {
        return singleplayer && !dedicated && !remote && ready && !stopping && !stopped;
    }
}
