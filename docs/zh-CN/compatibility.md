# 兼容范围

[English](../compatibility.md) · [文档导航](README.md)

| 组件 | 目标 | 当前依据／状态 |
| --- | --- | --- |
| Python 核查工具 | Python 3.12+，Windows x64／Linux x86_64 | 自动解析、CLI 与打包验证，矩阵见 CI |
| 输入程序格式 | AMD64 PE32+，EXE 或 DLL | 解析边界以核查工具契约为准 |
| Windows 宿主指纹与构建门禁 | Windows x64、C++20 | EXE／已加载 DLL 只读哈希及隔离组合身份、会话代次测试；没有已认可的消光构建 |
| 消光宿主 | 第一代，Windows x64，单人 | 尚未实现，没有已验证支持的游戏版本 |
| Minecraft 客体 | 通过 Fabric 运行真实 Minecraft | Ubuntu Fabric 源码编译、重映射及生产 JAR 检查通过；游戏内 GameTest 未运行 |
| 客户端烘焙模型读取 | Minecraft 1.21.4 固定 Yarn/Fabric | 静态方块模型四边形和相机姿态源码；真实游戏执行及宿主帧关联尚未验收 |
| 桥接核心及原生测试进程 | JDK 21、C++20、Linux x86_64／Windows x64 | Linux/GCC 与 Windows/MinGW 严格编译、参考帧及真实进程测试通过；MSVC 尚未验收 |
| 其他消光作品 | 第二代／《困兽》 | 不在首阶段目标内 |
| 联机模式 | 合作／入侵／其他多人 | 不支持；未来宿主必须在代码中拒绝 |

上游 GModLight 研究使用 Steam 程序版本 1.55.0.0、build 24076292；这是研究输入，不是本项目兼容清单。[宿主研究](../host-research.md)

Linux 可以读取游戏二进制元数据而不运行游戏。文件版本或导出名称读取成功、构建通过、生成式测试通过，均不能证明真实游戏兼容。

可玩发布需要精确游戏程序身份，以及相机／深度、原生碰撞、普通伤害、模式转换和清理恢复的项目实测记录。

## 客体配置版本

Minecraft **1.21.4**、Yarn **1.21.4+build.8**、Fabric Loader **0.16.14**、Fabric API **0.119.4+1.21.4**、Loom **1.11.8**、Gradle **8.14.3**、JDK **21**。上述配置已在 Ubuntu/JDK 21 的 [CI #38892925451](https://github.com/ranzevor/HarranCraft/actions/runs/38892925451) 完成依赖解析及编译；Fabric 游戏实际执行、Mod/Mixin 装载仍未验收。原生测试需要 CMake **3.25+** 和 C++20 编译器。[本地验证](guest-validation.md)
