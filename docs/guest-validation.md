# Local guest validation

[简体中文](zh-CN/guest-validation.md) · [Development](development.md) · [Bridge contract](bridge.md)

Run commands from the repository root in an isolated source checkout. Use disposable generated worlds, not a real installation or existing saves. The Fabric sources and production JAR passed an Ubuntu CI build; actual GameTests and live game integration remain unrun. Successful compilation and protocol tests do not replace those acceptance results.

## Toolchain

Python 3.12+, JDK 21, CMake 3.25+ and a C++20 compiler are required for the game-free bridge checks. CMake enables strict warnings as errors for GCC/Clang or MSVC. The guest configuration pins Minecraft 1.21.4, Yarn 1.21.4+build.8, Fabric Loader 0.16.14, Fabric API 0.119.4+1.21.4 and Loom 1.11.8. This is a configured combination, not a validated compatibility matrix.

`tools/gradle.py` downloads Gradle 8.14.3 from its official distribution endpoint and checks SHA-256 `bd71102213493060956ec229d946beee57158dbd89d0e62b91bca0fa2c5f3531`. It is a custom bootstrap, not the official Gradle Wrapper. First-time Gradle/Fabric/Minecraft dependency resolution needs network access. No dependency archive or game binary is stored in the repository.

## Repository and process tests

```console
python -m pip install -r requirements-dev.txt
python tools/verify.py
python tools/package.py
python tools/test_bridge.py
```

The bridge command builds C++ tests, compiles the dependency-free Java protocol core, runs core/lifecycle regressions, compares a complete Java/C++ reference frame and starts actual Java/C++ peers against a controlled world. It also runs `hc-pipe-peer` fault scenarios and checks actual worker/process retirement. Linux descriptor counts are measured through `/proc/self/fd`; an unavailable measurement is reported separately. It does not start Minecraft. The pull-request CI runs these game-free Java/C++ process checks on Ubuntu with JDK 21 / GCC 14 and on Windows Server 2025 with JDK 21 / MinGW x64 GCC / Ninja. Each platform has an independent job and result. The Windows job selects the native MinGW compiler explicitly; MSVC remains a separate unverified toolchain. Neither job resolves Fabric dependencies or starts Minecraft; real game validation remains separate.

