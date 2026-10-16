# Collision geometry side-channel

[简体中文](zh-CN/geometry.md) · [Block bridge](bridge.md) · [Architecture](architecture.md)

## Data authority and integration boundary

Minecraft remains authoritative for block state and collision geometry. On its server thread, `GuestConnections.collisionSnapshot(server, epoch, key)` reads a loaded, subscribed position from the real Minecraft world. The returned immutable `MinecraftCollisionShapes.Snapshot` converts to a `GeometryFrame` using `geometryFrame(revision)`. This includes the canonical state and the actual `VoxelShape` collision boxes, not a reconstructed block catalog or render mesh.

`GeometryFrame.encode()` produces bytes understood by the independent C++20 [geometry codec](../include/harrancraft/geometry_wire.hpp). A separate geometry channel is required: the existing [512-byte block protocol](bridge.md#version-1-frame) is unchanged. The native [geometry peer](../native/tests/geometry_peer.cpp) verifies a real Java-to-C++ process stream and byte-for-byte round trip. No launcher, game-side side-channel writer, native Dying Light collision registration or authoritative host acknowledgement is implemented yet.

## Geometry record version 1

Records use little-endian integer and IEEE-754 binary64 fields. A record is exactly `320 + 48 × box_count` bytes, from 320 to 6,464 bytes. Neither native struct packing nor padding between records is permitted.

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 4 | Magic `0x31474348` |
| 4 | 2 | Version `1` |
| 6 | 2 | Kind: SNAPSHOT `1`, RETIRE `2` |
| 8 | 4 | Total record byte count |
| 12 | 4 | Zero flags |
| 16 | 8 | Positive session epoch |
| 24 | 8 | Positive per-session revision |
| 32, 36, 40 | 4 each | Signed block X, Y, Z |
| 44 | 2 | AABB count, 0–128 |
| 46, 48 | 2 each | Dimension and state text lengths |
| 50–63 | 14 | Reserved zero |
| 64 | 64 | Printable ASCII dimension, zero-padded |
| 128 | 192 | Printable ASCII canonical block state, zero-padded |
| 320+ | 48 per box | Six doubles: min X/Y/Z, max X/Y/Z |

A SNAPSHOT requires a nonempty canonical state, may include zero boxes (air), and requires finite bounds with strictly positive extent in each axis. A RETIRE record identifies a previously staged position and has an empty state and zero boxes. Epochs and revisions fit positive signed 64-bit integers. Invalid version, header, size, count, text, padding, bounds, truncation or trailing data is rejected. Input parsing checks the maximum before allocating a record.

## Lifecycle and scope

Revisions must be assigned strictly increasingly by the sending session, and native consumers must reject retired epochs and stale/replayed revisions. A full resnapshot after reconnection is required; old position data must not survive session teardown. These sequencing and lifetime rules are not yet enforced by a production geometry transport; codecs validate record structure but do not register host resources.

Boxes are block-local and use Minecraft coordinate units. Translating them into host coordinates requires a separately validated world transform and original-world collision registration. No visual occlusion or actual Dying Light physical blocking is established by the geometry codec.

## Verification

`python tools/test_bridge.py` compiles the Java and C++ codecs, runs corrupt-header, size, invalid-double, padding and capacity regressions, compares the complete golden record across languages and pipes four records through a real C++ subprocess. Native CTest checks the independent codec. GameTest sources additionally encode and decode a collision snapshot derived from a real Minecraft block, but game execution is not enabled in CI.
