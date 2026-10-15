# Changelog

## Unreleased

### Added

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

- Repository text checks cover Java, C++, Gradle and properties files; generated caches are pruned before traversal. CI Action and permission restrictions remain unchanged.
- Failed welcomes, replayed receipts and events close the session. Adapter exceptions invalidate it; input and output stream closure are independent. Chunk state reads occur only after thread, mode and subscription checks. Default Fabric builds do not execute Minecraft.

### Validation

Java/C++ reference frames, controlled process checks and affected lifecycle/tooling regressions pass. Fabric dependency resolution, GameTests and target Windows bridge execution remain unrun. No Dying Light host adapter or playable release is included.

## 简体中文

### 未发布变更

- 新增 Fabric 方块适配源码：服务端线程访问、已加载位置检查、规范状态条件替换及订阅区块变化。
- 新增版本 1 Java／C++ 协议、私有管道、有界命令排序、回执去重、会话代次及断连恢复。
- 新增严格原生／Java 测试、受控世界真实跨进程测试、生命周期回归及明确启用的本地服务端／客户端 GameTest 源码。
- 新增固定校验和的 Gradle 启动器、有界解包、缓存保护及独占安装；源码包排除生成的游戏与工具链数据。
- 仓库文本检查覆盖 Java、C++、Gradle 和属性文件，遍历前排除生成缓存；保留原 CI Action 与权限限制。
- 握手、重复回执或事件发送失败时关闭会话；适配器异常使其失效，输入／输出分别关闭；区块状态读取位于线程、模式和订阅检查之后；默认 Fabric 构建不运行 Minecraft。
- 中英文桥接契约、验证入口、工具链状态及 H2 验收顺序同步维护。

Java／C++ 参考帧、受控进程及受影响生命周期／工具回归通过；Fabric 依赖解析、GameTest 和目标 Windows 桥接执行尚未运行。当前不包含消光宿主适配器或可玩发行版。
