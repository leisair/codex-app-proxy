## 下载与使用

请下载本 Release 下方的 `windows-x64.zip` 文件，不要下载 GitHub 自动生成的 Source code 压缩包。

1. 解压 ZIP，只需一次。
2. 打开 `START-HERE.html`，按向导确认代理配置。
3. 启动代理软件，完全退出已有 ChatGPT/Codex，然后双击 `CodexProxyLauncher.exe`。

V2RayN 默认 mixed 端口 `10808` 的用户无需修改 `config.json`。不需要开启 Windows 系统代理或 TUN。

## v1.1.0 更新内容

- 在 `START-HERE.html` 高级设置中加入三档诊断日志：仅错误、始终记录、关闭。
- 出厂默认采用“仅错误”：正常启动不创建日志目录，失败时才写入完整启动上下文。
- 旧版 `config.json` 缺少 `log_mode` 时自动沿用安全默认值，无需手动迁移。
- `CodexProxyLauncher.exe` 现已嵌入旧 Codex 图标；快捷方式会自动继承启动入口图标。
- 同步完善错误提示、隐私说明、离线向导和自动化测试。

## 文件校验

同一 Release 提供 `.sha256` 文件。当前 EXE 尚未代码签名，请只从本仓库 Release 下载，或审查源码后自行构建。

完整说明与故障排查请查看压缩包内的 `START-HERE.html`。
