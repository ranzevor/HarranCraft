# Compatibility

[简体中文](zh-CN/compatibility.md) · [Documentation](README.md)

| Component | Target | Evidence required / current state |
| --- | --- | --- |
| Python inspector | Python 3.12+, Windows x64 and Linux x86_64 | Automated parser, CLI and package verification; matrix in CI |
| Inspected file format | AMD64 PE32+, EXE or DLL | Bounded metadata subset specified by the inspector contract |
| Windows host self-image probe | Windows x64, C++20 | Read-only DLL source and controlled Windows process/ABI tests; does not validate Dying Light |
| Dying Light host | Dying Light **1**, Windows x64, single-player | Not implemented; no validated supported game build |
| Minecraft guest | Real Minecraft through Fabric | Ubuntu Fabric source compilation, remapping and production JAR checks passed; in-game GameTests unrun |
| Bridge core and native peer | JDK 21, C++20, Linux x86_64 and Windows x64 | Strict compilation, reference-frame and real process tests pass on Linux/GCC and Windows/MinGW; MSVC remains unverified |
| Other Dying Light titles | Second game / The Beast | Outside the initial target |
| Network game modes | Co-op / invasions / other multiplayer | Not supported; future host must reject them in code |

The upstream GModLight research identifies a Steam executable version 1.55.0.0 and build 24076292. That is a research input, not this project's supported-version list. [Host research](host-research.md)

The inspector may read a game's binary metadata on Linux without running the game. A parsed file version, matching export name, successful build or clean fixture test does not establish live game compatibility.

The table changes when there is project evidence for a specific component and environment. A playable release requires recorded depth/camera, native collision, ordinary damage, mode-transition and cleanup results with exact game build identities.

## Configured guest toolchain

Minecraft **1.21.4**, Yarn **1.21.4+build.8**, Fabric Loader **0.16.14**, Fabric API **0.119.4+1.21.4**, Loom **1.11.8**, Gradle **8.14.3**, JDK **21**. These configured versions resolved and compiled successfully on Ubuntu with JDK 21 in [CI #38892925451](https://github.com/ranzevor/HarranCraft/actions/runs/38892925451). Actual Fabric game execution, including Mod/Mixin loading, remains unverified. Native tests require CMake **3.25+** and a C++20 compiler. [Local validation](guest-validation.md)
