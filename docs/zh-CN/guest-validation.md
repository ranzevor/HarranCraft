# 客体本地验证

[English](../guest-validation.md) · [开发指南](development.md) · [桥接契约](bridge.md)

在隔离源码目录根部执行命令，使用生成的一次性世界，不使用真实安装或已有存档。本批 Fabric 构建及 GameTest 尚未执行；协议测试通过不能替代这两项验收。

## 工具链

不依赖游戏的桥接检查需要 Python 3.12+、JDK 21、CMake 3.25+ 和 C++20 编译器。CMake 对 GCC／Clang 或 MSVC 启用严格警告并视为错误。客体配置固定 Minecraft 1.21.4、Yarn 1.21.4+build.8、Fabric Loader 0.16.14、Fabric API 0.119.4+1.21.4、Loom 1.11.8；这是配置组合，不是已验收兼容矩阵。

`tools/gradle.py` 从官方发行端点下载 Gradle 8.14.3，并校验 SHA-256 `bd71102213493060956ec229d946beee57158dbd89d0e62b91bca0fa2c5f3531`。该入口是自定义启动器，不是官方 Gradle Wrapper。首次 Gradle／Fabric／Minecraft 依赖解析需要联网；仓库不存放依赖压缩包或游戏二进制。

## 仓库与进程测试

```console
python -m pip install -r requirements-dev.txt
python tools/verify.py
python tools/package.py
python tools/test_bridge.py
```

桥接命令构建 C++ 测试、编译无第三方依赖的 Java 协议核心、执行核心和生命周期回归、比对完整跨语言参考帧，并启动真实 Java／C++ 进程访问受控世界；还会运行 `hc-pipe-peer` 故障场景并确认工作线程及进程实际结束。Linux 通过 `/proc/self/fd` 测量文件描述符数量，不支持时单独报告该测量未提供。命令不会启动 Minecraft。PR 的 CI 分别在 Ubuntu（JDK 21／GCC 14）和 Windows Server 2025（JDK 21／MinGW x64 GCC／Ninja）执行真实 Java／C++ 进程测试，每个平台保留独立结果。Windows 作业明确选用原生 MinGW 编译器，MSVC 工具链仍须单独验收。这些作业均不解析 Fabric 依赖、不启动 Minecraft；真实游戏验收仍须独立完成。

## Fabric 编译

```console
python tools/gradle.py build
```

默认构建编译主程序、单元测试及 GameTest 源码，并检查生产 JAR 内容，但不启用 Minecraft 执行。运行产物位于 `guest/build/libs`；Python wheel 仍仅包含核查工具。生产 JAR 检查拒绝游戏类及测试入口／测试类。

## 明确启用 Minecraft 执行

