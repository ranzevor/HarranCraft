# 本地验证证据 — 2026-10-17

[English](../validation-20261017.md) · [验证命令](guest-validation.md) · [兼容范围](compatibility.md)

收到的 Windows 证据确认：真实 Minecraft 服务端 GameTest 三项通过；客户端运行在首次桥接接入处失败；另有两项原生 CTest 失败。这些结果尚不构成完整客户端验收或消光兼容批准。

## 证据身份

证据压缩包 SHA-256 为 `80d84966ce2b9e2ce3bd7c42d996dd2bba5a968367abdabc576241f69f6b3baa`。哈希清单中的 19 个独立载荷文件全部匹配，第 20 个文件是清单自身。这验证收到文件的内部一致性；原始日志含本机路径，不作为仓库输入提交。

七份验证 JSON 均记录提交 `b1b6234eb81955cc170cc5e8ce66df5191073ca4`、树 `b21113cc790ddb10c74d968d8ca8e1f7da88de04`、181 个源码文件、`dirty=false`，以及相同的执行前后源码快照；快照 SHA-256 为 `b10fa8903b9e86dc5f1ad85261a8f14f6074f4f41f5cdf6493650d8ac1b2f406`。这些身份属于对应的七次包装入口调用。报告将后续直接 Gradle 运行归于该提交，但游戏控制台日志未独立绑定实际执行树。之后的客户端诊断轮重新编译了修改后的测试源码，未提供补丁及源码快照。

游戏日志记录 Minecraft 1.21.4、Fabric Loader 0.16.14、Fabric API 0.119.4+1.21.4、Loom 1.11.8 及 Java 21，配置的 Yarn 为 1.21.4+build.8。Java 21.0.3 与 21.0.5 补丁版本来自独立预检，不能直接推广到每次游戏运行。

## 实际结果

| 执行 | 已记录结果 | 范围 |
| --- | --- | --- |
| 直接 `runGameTest` | 任务实际执行；3 项必需测试在 1.818 秒内通过；Gradle 成功 | 真实 Minecraft 测试服务端；规范状态、空气／完整方块／半砖碰撞形状、隔离服务端拒绝 |
| 直接 `runClientGameTest` 原始轮 | 在 `GuestConnections.attach` 失败；Gradle 失败 | 客户端、集成世界及渲染资源已启动；首次桥接接入被拒绝 |
| 客户端诊断轮 | 测试侧异常记录 `singleplayer=true`、`dedicated=false`、`remote=false`、`loading=true`、`stopping=false`、`stopped=false` | 独立的修改源码运行；`client-latest.log` 属于此轮 |
| 手工原生 CTest | 14/16 通过；`material-coordinator` SegFault；`geometry-exception` 在 40.04 秒超时 | 报告使用 MSVC 19.44.35228 Debug 构建；不构成完整 MSVC 包装入口验收 |
| 七份包装入口 JSON | 4 份 `preflight_passed`、2 份 JDK 17 `blocked`、1 份桥接 `failed` | 预检通过时正式执行阶段仍为 `not_run` |

