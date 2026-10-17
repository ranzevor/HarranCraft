# Render model record protocol

[简体中文](zh-CN/render-wire.md) · [Baked-model observation](render-model.md) · [Architecture](architecture.md)

## Source and ownership

[RenderWire](../guest/src/main/java/io/ranzevor/harrancraft/bridge/RenderWire.java) serializes an immutable Minecraft client [RenderFrame](../guest/src/main/java/io/ranzevor/harrancraft/bridge/RenderFrame.java). Its C++20 counterpart is [render_wire.hpp](../include/harrancraft/render_wire.hpp). The record includes the exact caller-assigned epoch, world, camera, logical frame and capture identifiers; dimension/block position, canonical block state, camera position/yaw/pitch, model quads, atlas and sprite names, tint/shade/emission metadata and packed vertex colors, light and normals.

This independent rendering record format does not alter the 512-byte block bridge v1 or the collision-geometry record v1. Atlas/sprite identifiers and quad UVs are **not** texture pixel data, resolved material parameters, dynamic lighting or evidence that the host can render the model. The current client sampler supports static baked models only.

## Version 1 binary layout

All integers and IEEE-754 floating-point values are little-endian. Each record contains a fixed 384-byte header and zero to 512 fixed 400-byte quads, for a maximum of **205,184 bytes**. Unused bytes must be zero; there is no native struct packing.

| Offset | Bytes | Field |
| --- | --- | --- |
| 0 | 4 | Magic `0x31524348` |
| 4 | 2 | Version `1` |
| 6 | 2 | SNAPSHOT `1` or RETIRE `2` |
| 8 | 4 | Total byte length |
| 12 | 4 | Zero reserved |
| 16, 24, 32, 40, 48 | 8 each | Positive epoch, world, camera, logical frame, capture |
| 56, 60, 64 | 4 each | Signed block X, Y, Z |
| 68 | 2 | Quad count, at most 512 |
| 70, 72 | 2 each | Dimension and canonical state ASCII lengths |
| 74 | 2 | Zero reserved |
| 76, 84, 92 | 8 each | Minecraft camera position X/Y/Z (binary64) |
| 100, 104 | 4 each | Camera yaw/pitch (binary32) |
| 108 | 4 | Zero reserved |
| 112 | 64 | Dimension ASCII, zero-padded |
| 176 | 192 | Canonical state ASCII, zero-padded |
| 368 | 16 | Zero reserved |

Each quad is exactly 400 bytes:

| Quad-relative offset | Bytes | Field |
| --- | --- | --- |
| 0 | 2 | Signed cull face (`-1` means unculled) |
| 2 | 2 | Model face direction (`0`–`5`) |
| 4 | 2 | Signed tint index (`-1` means none) |
| 6, 7 | 1 each | Shade flag (`0`/`1`), light emission (`0`–`15`) |
| 8, 10 | 2 each | Atlas and sprite identifier lengths |
| 12 | 4 | Zero reserved |
| 16 | 128 | Atlas identifier, printable ASCII, zero-padded |
| 144 | 128 | Sprite identifier, printable ASCII, zero-padded |
| 272 | 128 | Four vertices, 32 bytes each |

Each vertex has five binary32 floats (local X/Y/Z and atlas U/V) followed by three raw 32-bit words (packed color, light and normal). Unknown render vertex layouts must be rejected by the client sampler rather than reinterpreted.

SNAPSHOT requires a nonempty canonical block state; a valid invisible model has zero quads. RETIRE carries the same five-part identity and position but an empty state, zero camera and zero quads. Record construction and decoding reject unknown versions, excessive lengths/counts, nonzero padding, malformed ASCII, invalid face/tint/shade metadata, non-finite positions/UV/camera values and truncated data. A stream reader validates total length before allocating the full record.

## Async client publisher and bounded native receiver

The optional client-side [RenderPublisher](../guest/src/main/java/io/ranzevor/harrancraft/bridge/RenderPublisher.java) connects a valid [RenderCaptureSession](../guest/src/main/java/io/ranzevor/harrancraft/bridge/RenderCaptureSession.java) to a caller-owned private output through [RenderPipe](../guest/src/main/java/io/ranzevor/harrancraft/bridge/RenderPipe.java). The Minecraft client thread captures and enqueues immutable records, while daemon workers handle blocking output, cancellation and endpoint closure. At most eight pending records are permitted; the pipe rejects a changed epoch/world/camera, nonincreasing logical frame or a missing/duplicate capture number. Saturation, failed output, invalid capture or source revocation stops publication. Callers must retain the transport handle and separately verify worker termination and cleanup failures. The output stream remains caller-owned if source creation is refused.

The [RenderReceiver](../include/harrancraft/render_receiver.hpp) consumes arbitrary byte fragments on one owner thread, buffers at most one 205,184-byte record and checks total size before accepting the body. An explicit `begin(epoch, world, camera, dimension)` fixes the expected capture identities. The native receiver accepts strictly increasing logical frames and contiguous capture numbers, stages at most sixteen positions, and retires models on empty snapshots, RETIRE, clean EOF or explicit stop. Unknown formats, truncated EOF, source identity changes, duplicate/gapped captures, stale frame numbers and staging capacity overflow invalidate the entire candidate scene. Restart requires a strictly newer epoch.

The production path currently consists of an optional client-owned publication API plus native decoding and staged model state, **not** a launched/injected Dying Light render endpoint. No graphics objects, host GPU resources, texture atlas pixels or frame-synchronous native draw calls are owned by these classes. The stream can end after a write has started, so the native side still rejects stale session identities.

## Process verification and remaining integration

The newly registered Java/C++ golden fixture compares **all 784 bytes** of an identical sample render record. The independent C++ test validates maximum records, retirement, malformed headers, truncated data and non-finite floating-point values. [RenderWireProcessTests](../guest/src/test/java/io/ranzevor/harrancraft/bridge/RenderWireProcessTests.java) passes snapshots and retirement records through a real Java-to-C++ process pipe to [hc-render-peer](../native/tests/render_peer.cpp), comparing the returned bytes exactly. Client GameTest source additionally serializes a real diamond-block capture, but CI compiles that test without starting Minecraft.

An optional client publisher and reusable native receiver are implemented and tested in isolation. They do not yet form a live Minecraft-to-Dying Light pipeline: there is no host adapter, atlas image transport, draw-call reconstruction, native graphics submission or proof of real Minecraft/Dying Light frame timing. Such work must retain ownership, explicitly refuse stale identities, resolve texture pixels and establish host depth/projection correspondence separately.