运行游戏测试前阅读 [Minecraft EULA](https://www.minecraft.net/eula)。仅接受相应协议的执行者应传入两个启用参数。

```console
python tools/gradle.py -PrunMinecraftTests=true -PacceptMinecraftEula=true runGameTest
```

显式启用游戏测试时，`build` 和 `check` 生命周期可运行服务端 GameTest；单独执行 `test` 不运行 GameTest。客户端 GameTest 仅在请求 `runClientGameTest` 时运行。服务端 GameTest 检查规范方块状态，并验证隔离 GameTest 服务器不能作为生产桥接端点。客户端测试使用新建嵌入式世界及 `test_bridge.py` 生成的原生测试进程。Linux 默认单配置输出对应：

```console
python tools/gradle.py -PrunMinecraftTests=true -PacceptMinecraftEula=true -PpeerExecutable=../build/bridge/native/hc-bridge-peer runClientGameTest
```

Windows Visual Studio 多配置构建将对端参数改为 `-PpeerExecutable=../build/bridge/native/Debug/hc-bridge-peer.exe`。该路径相对 `guest/` 解析；使用其他 CMake 生成器时以实际输出为准。客户端测试需要可用图形会话，检查放置、服务端权威状态、独立方块变化、移除、旧代次、无效版本及断连清理，使用显式的自有对端取消回调并检查传输实际结束，并捕获空场景／放置后／移除后三张截图。GameTest 世界位于 `guest/build/run`，报告和截图保存在生成的构建输出目录；不得将运行目录指向已有存档。

记录实际退出码、源码提交、JDK／编译器及所选依赖版本，保留失败日志并检查三张截图。真实局域网／模式转换、Windows 执行、完整 Fabric 解析与客户端渲染各自验收，不能从模拟 `allowed=false` 推断通过。上述命令不运行消光游戏。

## 启动器恢复

校验和错误、无效 ZIP 路径、符号链接／特殊条目、缺少启动文件或压缩包超限会在安装生效前失败。不完整暂存会清理，已有无效缓存会保留；重试前仅检查并删除生成的 `build/toolchains/gradle-8.14.3` 缓存。已有 `.install-lock` 不会被抢占；确认没有安装过程运行后再删除失效的生成锁。不得删除无关工具链或安装目录。

## 预检、时限与结果

两个现有入口均接受 `--hc-preflight`、`--hc-timeout SECONDS` 和 `--hc-revision COMMIT`。预检需要 Git 检出目录，记录 HEAD、tree、已跟踪及未忽略文件的 SHA-256 快照，并实际探测已安装 Java／CMake 工具。`--hc-revision` 接受完整 40 位提交，不匹配时拒绝执行。未提交修改会明确记录，不会归为纯提交版本；执行期间源码变化使结果失败。预检不会下载 Gradle、解析 Fabric 或启动游戏。

```console
python tools/test_bridge.py --hc-preflight
python tools/gradle.py --hc-preflight build
python tools/test_bridge.py --hc-timeout 600
python tools/gradle.py --hc-timeout 1800 build
```

时限是包括工具探测和启动器准备在内的整次执行预算；桥接命令另有每阶段 120 秒上限。超时或中断只取消本次持有的进程树，并进行有界清理。Linux 使用独立进程组和负责回收后代的子进程；Windows 先将等待放行的辅助进程加入 Job Object，再放行命令。父进程返回 0 但遗留后代仍判失败，即使后续清理成功。共享的 `build/.validation-lock` 阻止两个入口同时修改相同构建输出；不会删除或抢占已有锁。移除失效锁前须核对其所有者记录和仍在运行的验收。

每次执行生成独立的 `reports/validation/<run>/result.json`，终端以 JSON 返回仓库相对位置。报告契约见[版本 1 Schema](../../schemas/validation-v1.schema.json)。状态区分 `blocked`、`failed`、`timed_out`、`interrupted`、`passed` 和 `incomplete`；仅预检成功为 `preflight_passed`，实际执行阶段仍为 `not_run`。每个已执行阶段分别保留标准输出、标准错误、实际退出码、耗时及清理结果。Java 类编译到本次独立目录，不复用上次测试类；上述原生对端位置不变。

Gradle 启动器准备、Gradle 执行及证据收集分别记为阶段。报告保留 Gradle 普通控制台实际输出的任务名称和结果，包括失败的编译或 GameTest 任务；依赖、Mixin 及运行时的具体错误保留在对应日志中。验收入口拒绝重定向项目／构建文件、附加初始化脚本／复合构建和 dry-run。启动器准备前检查原有 Minecraft 明确启用条件；客户端运行还检查所提供的对端文件并记录哈希。显示配置存在或命令获准不等于游戏测试完成。

证据收集保留 `guest/build` 中新增或变化的 PNG、XML 和日志，排除世界数据、编译类及依赖缓存；Gradle 失败后仍执行收集。未变化的旧截图不能满足本次客户端验收，三种预期名称均须存在，兼容 Fabric 的计数前缀。复制文件记录大小和 SHA-256；PNG 检查签名及尺寸，截图另行实际检查前，`visual_review` 保持 `not_run`。遍历、源码文件和证据均有限额，超限使收集失败，不静默遗漏。原始工具日志可能含本地路径，分享前须检查；入口不会自动上传，也不将 Minecraft 或消光验收标记完成。

进程归属依据 [Python subprocess](https://docs.python.org/3.13/library/subprocess.html)、[Windows Job Objects](https://learn.microsoft.com/en-us/windows/win32/procthread/job-objects) 和 [Linux 子进程回收](https://man7.org/linux/man-pages/man2/PR_SET_CHILD_SUBREAPER.2const.html)。这些机制管理可信构建／测试命令的后代，不构成运行恶意程序的沙箱。

### 调用与完成判定

验收入口接受精确的根项目任务 `build`、`check`、`test`、`runGameTest`、`runClientGameTest`，可组合 `clean` 并使用前导冒号；拒绝任务缩写、其他项目任务路径、排除任务及空跑。退出码为 0 不足以判定通过：每个请求的根任务都必须出现在实际任务结果中。普通任务可通过执行、`UP-TO-DATE` 或 `FROM-CACHE` 完成；游戏任务必须实际执行。任务缺失、跳过或出现任何 `FAILED` 任务均阻止通过。`uncompleted_tasks` 和 `unexecuted_game_tasks` 保留具体未满足项。证据目录读取错误保持为失败，不转换为空集合成功。

支持的 Gradle 选项为 `--offline`、`--refresh-dependencies`、`--rerun-tasks`、`--stacktrace`、`--full-stacktrace`、`--info`、`--build-cache`、`--no-build-cache`、`--parallel`、`--no-parallel`、`--continue`、`--no-daemon`、`--console=plain` 和 `--max-workers=N`（1–9999）。其他选项在预检前拒绝，包括仅输出帮助／版本的命令、守护进程控制、构建扫描、其他属性写法及选项缩写。入口固定非守护进程执行及普通控制台输出。

项目属性采用 `-Pname=value`，仅接受 `runMinecraftTests`、`acceptMinecraftEula`、`peerExecutable`、`runLanTests`；拒绝重复属性及非布尔游戏开关。两个游戏开关必须显式为 true 才允许游戏执行，否则本次调用明确传入两个 false，覆盖环境变量或用户级 Gradle 属性中的隐含值。客户端测试继续执行既有对端文件和图形条件检查。

工具探测在 `tools` 中保留标准输出／错误引用、退出码、耗时、清理结果及原始失败详情。探测失败、条件缺失、超时和中断分别保留对应状态；进程成功但版本无法识别时，探测检查仍判失败。源码身份获取超时保持 `timed_out`；最终报告不会将已中断或失败的预检保留为 `running`。

## 世界生命周期与隔离 LAN 验收

客户端执行还会在一次性世界中运行 `WorldLifecycleTests`：通过三个新连接反复进行原生方块往返；验证测试线程无法取得服务端接入权限，且拒绝时不接管传入流；验证拒绝替换箱子时，原方块实体身份及七颗钻石的物品样本保持不变。关闭世界时保留原生观察连接，由已注册的生命周期回调回收，不由测试提前断开。重新打开本次生成的存档，先检查金块保存结果再继续修改；重开和替换世界都必须拒绝旧代次并完成新连接的方块往返。不打开已有存档。

原生 `observe` 场景订阅一个已加载位置，接收独立来源的变化，并等待客体关闭流。`ObservationTests` 以真实进程验证该对端，包括错误代次／顺序／来源／类型、截断帧拒绝和连续回收；人为提供的客体帧不是 Minecraft 执行。

LAN 验收需要额外显式启用及隔离网络：测试调用真实 `openToLan`，临时开放监听端口并启动 Minecraft LAN 发现广播。不接入另一名玩家，也不修改真实安装。从仓库根部执行，Windows Visual Studio 默认对端路径为：

```console
python tools/gradle.py --hc-timeout 1800 -PrunMinecraftTests=true -PacceptMinecraftEula=true -PrunLanTests=true -PpeerExecutable=../build/bridge/native/Debug/hc-bridge-peer.exe runClientGameTest
```

Linux 改用 `../build/bridge/native/hc-bridge-peer`。`runLanTests` 仅在已启用客户端测试时接受；入口每次默认明确传入 false，覆盖环境中的项目属性，普通客户端测试不开放 LAN。LAN 场景验证真实模式拒绝、自动 tick 回收原生观察连接、拒绝接入新的调用方自有传输及原方块不变。另用受控命令队列配合真实 Minecraft 适配器及实际 LAN 状态，验证已排队 SET 被丢弃；这不是原生管道队列实测。关闭生成世界后须确认服务端与发现线程结束、监听端口可重新绑定；随后新建私有世界，拒绝旧代次并完成新连接往返。

每个已完成客户端场景在本次 Gradle 输出中记录 `HARRANCRAFT_SCENARIO <name>`。报告的 `client_scenarios` 分别记录已通过、缺失、意外及重复名称，`invalid_records` 记录格式无效的标记数量；仅任务退出成功和截图存在不足以通过。默认要求 `block-roundtrip`、`reconnect`、`block-entity-preservation`、`thread-ownership`、`world-stop`、`world-reopen`、`world-replacement`，显式 LAN 执行另要求 `lan-revocation` 与 `lan-recovery`。缺失、意外、重复或格式无效的完成记录均使验收失败；记录数量超过 64 条时以 `client_scenario_limit_exceeded` 失败；`lan_execution=false` 和 `lan=not_requested` 不能代表 LAN 已验收。截图视觉审查仍单独执行。

上述游戏测试源码需要配置的 Fabric 构建及真实客户端／服务端执行。接口依据：[Yarn 1.21.4+build.8 嵌入式服务端](https://maven.fabricmc.net/docs/yarn-1.21.4+build.8/net/minecraft/server/integrated/IntegratedServer.html)及 [Fabric 1.21.4 测试存档接口](https://github.com/FabricMC/fabric-api/blob/1.21.4/fabric-client-gametest-api-v1/src/client/java/net/fabricmc/fabric/api/client/gametest/v1/world/TestWorldSave.java)。
