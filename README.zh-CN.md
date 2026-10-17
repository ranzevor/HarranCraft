# HarranCraft

**Minecraft ×《消逝的光芒 1》——两个运行时，同一个世界。**

[English](README.md) · [文档](docs/zh-CN/README.md) · [路线图](docs/zh-CN/roadmap.md) · [参与贡献](docs/zh-CN/contributing.md)

HarranCraft 是将真实 Minecraft 运行时与《消逝的光芒 1》融合的实验项目。目标是在哈兰放置和移除 Minecraft 方块，同时保留原生世界、敌人和玩法的实际作用：正确场景遮挡、原生方块碰撞、双向伤害、TNT 联动，以及桥接停止后的可靠恢复。

首阶段游戏目标为 **《消逝的光芒 1》、Windows x64、单人模式**。第二代、《困兽》、合作和入侵模式不在该目标范围内。

## 当前可用能力

仓库目前提供 **`hc-inspect` 只读程序文件核查工具**，用于识别 Windows 程序文件。它可在 Windows 和 Linux 上通过 Python 3.12 或更高版本运行，运行时仅依赖 Python 标准库。

| 能力 | 当前状态 |
| --- | --- |
| SHA-256、AMD64 PE32+ 文件头与节区元数据 | 已实现 |
| 具名导出、仅序号导出和转发导出 | 已实现 |
| 固定文件版本资源读取 | 已实现 |
| 英文／简体中文命令行及带版本号的 JSON 报告 | 已实现 |
| 损坏输入、内存上界和安装包 CLI 测试 | 已纳入验证入口 |
| Java／C++ 桥接协议及私有管道 | 受控世界及真实跨进程测试通过 |
| Fabric 方块读取、条件修改及变化观察 | 源码已实现；Ubuntu Fabric 构建与生产 JAR 检查通过，游戏内测试未运行 |
| Windows 宿主程序身份探测 | Windows x64 只读 EXE／已加载 DLL 指纹、受控 ABI／进程测试，不加载游戏 |
| 真实 Minecraft 碰撞形状快照 | Fabric 权威 VoxelShape 只读采样，代次和订阅门禁；GameTest 源码编译，原生宿主碰撞未验收 |
| 碰撞几何异步传输 | Java／C++ 协议、客体有界异步管道、原生流式接收和几何候选表；未接入消光物理 |
| 客户端烘焙模型观察 | 真实静态方块 BakedModel 四边形、图集标识、压缩顶点及相机姿态；GameTest 源码已编写，尚无实机执行 |
| 渲染模型数据流 | Java／C++ 版本 1、有界客户端异步发布、原生分片接收及候选模型生命周期验证；仍无消光渲染端点 |
| 源精灵材质批次 | 客户端 NativeImage 像素、有界 ARGB 记录及双流异步批次；原生端按五项身份关联并容忍跨通道乱序，仅完整材质可用；尚无最终显存图集或消光纹理上传 |
| H1 宿主观察核心 | C++20 EXE／DLL 组合构建身份、会话与相机帧契约及隔离 CTest；未接入游戏 |
| 相机与深度准备 | 版本化双相机标定及采集归属；Windows D3D11 只读深度视图准备、有界源引用及已注册 WARP 回归；尚未关联真实游戏相机与深度 |
| 消光宿主适配器 | 尚未实现 |
| 游戏内遮挡、原生方块碰撞、伤害与 TNT | 尚未实现或实机验证 |
| 可玩发行包或安装器 | 尚未提供 |

核查工具识别文件内容，**不加载 DLL、不挂钩游戏、不安装模组、不修改存档，也不将某个游戏版本判定为兼容**。[完整路线图](docs/zh-CN/roadmap.md)继续保留完整双运行时融合目标。

H2-A 提供接收调用方自有私有管道的 Fabric 嵌入接口、已加载位置读取、条件方块替换及订阅变化事件。当前没有最终用户启动器或消光端点。[桥接契约](docs/zh-CN/bridge.md) · [本地验证](docs/zh-CN/guest-validation.md)

## 使用核查工具

在 Python 3.12+ 环境中，从源码目录执行：

```console
python -m pip install .
hc-inspect --help
hc-inspect --json DyingLightGame.exe engine_x64_rwdi.dll gamedll_x64_rwdi.dll
```

