# Local validation evidence — 2026-10-17

[简体中文](zh-CN/validation-20261017.md) · [Validation commands](guest-validation.md) · [Compatibility](compatibility.md)

The supplied Windows evidence establishes three successful Minecraft server GameTests and a client run that failed at its first bridge attachment. It also records two failing native CTests. These observations do not constitute a completed client acceptance run or Dying Light compatibility approval.

## Evidence identity

The evidence archive has SHA-256 `80d84966ce2b9e2ce3bd7c42d996dd2bba5a968367abdabc576241f69f6b3baa`. All 19 distinct payload entries in its hash manifest match; the twentieth file is the manifest itself. This verifies the received files' internal consistency. Raw logs include local paths and are not repository inputs.

All seven validation JSON files identify revision `b1b6234eb81955cc170cc5e8ce66df5191073ca4`, tree `b21113cc790ddb10c74d968d8ca8e1f7da88de04`, 181 source files, `dirty=false` and identical before/after source snapshots. Their snapshot SHA-256 is `b10fa8903b9e86dc5f1ad85261a8f14f6074f4f41f5cdf6493650d8ac1b2f406`. These identities belong to those seven wrapper invocations. The report attributes subsequent direct Gradle runs to that revision, but the game console logs do not independently bind an execution tree. A later diagnostic client run recompiled modified test source; its patch and source snapshot were not supplied.

The game logs identify Minecraft 1.21.4, Fabric Loader 0.16.14, Fabric API 0.119.4+1.21.4, Loom 1.11.8 and Java 21. The configured Yarn mapping is 1.21.4+build.8. Java 21.0.3 and 21.0.5 patch versions are recorded by separate preflight invocations, not by every game execution.

## Observed results

| Execution | Recorded result | Scope |
| --- | --- | --- |
| Direct `runGameTest` | Task executed; 3 required tests passed in 1.818 seconds; Gradle succeeded | Actual Minecraft test server; canonical state, air/full-block/slab collision shapes and isolated-server refusal |
| Direct `runClientGameTest`, baseline | Failed at `GuestConnections.attach`; Gradle failed | Client, integrated world and rendering resources started; first bridge attachment refused |
| Diagnostic client run | Test-side exception reported `singleplayer=true`, `dedicated=false`, `remote=false`, `loading=true`, `stopping=false`, `stopped=false` | Separate modified-source run; `client-latest.log` belongs to this run |
| Manual native CTest run | 14/16 passed; `material-coordinator` SegFault; `geometry-exception` timeout at 40.04 seconds | Reported MSVC 19.44.35228 Debug build; no completed MSVC wrapper validation |
| Seven wrapper JSON files | 4 `preflight_passed`, 2 JDK-17 `blocked`, 1 bridge `failed` | Preflight success leaves execution stages `not_run` |

