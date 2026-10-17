# Architecture and ownership

[简体中文](zh-CN/architecture.md) · [Documentation](README.md)

## Implemented components

`hc-inspect` is an offline development tool. Its data flow is explicit file argument → bounded PE parsing and streaming hash → versioned report. Inputs are read-only; reports carry input positions rather than paths. The parser does not participate in the game process or grant permission for a native callback.

The H2-A source adds a Fabric block adapter, chunk-change observation, a Java session/transport core and an independent C++ test peer. Controlled process tests exercise the wire and lifecycle; Ubuntu Fabric and GameTest source compilation plus production JAR verification pass, while real GameTests remain unrun. The embedding API accepts caller-owned private pipes; it does not provide a user launcher. [Block bridge contract](bridge.md)

The Windows-only [self-image probe](host-observation.md#windows-self-image-probe) supplies read-only on-disk main-executable fingerprints through a fixed C ABI. It does not establish the active engine build from mapped memory or prove session mode or callback safety. The independent [H1 observation core](host-observation.md) checks explicit executable fingerprints, positively asserted single-player/read-phase evidence, session generations and exact world/camera/frame/capture correspondence. Its host-to-guest clip-space projection is mathematical only; it neither locates Dying Light callbacks nor reads the game's camera or depth resources. No game build is accepted by default. The [composite host build gate](host-observation.md#composite-host-build-identity) matches an EXE and its required DLL fingerprints together, revoking camera/session identity after any missing or changed module.

An independent [geometry record codec](geometry.md) now encodes actual Fabric collision snapshots into bounded versioned data readable by a native test peer. Its optional, caller-owned guest geometry output is queued off the Minecraft server thread and retired with the main session. A bounded native stream receiver consumes fragmented geometry bytes and retires staged candidates on EOF or protocol failure. This has only controlled-process evidence: no attachment to Dying Light, registered native collider or render pipeline.

The client-only [baked-model observer](render-model.md) reads actual static Minecraft BakedModel vertex quads and camera pose under positive logical epoch/world/camera/frame/capture tokens. It is independent from collision VoxelShapes. The [independent render model record v1](render-wire.md) preserves quads and identity on Java/C++ with controlled cross-process tests. An optional client output queue and bounded native receiver support strict source identity and model retirement, but there is no Dying Light-attached render endpoint, calibrated projection/depth or synchronized engine frames. The [source-sprite sampler and batch ownership](texture-wire.md) now capture bounded NativeImage ARGB pixels, publish complete model/texture manifests asynchronously and stage only fully matched CPU-side source materials with reload/EOF revocation. Final GPU atlas contents, native host materials and Dying Light graphics registration remain absent.

## Planned runtime boundary

The native Dying Light 1 adapter owns host object identities, original-world queries, camera/depth acquisition and accepted engine calls. A real Fabric guest owns Minecraft inventory, blocks, items and Minecraft simulation. A bridge transports versioned snapshots and commands; it is not a second independent authority over either game's health or objects.

The native adapter targets a Windows x64 toolchain; the guest's pinned toolchain in [compatibility](compatibility.md) has resolved and compiled in Ubuntu CI, with real game validation pending. Garry's Mod is not a component. Native geometry submission and a depth-aware GPU compositor remain competing experiments until host evidence selects a route.

| State or operation | Authority and acceptance |
| --- | --- |
| Minecraft placement/removal | Guest world change; native adapter acknowledges the corresponding owned collider or rejects/rolls back the operation |
| Host entities and health | Host entity lifetime and health remain authoritative; guest representations must not independently force host death |
| Minecraft entities and health | Guest simulation owns its state; host-originated events are settled once with source identity |
| Input focus | Exactly one receiver per routed action; all held actions are released on focus loss/disconnect |
| Collision resources | Adapter owns its created shapes and registrations; every allocation/activation has a corresponding rollback/retirement path |
| Frames and cameras | Each frame carries its source camera, epoch and capture identity; mismatched data is rejected, not silently paired |

## Wire and execution requirements

`include/harrancraft/wire.hpp` defines the implemented version 1, 512-byte little-endian protocol. Java `Frame` matches its encoding and strict validation; reference-frame and real-process tests cover the controlled endpoints. HarranCraft mappings must not reuse a reference project's mapping name or assume its layout.

Every game-facing event needs an origin, session generation, unique event identity and applicable target lifetime. Bound queues and work per update; define saturation and acknowledgements. Reconnection cannot replay an old session's damage or resurrect removed collision resources. A protocol break requires a version change and producer/consumer refusal or migration behavior.

Host modification is gated by accepted image fingerprints, a positively identified single-player session and a verified engine-safe callback phase. Unknown or disallowed states invalidate the mutation path. Rendering callbacks and window messages are observation candidates, not automatically safe mutation points. No blocking wait, filesystem work or synchronous GPU readback belongs in a game mutation callback.

H2-A authorizes only Minecraft block operations. Server-thread mode checks precede access and revoke active sessions when disallowed. Stream workers never perform world access. The chunk callback observes actual changes; failed event/receipt delivery invalidates the session rather than replaying mutations. New-epoch snapshots are required after disconnect, because a committed Minecraft change is not automatically rolled back when its acknowledgement fails. Host collision acknowledgement and cross-runtime rollback in the table above remain future integration contracts.

## Core experiments

Correct rendering requires host scene depth and camera/frame correspondence, or actual native geometry with equivalent occlusion. Transparency, HUD ordering, jitter and resize must be measured. Distance proxies and image alignment alone are insufficient.

Native block collision must be created and removed in the host physics world and tested against its player, enemies and projectiles. AI navigation is separately measured. Ordinary nonlethal damage, attribution and two-way explosion settlement are tested without using Kill as a substitute.

On stopping or losing a session: revoke mutation first, invalidate queues/generations, release input, retire owned host objects through safe lifetime paths, restore hidden state, then detach graphics and IPC resources. If safe destruction cannot be established, report the uncompleted cleanup instead of discarding ownership records and claiming restoration.
