# Architecture and ownership

[简体中文](zh-CN/architecture.md) · [Documentation](README.md)

## Delivered component

`hc-inspect` is an offline development tool. Its data flow is explicit file argument → bounded PE parsing and streaming hash → versioned report. Inputs are read-only; reports carry input positions rather than paths. The parser does not participate in the game process or grant permission for a native callback.

## Planned runtime boundary

The native Dying Light 1 adapter owns host object identities, original-world queries, camera/depth acquisition and accepted engine calls. A real Fabric guest owns Minecraft inventory, blocks, items and Minecraft simulation. A bridge transports versioned snapshots and commands; it is not a second independent authority over either game's health or objects.

The native adapter is expected to use a Windows x64 toolchain; the concrete runtime and Minecraft/Fabric/JDK versions are selected alongside working integrations. Garry's Mod is not a component. Native geometry submission and a depth-aware GPU compositor remain competing experiments until host evidence selects a route.

| State or operation | Authority and acceptance |
| --- | --- |
| Minecraft placement/removal | Guest world change; native adapter acknowledges the corresponding owned collider or rejects/rolls back the operation |
| Host entities and health | Host entity lifetime and health remain authoritative; guest representations must not independently force host death |
| Minecraft entities and health | Guest simulation owns its state; host-originated events are settled once with source identity |
| Input focus | Exactly one receiver per routed action; all held actions are released on focus loss/disconnect |
| Collision resources | Adapter owns its created shapes and registrations; every allocation/activation has a corresponding rollback/retirement path |
| Frames and cameras | Each frame carries its source camera, epoch and capture identity; mismatched data is rejected, not silently paired |

## Wire and execution requirements

When the first native producer and Fabric consumer are introduced, `include/harrancraft/wire.hpp` will be the single native wire-layout authority with a validated matching Java implementation. That file and ABI are not present in the inspector-only baseline. HarranCraft mappings must not reuse a reference project's mapping name or assume its layout.

Every game-facing event needs an origin, session generation, unique event identity and applicable target lifetime. Bound queues and work per update; define saturation and acknowledgements. Reconnection cannot replay an old session's damage or resurrect removed collision resources. A protocol break requires a version change and producer/consumer refusal or migration behavior.

Host modification is gated by accepted image fingerprints, a positively identified single-player session and a verified engine-safe callback phase. Unknown or disallowed states invalidate the mutation path. Rendering callbacks and window messages are observation candidates, not automatically safe mutation points. No blocking wait, filesystem work or synchronous GPU readback belongs in a game mutation callback.

## Core experiments

Correct rendering requires host scene depth and camera/frame correspondence, or actual native geometry with equivalent occlusion. Transparency, HUD ordering, jitter and resize must be measured. Distance proxies and image alignment alone are insufficient.

Native block collision must be created and removed in the host physics world and tested against its player, enemies and projectiles. AI navigation is separately measured. Ordinary nonlethal damage, attribution and two-way explosion settlement are tested without using Kill as a substitute.

On stopping or losing a session: revoke mutation first, invalidate queues/generations, release input, retire owned host objects through safe lifetime paths, restore hidden state, then detach graphics and IPC resources. If safe destruction cannot be established, report the uncompleted cleanup instead of discarding ownership records and claiming restoration.
