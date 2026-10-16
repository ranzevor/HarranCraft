# Host observation core

[简体中文](zh-CN/host-observation.md) · [Architecture](architecture.md) · [Host research](host-research.md)

The C++20 read-only observation core is defined in [host_observation.hpp](../include/harrancraft/host_observation.hpp). Its isolated [CTest](../native/tests/host_observation_test.cpp) does not load or start a game.

## Build and session identity

`ImageIdentity` records a 32-byte SHA-256 digest, file length and AMD64 PE machine identifier (`0x8664`). `Observer` accepts an explicit allowlist provided by its owner. The default empty allowlist accepts no build; synthetic identities in CTest are test fixtures, not recognized Dying Light versions.

The future Windows adapter must measure a legitimate executable independently, determine whether the active session is single-player, establish the current world identity and prove an engine-safe **read** phase. An untrusted callback claim or matching version text does not establish these facts. No concrete Dying Light build is approved by this module.

An observation session opens only with an accepted image and `single_player / playing / verified_engine_read` evidence for a nonzero world. Cooperative, invasion, loading, shutdown, window/Present or unknown states are rejected. Any changed context on an attempted capture or snapshot revokes the active session. Even a verified read phase grants no mutation capability. All observer methods require the creating thread; cross-thread access fails rather than sharing engine-owned pointers.

## Frame correspondence

Each camera sample carries session epoch, world, camera, frame and capture identities, plus camera position and column-major view/projection matrices. Epochs increase on reattachment; a failed replacement invalidates the old session. Capture and frame identities increase strictly within an epoch. Missing, out-of-order, non-finite and wrong-world data are rejected. Only the latest sample is retained, and a snapshot can be retrieved only by its exact five-field identity and unchanged session evidence. World changes, mode loss and stopping clear the retained sample.

`project_clip` transforms a guest point with an explicit affine guest-to-host mapping, then the captured view and projection matrices. It returns homogeneous clip coordinates, not pixels or proven occlusion. The matrices use column-major storage and column vectors. The real adapter must calibrate axes, units, handedness, projection conventions and frame correspondence before screen/depth integration. Neither a synthetic projection nor an aligned image establishes access to the host depth buffer.

## Isolated verification

`python tools/test_bridge.py` invokes CMake and CTest, including `host-observation`, alongside existing native wire and Java process checks. The observer test covers refused builds/modes/phases, generations, camera replacement, frame/capture correlation, invalid floating-point data, owner-thread enforcement, world switching and stopped-session refusal. It does not verify game callback safety, game binary identity, camera accuracy or native physics.

Dying Light runtime hooks, engine-safe callback provenance, live camera/world readings, native resource retirement and original-world interactions remain separate H1/H3 acceptance conditions.
