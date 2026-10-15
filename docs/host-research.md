# Dying Light 1 interface research

[Documentation](README.md)

Reference source: GModLight at `e53209d25c295550e40b8fa977cc36147567b7de`. Its [engine analysis](https://github.com/GooseMcGee/DyingLight-GMod/blob/e53209d25c295550e40b8fa977cc36147567b7de/docs/ENGINE_ANALYSIS.md) identifies Steam DyingLightGame.exe 1.55.0.0, build 24076292. GModLight integrates Garry's Mod, not Minecraft. Its MIT source is research evidence; no reference implementation is included in the current inspector.

| Interface | Evidence and remaining experiment |
| --- | --- |
| Loading and camera | Engine exports and [camera code](https://github.com/GooseMcGee/DyingLight-GMod/blob/e53209d25c295550e40b8fa977cc36147567b7de/dl_plugin/src/engine.cpp#L97-L145) provide candidate names. Exact accepted binaries and callback lifetimes remain to be established. |
| Update phase | The analysis retracts the TimerUpdate-per-frame claim in round 7. [Current message-pump code](https://github.com/GooseMcGee/DyingLight-GMod/blob/e53209d25c295550e40b8fa977cc36147567b7de/dl_plugin/src/main.cpp#L572-L616) is time-throttled, not proof of an engine-safe phase. |
| Scene occlusion | [The compositor](https://github.com/GooseMcGee/DyingLight-GMod/blob/e53209d25c295550e40b8fa977cc36147567b7de/dl_plugin/src/overlay.cpp#L80-L210) has guest distance reprojection but no host-depth comparison. Identify real scene depth/camera/pass correspondence or native geometry. |
| World query / physics | Ray queries and [stock-mesh creation](https://github.com/GooseMcGee/DyingLight-GMod/blob/e53209d25c295550e40b8fa977cc36147567b7de/dl_plugin/src/engine.cpp#L1371-L1420) do not establish arbitrary block colliders. Trace shape ownership, registration, filtering and deletion with failure rollback. |
| Damage | [Descriptor and virtual calls](https://github.com/GooseMcGee/DyingLight-GMod/blob/e53209d25c295550e40b8fa977cc36147567b7de/dl_plugin/src/engine.cpp#L590-L680) depend on fixed game data. Ordinary first-hit damage, attribution and reverse settlement need independent evidence. |
| Mode and restoration | [The plan](https://github.com/GooseMcGee/DyingLight-GMod/blob/e53209d25c295550e40b8fa977cc36147567b7de/docs/PLAN.md) describes co-op refusal as unfinished. Positive session classification and restoration must be implemented rather than delegated to private-visibility instructions. |

The inspector can collect file identities and exports without game execution. It does not solve calling conventions, safe threads, depth resource ownership, collision registration or gameplay authority. Those questions remain concrete experiments, while independent guest and protocol work may proceed.

Further implementation research: [SkyCraft](https://github.com/chasmlol/SkyCraft) for Fabric/geometry and [universal-modder](https://github.com/rehan-remade/universal-modder) for Minecraft pixel bridging. Their source revisions, dependencies and licenses must be examined before actual reuse; no version or compatibility assumption is adopted here.
