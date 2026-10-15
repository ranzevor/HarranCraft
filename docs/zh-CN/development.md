# 开发指南

[English](../development.md) · [文档导航](README.md)

当前核查工具使用 Python 3.12+ 和标准库；[开发依赖](../../requirements-dev.txt)固定直接及传递依赖版本，PEP 517 构建后端为 setuptools 82.0.1。桥接核心使用 JDK 21、C++20 和 CMake 3.25+；独立配置的 Fabric 工具链与游戏测试启用条件见[客体本地验证](guest-validation.md)。

```console
python -m venv .venv
```

按当前 shell 的标准方式激活虚拟环境，然后执行：

```console
python -m pip install -r requirements-dev.txt
python tools/verify.py
python tools/package.py
```

验证入口遇到工具缺失或命令失败会返回失败。打包入口生成源码包与 wheel，检查 wheel 内容，在临时目标目录安装并通过子进程调用 CLI；不发布软件包或写入游戏目录。

单项检查及格式化：

```console
python -m ruff check .
python -m ruff format --check --diff .
python tools/check_repository.py
python tools/run_tests.py
python -m ruff format .
```

仅对已审查语义的修改使用 `ruff check --fix`。源码统一 UTF-8、LF、末尾换行与无行尾空白，配置位于 `.editorconfig`、`.gitattributes` 和 `pyproject.toml`。

桥接及 Fabric 验证：

```console
python tools/test_bridge.py
python tools/gradle.py build
```

第一条命令依赖已安装的 Java／C++ 工具，使用受控世界，不需要游戏依赖；第二条解析固定 Fabric 工具链、编译全部源码集并检查生产 JAR，默认不运行游戏。[客体本地验证](guest-validation.md)分别说明服务端／客户端 GameTest、明确接受 EULA、Windows 对端路径、截图和缓存恢复；CI 不执行这些游戏命令。

`src/harrancraft_tools` 为工具源码，`hc-inspect` 与 `python -m harrancraft_tools` 复用同一入口。包内 `__version__` 为工具版本权威。生成式 PE 输入仅存在于测试临时目录；wheel 包含工具、语言资源和许可，不包含测试样本、可执行替身或私人输入；sdist 包含源码、测试、文档、CMake／原生契约及 Fabric 源码集，排除 `guest/build`、`.gradle` 缓存和生成的游戏目录；客体与核查工具 wheel 分开构建。

CI 使用固定 action 提交、只读令牌并禁用 checkout 凭据持久化，在 PR 和手动运行时检查 Ubuntu／Python 3.12 与 Windows／Python 3.13。没有定时扫描、游戏安装或发行任务；精确 Python 和源码版本由日志记录。

依赖及工作流变更应审查来源、兼容与受影响测试。强制审查、私密安全报告、仓库元数据和发布属于独立的平台设置／操作，不能仅凭文件声称已启用。工程规则见[AGENTS.md](../../AGENTS.md)，贡献流程见[贡献指南](contributing.md)，测试边界见[验证模型](../testing.md)。
