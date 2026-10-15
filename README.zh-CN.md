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
| 消光宿主适配器及真实 Fabric 客体 | 尚未实现 |
| 游戏内遮挡、原生方块碰撞、伤害与 TNT | 尚未实现或实机验证 |
| 可玩发行包或安装器 | 尚未提供 |

核查工具识别文件内容，**不加载 DLL、不挂钩游戏、不安装模组、不修改存档，也不将某个游戏版本判定为兼容**。宿主与客体开发仍属于[完整路线图](docs/zh-CN/roadmap.md)，该工具不替代融合目标。

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

## 融合设计

计划中的原生宿主适配器与 Fabric 客体通过带版本号的协议交换世界、输入和玩法事件。渲染与物理交互分别验收：画面对齐不等于深度遮挡，客体碰撞不等于宿主原生障碍物。

修改宿主前必须识别具体程序版本、正向确认单人会话，并验证引擎安全执行阶段。状态未知或不可用时拒绝修改。可玩发布前，必须在原版游戏中验证相机与深度对应、碰撞所有权及双向伤害。

[架构](docs/zh-CN/architecture.md)规定状态归属、恢复和协议要求；[宿主研究](docs/host-research.md)保留上游证据及未解决的具体实验。Garry's Mod 只是参考工程的客体，不是 HarranCraft 的运行依赖。

## 仓库导航

| 位置 | 用途 |
| --- | --- |
| `src/harrancraft_tools/` | 只读核查工具与语言资源 |
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
