# Source sprite pixel transport

[简体中文](zh-CN/texture-wire.md) · [Render records](render-wire.md) · [Baked-model observation](render-model.md)

## Actual Minecraft source image

The client-only [MinecraftSpriteTextures](../guest/src/main/java/io/ranzevor/harrancraft/MinecraftSpriteTextures.java) revisits the same loaded block and seeded BakedModel as an immutable [RenderFrame](../guest/src/main/java/io/ranzevor/harrancraft/bridge/RenderFrame.java). It checks quad metadata, sprite/atlas IDs, vertices and count before sampling. The client-only [SpriteContentsAccessor](../guest/src/main/java/io/ranzevor/harrancraft/mixin/SpriteContentsAccessor.java) reads the NativeImage already held by Minecraft's SpriteContents and copies row-major source ARGB pixels on the client thread. Atlas origin X/Y and normalized U/V are taken from the actual Sprite. No GPU readback or asset installation occurs.

Sampling refuses changed world/camera, block entities, fluids, different model quads, more than 16 sprite IDs, image dimensions over 64 × 64 and source sheets whose image dimensions differ from the logical sprite frame. There is no silent truncation or fallback color. Full animated sheets, animated display frames, shader effects, mip selection, biome tint multiplication, lighting and the final GPU atlas are **not** captured. Loaded source pixels do not establish the final rendered material.

## TextureWire version 1

[Java](../guest/src/main/java/io/ranzevor/harrancraft/bridge/TextureWire.java) and [C++20](../include/harrancraft/texture_wire.hpp) use the same 32-bit **0xAARRGGBB** pixel words in little-endian byte order. This independent protocol does not modify the existing 512-byte block, 6,464-byte collision or 205,184-byte baked-model records.

Each record consists of 320 header bytes and exactly four bytes per pixel, in row-major order; **16,704 bytes maximum**.

| Offset | Bytes | Field |
| --- | --- | --- |
| 0 | 4 | Magic 0x31544348 |
| 4 | 2 | Version 1 |
| 6 | 2 | SNAPSHOT 1 / RETIRE 2 |
| 8 | 4 | Total record length |
| 12 | 4 | Zero reserved |
| 16, 24, 32, 40, 48 | 8 each | Positive epoch, world, camera, logical frame, capture |
| 56, 60 | 4 each | Sprite atlas origin X, Y |
| 64, 66 | 2 each | Width, height (SNAPSHOT: 1–64) |
| 68 | 4 | Zero reserved |
| 72, 76, 80, 84 | 4 each | Atlas minU/maxU/minV/maxV (binary32) |
| 88, 90 | 2 each | Atlas and sprite identifier lengths |
| 92 | 4 | Zero reserved |
| 96 | 96 | Atlas ASCII, zero-padded |
| 192 | 128 | Sprite ASCII, zero-padded |
| 320 onward | 4 per pixel | Packed ARGB words |

The epoch/world/camera/frame/capture identifiers are caller-supplied logical identities, not proof of synchronized Dying Light frames. SNAPSHOT requires complete pixels, nonnegative atlas coordinates and finite increasing U/V in the [0,1] interval. RETIRE identifies the atlas/sprite but has zero image dimensions, zero coordinates, zero U/V and no pixels. Both endpoints reject malformed versions, flags, padding, text, dimensions, pixel counts, finite-value violations and truncated data. The Java stream reader rejects oversized declarations before allocating full records.

## Material batch ownership and resource reload

[MaterialPublisher](../guest/src/main/java/io/ranzevor/harrancraft/bridge/MaterialPublisher.java) captures the immutable baked model and all unique source sprites from the **same** client capture before enqueueing either output. It checks the exact atlas/sprite manifest (up to 16 unique IDs), excludes duplicates and refuses missing or foreign images. The existing model \`RenderPipe\` and new \`TexturePipe\` use separate caller-owned outputs; the texture transport accepts up to **two complete batches** and writes each batch on a daemon worker without blocking the Minecraft client thread. If either output fails, both are invalidated. Cross-stream arrival order is not guaranteed; the native consumer requires an authoritative model manifest before materials become usable. A future host coordinator must handle this ordering rather than assuming it from these asynchronous queues.

[MaterialCache](../include/harrancraft/material_cache.hpp) keeps CPU-side source pixels only for one current model capture and exposes them **only after every required sprite arrives**. [MaterialReceiver](../include/harrancraft/material_receiver.hpp) incrementally parses arbitrary texture-stream fragments with one bounded 16,704-byte record buffer. It rejects unknown sprites, repeated or stale images, mismatched epoch/world/camera/frame/capture identities, malformed payloads and missing fragments, retiring all cached data on incomplete EOF, channel shutdown, explicit withdrawal or failed validation. Replacing the model discards old pixels; opening a new session requires a newer epoch. This does not create or delete GPU texture handles.

On the Minecraft side, [HarranCraftClient](../guest/src/main/java/io/ranzevor/harrancraft/HarranCraftClient.java) registers a Fabric \`CLIENT_RESOURCES\` reload listener. [MaterialReloadRegistry](../guest/src/main/java/io/ranzevor/harrancraft/bridge/MaterialReloadRegistry.java) holds a bounded set of up to four active publishers and cancels their owned model/texture output endpoints when resource packs reload. The client owner must still verify worker termination and cleanup errors; closing an output does not prove the host has safely destroyed any registered graphics resources. Game-side reload behavior and Mixin loading are not executed by current CI.

## Verification and integration boundary

The registered bridge suite executes Java texture-codec tests, native CTest, a complete 336-byte cross-language golden byte comparison, and actual Java-to-C++ process-pipe roundtrips and corrupt/truncated-record refusal. Fabric compiles the real client-only NativeImage accessor and a client GameTest that checks pixels from a placed Minecraft block; CI does **not** execute Minecraft or GameTests.

The optional client material publisher, bounded native stream receiver and CPU-side material cache are now implemented with isolated tests, but not connected through a production Dying Light adapter. The registered native material peer and Java process tests assemble multiple sprites against an exact baked-model manifest, including negative incomplete/duplicate/corrupt-stream cases. Later integration must verify real client reloads, cross-channel ordering, GPU upload/release and animated textures, biome tint, dynamic lighting and camera/depth correspondence. No game assets or textures are committed to the repository.
