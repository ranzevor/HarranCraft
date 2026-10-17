# Changelog

## Unreleased

### Added

- Native cross-channel material coordination with exact capture identities, bounded out-of-order assembly, complete-manifest activation, superseded-capture draining, absolute deadlines and independent Java/C++ OS-pipe regressions.
- Bounded multi-sprite material batch publisher, client-resource reload cancellation, and native CPU texture cache with complete-manifest gates, incremental source-pixel reception and Java/C++ process tests.
- Client-only loaded SpriteContents NativeImage pixel sampling with atlas UV/placement metadata, bounded ARGB Java/C++ record v1, golden-byte and native process regressions; no game textures committed.
- Bounded client render publisher and owner-thread-only native stream receiver, with fail-closed camera/frame/capture identity checks, backpressure, stale-model retirement and process/worker regressions.
- Version 1 Java/C++ render-model records with bounded quads, atlas/sprite identifiers, packed vertex attributes, camera/frame identities, strict decoding and complete cross-process binary checks.
- Client-only Minecraft baked static-block model sampler with retained quad vertices, UV, sprite identities and camera pose; bounded immutable render frames, source-thread/lifetime gates and pure-Java/GameTest source regressions.
- Bounded native geometry stream receiver with incremental framing, strict session/revision refusal, EOF cleanup and real Java/C++ pipe integration regressions.
- Session-bound, caller-owned asynchronous guest collision geometry pipe with bounded output, initial GET and subscribed-change publication, cancellation and resource-retirement tests.
- Separate version 1 Java/C++ collision geometry records with bounded double-precision AABBs, strict decode, golden-byte comparison and native process tests; preserve the 512-byte block protocol.
- Real Minecraft collision VoxelShape sampling for subscribed blocks, bounded immutable AABBs, session/epoch checks and GameTest source coverage.
- Bounded C++20 host EXE/DLL composite build allowlisting with camera/session revocation, epoch recovery and native/Windows-process regressions.
- Read-only Windows x64 host-image probe DLL with versioned C ABI, bounded CNG SHA-256 of the current executable and already-loaded DLLs, strict PE validation and native/Python cross-checks.
- C++20 H1 read-only host observation core with explicit build/mode/read-phase gates, session and camera identities, clip-space coordinate conversion and isolated native CTest.
- Client GameTest sources for reconnect, world shutdown/reopen/replacement, thread ownership and chest-data preservation; isolated LAN revocation/recovery requires a separate explicit opt-in.
- Native observation-peer regressions and per-scenario client completion evidence in local validation reports.
- Local validation preflight, source fingerprints, invocation deadlines, owned-process cleanup and versioned per-stage reports in the existing bridge and Gradle entry points. New or changed game-test evidence is collected without reusing unchanged screenshots.
- Read-only AMD64 PE32+ inspector with streaming SHA-256, section and export metadata, and fixed file versions.
- Versioned JSON report schema, input-index reporting, bounded malformed-input handling and English/简体中文 CLI resources.
- Generated PE regressions, CLI subprocess checks and isolated wheel-install verification.
- Bilingual project entry points and development documentation, engineering/contribution policies, issue/PR templates and Windows/Linux CI.
- Fabric block-adapter source with server-thread access, loaded-position checks, canonical conditional replacement and subscribed chunk-change events.
- Version 1 Java/C++ wire contract, private-pipe transport, bounded session ordering, deduplicated receipts, epochs and disconnect recovery.
- Strict native/Java tests, real cross-process tests using a controlled world, lifecycle regressions and opt-in local server/client GameTest sources.
- Checksum-pinned Gradle bootstrap with bounded extraction, cache preservation and exclusive installation; source-package exclusions for generated game and toolchain data.
- Bilingual bridge contracts, guest validation, toolchain status and H2 acceptance sequence.

### Changed

