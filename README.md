# HarranCraft

**Minecraft × Dying Light 1 — two runtimes, one world.**

[简体中文](README.zh-CN.md) · [Documentation](docs/README.md) · [Roadmap](docs/roadmap.md) · [Contributing](CONTRIBUTING.md)

HarranCraft is an experimental integration of a real Minecraft runtime with Dying Light 1. The goal is to build and remove Minecraft blocks in Harran while keeping the host world, its enemies and its gameplay meaningful: correct scene occlusion, native block collision, two-way damage, TNT interactions and reliable restoration when the bridge stops.

The initial game target is **Dying Light 1, Windows x64, single-player**. Dying Light 2, The Beast, co-op and invasions are outside this target.

## What works now

The repository provides **`hc-inspect`**, a read-only development tool for identifying Windows program files. It runs on Windows and Linux with Python 3.12 or newer, without runtime packages beyond Python's standard library.

| Capability | Current state |
| --- | --- |
| SHA-256, AMD64 PE32+ headers and section metadata | Implemented |
| Named, ordinal-only and forwarded exports | Implemented |
| Fixed file-version resource extraction | Implemented |
| English/简体中文 CLI and versioned JSON reports | Implemented |
| Malformed-input, bounded-memory and packaged-CLI tests | Included in the verification suite |
| Java/C++ bridge protocol and private-pipe transport | Controlled-world and actual cross-process tests pass |
| Fabric block reads, conditional writes and change observation | Source implemented; Ubuntu Fabric build and production JAR checks passed; in-game tests not run |
| Windows host image probe | Read-only Windows x64 EXE/loaded-DLL fingerprints, controlled ABI/process tests; no game loading |
| Real Minecraft collision-shape snapshots | Authoritative Fabric VoxelShape read API, epoch and subscription gating; GameTest source compiled, native host collision untested |
| Collision geometry transport | Java/C++ codec, bounded guest-side async pipe, native stream receiver and staged AABBs; not connected to Dying Light physics |
| Client baked-model observation | Real static Minecraft BakedModel quads, atlas IDs, packed vertices and camera pose with logical frame keys; GameTest source, no game execution |
| Render model stream | Java/C++ record v1, bounded client-side async publisher, native incremental receiver and candidate model lifetime tests; no Dying Light renderer |
| Loaded sprite materials | Client NativeImage pixels, bounded ARGB records and async dual-stream batches; native five-part identity association tolerates cross-channel reordering and activates only complete materials; no GPU atlas or host texture upload |
| H1 host observation core | C++20 composite EXE/DLL build identity, session and camera contracts; isolated CTest, no game integration |
| Camera and depth preparation | Versioned dual-camera calibration and capture ownership; Windows D3D11 read-only depth-view preparation with bounded source references and a registered WARP regression; no live game camera/depth association |
| Dying Light host adapter | Not implemented |
| In-game occlusion, native block collision, damage and TNT | Not implemented or game-tested |
| Playable distribution or installer | Not available |

The inspector identifies file contents. It does **not** load a DLL, hook a game, install a mod, alter a save, or certify a game build as compatible. The [full roadmap](docs/roadmap.md) retains the complete dual-runtime target.

H2-A includes a Fabric embedding API for caller-owned private pipes, loaded-position reads, conditional block replacement and subscribed change events. It does not yet include a user launcher or a Dying Light endpoint. [Bridge contract](docs/bridge.md) · [Local validation](docs/guest-validation.md)

## Try the inspector

Use a Python environment with Python 3.12+. From a source checkout:

```console
python -m pip install .
hc-inspect --help
hc-inspect --json DyingLightGame.exe engine_x64_rwdi.dll gamedll_x64_rwdi.dll
```

Pass explicit file locations from an authorized copy; the command does not discover game installations. Paths are used only to open inputs and are omitted from the report. `input_index` identifies each file by its position in the command.

```console
hc-inspect --language zh-CN --json --exports engine_x64_rwdi.dll
```