Normal native pipe fault scenarios require explicit diagnostic completion and EOF before forcibly terminating the owned peer. Input saturation verifies that the peer observes its output pipe closing while it is still writing. Windows uses synchronous [`WriteFile`](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-writefile#pipes) and accepts only the documented [`ERROR_BROKEN_PIPE` (109, ended) or `ERROR_NO_DATA` (232, closing) status](https://learn.microsoft.com/en-us/windows/win32/debug/system-error-codes--0-499-); POSIX requires `EPIPE`. Generic CRT errors and other Win32 errors do not satisfy the closure handshake. Diagnostic parsing is bounded, unexpected errors preserve their causes even after process exit, and failed evidence does not waive child or worker cleanup. Negative cases check diagnostic-close failure exits and a three-second cancellation timeout by holding delivery of an already-observed EOF. An intentionally broken buffered output may retain its exact owned-close exception; unrelated cleanup errors remain failures. This controlled-peer handshake is part of the test harness; the production pipe caller retains asynchronous cancellation.

Geometry wire, async pipe failure/retirement regressions, bounded native incremental reception and Java/C++ cross-process checks are included in `python tools/test_bridge.py`. The separate record format is documented in [Geometry side-channel](geometry.md); it does not invoke a game, transmit to Dying Light or execute GameTests.

The registered `camera-projection` and `capture-projection` CTests run in the same command's `native-tests` stage. They check analytic projection values for both handedness conventions, depth ranges and reverse Z, exact AABB calibration limits, and current-capture ownership using decoded material and geometry records with a controlled host observer. Camera/configuration changes, incomplete materials, source withdrawal and stale identities must invalidate the projected result. These are [controlled camera contracts](camera-projection.md); they do not read either game's live projection matrix or scene depth.

On Windows x64, the same native stage also runs `d3d11-depth` using the operating system's D3D11 WARP software device at feature level 11.0. The test compiles its own small shaders with `d3dcompiler`, creates controlled color/depth surfaces and checks normal/reverse-Z occlusion, unchanged depth and attachment lifetime. Device creation or shader compilation failure fails the test; it is not a skip or a request to launch a game. Windows D3D11 development headers/import libraries and the D3DCompiler runtime are required by this target. Linux does not build or execute this Windows-only test. See the [depth contract](d3d11-depth.md) for the exact surface and host-evidence limits.

Pure-Java render identity, packed-vertex, render-record-v1 and async pipeline regressions, full cross-language golden bytes, bounded receiver CTest and native process exchange run as part of `python tools/test_bridge.py`. The client-only baked-model sampler and its GameTest compile with the pinned Fabric configuration. [Baked-model observation](render-model.md) documents the client-thread and projection boundaries; CI does not sample live Minecraft geometry.

The [source sprite protocol and material coordination contracts](texture-wire.md) add Java/native ARGB checks, complete golden bytes, dual asynchronous publishing and bounded native material reception. The registered `material-coordinator` CTest exercises cross-channel ordering, all fragment splits in its fixtures, complete-manifest activation, replacement/withdrawal, admission limits, absolute deadlines and same-instance recovery under a newer epoch. `MaterialPipelineTests` checks immediate paired cancellation on asynchronous write/flush failure without another call by the client owner.

The `material-coordinator-process-tests` stage runs `MaterialCoordinatorProcessTests` against `hc-material-coordinator-peer`. Models use the child's stdin; textures use a separate private Windows named pipe or POSIX FIFO. The suite verifies actual consumption of each independent channel, complete returned model/pixel bytes, forced texture-first delivery through `MaterialPublisher`, incomplete/duplicate/foreign records, timeouts and EOF/endpoint cleanup. It also saturates the actual publisher's texture queue after native material readiness, checks paired cancellation and native withdrawal, then recovers with new processes/endpoints and newer epochs while rejecting old traffic. The existing ordered `MaterialProcessTests` remains a separate single-model receiver check. Results for these stages are process evidence, with no Minecraft or Dying Light execution.

The pinned Fabric build compiles the client-only SpriteContents accessor, optional material publisher and Fabric `CLIENT_RESOURCES` reload listener alongside GameTest source that reads an actual placed block's bounded NativeImage pixels. GameTests and Mixin loading are not executed in ordinary CI. No game binary or extracted image is included in build source.

## Fabric compilation

```console
python tools/gradle.py build
```

The default build compiles main, unit-test and GameTest sources and checks the production JAR inventory, but does not enable Minecraft execution. [CI #38892925451](https://github.com/ranzevor/HarranCraft/actions/runs/38892925451) executed `compileJava`, `compileGametestJava`, `remapJar`, `verifyGuestJar` and `build` successfully on Ubuntu/JDK 21 with game and LAN switches false. Runtime output is under `guest/build/libs`; the Python wheel remains an inspector-only artifact. Production JAR checks reject game classes, server/client GameTest entrypoints and test classes. A repository regression checks the separate production/test source descriptors.

## Explicit Minecraft execution

Read the [Minecraft EULA](https://www.minecraft.net/eula) before running game tests. Only an executor accepting those terms should pass the two opt-in properties.

```console
python tools/gradle.py -PrunMinecraftTests=true -PacceptMinecraftEula=true runGameTest
```

With explicit game-test opt-in, the `build` and `check` lifecycles can run server GameTests; `test` alone does not run GameTests. Client GameTests run only when `runClientGameTest` is requested. Server GameTests check canonical block states, real air/full-block/slab collision shapes and rejection of the isolated GameTest server as a production bridge endpoint. The client test uses a new integrated world and the native peer produced by `test_bridge.py`. With the default Linux single-configuration output:

```console
python tools/gradle.py -PrunMinecraftTests=true -PacceptMinecraftEula=true -PpeerExecutable=../build/bridge/native/hc-bridge-peer runClientGameTest
```

For a Windows Visual Studio multi-configuration build, use `-PpeerExecutable=../build/bridge/native/Debug/hc-bridge-peer.exe` instead. The path is resolved relative to `guest/`; use the actual CMake output when selecting a different generator. The client test needs a working graphics session. It checks placement, authoritative server state, an independent block change, removal, stale epochs, invalid versions and disconnect cleanup with an explicitly owned cancellation callback and transport-termination checks; it captures empty/placed/removed screenshots. Local GameTest files remain under `guest/build/run` and test reports/screenshots under generated build outputs. Do not point a run directory at an existing save.

Record actual command exit codes, source commit, JDK/compiler versions and the selected dependency versions. Preserve failure logs and inspect all three screenshots. Real LAN/mode transitions, Windows execution, complete Fabric resolution and client rendering remain distinct acceptance results; do not infer them from a synthetic `allowed=false` test. No Dying Light game execution is part of these commands.

## Bootstrap recovery

A bad checksum, invalid ZIP path, symlink/special entry, missing launcher or archive-size limit fails before promotion. Incomplete staging is removed; an existing invalid cache is preserved. Inspect and remove only the generated `build/toolchains/gradle-8.14.3` cache before retrying. An existing `.install-lock` is never stolen: confirm no installer is running before removing a stale generated lock. Do not delete unrelated toolchains or installation directories.

## Preflight, deadlines and results

Both existing entry points accept `--hc-preflight`, `--hc-timeout SECONDS` and `--hc-revision COMMIT`. Preflight requires a Git checkout, records its HEAD/tree and a SHA-256 snapshot of tracked and unignored files, and probes the installed Java/CMake tools. `--hc-revision` accepts a complete 40-character commit and rejects a different HEAD. Dirty source is recorded rather than silently attributed to the commit; a source change during execution fails the result. Preflight never downloads Gradle, resolves Fabric or launches a game.

```console
python tools/test_bridge.py --hc-preflight
python tools/gradle.py --hc-preflight build
python tools/test_bridge.py --hc-timeout 600
python tools/gradle.py --hc-timeout 1800 build
```

The timeout is the complete invocation's execution budget, including probes and bootstrap; bridge commands also have a 120-second per-stage ceiling. Deadline expiry or interruption cancels only the invocation's owned process tree and performs bounded cleanup. Linux uses an isolated process group and child-subreaper helper; Windows assigns a gated helper to a Job Object before releasing the command. When the only remaining owned process is MSVC's `vctip.exe` telemetry helper, cleanup allows at most 30 seconds within the invocation deadline for it to exit naturally. Any descendant still alive after its applicable grace is terminated and the validation fails, even if the command returned zero and cleanup succeeds. A shared `build/.validation-lock` prevents these entry points from concurrently changing the same build outputs. An existing lock is not removed or stolen; check its owner record and active validation before removing a stale lock.

Every invocation writes a distinct `reports/validation/<run>/result.json` and prints its repository-relative location as JSON. The [version 1 schema](../schemas/validation-v1.schema.json) defines the report. Results distinguish `blocked`, `failed`, `timed_out`, `interrupted`, `passed` and `incomplete`; preflight-only success is `preflight_passed`, with execution stages still `not_run`. Each executed stage retains separate stdout/stderr logs, its actual exit code, elapsed time and cleanup result. Java classes are compiled into a new per-run output directory, not reused from an earlier test run. The native peer location documented above remains unchanged.

Gradle bootstrap, Gradle execution and evidence collection are separate stages. The report retains the task names and outcomes actually emitted by Gradle's plain console, including failed compile or GameTest tasks; detailed dependency, Mixin or runtime failures remain in their stage logs. Project/build-file redirection, additional init scripts/composite builds and dry-run execution are rejected by the validation wrapper. Existing explicit Minecraft opt-ins are required before bootstrap; client runs additionally check the supplied peer file and record its hash. A configured display or accepted command is not a completed game test.

Evidence collection preserves new or changed PNG, XML and log files under `guest/build`, excluding world data, compiled classes and dependency caches. It runs after Gradle failure as well as success. Unchanged screenshots cannot satisfy a new client run; all three expected names, including Fabric counter prefixes, are required. Copied files carry sizes and SHA-256 values; PNG signatures and dimensions are checked, while `visual_review` remains `not_run` until the screenshots are separately inspected. Traversal, source files and evidence have explicit limits; exceeding a limit fails collection rather than silently omitting results. Raw tool logs may contain local paths: inspect them before sharing. Nothing is uploaded automatically, and the report does not mark Minecraft or Dying Light acceptance complete.

Process ownership follows [Python subprocess](https://docs.python.org/3.13/library/subprocess.html), [Windows Job Objects](https://learn.microsoft.com/en-us/windows/win32/procthread/job-objects) and [Linux child subreapers](https://man7.org/linux/man-pages/man2/PR_SET_CHILD_SUBREAPER.2const.html). These controls manage trusted build/test descendants; they are not a sandbox for hostile programs.

### Invocation and completion contract

The validation entry accepts exact root tasks `build`, `check`, `test`, `runGameTest` and `runClientGameTest`, optionally with `clean` and a leading colon. Task abbreviations, other-project task paths, exclusions and dry runs are rejected. A zero exit code is insufficient: every requested root task must appear in the recorded task results. Ordinary tasks may complete by execution, `UP-TO-DATE` or `FROM-CACHE`; requested game tasks require actual execution. Missing or skipped tasks, or any reported `FAILED` task, prevent success. `uncompleted_tasks` and `unexecuted_game_tasks` retain the exact unmet requirements. Evidence-directory read failures remain failures rather than empty successful collections.

Supported Gradle options are `--offline`, `--refresh-dependencies`, `--rerun-tasks`, `--stacktrace`, `--full-stacktrace`, `--info`, `--build-cache`, `--no-build-cache`, `--parallel`, `--no-parallel`, `--continue`, `--no-daemon`, `--console=plain` and `--max-workers=N` (1–9999). Other options, including help/version-only commands, daemon control, build scans, alternate property syntax and option abbreviations, are rejected before preflight. The wrapper fixes non-daemon execution and plain console output.

Project properties use `-Pname=value`; accepted names are `runMinecraftTests`, `acceptMinecraftEula`, `peerExecutable` and `runLanTests`. Duplicate properties and non-boolean game switches are rejected. Both game switches must be explicitly true to enable game execution. Otherwise the invocation supplies both as false, overriding ambient environment or user-level Gradle property values. The existing peer-file and graphics checks still apply to client tests.

Tool probes retain their stdout/stderr references, exit code, elapsed time, cleanup outcome and original failure details under `tools`. Failed probes, missing prerequisites, timeouts and interruptions keep their distinct outcomes; a successful process with an unrecognized version is a failed probe check. Source-identity timeouts remain `timed_out`. Interrupted or failed preflight does not leave a `running` state in the final report.

## World lifecycle and isolated LAN acceptance

Client execution also runs `WorldLifecycleTests` in disposable worlds. It repeats native block round trips on three fresh connections, rejects attachment from the game-test thread without consuming the offered transport, and verifies that refusing to replace a chest preserves its block-entity identity and seven-diamond inventory fixture. An observer remains attached while a world closes: the registered lifecycle callback, not a test-side detach, must retire the connection. The generated save is reopened and its gold block verified before further mutation; both reopened and replacement worlds must reject a prior epoch and accept a new block round trip. No existing save is opened.

The native `observe` scenario subscribes to one loaded position, receives independently originated events and waits for the guest to close its stream. `ObservationTests` checks this peer with real processes, including invalid epoch/sequence/origin/type and partial-frame rejection, followed by repeated retirement. Its supplied guest frames are not Minecraft execution.

LAN validation requires a separate explicit opt-in and an isolated network: it calls the real `openToLan` API, temporarily opens a listening port and starts Minecraft LAN discovery. It does not connect another player or modify a real installation. From the repository root, using the default Windows Visual Studio peer output:

```console
python tools/gradle.py --hc-timeout 1800 -PrunMinecraftTests=true -PacceptMinecraftEula=true -PrunLanTests=true -PpeerExecutable=../build/bridge/native/Debug/hc-bridge-peer.exe runClientGameTest
```

On Linux, use `../build/bridge/native/hc-bridge-peer`. `runLanTests` is accepted only with enabled client tests and defaults to an explicit false on every wrapper invocation, overriding ambient project properties. Ordinary client tests do not open LAN. The LAN case checks actual mode rejection, automatic end-tick closure of the native observer, refusal of a new caller-owned transport and an unchanged block. A separate controlled command queue uses the real Minecraft adapter and actual post-LAN mode to verify that a queued SET is discarded; this is not a native-pipe queue measurement. After closing the generated world, the server and discovery workers must end and the listening port must be bindable again. A fresh private world must then reject the old epoch and complete a new round trip.

Each completed client scenario emits `HARRANCRAFT_SCENARIO <name>` in that invocation's Gradle output. The report's `client_scenarios` records passed, missing, unexpected and duplicate names, plus `invalid_records` for malformed markers; successful tasks and screenshots alone are insufficient. Default requirements are `block-roundtrip`, `reconnect`, `block-entity-preservation`, `thread-ownership`, `world-stop`, `world-reopen` and `world-replacement`. Explicit LAN execution additionally requires `lan-revocation` and `lan-recovery`. Missing, unexpected, duplicate or malformed completion records fail validation. A record count above 64 also fails with `client_scenario_limit_exceeded`. `lan_execution=false` and `lan=not_requested` do not establish LAN acceptance. Screenshot visual review remains separate.

These game-test sources require the configured Fabric build and actual client/server execution. API references: [Yarn 1.21.4+build.8 integrated server](https://maven.fabricmc.net/docs/yarn-1.21.4+build.8/net/minecraft/server/integrated/IntegratedServer.html) and [Fabric 1.21.4 test-world save API](https://github.com/FabricMC/fabric-api/blob/1.21.4/fabric-client-gametest-api-v1/src/client/java/net/fabricmc/fabric/api/client/gametest/v1/world/TestWorldSave.java).
