package io.ranzevor.harrancraft.bridge;

import java.util.ArrayList;
import java.util.List;

/** Immutable client-baked model quads tagged with caller-owned frame identities. */
public record RenderFrame(Identity identity, Frame.Key key, String state,
                          CameraPose camera, List<Quad> quads) {
    public static final int MAX_QUADS = 512;
    public static final int PACKED_VERTEX_WORDS = 8;
    public static final int VERTICES_PER_QUAD = 4;

    public record Identity(long epoch, long world, long camera, long frame, long capture) {
        public Identity {
            if (epoch <= 0 || world <= 0 || camera <= 0 || frame <= 0 || capture <= 0) {
                throw new IllegalArgumentException("Invalid render frame identity");
            }
        }
    }

    public record CameraPose(double x, double y, double z, float yaw, float pitch) {
        public CameraPose {
            if (!Double.isFinite(x) || !Double.isFinite(y) || !Double.isFinite(z)
                    || !Float.isFinite(yaw) || !Float.isFinite(pitch)) {
                throw new IllegalArgumentException("Non-finite Minecraft camera pose");
            }
        }
    }

    public record Vertex(float x, float y, float z, float u, float v,
                         int packedColor, int packedLight, int packedNormal) {
        public Vertex {
            if (!Float.isFinite(x) || !Float.isFinite(y) || !Float.isFinite(z)
                    || !Float.isFinite(u) || !Float.isFinite(v)) {
                throw new IllegalArgumentException("Invalid baked vertex");
            }
        }
    }

    public record Quad(int cullFace, int face, int tintIndex, boolean shade,
                       int lightEmission, String atlas, String sprite, List<Vertex> vertices) {
        public Quad {
            if (cullFace < -1 || cullFace > 5 || face < 0 || face > 5
                    || tintIndex < -1 || tintIndex > 255
                    || lightEmission < 0 || lightEmission > 15
                    || atlas == null || sprite == null || vertices == null
                    || vertices.size() != VERTICES_PER_QUAD) {
                throw new IllegalArgumentException("Invalid baked quad");
            }
            validateText(atlas, 128);
            validateText(sprite, 128);
            vertices = List.copyOf(vertices);
        }
    }

    public RenderFrame {
        if (identity == null || key == null || key.dimension() == null
                || state == null || camera == null || quads == null
                || quads.size() > MAX_QUADS) {
            throw new IllegalArgumentException("Invalid model capture");
        }
        validateText(key.dimension(), 64);
        validateText(state, 192);
        quads = List.copyOf(quads);
    }

    private static void validateText(String text, int limit) {
        if (text.isEmpty() || text.length() > limit) {
            throw new IllegalArgumentException("Invalid render identifier length");
        }
        for (int index = 0; index < text.length(); ++index) {
            char c = text.charAt(index);
            if (c < 33 || c > 126) {
                throw new IllegalArgumentException("Invalid render identifier text");
            }
        }
    }

    public static List<Vertex> unpackVertices(int[] raw) {
        if (raw == null || raw.length != VERTICES_PER_QUAD * PACKED_VERTEX_WORDS) {
            throw new IllegalArgumentException("Unknown packed Minecraft vertex layout");
        }
        List<Vertex> result = new ArrayList<>(VERTICES_PER_QUAD);
        for (int index = 0; index < VERTICES_PER_QUAD; ++index) {
            int at = index * PACKED_VERTEX_WORDS;
            result.add(new Vertex(Float.intBitsToFloat(raw[at]),
                    Float.intBitsToFloat(raw[at + 1]),
                    Float.intBitsToFloat(raw[at + 2]),
                    Float.intBitsToFloat(raw[at + 4]),
                    Float.intBitsToFloat(raw[at + 5]),
                    raw[at + 3], raw[at + 6], raw[at + 7]));
        }
        return List.copyOf(result);
    }
}