- Windows native flood tests use synchronous Win32 writes and exact broken-pipe errors to acknowledge receiver closure without accepting generic CRT errors.
- Native pipe fault tests acknowledge output closure and complete bounded diagnostics before owned-process termination, preserving original evidence failures and strict cleanup checks across cancellation races.
- Material publishers immediately invalidate both transports and unregister on either asynchronous output failure, with one-time cancellation, preserved cleanup errors and silent-owner/concurrent-failure regressions.
- Validation preserves probe timeout/interruption and diagnostic outcomes, requires completion of requested Gradle tasks even after exit code zero, rejects ambiguous invocation options and fixes game-test opt-ins per invocation.
- Private-pipe shutdown supports explicit owned-endpoint cancellation, preserves I/O and cleanup failures, and exposes worker termination separately from connection invalidation. Queue handoff rejects reads completing after close. Native fault tests cover saturation, broken and partial pipes, concurrent close and repeated resource retirement.
- Repository text checks cover Java, C++, Gradle and properties files; generated caches are pruned before traversal. CI Action and permission restrictions remain unchanged.
- Failed welcomes, replayed receipts and events close the session. Adapter exceptions invalidate it; input and output stream closure are independent. Chunk state reads occur only after thread, mode and subscription checks. Default Fabric builds do not execute Minecraft.

### Validation

