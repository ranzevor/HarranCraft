# Local guest validation

[简体中文](zh-CN/guest-validation.md) · [Development](development.md) · [Bridge contract](bridge.md)

Run commands from the repository root in an isolated source checkout. Use disposable generated worlds, not a real installation or existing saves. The guest build and GameTests have not yet been executed for this batch; successful protocol tests do not replace them.

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

The bridge command builds C++ tests, compiles the dependency-free Java protocol core, runs core/lifecycle regressions, compares a complete Java/C++ reference frame and starts actual Java/C++ peers against a controlled world. It also runs `hc-pipe-peer` fault scenarios and checks actual worker/process retirement. Linux descriptor counts are measured through `/proc/self/fd`; an unavailable measurement is reported separately. It does not start Minecraft. Run it on both Linux and the target Windows toolchain.

## Fabric compilation

```console
python tools/gradle.py build
```

The default build compiles main, unit-test and GameTest sources and checks the production JAR inventory, but does not enable Minecraft execution. Runtime output is under `guest/build/libs`; the Python wheel remains an inspector-only artifact. Production JAR checks reject game classes and test entrypoints/classes.

## Explicit Minecraft execution

Read the [Minecraft EULA](https://www.minecraft.net/eula) before running game tests. Only an executor accepting those terms should pass the two opt-in properties.

```console
python tools/gradle.py -PrunMinecraftTests=true -PacceptMinecraftEula=true runGameTest
```

Server GameTests check canonical block states and rejection of the isolated GameTest server as a production bridge endpoint. The client test uses a new integrated world and the native peer produced by `test_bridge.py`. With the default Linux single-configuration output:

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

The timeout is the complete invocation's execution budget, including probes and bootstrap; bridge commands also have a 120-second per-stage ceiling. Deadline expiry or interruption cancels only the invocation's owned process tree and performs bounded cleanup. Linux uses an isolated process group and child-subreaper helper; Windows assigns a gated helper to a Job Object before releasing the command. A zero-exit parent with leftover descendants is a failed result, even when cleanup subsequently succeeds. A shared `build/.validation-lock` prevents these entry points from concurrently changing the same build outputs. An existing lock is not removed or stolen; check its owner record and active validation before removing a stale lock.

Every invocation writes a distinct `reports/validation/<run>/result.json` and prints its repository-relative location as JSON. The [version 1 schema](../schemas/validation-v1.schema.json) defines the report. Results distinguish `blocked`, `failed`, `timed_out`, `interrupted`, `passed` and `incomplete`; preflight-only success is `preflight_passed`, with execution stages still `not_run`. Each executed stage retains separate stdout/stderr logs, its actual exit code, elapsed time and cleanup result. Java classes are compiled into a new per-run output directory, not reused from an earlier test run. The native peer location documented above remains unchanged.

Gradle bootstrap, Gradle execution and evidence collection are separate stages. The report retains the task names and outcomes actually emitted by Gradle's plain console, including failed compile or GameTest tasks; detailed dependency, Mixin or runtime failures remain in their stage logs. Project/build-file redirection, additional init scripts/composite builds and dry-run execution are rejected by the validation wrapper. Existing explicit Minecraft opt-ins are required before bootstrap; client runs additionally check the supplied peer file and record its hash. A configured display or accepted command is not a completed game test.

Evidence collection preserves new or changed PNG, XML and log files under `guest/build`, excluding world data, compiled classes and dependency caches. It runs after Gradle failure as well as success. Unchanged screenshots cannot satisfy a new client run; all three expected names, including Fabric counter prefixes, are required. Copied files carry sizes and SHA-256 values; PNG signatures and dimensions are checked, while `visual_review` remains `not_run` until the screenshots are separately inspected. Traversal, source files and evidence have explicit limits; exceeding a limit fails collection rather than silently omitting results. Raw tool logs may contain local paths: inspect them before sharing. Nothing is uploaded automatically, and the report does not mark Minecraft or Dying Light acceptance complete.

Process ownership follows [Python subprocess](https://docs.python.org/3.13/library/subprocess.html), [Windows Job Objects](https://learn.microsoft.com/en-us/windows/win32/procthread/job-objects) and [Linux child subreapers](https://man7.org/linux/man-pages/man2/PR_SET_CHILD_SUBREAPER.2const.html). These controls manage trusted build/test descendants; they are not a sandbox for hostile programs.

### Invocation and completion contract

The validation entry accepts exact root tasks `build`, `check`, `test`, `runGameTest` and `runClientGameTest`, optionally with `clean` and a leading colon. Task abbreviations, other-project task paths, exclusions and dry runs are rejected. A zero exit code is insufficient: every requested root task must appear in the recorded task results. Ordinary tasks may complete by execution, `UP-TO-DATE` or `FROM-CACHE`; requested game tasks require actual execution. Missing or skipped tasks, or any reported `FAILED` task, prevent success. `uncompleted_tasks` and `unexecuted_game_tasks` retain the exact unmet requirements. Evidence-directory read failures remain failures rather than empty successful collections.

Supported Gradle options are `--offline`, `--refresh-dependencies`, `--rerun-tasks`, `--stacktrace`, `--full-stacktrace`, `--info`, `--build-cache`, `--no-build-cache`, `--parallel`, `--no-parallel`, `--continue`, `--no-daemon`, `--console=plain` and `--max-workers=N` (1–9999). Other options, including help/version-only commands, daemon control, build scans, alternate property syntax and option abbreviations, are rejected before preflight. The wrapper fixes non-daemon execution and plain console output.

Project properties use `-Pname=value`; accepted names are `runMinecraftTests`, `acceptMinecraftEula` and `peerExecutable`. Duplicate properties and non-boolean game switches are rejected. Both game switches must be explicitly true to enable game execution. Otherwise the invocation supplies both as false, overriding ambient environment or user-level Gradle property values. The existing peer-file and graphics checks still apply to client tests.

Tool probes retain their stdout/stderr references, exit code, elapsed time, cleanup outcome and original failure details under `tools`. Failed probes, missing prerequisites, timeouts and interruptions keep their distinct outcomes; a successful process with an unrecognized version is a failed probe check. Source-identity timeouts remain `timed_out`. Interrupted or failed preflight does not leave a `running` state in the final report.
