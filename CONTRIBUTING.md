# Contributing

感谢你为 ChatGPT/Codex App Proxy Launcher 提交改进。

## 开始之前

- 项目只支持 Windows x64 和 Microsoft Store 版 ChatGPT/Codex 桌面 App。
- 不要提交账号、代理节点、订阅内容或诊断日志。
- 不要把 `build/`、`out/`、`dist/` 或本地生成的二进制文件提交到仓库。

## 本地验证

需要 Visual Studio 2022 C++ Build Tools、CMake 3.20+ 和 Node.js 18+：

```powershell
.\build.ps1
```

提交前至少运行：

```powershell
node tests/verify-start-here.mjs resources/START-HERE.html resources/default_config.json
git diff --check
```

涉及启动器、配置、日志或发布包的改动，还应运行完整 C++ 测试并确认发布包仍只包含四个根目录文件。

## 提交建议

- 一个提交只解决一个清晰的问题。
- 用户可见行为变化要同步更新 README、CHANGELOG 和离线向导。
- 发布版本使用 `vMAJOR.MINOR.PATCH` 标签；标签对应的说明文件放在 `.github/releases/`。
- Pull request 请说明验证方式，以及是否影响旧版 `config.json`。
