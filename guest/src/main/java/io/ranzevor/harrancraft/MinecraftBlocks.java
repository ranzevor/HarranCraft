package io.ranzevor.harrancraft;

import io.ranzevor.harrancraft.bridge.BlockWorld;
import io.ranzevor.harrancraft.bridge.Frame;
import java.util.Comparator;
import java.util.HashSet;
import java.util.StringJoiner;
import net.minecraft.block.Block;
import net.minecraft.block.BlockState;
import net.minecraft.registry.Registries;
import net.minecraft.registry.RegistryKey;
import net.minecraft.registry.RegistryKeys;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.world.ServerWorld;
import net.minecraft.state.property.Property;
import net.minecraft.util.Identifier;
import net.minecraft.util.math.BlockPos;

/** Resolves only loaded world positions; compare-and-set never discards block-entity data. */
public final class MinecraftBlocks implements BlockWorld {
    private final MinecraftServer server;

    public MinecraftBlocks(MinecraftServer server) { this.server = server; }

    ServerWorld resolve(Frame.Key key) {
        Identifier id = Identifier.tryParse(key.dimension());
        return id == null ? null : server.getWorld(RegistryKey.of(RegistryKeys.WORLD, id));
    }

    @Override public Outcome read(Frame.Key key) {
        GuestConnections.requireThread(server);
        if (!GuestConnections.allowed(server)) return new Outcome(Frame.FORBIDDEN, "");
        ServerWorld world = resolve(key);
        if (world == null) return new Outcome(Frame.UNAVAILABLE, "");
        BlockPos pos = new BlockPos(key.x(), key.y(), key.z());
        if (world.isOutOfHeightLimit(pos) || !world.getWorldBorder().contains(pos)) {
            return new Outcome(Frame.INVALID, "");
        }
        if (!world.getChunkManager().isChunkLoaded(pos.getX() >> 4, pos.getZ() >> 4)) {
            return new Outcome(Frame.UNAVAILABLE, "");
        }
        String state = describe(world.getBlockState(pos));
        return state.length() <= 192 ? new Outcome(Frame.OK, state) : new Outcome(Frame.UNSUPPORTED_STATE, "");
    }

    @Override public Outcome compareAndSet(Frame.Key key, String expected, String state) {
        Outcome before = read(key);
        if (before.status() != Frame.OK) return before;
        if (!before.state().equals(expected)) return new Outcome(Frame.CONFLICT, before.state());
        BlockState next;
        try { next = parse(state); }
        catch (IllegalArgumentException e) { return new Outcome(Frame.INVALID, before.state()); }
        ServerWorld world = resolve(key);
        if (world == null) return new Outcome(Frame.UNAVAILABLE, "");
        BlockPos pos = new BlockPos(key.x(), key.y(), key.z());
        BlockState old = world.getBlockState(pos);
        if (old.hasBlockEntity() || next.hasBlockEntity()) return new Outcome(Frame.UNSUPPORTED_STATE, before.state());
        if (!GuestConnections.allowed(server)) return new Outcome(Frame.FORBIDDEN, before.state());
        if (!next.getBlock().isEnabled(world.getEnabledFeatures())) return new Outcome(Frame.UNSUPPORTED_STATE, before.state());
        if (!old.equals(next)) world.setBlockState(pos, next, Block.NOTIFY_ALL);
        String actual = describe(world.getBlockState(pos));
        return new Outcome(actual.equals(describe(next)) ? Frame.OK : Frame.CONFLICT, actual);
    }

    public static String describe(BlockState state) {
        String id = Registries.BLOCK.getId(state.getBlock()).toString();
        if (state.getProperties().isEmpty()) return id;
        StringJoiner values = new StringJoiner(",", id + "[", "]");
        state.getProperties().stream().sorted(Comparator.comparing(Property::getName))
                .forEach(p -> values.add(p.getName() + "=" + value(state, p)));
        return values.toString();
    }

    private static <T extends Comparable<T>> String value(BlockState state, Property<T> property) {
        return property.name(state.get(property));
    }

    public static BlockState parse(String text) {
        int start = text.indexOf('[');
        String name = start < 0 ? text : text.substring(0, start);
        Identifier id = Identifier.tryParse(name);
        if (id == null || !name.equals(id.toString())) throw new IllegalArgumentException("Invalid block identity");
        Block block = Registries.BLOCK.getOptionalValue(id).orElseThrow(() -> new IllegalArgumentException("Unknown block"));
        BlockState result = block.getDefaultState();
        if (start >= 0) {
            if (!text.endsWith("]")) throw new IllegalArgumentException("Invalid block properties");
            String properties = text.substring(start + 1, text.length() - 1);
            HashSet<String> seen = new HashSet<>();
            for (String pair : properties.split(",", -1)) {
                String[] parts = pair.split("=", -1);
                if (parts.length != 2 || !seen.add(parts[0])) throw new IllegalArgumentException("Invalid property assignment");
                Property<?> property = block.getStateManager().getProperty(parts[0]);
                if (property == null) throw new IllegalArgumentException("Unknown property");
                result = apply(result, property, parts[1]);
            }
        }
        // Full canonical states avoid registry-default changes silently altering command meaning.
        if (!describe(result).equals(text)) throw new IllegalArgumentException("Noncanonical block state");
        return result;
    }

    private static <T extends Comparable<T>> BlockState apply(BlockState state, Property<T> property, String value) {
        T parsed = property.parse(value).orElseThrow(() -> new IllegalArgumentException("Invalid property value"));
        return state.with(property, parsed);
    }
}