明确传入有权读取的游戏副本文件位置；工具不会搜索游戏安装目录。路径仅用于打开输入，不写入报告；`input_index` 按命令中的文件顺序标识输入。

```console
hc-inspect --language zh-CN --json --exports engine_x64_rwdi.dll
```

`--exports` 增加导出名称、序号、RVA 和转发字符串；默认仅输出元数据及数量。结果输出到终端，可使用 shell 重定向保存。最多接受 16 个普通文件，单文件上限 512 MiB。错误、资源约束和字段含义见[CLI 与 JSON 契约](docs/zh-CN/inspector.md)。

构建工具和运行生成式测试不需要游戏；后续真实融合验收需要两个游戏的合法副本及获授权的 Windows 图形会话。

## 开发与验证

```console
python -m pip install -r requirements-dev.txt
python tools/verify.py
python tools/package.py
```

验证入口覆盖 Python 语法、Lint、格式、仓库文件、Markdown 链接、语言资源、报告 Schema、单元测试及真实 CLI 子进程。打包入口生成 sdist 和 wheel，在隔离临时目录安装 wheel 并实际调用 CLI。wheel 中是核查工具，不是游戏模组。

[CI](https://github.com/ranzevor/HarranCraft/actions/workflows/ci.yml)在 PR 和手动运行时执行 Ubuntu 与 Windows 检查，结果对应实际源码版本，不能代替游戏内兼容验证。[开发指南](docs/zh-CN/development.md) · [兼容范围](docs/zh-CN/compatibility.md) · [验证分层](docs/testing.md)

已安装 JDK 21、CMake 3.25+ 及 C++20 编译器时，可运行不依赖游戏的 Java／C++ 检查：

```console
python tools/test_bridge.py
```

Fabric 配置版本及明确的本地构建／GameTest 命令见[本地验证](docs/zh-CN/guest-validation.md)。CI 编译 Fabric 及 GameTest 源码并检查生产 JAR，不启动 Minecraft；游戏测试默认关闭，不在 CI 中运行。

## 融合设计

已实现的客体桥接通过带版本号的协议与原生测试进程交换方块命令和事件；计划中的宿主适配器继续扩展原生世界交互、输入和玩法事件。渲染与物理交互分别验收：画面对齐不等于深度遮挡，客体碰撞不等于宿主原生障碍物。

修改宿主前必须识别具体程序版本、正向确认单人会话，并验证引擎安全执行阶段。状态未知或不可用时拒绝修改。可玩发布前，必须在原版游戏中验证相机与深度对应、碰撞所有权及双向伤害。

[架构](docs/zh-CN/architecture.md)规定状态归属、恢复和协议要求；[宿主研究](docs/host-research.md)保留上游证据及未解决的具体实验。Garry's Mod 只是参考工程的客体，不是 HarranCraft 的运行依赖。

## 仓库导航

| 位置 | 用途 |
| --- | --- |
| `src/harrancraft_tools/` | 只读核查工具与语言资源 |
| `guest/` | Fabric 端点、协议核心及隔离 GameTest 源码 |
| `include/harrancraft/wire.hpp` | 原生 wire 布局权威 |
| `include/harrancraft/host_probe.h` | Windows 宿主身份 C ABI |
| `native/host/` | Windows 身份探测与受控 D3D11 深度附件源码 |
| `native/tests/` | C++ 契约测试与私有管道测试进程 |
| `schemas/` | 机器可读报告契约 |
| `tests/` | 动态生成的输入样本及回归测试 |
| `tools/` | 仓库、构建和安装包 CLI 验证 |
| `docs/` | 架构、开发、兼容、研究及证据 |
| `.github/` | CI、贡献模板及代码责任归属 |

## 参与贡献

问题报告和讨论可使用英文或简体中文。请从[贡献指南](docs/zh-CN/contributing.md)开始，遵循[工程契约](AGENTS.md)，通过 Issue 提交缺陷、具体功能或实验结果。提供精确版本，并区分模拟、进程级及真实游戏观察。

[支持渠道](SUPPORT.md) · [安全报告](SECURITY.md) · [行为准则](CODE_OF_CONDUCT.md) · [变更记录](CHANGELOG.md)

## 许可

项目代码和文档采用 [MIT 许可证](LICENSE)。[第三方说明](THIRD_PARTY_NOTICES.md)区分开发工具与研究参考；本项目不分发游戏二进制、素材、专有 SDK 或存档，游戏相关权利归各自权利人所有。