服务端控制台记录总数，没有逐项打印测试名称。上述三项范围通过[报告版本的 ServerTests](https://github.com/ranzevor/HarranCraft/blob/b1b6234eb81955cc170cc5e8ce66df5191073ca4/guest/src/gametest/java/io/ranzevor/harrancraft/test/ServerTests.java)唯一服务端入口及其三个测试方法关联。`isolatedServerRefusal` 验证拒绝测试服务器，不证明已成功接入集成式单人服务器。

原始客户端直接失败原因为 Java 异常 `Bridge requires a private integrated server`，最终进程退出码为 `-1073740791`。诊断轮异常来自 `GamePeer`，栈中未显示 `GuestConnections.attach`。诊断状态可用于分析，但不能单凭映射后的方法名确定语义或认定过早接入竞态。

客户端日志没有默认 `HARRANCRAFT_SCENARIO` 完成记录。包内没有截图文件，报告对空场景的描述及哈希不等于独立目视检查。七项客户端场景、放置／移除截图、真实客户端模型／像素采样和 HarranCraft 资源重载撤销均未完成验收。

唯一失败的包装入口 JSON 记录 configure 成功，随后 `native-build` 退出 1 并报告 `owned_descendant_retained`，剩余 20 个 `MSBuild.exe` 和 20 个 `conhost.exe`。强制清理通过，所有后代结束；之后 37 个阶段未运行。叙述报告引用的三份 Fabric 执行失败 JSON 未随包提供，不能据此独立确认相关 Gradle 失败、所有路径不可写、确切原生崩溃码或 CPU 采样结论。

随包服务端 EULA 文件为 `false`，控制台则记录 `acceptGameTestEula`、EULA 文件加载失败及随后测试服务器运行；客户端文件为 `true`。这些是独立观察，尚不证明 Loom 输出路径错误，也不改变仓库的游戏执行／EULA 显式开关。直接 Gradle 成功本身不满足包装入口的源码、进程回收、证据和任务完成门禁。

## 集成服务器就绪语义修正

固定 Yarn 的 `MinecraftServer.isLoading()` 实际返回 Mojang 的**就绪**标志。旧生产条件对此取反，因而会拒绝已经就绪的私有服务器。

| 来源 | 已核实的映射或行为 |
| --- | --- |
| [Yarn 1.21.4+build.8 Javadoc](https://maven.fabricmc.net/docs/yarn-1.21.4%2Bbuild.8/net/minecraft/server/MinecraftServer.html#isLoading()) 与[映射归档](https://maven.fabricmc.net/net/fabricmc/yarn/1.21.4+build.8/yarn-1.21.4+build.8-v2.jar) | `isLoading()` = intermediary `method_3820` = official `ai()`；`loading` = `field_4547` |
| [Minecraft 1.21.4 官方服务端映射](https://piston-data.mojang.com/v1/objects/0b1e60cc509cfb0172573ae56b436c29febbc187/server.txt) | `isReady()` 映射至 `ai()`；`isReady` 映射至字段 `ag` |
| [1.21.4 官方服务端包](https://piston-data.mojang.com/v1/objects/4707d00eb834b446575d89a61a11b5d548d8c001/server.jar)的只读字节码检查 | `ai()` 读取 `ag`；`runServer()` 在 tick／任务周期后将该字段置为 `true` |

映射归档 SHA-256 为 `a7a042f45ebc0384d5b72c926a435695610380c1a4dab0992fa8dd0aa386f960`；官方服务端映射为 `dae4a75ad2ca1d52d14e9c080e70e7fbb2888c3533c015ea2d970b2a15856639`。服务端包 SHA-256 为 `1066970b09e9c671844572291c4a871cc1ac2b85838bf7004fa0e778e10f1358`，其中实际服务端 JAR 为 `de9538d2497c8f4174805f3fcedb8ade3060446aa203f351b820ea5bd04adbf4`，与包内版本清单相符。检查仅解析文件和字节码，未启动 Minecraft；仓库不包含游戏二进制。

`GuestConnections.allowed` 现要求就绪值为 true，同时保留私有集成模式及两个停止标志为 false 的条件。纯 Java 门禁矩阵通过真实会话握手和排队修改覆盖全部 64 种组合；被拒绝状态必须关闭会话并清除订阅。客户端 GameTest 在服务端线程查询状态，在 GameTest 驱动线程最多推进 200 tick；新建、重开和替换世界共用该检查。实际接入仍在服务端线程重新执行生产门禁。等待 `!server.isLoading()` 会保留原缺陷。

## 原生回归修正

`MaterialCoordinator` 内含合计 221,888 字节的两个有界分片解析数组。多个测试函数在同一栈帧同时保留多份协调器；原始 `capacities_and_ownership` 测试在无优化 MinGW 编译下的栈帧为 1,114,032 字节，超过 Windows 可执行文件常用的 1 MiB 栈保留量。这独立暴露了与报告崩溃相符的测试缺陷，但收到的 CTest 日志本身未记录栈溢出状态。测试改为在堆上显式拥有各协调器，保留确定析构、原有同时存活范围、限制及断言。协调器 CTest 是内存测试，与命名管道进程测试不同。

分配异常测试必须命中实际可抛出的载荷复制。对全局下一次分配无条件注入，可能先命中 Debug STL 在 `noexcept` 字符串构造函数中的簿记分配。修正后的测试测量实际长状态字符串缓冲区的分配请求，拒绝歧义校准，并在注入前确认默认／移动构造的簿记不使用该请求大小；继续实际触发四次异常，保留精确类型、接收器／投影器失效、独立来源归属及成功恢复检查。MSVC 特有失败结果仍须由 Windows 复验确认，不能由 Linux 或 MinGW 结果替代。

Microsoft 明确记录了[可执行文件默认 1 MB 栈保留量](https://learn.microsoft.com/en-us/cpp/build/reference/stack-stack-allocations?view=msvc-170)。官方 STL `vs-2022-17.14` 对应 `1f6e5b16ec02216665624c1e762f3732605cf2b4`，其 [xstring](https://github.com/microsoft/STL/blob/1f6e5b16ec02216665624c1e762f3732605cf2b4/stl/inc/xstring)包含 `noexcept` 字符串构造路径，[xmemory](https://github.com/microsoft/STL/blob/1f6e5b16ec02216665624c1e762f3732605cf2b4/stl/inc/xmemory)包含对应代理分配。这证明上游存在该分配路径；收到的证据没有本地 STL 头文件或能够确认具体超时路径的异常调用栈。

既有 MSVC CI 作业与严格后代进程回收门禁继续生效。此前配置阶段残留 `vctip.exe` 的阻塞，与本地原生 CTest 失败及本地包装入口的 MSBuild 后代分别记录。超时、断言、进程所有权规则和协议容量均未放宽。

## 消光文件只读身份

`hc-inspect` 将三个文件记录为 AMD64 PE32+，状态均为 inspected。原始游戏文件未提供，以下是检查 JSON 记录的身份，未独立重新读取游戏文件计算哈希。

| 文件 | 字节数 | SHA-256 | 版本值；命名导出数 |
| --- | ---: | --- | --- |
| `DyingLightGame.exe` | 1,255,560 | `f3f8a3b0841dcbc41a16a221da9935b6d70738700f5e46600f77f41d993d0835` | 0.9.0.0 和 1.55.0.0；2 |
| `engine_x64_rwdi.dll` | 11,396,160 | `24c8bc14d65efe282e4cdae678d0230d71167afa37f8611715e5084f9733b361` | 1.0.0.0；6,817 |
| `gamedll_x64_rwdi.dll` | 31,792,192 | `76f20d27c45c0b807d2f099022ad0abc42ca90a8ade7f761641518af346d26a0` | 无版本值；7 |

这些元数据不产生允许列表条目。证据包未证明消光实际运行、已验证引擎阶段、实时相机／深度读取、原生资源注册、碰撞、伤害或 TNT 行为。

## 复验与剩余验收

使用包含就绪和测试修正的确切干净提交，通过[既有验证入口](guest-validation.md)记录提交、执行前后源码身份、编译器／JDK 版本及完整阶段日志。如需直接诊断命令，应单独保留包装入口失败结果，并为诊断轮记录实际命令、源码变化及输出。

下一项真实客户端验收是默认私有世界 `runClientGameTest`：七项完成记录、三张本轮新图及目视检查、权威放置／变化／移除、模型／像素采样、世界重开／替换，以及自有对端和工作线程回收。原生 MSVC 先复验 `material-coordinator` 与 `geometry-exception`，再完成已注册原生套件和 Java／C++ 阶段。服务端 GameTest 也应在修正提交上重跑，确认隔离服务端仍被拒绝，并保留状态和碰撞形状采样结果。LAN 仍需单独显式启用。

普通 GitHub Fabric CI 继续仅编译。当前提交的具体 CI 证据记录于 [PR #2](https://github.com/ranzevor/HarranCraft/pull/2)，以上历史观察保留原有版本和执行范围。

## 关键证据哈希

| 收到的文件 | SHA-256 |
| --- | --- |
| `inspection.json` | `7cd7536d969077e4231161a9c6504b2dc6ee2aba10f463a59a60976fca2c8ab0` |
| `gametest/server-gradle-console.log` | `80e6dd30414550b5728b979d7125f29acfc145e1b64ca490cccef05ddb718763` |
| `gametest/server-latest.log` | `6f3cfc922d345df8c2fa8324733c1657cefdeb133e81aacd2491f65c382caa67` |
| `gametest/client-gradle-console.log` | `5d4cd79b67f79dd0f38a32df2479fc1ac615e06a73ec4adc63cf269f4c92b38a` |
| `gametest/client-gradle-diag.log` | `40ec52028fbdf05ef77cba060588dd69024b80a7e54b06bf47dc203b2237d4b5` |
| `gametest/client-latest.log` | `0559d01deca5230fed9642a9636935fa7e757d113477f28b266f5bc09b91caac` |
| `native-ctest-full.log` | `96db1f7a9d7bb389f4cadede788a930ec250bc30c5c660ba0c7650994d3799fb` |
