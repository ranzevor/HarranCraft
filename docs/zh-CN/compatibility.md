# 兼容范围

[English](../compatibility.md) · [文档导航](README.md)

| 组件 | 目标 | 当前依据／状态 |
| --- | --- | --- |
| Python 核查工具 | Python 3.12+，Windows x64／Linux x86_64 | 自动解析、CLI 与打包验证，矩阵见 CI |
| 输入程序格式 | AMD64 PE32+，EXE 或 DLL | 解析边界以核查工具契约为准 |
| Windows 宿主指纹与构建门禁 | Windows x64、C++20 | EXE／已加载 DLL 只读哈希及隔离组合身份、会话代次测试；没有已认可的消光构建 |
| 消光宿主 | 第一代，Windows x64，单人 | 尚未实现，没有已验证支持的游戏版本 |
| Minecraft 客体 | 通过 Fabric 运行真实 Minecraft | Ubuntu 编译、重映射及生产 JAR 检查通过；本地服务端 GameTest 3/3 通过，客户端首次桥接接入失败 |
| 客户端烘焙模型及渲染协议 | Minecraft 1.21.4 固定 Yarn/Fabric、Java 21／C++20 | 静态方块采样源码和有界版本化编解码；客户端采样及宿主帧关联尚未验收 |
| 已加载源精灵及材质传输 | Minecraft 1.21.4 SpriteContents／NativeImage、Java 21／C++20 | 有界静态源图（单图≤64×64、模型≤16 种）、客户端重载监听、异步批次和原生 CPU 材质缓存；实机采样／重载、最终图集和宿主显存上传尚未验收 |
| 受控双相机投影 | C++20、显式版本 1 相机／标定输入 | [有界当前采集投影](camera-projection.md)、解析值及实时接收器生命周期回归；实际游戏相机／深度采集和同步尚未验收 |
| 桥接核心及原生测试进程 | JDK 21、C++20、Linux x86_64／Windows x64 | Linux/GCC 与 Windows/MinGW 分别执行严格编译、参考帧及真实进程检查；各版本结果记录于 [PR #2](https://github.com/ranzevor/HarranCraft/pull/2)；MSVC 完整验收尚未完成 |
| 其他消光作品 | 第二代／《困兽》 | 不在首阶段目标内 |
| 联机模式 | 合作／入侵／其他多人 | 不支持；未来宿主必须在代码中拒绝 |

[2026-10-17 本地记录](validation-20261017.md)分别说明服务端成功、客户端失败及未完成验收。所附报告将直接 Gradle 执行归于 `b1b6234eb81955cc170cc5e8ce66df5191073ca4`，所附日志未独立证明这些命令的源码身份；该轮七项客户端场景均无完成记录，未到达模型／像素采样，资源重载行为仍未验收。

上游 GModLight 研究使用 Steam 程序版本 1.55.0.0、build 24076292；这是研究输入，不是本项目兼容清单。[宿主研究](../host-research.md)

Linux 可以读取游戏二进制元数据而不运行游戏。文件版本或导出名称读取成功、构建通过、生成式测试通过，均不能证明真实游戏兼容。

可玩发布需要精确游戏程序身份，以及相机／深度、原生碰撞、普通伤害、模式转换和清理恢复的项目实测记录。

## 客体配置版本

Minecraft **1.21.4**、Yarn **1.21.4+build.8**、Fabric Loader **0.16.14**、Fabric API **0.119.4+1.21.4**、Loom **1.11.8**、Gradle **8.14.3**、JDK **21**。上述配置已在 Ubuntu/JDK 21 的 [CI #38892925451](https://github.com/ranzevor/HarranCraft/actions/runs/38892925451) 完成依赖解析及编译；普通云端 Fabric CI 仍仅编译，本地服务端成功不构成集成客户端或双运行时验收完成。原生测试需要 CMake **3.25+** 和 C++20 编译器。[本地验证](guest-validation.md)
