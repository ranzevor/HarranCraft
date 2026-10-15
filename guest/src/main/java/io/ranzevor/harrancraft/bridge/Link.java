package io.ranzevor.harrancraft.bridge;

public interface Link {
    Frame poll();
    boolean send(Frame frame);
    int outputCapacity();
    boolean isOpen();
    void close();
}