[CI #39016955612](https://github.com/ranzevor/HarranCraft/actions/runs/39016955612) at `c4888ff3f3a05263c069854188b623637277e50b` passed both Python jobs, Ubuntu Java/C++ validation and the Ubuntu/JDK 21 Fabric build. The Windows/MinGW job passed the native, material pipeline and cross-channel process stages, then failed the existing pipe input-saturation evidence check; its observation stage did not run. MSVC configuration returned zero but failed the owned-descendant gate with `vctip.exe` retained, preventing subsequent build and test stages. These are separate job outcomes, not a complete CI pass. Current revision evidence is maintained in [PR #2](https://github.com/ranzevor/HarranCraft/pull/2).

Fabric compilation includes production and GameTest sources, Mod JAR remapping and production JAR verification. Minecraft GameTests, live Mod/Mixin loading, Windows Fabric builds and MSVC-specific native acceptance remain unrun or incomplete. No Dying Light host adapter or playable release is included.

[CI #39018668887](https://github.com/ranzevor/HarranCraft/actions/runs/39018668887) at `f1a50589b8ad783d15e213261d8175f25e23c0a7` retained the precise MinGW failure: the native flood's CRT flush returned `errno=22`, with missing terminal evidence and native exit 1 also reported. Both Python jobs, Ubuntu bridge validation and Fabric compilation passed; MSVC retained the same configure-stage blocker. This evidence identifies that run's CRT failure without retroactively assigning a cause to the earlier run's missing exception.

## 简体中文

### 未发布变更

- Windows 原生持续写入测试采用同步 Win32 写入及确切的断管错误确认接收端关闭，不把通用 CRT 错误视作关闭证据。
- 原生管道故障测试在结束自有进程前确认输出关闭与有界诊断流完成，保留取消竞态中的原始证据异常及严格资源回收检查。
- 新增原生跨通道材质协调：精确采集身份、有界乱序组装、完整清单激活、已替换采集的在途校验、绝对期限及独立 Java／C++ OS 管道回归。
- 材质发布器在任一异步输出失败时立即取消双方传输并注销，保留单次取消、清理异常及所属线程静默／并发故障回归。
- 新增有界多精灵材质异步批次、客户端资源重载时取消输出、原生 CPU 源像素完整性缓存与流接收，并补充 Java／C++ 进程回归。
- 新增客户端 SpriteContents／NativeImage 已加载源图像只读采样、图集 UV／坐标、有界 ARGB Java／C++ 协议及跨语言／跨进程回归，不提交游戏贴图。
- 新增有界客户端渲染发布器与原生分片接收器，拒绝错误相机／帧／采集身份，覆盖背压、断连撤销及真实跨进程与线程验收。
- 新增版本 1 Java／C++ 渲染模型记录，包含有界四边形、图集／精灵标识、打包顶点及相机／帧身份，并通过严格解码及完整跨进程二进制验证。
- 新增 Minecraft 客户端静态烘焙模型采样、真实四边形顶点／UV／精灵标识及相机姿态，并完善有界不可变快照、线程／代次门禁和 Java／GameTest 源码回归。
- 新增有界原生几何流接收器、增量分帧、会话／修订号拒绝、EOF 清理及真实 Java／C++ 管道回归。
- 新增与会话绑定的客体异步几何输出、GET／订阅变化发布、容量门禁、取消回收及独立故障测试。
- 新增独立版本 1 Java／C++ 碰撞几何记录、有界双精度包围盒、严格解码、跨语言参考帧及原生进程测试；保留现有 512 字节方块协议。
- 新增 Minecraft 真实 VoxelShape 碰撞几何采样、有界不可变包围盒、会话／代次校验及 GameTest 源码。
- 新增 C++20 宿主 EXE／DLL 组合允许列表、相机／会话撤销、代次恢复及原生／Windows 进程回归。
- 新增 Windows x64 只读宿主身份探测 DLL、版本化 C ABI、主程序及已加载 DLL 的受限 CNG 哈希、严格 PE 检查及原生／Python 对照验证。
- 新增 C++20 H1 只读宿主观察核心：程序构建、模式及读阶段门禁、会话与相机身份、裁剪空间坐标转换及原生隔离 CTest。
- 新增断连重连、世界关闭／重开／替换、线程归属及箱子数据保留的客户端 GameTest 源码；隔离 LAN 撤销与恢复需要单独显式启用。
- 新增原生观察进程回归，并在本地验收报告中核对客户端逐场景完成记录。
- 验收报告保留探测超时、中断及诊断结果；即使退出码为零，也须确认请求的 Gradle 任务完成；拒绝歧义调用选项，并按本次调用固定游戏测试开关。
- 在现有桥接与 Gradle 入口新增本地预检、源码指纹、整次时限、自有进程清理及带版本号的分阶段报告；收集新增或变化的游戏测试证据，不复用未变化的旧截图。
- 私有管道关闭支持显式的自有对端取消，保留 I/O 与清理异常，并分别查询连接失效和工作线程结束。关闭后才完成的读取不再进入队列；原生故障测试覆盖饱和、断管、截断、并发关闭及反复资源回收。
- 新增 Fabric 方块适配源码：服务端线程访问、已加载位置检查、规范状态条件替换及订阅区块变化。
- 新增版本 1 Java／C++ 协议、私有管道、有界命令排序、回执去重、会话代次及断连恢复。
- 新增严格原生／Java 测试、受控世界真实跨进程测试、生命周期回归及明确启用的本地服务端／客户端 GameTest 源码。
- 新增固定校验和的 Gradle 启动器、有界解包、缓存保护及独占安装；源码包排除生成的游戏与工具链数据。
- 仓库文本检查覆盖 Java、C++、Gradle 和属性文件，遍历前排除生成缓存；保留原 CI Action 与权限限制。
- 握手、重复回执或事件发送失败时关闭会话；适配器异常使其失效，输入／输出分别关闭；区块状态读取位于线程、模式和订阅检查之后；默认 Fabric 构建不运行 Minecraft。
- 中英文桥接契约、验证入口、工具链状态及 H2 验收顺序同步维护。

[CI #39016955612](https://github.com/ranzevor/HarranCraft/actions/runs/39016955612) 对应 `c4888ff3f3a05263c069854188b623637277e50b`：两个 Python 作业、Ubuntu Java／C++ 验证和 Ubuntu/JDK 21 Fabric 构建通过。Windows/MinGW 的原生、材质管线和跨通道进程阶段通过，随后既有管道输入饱和场景的证据检查失败，观察阶段未执行。MSVC 配置返回零，但因保留 `vctip.exe` 未通过自有后代进程检查，后续构建和测试未执行。这些是各作业的独立结果，不代表整轮 CI 通过。当前版本证据维护于 [PR #2](https://github.com/ranzevor/HarranCraft/pull/2)。

Fabric 编译覆盖生产与 GameTest 源码、Mod JAR 重映射及生产 JAR 校验。Minecraft GameTest、实际 Mod/Mixin 装载、Windows Fabric 构建及 MSVC 专项验收仍未完成。当前不包含消光宿主适配器或可玩发行版。

[CI #39018668887](https://github.com/ranzevor/HarranCraft/actions/runs/39018668887) 对应 `f1a50589b8ad783d15e213261d8175f25e23c0a7`，保留了确切的 MinGW 失败原因：原生持续写入的 CRT flush 返回 `errno=22`，同时报告缺少诊断终止记录及原生退出码 1。两个 Python 作业、Ubuntu 桥接验证和 Fabric 编译通过；MSVC 仍在配置阶段受阻。该证据确定本轮 CRT 失败，不反推此前未保留原始异常的作业一定具有相同原因。
