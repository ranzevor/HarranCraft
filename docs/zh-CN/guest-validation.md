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

桥接命令构建 C++ 测试、编译无第三方依赖的 Java 协议核心、执行核心和生命周期回归、比对完整跨语言参考帧，并启动真实 Java／C++ 进程访问受控世界；还会运行 `hc-pipe-peer` 故障场景并确认工作线程及进程实际结束。Linux 通过 `/proc/self/fd` 测量文件描述符数量，不支持时单独报告该测量未提供。命令不会启动 Minecraft；应分别在 Linux 和目标 Windows 工具链运行。

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

服务端 GameTest 检查规范方块状态，并验证隔离 GameTest 服务器不能作为生产桥接端点。客户端测试使用新建嵌入式世界及 `test_bridge.py` 生成的原生测试进程。Linux 默认单配置输出对应：

```console
python tools/gradle.py -PrunMinecraftTests=true -PacceptMinecraftEula=true -PpeerExecutable=../build/bridge/native/hc-bridge-peer runClientGameTest
```

Windows Visual Studio 多配置构建将对端参数改为 `-PpeerExecutable=../build/bridge/native/Debug/hc-bridge-peer.exe`。该路径相对 `guest/` 解析；使用其他 CMake 生成器时以实际输出为准。客户端测试需要可用图形会话，检查放置、服务端权威状态、独立方块变化、移除、旧代次、无效版本及断连清理，使用显式的自有对端取消回调并检查传输实际结束，并捕获空场景／放置后／移除后三张截图。GameTest 世界位于 `guest/build/run`，报告和截图保存在生成的构建输出目录；不得将运行目录指向已有存档。

记录实际退出码、源码提交、JDK／编译器及所选依赖版本，保留失败日志并检查三张截图。真实局域网／模式转换、Windows 执行、完整 Fabric 解析与客户端渲染各自验收，不能从模拟 `allowed=false` 推断通过。上述命令不运行消光游戏。

## 启动器恢复

校验和错误、无效 ZIP 路径、符号链接／特殊条目、缺少启动文件或压缩包超限会在安装生效前失败。不完整暂存会清理，已有无效缓存会保留；重试前仅检查并删除生成的 `build/toolchains/gradle-8.14.3` 缓存。已有 `.install-lock` 不会被抢占；确认没有安装过程运行后再删除失效的生成锁。不得删除无关工具链或安装目录。
