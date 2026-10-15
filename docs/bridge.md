# Block bridge protocol

[简体中文](zh-CN/bridge.md) · [Architecture](architecture.md) · [Local validation](guest-validation.md)

## Endpoints and authority

`GuestConnections.attach(server, input, output)` attaches caller-owned private pipes on the Minecraft server thread. The caller must supply both streams, control the peer and provide streams whose close operation releases blocked I/O. For process pipes, construct `PipeLink(input, output, cancelIo)` and use `GuestConnections.attach(server, link)` to retain the transport handle. The cancellation callback runs once on a separate worker and must release only this connection's explicitly owned endpoint; the bridge does not discover or terminate a process on its own. A rejected attachment leaves a supplied link with its caller. The bridge owns those streams after attachment. It creates no listening socket, process launcher or Dying Light adapter. `native/tests/bridge_peer.cpp` is a protocol-test process.

`MinecraftBlocks` reads and conditionally changes loaded positions in an integrated single-player server. Loading, stopping, dedicated or remote/LAN server states reject attachment and invalidate an active session. These Minecraft checks do not identify Dying Light modes. Read, write, attach, detach and change observation belong to the server thread; stream I/O, endpoint cancellation and stream closure use daemon workers.

A position is the dimension identifier plus signed X/Y/Z coordinates. Out-of-height or world-border positions are invalid; missing worlds and unloaded chunks are unavailable. The adapter does not force chunk loading. Block states use complete canonical identifiers and alphabetically ordered properties, such as `minecraft:oak_log[axis=x]`. Unknown identifiers, duplicate or invalid properties and noncanonical defaults are rejected. Replacing either an existing or a target block entity is unsupported in this batch, preserving its data; block-entity support remains a future capability.

## Version 1 frame

[wire.hpp](../include/harrancraft/wire.hpp) is the native layout authority; Java [Frame](../guest/src/main/java/io/ranzevor/harrancraft/bridge/Frame.java) is its matching implementation. Frames are exactly 512 bytes, little-endian. There is no native struct packing on the wire.

| Offset | Bytes | Field |
| --- | --- | --- |
| 0 | 4 | Magic `0x31435248` |
| 4 | 2 | Version `1` |
| 6 | 2 | Message kind |
| 8 | 8 | Session epoch |
| 16 | 8 | Request or event identity |
| 24 | 4 | Origin: native `1`, guest `2` |
| 28 | 4 | Status |
| 32, 36, 40 | 4 each | Signed X, Y, Z |
| 44, 46, 48 | 2 each | Dimension, expected-state and current/target-state lengths |
| 50 | 2 | Zero reserved bytes |
| 52 | 8 | Causing command identity, or zero |
| 60 | 4 | Zero reserved bytes |
| 64 | 64 | Dimension text |
| 128 | 192 | Expected/previous state text |
| 320 | 192 | Target/current state text |

Text contains printable ASCII bytes 32–126; unused text capacity must be zero. Epoch, identity and cause are in `0..2^63-1`; the active epoch is positive. Invalid headers, padding, lengths, characters, truncated frames, kinds, origins and statuses reject the frame and terminate the transport. A layout change requires a protocol-version change and peer refusal or migration tests.

Kinds are HELLO `1`, WELCOME `2`, GET `3`, SET `4`, RESULT `5`, EVENT `6`, PING `7`, PONG `8`, CLOSE `9`. Statuses are OK `0`, STALE_EPOCH `1`, INVALID `2`, UNAVAILABLE `3`, CONFLICT `4`, FORBIDDEN `5`, RETIRED_ID `6`, CAPACITY `7`, UNSUPPORTED_STATE `8`.

## Ordering, subscriptions and recovery

The native peer begins with empty HELLO, epoch/identity zero, native origin and OK status. WELCOME supplies the guest's new positive epoch. Subsequent GET, SET, PING and CLOSE requests carry that epoch, a positive request identity, native origin, OK status and zero cause. PING has an empty body and receives PONG with the same identity. CLOSE ends the session without a receipt.

GET contains only a position; a successful read subscribes it. SET additionally contains the exact expected and target states. It compares against the authoritative world before replacement, returning CONFLICT with the observed state on mismatch. Successfully resolved SET positions are subscribed even when replacement is refused. RESULT carries the request identity, status and observed state. No-op SET produces no change event.

`WorldChunkMixin` observes the returned previous state and actual current state after a chunk mutation. Subscribed changes emit EVENT with a separate increasing event identity, previous/current states and causation. A change during SET carries native origin and the command identity; an independent Minecraft change has guest origin and cause zero. An EVENT may arrive before its command's RESULT. The observation is not an acknowledgement of Dying Light collision or rendering.

Each session retains at most 128 subscribed positions and 512 request/response receipts. Repeating an identical retained request resends its receipt without executing it again; reusing that identity with different contents returns CONFLICT. Identities at or below the high-water mark after receipt eviction return RETIRED_ID. Reconnect never reuses old receipts or subscriptions. Both transport queues hold at most 256 frames; a tick consumes at most 32 requests. A command requires space for 129 output frames before execution. Queue exhaustion, failed sends, invalid input, a forbidden mode, disconnect or more than 30 seconds without valid-session activity invalidate the connection. Accepted PING requests keep an idle session alive.

Invalidation first stops mutation and clears receipts/subscriptions/queued work, then starts endpoint cancellation and closes the two streams independently. Queue handoff is ordered against invalidation; a late read cannot repopulate a closed input queue. An already-started write may still deliver bytes, so receivers must retain epoch checks. Adapter exceptions close the session and remain failures; they are not converted into successful results. A write may have changed Minecraft before its EVENT or RESULT fails. Such a disconnect does not roll back the authoritative world: reconnect with a new epoch, GET a fresh snapshot and reconcile before issuing another conditional write. Do not blindly retry an old command or infer a host-side commit.

## Termination and owned resources

`isOpen() == false` means the link refuses traffic, not that I/O has stopped. `PipeLink.isTerminated()` becomes true only after its reader, writer, cancellation and both close workers have terminated. The owner must also check `cleanupFailure()` before reporting successful cleanup. A pending or failed close remains incomplete; keep the link handle and resolve the owned endpoint before opening replacement connections. These checks are non-blocking; never join workers or wait for a process from a Minecraft tick or chunk callback.

`stopReason()` preserves the first stop reason: explicit close, input EOF, input/output failure, input/output saturation or interruption. `ioFailure()` preserves the I/O exception; `cleanupFailure()` preserves cancellation/close exceptions, including simultaneous failures as suppressed exceptions. These are embedding diagnostics, not gameplay events or wire fields. Frame version and layout are unchanged.

The two-stream constructor remains available for streams that support close-only cancellation. Java's [InputStream contract](https://docs.oracle.com/en/java/javase/21/docs/api/java.base/java/io/InputStream.html#readNBytes(int)) does not specify a universal asynchronous-close or interrupt guarantee. The native test peers use an explicit owned-process cancellation callback and bounded exit checks; no game process is selected by that test path.
