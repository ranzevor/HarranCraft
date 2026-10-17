# Minecraft baked-model observation

[简体中文](zh-CN/render-model.md) · [Architecture](architecture.md) · [Host observation](host-observation.md)

## Real model source

The client-only [MinecraftRenderModels](../guest/src/main/java/io/ranzevor/harrancraft/MinecraftRenderModels.java) reads actual Minecraft 1.21.4 [BakedModel](https://maven.fabricmc.net/docs/yarn-1.21.4%2Bbuild.8/net/minecraft/client/render/model/BakedModel.html) quads via BlockRenderManager on the Minecraft client thread. It obtains the active block state from the already loaded client world and uses the state/position rendering seed. Unculled and six face-specific [BakedQuad](https://maven.fabricmc.net/docs/yarn-1.21.4%2Bbuild.8/net/minecraft/client/render/model/BakedQuad.html) selections preserve Minecraft's model variants. No collision boxes are converted into visual meshes.

Each quad retains its declared cull face and model face, atlas and sprite identifiers, tint index, shading and light-emission metadata. Its four vertices retain local float positions, atlas UV, packed color, packed light and packed normal. The expected Minecraft 1.21.4 packed vertex layout is exactly four vertices of eight 32-bit words; unknown layouts and non-finite positions/UV are rejected, not guessed. A capture is capped at 512 quads. The output is an immutable copy rather than pointers to game-owned arrays.

Supported input is a loaded, ordinary model-rendered block in a private integrated single-player world. Air and other invisible states produce an empty model snapshot. Fluid-containing blocks, block-entity renderers, non-model render types, unloaded/out-of-bounds positions and invalid model data are rejected rather than represented as generic cubes. Atlas texture pixels, connected textures, runtime shader behavior, dynamic entities, fluids and lighting computed by the final renderer are not extracted.

## Camera and lifetime identity

[RenderFrame](../guest/src/main/java/io/ranzevor/harrancraft/bridge/RenderFrame.java) carries positive epoch, world, camera, frame and capture tokens, block dimension/position, canonical block state, immutable quads and the Minecraft camera position/yaw/pitch. The numeric tokens are **assigned by the caller**; they are not attestation of a Dying Light camera, actual GPU presentation or a matching host frame.

[RenderCaptureSession](../guest/src/main/java/io/ranzevor/harrancraft/bridge/RenderCaptureSession.java) requires strictly increasing logical frame/capture identities, an owner thread and a matching dimension. The client adapter pins the actual ClientWorld, integrated server and Camera objects; a world/camera change, disallowed mode or failed read invalidates the session. Recovery requires creating another session with newly assigned identities. Capturing model data does not call GPU readback or blocking IPC.

The H1 host camera gate uses a separate verified read-phase requirement. A real future H2-B/H3 compositor must correlate the two cameras by calibrated transforms, projection matrices, frame timing and actual host depth. Merely matching numeric keys, yaw/pitch or isolated quads does not prove that correspondence.

## Validation boundary

The dependency-free Java RenderFrameTests execute the eight-word packed-vertex decoder, bound/finite-data checks, immutable snapshots, token ordering, identity mismatch and cross-thread failure without a game. The pinned Fabric build compiles the client model adapter and client GameTest source, which samples a real placed block's baked quads and confirms that air has no model quads when executed. CI does not start Minecraft, accept its EULA or execute the GameTest. The separate [render record v1 codec and optional publisher](render-wire.md) preserve baked quads, atlas/sprite identifiers and camera tokens across Java/C++ through an isolated process test and bounded client-side asynchronous transport. It is not a live render-mesh publisher, atlas image delivery, Dying Light geometry submission or depth-aware composite.
