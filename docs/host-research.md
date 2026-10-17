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

## Camera and scene-depth evidence

At the fixed reference revision, [`ViewFromCombined`](https://github.com/GooseMcGee/DyingLight-GMod/blob/e53209d25c295550e40b8fa977cc36147567b7de/dl_plugin/src/engine.cpp#L183-L235) tries matrix multiplication/transposition arrangements and accepts a transform close to the camera view. Its own comments distinguish the camera available at Present from the view used for the completed frame. [`OnFrame`](https://github.com/GooseMcGee/DyingLight-GMod/blob/e53209d25c295550e40b8fa977cc36147567b7de/dl_plugin/src/main.cpp#L1378-L1427) reads the camera and retains a Present fallback for world work. These are implementation observations, not evidence that HarranCraft has a verified frame association or mutation phase.

The reference compositor [disables depth testing](https://github.com/GooseMcGee/DyingLight-GMod/blob/e53209d25c295550e40b8fa977cc36147567b7de/dl_plugin/src/overlay.cpp#L186-L188) and [binds no depth-stencil view](https://github.com/GooseMcGee/DyingLight-GMod/blob/e53209d25c295550e40b8fa977cc36147567b7de/dl_plugin/src/overlay.cpp#L485-L503). It therefore does not supply the host scene-depth comparison required for shared-world occlusion.

The independent [D3D11 attachment contract](d3d11-depth.md) prepares a read-only view of a caller-identified scene depth resource, validates current bindings against H1/P2 identities and releases its own references. Its WARP regression uses project-created resources and shaders, with no reference-project code or game execution. Before using real host depth, the adapter must establish the exact build, scene pass, camera projection including jitter, context thread and resource lifetime. Matching COM identity proves an object association, not which frame last wrote its contents.

### 相机与场景深度依据

上述固定版本的 `ViewFromCombined` 尝试不同矩阵乘法／转置组合，再选择与相机 view 接近的变换；源码自身也区分 Present 时取得的相机与已完成画面使用的 view。`OnFrame` 读取相机，并保留 Present 中执行 world work 的回退。这些只能证明参考实现采取了该路径，不能证明 HarranCraft 已建立可靠帧关联或安全修改阶段。

参考合成器关闭深度测试，并在绑定颜色目标时传入空 DSV，因此未提供共享世界遮挡所需的宿主场景深度比较。[独立 D3D11 附件契约](zh-CN/d3d11-depth.md)为调用方已识别的场景深度准备只读视图，将当前绑定与 H1／P2 身份核对，并释放自有引用。WARP 回归采用项目自建资源和着色器，不运行参考工程或游戏。使用真实宿主深度前，仍须证明完整构建、场景 pass、包含 jitter 的相机投影、context 线程及资源生命周期；COM 身份相同不证明内容由哪一帧写入。