The server console logs the aggregate count rather than individual test names. The three cases above are associated through the sole server entrypoint and its three registered methods in [the reported revision's ServerTests](https://github.com/ranzevor/HarranCraft/blob/b1b6234eb81955cc170cc5e8ce66df5191073ca4/guest/src/gametest/java/io/ranzevor/harrancraft/test/ServerTests.java). `isolatedServerRefusal` verifies rejection of a test server, not successful admission of an integrated single-player server.

The baseline client failure is the Java exception `Bridge requires a private integrated server`; its eventual process exit is `-1073740791`. The diagnostic exception originates in `GamePeer`, and its stack does not show `GuestConnections.attach`. The diagnostic state is useful evidence, but does not establish the meaning of a mapped method name or prove an early-attachment race.

No default `HARRANCRAFT_SCENARIO` completion records occur in the client logs. No screenshot files were supplied; the report's description and hash of an empty scene are not an independent visual review. The seven client scenarios, placed/removed screenshots, actual client model/pixel sampling and HarranCraft resource-reload invalidation remain unaccepted.

The one failed wrapper JSON records a successful configure followed by `native-build` exit 1 and `owned_descendant_retained`: 20 `MSBuild.exe` and 20 `conhost.exe` descendants remained. Forced cleanup passed and all descendants were terminated; 37 later stages did not run. The three Fabric execution-failure JSON files referenced in the narrative report are absent. Claims about those Gradle failures, universal filesystem writability, exact native crash codes or sampled CPU usage cannot be independently established from this archive.

The supplied server EULA file says `false`, while the console records `acceptGameTestEula`, a failed EULA-file load and a subsequently running test server. The client file says `true`. These are separate observed facts; they do not establish an incorrect Loom output path or alter the repository's explicit execution/EULA opt-ins. Direct Gradle success does not satisfy the wrapper's source, process-retirement, evidence and completion gates by itself.

## Correct integrated-server readiness

The pinned Yarn method name is misleading: `MinecraftServer.isLoading()` returns Mojang's **ready** flag. The production condition previously negated it and therefore refused a ready private server.

| Source | Verified mapping or behavior |
| --- | --- |
| [Yarn 1.21.4+build.8 Javadoc](https://maven.fabricmc.net/docs/yarn-1.21.4%2Bbuild.8/net/minecraft/server/MinecraftServer.html#isLoading()) and [mapping archive](https://maven.fabricmc.net/net/fabricmc/yarn/1.21.4+build.8/yarn-1.21.4+build.8-v2.jar) | `isLoading()` = intermediary `method_3820` = official `ai()`; `loading` = `field_4547` |
| [Official Minecraft 1.21.4 server mapping](https://piston-data.mojang.com/v1/objects/0b1e60cc509cfb0172573ae56b436c29febbc187/server.txt) | `isReady()` maps to `ai()`; `isReady` maps to field `ag` |
| Read-only disassembly of the [official 1.21.4 server bundle](https://piston-data.mojang.com/v1/objects/4707d00eb834b446575d89a61a11b5d548d8c001/server.jar) | `ai()` reads `ag`; `runServer()` stores `true` in that field after the tick/task cycle |

The mapping archive SHA-256 is `a7a042f45ebc0384d5b72c926a435695610380c1a4dab0992fa8dd0aa386f960`; the official server mapping SHA-256 is `dae4a75ad2ca1d52d14e9c080e70e7fbb2888c3533c015ea2d970b2a15856639`. The server bundle SHA-256 is `1066970b09e9c671844572291c4a871cc1ac2b85838bf7004fa0e778e10f1358`, and its embedded server JAR SHA-256 is `de9538d2497c8f4174805f3fcedb8ade3060446aa203f351b820ea5bd04adbf4`, matching the bundle's version manifest. Inspection parsed files and bytecode without starting Minecraft; no game binary is included in the repository.

`GuestConnections.allowed` now requires that readiness value to be true, along with private integrated mode and both stopping flags false. The pure-Java admission matrix covers all 64 combinations through actual session handshakes and queued mutations; rejected states close the session and remove subscriptions. The client GameTests check readiness on the server thread while advancing at most 200 test ticks on the GameTest thread. Fresh, reopened and replacement worlds share this check. Attachment still rechecks the production gate on the server thread. Waiting for `!server.isLoading()` would preserve the defect.

## Native regression corrections

`MaterialCoordinator` contains two bounded incremental-parser arrays totaling 221,888 bytes. Several test functions kept multiple coordinators in one stack frame. An unoptimized MinGW compilation of the original `capacities_and_ownership` fixture reports a 1,114,032-byte frame, exceeding the usual 1 MiB Windows executable stack reservation. This independently exposes a fixture defect consistent with the reported crash; the supplied CTest log does not itself report a stack-overflow status. The tests now own each coordinator on the heap with deterministic destruction and unchanged simultaneous lifetimes, limits and assertions. The coordinator CTest is an in-memory test, distinct from the named-pipe process test.

The allocation-exception fixture must target actual throwing payload copies. Failing every next global allocation can instead hit a Debug STL bookkeeping allocation inside a `noexcept` string constructor. The corrected fixture measures the real long-state character-buffer allocation, rejects ambiguous calibration and verifies that default/move bookkeeping does not use that request size before injecting failures. It preserves four actual exceptions, exact exception-type checks, receiver/projector invalidation, independent source ownership and successful recovery. A Windows rerun remains necessary to validate the MSVC-specific failure outcome; Linux and MinGW results cannot replace it.

Microsoft documents the [default 1 MB executable stack reservation](https://learn.microsoft.com/en-us/cpp/build/reference/stack-stack-allocations?view=msvc-170). The official STL `vs-2022-17.14` source at `1f6e5b16ec02216665624c1e762f3732605cf2b4` exposes the `noexcept` string construction path in [xstring](https://github.com/microsoft/STL/blob/1f6e5b16ec02216665624c1e762f3732605cf2b4/stl/inc/xstring) and its proxy allocation in [xmemory](https://github.com/microsoft/STL/blob/1f6e5b16ec02216665624c1e762f3732605cf2b4/stl/inc/xmemory). This proves the upstream allocation path exists; the supplied evidence does not include the local STL headers or an exception call stack establishing the exact timeout path.

The existing MSVC CI job and strict descendant-retirement gate remain in force. Its earlier `vctip.exe` configure-stage blocker is distinct from the local native CTest failures and from the local wrapper's MSBuild descendants. No timeout, assertion, process-ownership rule or protocol capacity is relaxed.

## Read-only Dying Light file identity

`hc-inspect` recorded three AMD64 PE32+ files as inspected. The original game files were not supplied, so these are identities recorded by the inspection JSON, not independently repeated hashes of game inputs.

| File | Bytes | SHA-256 | Version values; named exports |
| --- | ---: | --- | --- |
| `DyingLightGame.exe` | 1,255,560 | `f3f8a3b0841dcbc41a16a221da9935b6d70738700f5e46600f77f41d993d0835` | 0.9.0.0 and 1.55.0.0; 2 |
| `engine_x64_rwdi.dll` | 11,396,160 | `24c8bc14d65efe282e4cdae678d0230d71167afa37f8611715e5084f9733b361` | 1.0.0.0; 6,817 |
| `gamedll_x64_rwdi.dll` | 31,792,192 | `76f20d27c45c0b807d2f099022ad0abc42ca90a8ade7f761641518af346d26a0` | No version value; 7 |

No allowlist entry follows from these metadata. The archive does not establish Dying Light execution, a verified engine phase, live camera/depth access, native resource registration, collision, damage or TNT behavior.

## Reproduction and remaining acceptance

Use an exact clean revision containing the readiness and fixture corrections. Record that revision, before/after source identities, compiler/JDK versions and complete stage logs through the [existing validation entry points](guest-validation.md). Preserve a failed wrapper result separately if a direct diagnostic command is needed; a diagnostic run must record its own command, source changes and outputs.

The next real-client acceptance is the default private-world `runClientGameTest`: all seven completion records, three newly generated screenshots with visual review, authoritative placement/change/removal, model/pixel sampling, world reopen/replacement and owned peer/worker retirement. Native MSVC validation must first rerun `material-coordinator` and `geometry-exception`, then complete the registered native suite and Java/C++ stages. Server GameTests should be repeated on the corrected revision to confirm the isolated-server refusal alongside state and collision-shape sampling. LAN remains a separate opt-in.

Ordinary GitHub Fabric CI remains compile-only. Current commit-specific CI results are recorded in [PR #2](https://github.com/ranzevor/HarranCraft/pull/2); the observations above retain their original version and execution scope.

## Selected evidence hashes

| Received file | SHA-256 |
| --- | --- |
| `inspection.json` | `7cd7536d969077e4231161a9c6504b2dc6ee2aba10f463a59a60976fca2c8ab0` |
| `gametest/server-gradle-console.log` | `80e6dd30414550b5728b979d7125f29acfc145e1b64ca490cccef05ddb718763` |
| `gametest/server-latest.log` | `6f3cfc922d345df8c2fa8324733c1657cefdeb133e81aacd2491f65c382caa67` |
| `gametest/client-gradle-console.log` | `5d4cd79b67f79dd0f38a32df2479fc1ac615e06a73ec4adc63cf269f4c92b38a` |
| `gametest/client-gradle-diag.log` | `40ec52028fbdf05ef77cba060588dd69024b80a7e54b06bf47dc203b2237d4b5` |
| `gametest/client-latest.log` | `0559d01deca5230fed9642a9636935fa7e757d113477f28b266f5bc09b91caac` |
| `native-ctest-full.log` | `96db1f7a9d7bb389f4cadede788a930ec250bc30c5c660ba0c7650994d3799fb` |
