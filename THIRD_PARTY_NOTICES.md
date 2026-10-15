# Third-party components and sources

The runtime wheel contains HarranCraft code and language resources. It uses Python's standard library and does not vendor an external runtime library.

Development tools are installed separately through [requirements-dev.txt](requirements-dev.txt): setuptools (MIT), Ruff (MIT), PyYAML (MIT), jsonschema, attrs, referencing, jsonschema-specifications and rpds-py (MIT), typing_extensions (PSF-2.0), and packaging (Apache-2.0 OR BSD-2-Clause). Their distributions retain their own license notices. The pinned GitHub checkout and setup-python actions are CI tools, not runtime components.

[Microsoft PE/COFF documentation](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format) and [version-resource documentation](https://learn.microsoft.com/en-us/windows/win32/menurc/vs-versioninfo) define the inspected formats. The parser is an independent implementation; game data is neither bundled nor required by its generated-input tests.

[GModLight](https://github.com/GooseMcGee/DyingLight-GMod) is a host-interface research reference under its own MIT license. No GModLight, MinHook, ASI-loader, GMod SDK or GMod guest code is included in this version. Any later source reuse must retain the component's license and copyright and record its fixed revision here.

## Fabric guest dependencies / Fabric 客体依赖

These components are resolved separately by the guest build, not vendored into this repository. Versions are configured values pending complete Fabric resolution. 这些组件由客体构建独立解析，不随仓库分发；表中版本为尚待完整解析验证的配置值。

| Component / 组件 | Configured version / 配置版本 | Source and license / 来源与许可 |
| --- | --- | --- |
| Fabric Loader | 0.16.14 | [FabricMC/fabric-loader](https://github.com/FabricMC/fabric-loader), Apache-2.0 |
| Fabric API | 0.119.4+1.21.4 | [FabricMC/fabric-api](https://github.com/FabricMC/fabric-api), Apache-2.0 |
| Yarn mappings | 1.21.4+build.8 | [FabricMC/yarn](https://github.com/FabricMC/yarn), CC0-1.0 |
| Fabric Loom | 1.11.8 | [FabricMC/fabric-loom](https://github.com/FabricMC/fabric-loom), MIT |
| Gradle | 8.14.3 | [Gradle](https://github.com/gradle/gradle), Apache-2.0; [distribution checksums](https://gradle.org/release-checksums/) |
| Minecraft Java Edition | 1.21.4 | [Minecraft EULA](https://www.minecraft.net/eula), proprietary game terms / 专有游戏协议 |

JDK 21, CMake and a C++20 compiler are externally installed development tools, not bundled distributions; their selected vendors' notices apply. The project supplies its own Gradle bootstrap rather than copied Wrapper code. JDK 21、CMake 和 C++20 编译器为外部安装工具，遵循所选发行方许可；项目提供自有 Gradle 启动入口，不复制官方 Wrapper 源码。

Minecraft and Dying Light game files, trademarks and other assets remain subject to their respective holders' rights. The repository MIT License does not grant rights to those materials.

Minecraft 与消光的游戏文件、商标和素材受各自权利人约束；本仓库 MIT 许可证不授予这些材料的权利。
