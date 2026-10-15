# Compatibility

[简体中文](zh-CN/compatibility.md) · [Documentation](README.md)

| Component | Target | Evidence required / current state |
| --- | --- | --- |
| Python inspector | Python 3.12+, Windows x64 and Linux x86_64 | Automated parser, CLI and package verification; matrix in CI |
| Inspected file format | AMD64 PE32+, EXE or DLL | Bounded metadata subset specified by the inspector contract |
| Dying Light host | Dying Light **1**, Windows x64, single-player | Not implemented; no validated supported game build |
| Minecraft guest | Real Minecraft through Fabric | Not implemented; Minecraft/Fabric/JDK versions not selected |
| Other Dying Light titles | Second game / The Beast | Outside the initial target |
| Network game modes | Co-op / invasions / other multiplayer | Not supported; future host must reject them in code |

The upstream GModLight research identifies a Steam executable version 1.55.0.0 and build 24076292. That is a research input, not this project's supported-version list. [Host research](host-research.md)

The inspector may read a game's binary metadata on Linux without running the game. A parsed file version, matching export name, successful build or clean fixture test does not establish live game compatibility.

The table changes when there is project evidence for a specific component and environment. A playable release requires recorded depth/camera, native collision, ordinary damage, mode-transition and cleanup results with exact game build identities.