`--exports` includes export names, ordinals, RVAs and forwarder strings. The default report contains metadata and counts. Output goes to the terminal; use shell redirection to save a report. Maximum input: 16 regular files, 512 MiB each. See [CLI and JSON contracts](docs/inspector.md) for errors, resource limits and interpretation.

No game is needed to build the tools or run the generated-fixture tests. Real game integration tests will require legitimate copies of both games and an authorized Windows graphics session.

## Development

```console
python -m pip install -r requirements-dev.txt
python tools/verify.py
python tools/package.py
```

Verification checks Python syntax/lint/format, repository files, Markdown links, language catalogs, report schema, unit tests and actual CLI subprocesses. Packaging builds an sdist and wheel, then installs the wheel into an isolated temporary directory and exercises its CLI. The wheel contains the inspector, not a game mod.

[CI](https://github.com/ranzevor/HarranCraft/actions/workflows/ci.yml) runs the same checks on Ubuntu and Windows for pull requests and manual runs. Results identify their source commit; they are not in-game compatibility evidence. [Development guide](docs/development.md) · [Compatibility](docs/compatibility.md) · [Validation model](docs/testing.md)

With JDK 21, CMake 3.25+ and a C++20 compiler already installed, run the game-free Java/C++ checks:

```console
python tools/test_bridge.py
```

The configured Fabric toolchain and explicit local build/GameTest commands are in [Local validation](docs/guest-validation.md). CI compiles Fabric and GameTest sources and validates the production JAR without running Minecraft. Game tests remain disabled by default and are not executed by CI.

## Integration design

The implemented guest bridge exchanges versioned block commands and events with a native test peer. The planned host adapter extends this boundary to original-world interaction, input and gameplay events. Rendering and physical interaction are separate requirements: an aligned image is not depth occlusion, and a guest-side collider is not a native obstacle.

A host mutation requires an identified build, a positively recognized single-player session and a verified engine-safe execution phase. Unavailable or unknown states must reject mutation. Camera/depth correspondence, collision ownership and two-way damage must be tested against the original game before a playable release.

The [architecture](docs/architecture.md) records ownership, recovery and protocol constraints. [Host research](docs/host-research.md) preserves the upstream evidence and the specific experiments that remain open. Garry's Mod is a reference project's guest, not a HarranCraft runtime dependency.

## Repository guide

| Location | Purpose |
| --- | --- |
| `src/harrancraft_tools/` | Read-only inspector and language resources |
| `guest/` | Fabric endpoint, protocol core and isolated GameTest sources |
| `include/harrancraft/wire.hpp` | Native wire-layout authority |
| `include/harrancraft/host_probe.h` | Windows host identity C ABI |
| `native/host/` | Windows identity probe and controlled D3D11 depth attachment sources |
| `native/tests/` | C++ contract tests and private-pipe test peer |
| `schemas/` | Machine-readable report contract |
| `tests/` | Generated input fixtures and regression tests |
| `tools/` | Repository, build and packaged-CLI verification |
| `docs/` | Architecture, development, compatibility, research and evidence |
| `.github/` | CI, contribution templates and code ownership |

## Contribute

English and Simplified Chinese reports and discussions are welcome. Start with [CONTRIBUTING.md](CONTRIBUTING.md), follow the [engineering contract](AGENTS.md), and open an issue for a defect, a concrete feature, or an experimental result. Include the exact revision and distinguish simulated, process-level and real-game observations.

[Support](SUPPORT.md) · [Security reporting](SECURITY.md) · [Code of conduct](CODE_OF_CONDUCT.md) · [Changelog](CHANGELOG.md)

## License

Project code and documentation are under the [MIT License](LICENSE). [Third-party notices](THIRD_PARTY_NOTICES.md) distinguish development tooling from research references. Game binaries, assets, proprietary SDKs and saves are not distributed by this project. Game rights remain with their respective holders.
