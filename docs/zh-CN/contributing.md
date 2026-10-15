# 贡献指南

[English](../../CONTRIBUTING.md) · [文档导航](README.md)

使用 Issue 表单提交可复现缺陷、具体功能或宿主实验，先查询已有问题。涉及玩法状态权威、协议布局、依赖及支持平台的变更，应明确受影响契约再扩展实现；小范围修复可直接提交 PR。可使用英文或简体中文，遵守[行为准则](../../CODE_OF_CONDUCT.md)，敏感问题走[安全报告](../../SECURITY.md)。

在 Python 3.12+ 隔离环境安装固定开发依赖并验证：

```console
python -m pip install -r requirements-dev.txt
python tools/verify.py
python tools/package.py
```

单项检查见[开发指南](development.md)。生成物放入忽略的输出目录；当前测试不需要游戏，不将游戏文件上传 Issue 或 CI。

从最新 main 建立聚焦分支，提交主题使用 `type: imperative summary`，例如 `fix: reject overlapping PE sections`；类型包括 `feat`、`fix`、`docs`、`test`、`refactor`、`build`、`chore`。记录实际变更，不写任务流水或未经验证的能力。

PR 应说明目的、行为／契约变化、测试命令和实际结果、兼容影响及未运行验证。同一行为的代码、测试、语言资源和文档一起维护，遵循[工程契约](../../AGENTS.md)，不以自动检查取代语义审查。

仓库负责人 `@ranzevor` 承担项目接纳及合并责任。`CODEOWNERS` 表达审查归属，分支保护和强制检查属于独立 GitHub 设置。合并不等于游戏发布或用户安装修改授权。

仅提交有权按仓库 MIT 许可证贡献的材料，保留第三方许可与署名；引入组件时记录准确来源、版本和许可。研究参考不自动成为依赖，不提交游戏素材、二进制、专有 SDK、存档、秘密或个人路径。

实验报告包含源码版本、目标程序身份、测试层级、受控输入、预期与实际结果及清理情况；区分玩家／敌人／投射物碰撞与导航，区分普通伤害与强制死亡。合成样本或构建成功不得写成实机通过。
