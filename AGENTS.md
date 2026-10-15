# HarranCraft engineering contract

## Target and authority

- Implement a real Minecraft × Dying Light **1** integration. The initial host is Windows x64 single-player. Preserve scene occlusion, native block collision, two-way damage, TNT and restoration as core requirements.
- Inspect the current branch, main and related PRs before writing. Retain valid concurrent changes. Use a task branch and PR after repository initialization; compare exact heads before merging.
- Repository code and recorded test results describe current capability. Plans, export names and upstream demonstrations do not establish a working HarranCraft interface.
- Changes to game installations, user saves, multiplayer sessions, releases, credentials or paid infrastructure require separate authorization. Source development and isolated tests do not authorize those operations.

## Implementation

- The current Python inspector reads explicitly supplied regular files. It must not load libraries, launch inspected programs, search user installations or transmit inputs. Default reports omit input paths.
- Keep bounded file reads, counts and traversal depths. A malformed, changed or unsupported input must produce a specific failure, not a successful partial inspection.
- For future host code, positively identify the build, single-player session and engine-safe phase before every game-affecting operation. Unknown/loading/shutdown/disallowed states reject mutation. Do not use visibility settings or player count as mode evidence.
- Do not treat `ILevel::TimerUpdate`, a time throttle or a window/Present thread-ID match as a verified game-update callback. No Present mutation fallback or blocking IPC, file I/O, waits or synchronous GPU readback in mutation callbacks.
- Define one authoritative wire layout when both native and Fabric endpoints are implemented. New mapping names must be HarranCraft-specific. Layout changes require a protocol version and producer/consumer refusal or migration tests.
- Track session generations, event identity, ownership, backpressure and disconnect cleanup. Render alignment is not host depth occlusion; guest collision is not native physics; Kill is not proof of ordinary damage.
- User-visible tool text belongs in the en-US and zh-CN catalogs. Preserve placeholders, error distinctions and fallback behavior. Machine fields remain language-neutral.

## Review and validation

- Apply `.editorconfig`, `.gitattributes` and `pyproject.toml`. Keep comments for non-obvious contracts and retain applicable licenses.
- Run `python tools/verify.py` and `python tools/package.py` with the pinned development tools. Check actual content and behavior as well as automated results.
- Add regressions for defects. Do not delete assertions, weaken limits, swallow failures or mark an unrun check as passed.
- Distinguish unit/format checks, CLI subprocess tests, packaging, native graphics experiments and real-game acceptance. Record exact commits and environments for observations that affect compatibility.
- Do not commit game binaries, extracted assets, proprietary SDKs, saves, personal paths, credentials, build outputs or temporary diagnostic files. Generate test fixtures in temporary directories; exclude them from the runtime wheel.
- Keep English and Simplified Chinese entry points, documentation navigation, compatibility, milestones, security and change records consistent with actual code. Do not copy inaccessible private policy text into the public repository.
