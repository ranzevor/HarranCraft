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

[MaterialPublisher](../guest/src/main/java/io/ranzevor/harrancraft/bridge/MaterialPublisher.java) captures the immutable baked model and all unique source sprites from the **same** client capture before enqueueing either output. It checks the exact atlas/sprite manifest, up to 16 unique IDs, and refuses missing, duplicate or foreign images. `RenderPipe` and `TexturePipe` use separate caller-owned outputs, with at most eight queued model records and two complete texture batches. Daemon workers write and flush the records without blocking the Minecraft client thread. Each output preserves FIFO order; the two outputs can reach the native receiver in either order.

A stop notification from either transport immediately invalidates its peer and removes the publisher from [MaterialReloadRegistry](../guest/src/main/java/io/ranzevor/harrancraft/bridge/MaterialReloadRegistry.java). This includes asynchronous write/flush failure, saturation and explicit closure, and does not wait for the client owner to call `publish()` or `isOpen()` again. Notifications operate on owned transports and the registry only; client source access remains on its owner thread. Each endpoint's cancellation runs once, and I/O and cleanup failures remain separately available. The owner must retain the transport handles and verify actual worker termination.

[HarranCraftClient](../guest/src/main/java/io/ranzevor/harrancraft/HarranCraftClient.java) registers a Fabric `CLIENT_RESOURCES` reload listener. The registry holds at most four active publishers and invalidates both owned outputs on resource reload. Game-side reload behavior and Mixin loading are not executed by ordinary CI.

## Cross-channel material coordination

[MaterialCoordinator](../include/harrancraft/material_coordinator.hpp) joins the two independently fragmented streams on one native owner thread. `begin(epoch, world, camera, dimension)` fixes the session; model and texture records must match **epoch, world, camera, logical frame and capture** exactly. Models require contiguous capture numbers and strictly increasing frames. Texture records for a capture can list sprites in any order; successive texture batches require increasing capture/frame identities. Empty models emit no textures, so a texture capture gap is judged against the model manifests. Advancing to a newer texture batch while an earlier known manifest still lacks sprites ends the session as incomplete.

Textures that arrive before their model remain isolated under their capture identity. Only a model's complete required sprite set can make its material available through `find()`. A new model, empty snapshot or model RETIRE immediately withdraws the previous material at the same position. Superseded pending captures release their model and pixel payloads; bounded manifest and seen-ID records remain to validate and drain their in-flight textures. Completing such a capture never restores the old model.

| Bound | Limit |
| --- | --- |
| Pending capture associations | 16 |
| Current model positions, including incomplete replacements | 16 |
| Retained model/texture record identities, including drain records | 256 |
| Charged retained storage | 4 MiB |
| Fixed model and texture parser buffers | 205,184 + 16,704 = 221,888 bytes, separate from retained storage |
| Assembly timeout | 1 second by default; configurable above zero and at most 5 seconds |

Retained-storage accounting charges model headers/quads, complete texture records and bounded manifest/seen-ID storage. STL container and allocation metadata are bounded by the record, sprite and capture limits but are not measured by this charge; **4 MiB is not an RSS limit**. A texture capture may be at most 16 captures ahead of the received model stream. Exceeding any admission limit clears the entire associated session.

Assembly deadlines use `steady_clock` and run from the first fragment or first record associated with an incomplete capture. Additional fragments, counterpart arrival and unrelated traffic do not extend the deadline. The owner must call `poll()` periodically, including when no input arrives; `feed_models()`, `feed_textures()` and `find()` also check deadlines. Ready materials have no independent TTL, but another incomplete capture timing out revokes them along with the rest of the session. A regressed supplied clock value is rejected.

Unknown or duplicate sprites, stale or mismatched identities, malformed records and capacity/timeout failures clear both parser buffers, pending associations and available materials. A matching texture RETIRE revokes the associated session; one arriving before its model is held until the manifest can validate it. EOF from either stream ends the entire session: a partial record in that stream is truncated, other outstanding fragments or associations are incomplete, and otherwise EOF is clean. An I/O failure reported with `fail(channel)` or an explicit `stop()` also clears all owned CPU state. Reopening requires a strictly newer epoch. The coordinator owns no I/O endpoints or game resources; its caller handles endpoint closure and must not resume an ended session.

[MaterialCache](../include/harrancraft/material_cache.hpp) and [MaterialReceiver](../include/harrancraft/material_receiver.hpp) remain available for callers that already provide one authoritative model before its textures. They share the sprite-manifest validation with the coordinator and retain their strict single-model, bounded incremental-reception contract. Independent asynchronous model/texture outputs use the coordinator.

## Verification and integration boundary

`python tools/test_bridge.py` registers the Java texture-codec and material-pipeline suites, native CTests, complete 336-byte cross-language golden-byte comparison and actual Java-to-C++ process exchanges. The `material-coordinator` CTest covers FIFO-preserving cross-stream interleavings, fragmentation, superseded captures, manifest errors, admission limits, absolute deadlines, EOF and new-epoch recovery. `MaterialPipelineTests` checks paired cancellation even when the client owner makes no further calls.

[MaterialCoordinatorProcessTests](../guest/src/test/java/io/ranzevor/harrancraft/bridge/MaterialCoordinatorProcessTests.java) drives [hc-material-coordinator-peer](../native/tests/material_coordinator_peer.cpp) through two real OS pipes: model records on process stdin and textures on a private named pipe on Windows or a private FIFO on POSIX. It compares complete returned model and pixel bytes, forces texture-first arrival with the actual asynchronous publisher, and checks incomplete/duplicate/foreign input, timeouts, channel EOF, withdrawal and process/endpoint cleanup. After a complete material is available, gated writers fill the texture queue and exercise saturation-driven paired closure and native withdrawal. Recovery creates new publishers, processes and endpoints with newer epochs, checks old-epoch refusal and verifies that all registry slots are available again. The existing ordered `MaterialProcessTests` continues to exercise the single-model receiver separately.

The pinned Fabric build compiles the client NativeImage accessor, material publisher, reload listener and GameTest source; ordinary CI **does not execute Minecraft, GameTests or Mixin injection**. The CPU coordination API has no production Dying Light adapter. Remaining integration includes actual client reloads, live cross-runtime transport, GPU upload/release, animation, biome tint, dynamic lighting and camera/depth correspondence. No game assets or textures are committed to the repository.
