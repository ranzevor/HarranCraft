# Compatibility

[简体中文](zh-CN/compatibility.md) · [Documentation](README.md)

| Component | Target | Evidence required / current state |
| --- | --- | --- |
| Python inspector | Python 3.12+, Windows x64 and Linux x86_64 | Automated parser, CLI and package verification; matrix in CI |
| Inspected file format | AMD64 PE32+, EXE or DLL | Bounded metadata subset specified by the inspector contract |
| Windows host fingerprint and build gate | Windows x64, C++20 | Read-only EXE/loaded-DLL hashing and isolated composite identity/epoch tests; no accepted Dying Light build |
| Dying Light host | Dying Light **1**, Windows x64, single-player | Not implemented; no validated supported game build |
| Minecraft guest | Real Minecraft through Fabric | Ubuntu Fabric source compilation, remapping and production JAR checks passed; in-game GameTests unrun |
| Client baked-model reader and render wire | Minecraft 1.21.4, pinned Yarn/Fabric, Java 21/C++20 | Static block-model quads plus bounded versioned binary codec; real game execution and host frame correlation unverified |
| Loaded source sprite materials | Minecraft 1.21.4 SpriteContents/NativeImage, Java 21/C++20 | Bounded static source pixels (≤64×64, ≤16 sprites/model), client reload listener, async batch and native CPU material cache; in-game reload/Mixin, final atlas and host GPU upload unverified |
| Controlled dual-camera projection | C++20, explicit version 1 camera/calibration inputs | [Bounded current-capture projection](camera-projection.md), analytic values and live receiver lifecycle regressions; actual game camera/depth acquisition and synchronization unverified |
| Bridge core and native peer | JDK 21, C++20, Linux x86_64 and Windows x64 | Strict compilation, reference-frame and real process checks run independently on Linux/GCC and Windows/MinGW; revision-specific outcomes are recorded in [PR #2](https://github.com/ranzevor/HarranCraft/pull/2); MSVC remains unverified |
| Other Dying Light titles | Second game / The Beast | Outside the initial target |
| Network game modes | Co-op / invasions / other multiplayer | Not supported; future host must reject them in code |

The upstream GModLight research identifies a Steam executable version 1.55.0.0 and build 24076292. That is a research input, not this project's supported-version list. [Host research](host-research.md)

The inspector may read a game's binary metadata on Linux without running the game. A parsed file version, matching export name, successful build or clean fixture test does not establish live game compatibility.

The table changes when there is project evidence for a specific component and environment. A playable release requires recorded depth/camera, native collision, ordinary damage, mode-transition and cleanup results with exact game build identities.

## Configured guest toolchain

Minecraft **1.21.4**, Yarn **1.21.4+build.8**, Fabric Loader **0.16.14**, Fabric API **0.119.4+1.21.4**, Loom **1.11.8**, Gradle **8.14.3**, JDK **21**. These configured versions resolved and compiled successfully on Ubuntu with JDK 21 in [CI #38892925451](https://github.com/ranzevor/HarranCraft/actions/runs/38892925451). Actual Fabric game execution, including Mod/Mixin loading, remains unverified. Native tests require CMake **3.25+** and a C++20 compiler. [Local validation](guest-validation.md)
