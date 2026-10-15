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
