# Host observation core

[简体中文](zh-CN/host-observation.md) · [Architecture](architecture.md) · [Host research](host-research.md)

The C++20 read-only observation core is defined in [host_observation.hpp](../include/harrancraft/host_observation.hpp). Its isolated [CTest](../native/tests/host_observation_test.cpp) does not load or start a game.

## Build and session identity

`ImageIdentity` records a 32-byte SHA-256 digest, file length and AMD64 PE machine identifier (`0x8664`). `Observer` accepts an explicit allowlist provided by its owner. The default empty allowlist accepts no build; synthetic identities in CTest are test fixtures, not recognized Dying Light versions.

The future Windows adapter must measure a legitimate executable independently, determine whether the active session is single-player, establish the current world identity and prove an engine-safe **read** phase. An untrusted callback claim or matching version text does not establish these facts. No concrete Dying Light build is approved by this module.

An observation session opens only with an accepted image and `single_player / playing / verified_engine_read` evidence for a nonzero world. Cooperative, invasion, loading, shutdown, window/Present or unknown states are rejected. Any changed context on an attempted capture or snapshot revokes the active session. Even a verified read phase grants no mutation capability. All observer methods require the creating thread; cross-thread access fails rather than sharing engine-owned pointers.

## Windows self-image probe

The Windows x64-only [host probe DLL](../native/host/host_probe_windows.cpp) exports the versioned C interface declared in [host_probe.h](../include/harrancraft/host_probe.h). The function `hc_probe_current_executable_v1` measures the current process's main executable file only; it never selects another process, loads a game, scans installations or discovers private paths. An explicitly sized 56-byte result returns version, AMD64 PE machine, byte length and SHA-256; invalid buffers are rejected without writes, while other failures clear the result.

The probe resolves the main module's file name, opens it read-only without sharing writes, verifies an AMD64 PE32+ executable, then streams at most 512 MiB through Windows CNG SHA-256. Size, file index and last-write metadata are checked again before returning. Failure statuses distinguish unavailable files, unsupported images, excessive size, changed files and system failures. No path or process handle crosses the public interface.

This is an **on-disk executable fingerprint associated with the current process path**. It is not an in-memory code integrity measurement, proof that the mapped image and opened path are the same file object, permission to attach to a game, or evidence of its multiplayer state or safe engine callback. File hashing is a bounded setup operation, not allowed in game update/graphics callbacks or under the loader lock. The H1 `Observer` still needs separately verified build allowlisting and trusted session/phase evidence; no Dying Light build is enabled automatically.

## Frame correspondence

Each camera sample carries session epoch, world, camera, frame and capture identities, plus camera position and column-major view/projection matrices. Epochs increase on reattachment; a failed replacement invalidates the old session. Capture and frame identities increase strictly within an epoch. Missing, out-of-order, non-finite and wrong-world data are rejected. Only the latest sample is retained, and a snapshot can be retrieved only by its exact five-field identity and unchanged session evidence. World changes, mode loss and stopping clear the retained sample.

`project_clip` transforms a guest point with an explicit affine guest-to-host mapping, then the captured view and projection matrices. It returns homogeneous clip coordinates, not pixels or proven occlusion. The matrices use column-major storage and column vectors. The real adapter must calibrate axes, units, handedness, projection conventions and frame correspondence before screen/depth integration. Neither a synthetic projection nor an aligned image establishes access to the host depth buffer.

## Isolated verification

`python tools/test_bridge.py` invokes CMake and CTest, including `host-observation`, alongside existing native wire and Java process checks. The observer test covers refused builds/modes/phases, generations, camera replacement, frame/capture correlation, invalid floating-point data, owner-thread enforcement, world switching and stopped-session refusal. On Windows x64 the same CTest build also loads the DLL into a controlled executable, verifies the C ABI, repeatability and handle cleanup, and compares the exported digest against Python's independent SHA-256 of that executable. It does not verify game callback safety, game binary identity, camera accuracy or native physics.

Dying Light runtime hooks, engine-safe callback provenance, live camera/world readings, native resource retirement and original-world interactions remain separate H1/H3 acceptance conditions.
