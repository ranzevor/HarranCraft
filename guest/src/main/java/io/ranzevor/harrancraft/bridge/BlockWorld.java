package io.ranzevor.harrancraft.bridge;

public interface BlockWorld {
    record Outcome(int status, String state) {}
    Outcome read(Frame.Key key);
    Outcome compareAndSet(Frame.Key key, String expected, String state);
}
