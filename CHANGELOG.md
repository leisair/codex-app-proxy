# Changelog

本项目的用户可见变更记录在此文件中。版本遵循语义化版本号。

## [1.1.2] - 2026-07-12

### Changed

- 产品名称统一为 ChatGPT/Codex App Proxy Launcher，仓库 slug 保持 `codex-app-proxy`。
- README 增加中英文搜索描述，明确 Microsoft Store、Windows x64、HTTP/SOCKS5 和 per-app proxy 关键词。

### Added

- 新增 `CONTRIBUTING.md`、`SECURITY.md` 和 GitHub Issue 模板。
- 保留并核实仓库 About 描述与现有 Topics 配置。

## [1.1.1] - 2026-07-11

### Changed

- 修正 README 对发布包文件和运行时 `logs/` 目录的描述。
- 更新 START-HERE 配置向导截图。
- Release 文案改为按版本标签独立存放，避免后续发布误用旧版本说明。
- 标签发布缺少对应说明文件时，GitHub Actions 会明确失败。

## [1.1.0] - 2026-07-11

### Added

- 新增 `errors`、`always`、`off` 三档诊断日志模式。
- START-HERE 高级设置可直接选择日志模式并保存到 `config.json`。
- Codex 旧版图标以多尺寸 Windows ICO 嵌入启动器。

### Changed

- 默认日志模式为 `errors`：正常启动不创建日志目录，失败时回放本次启动上下文。
- 旧配置缺少 `log_mode` 时自动采用默认值，无需手动迁移。
- 同步完善错误提示、隐私说明、README、离线向导与测试。

## [1.0.0] - 2026-07-11

### Added

- 首个稳定版本。
- 便携式 Windows x64 启动器，通过 Chromium 启动参数为 Microsoft Store 版 ChatGPT/Codex 设置单应用代理。
- 单文件离线配置向导、常用代理预设、环境检查、错误提示和自动化发布流程。

[1.1.2]: https://github.com/leisair/codex-app-proxy/compare/v1.1.1...v1.1.2
[1.1.1]: https://github.com/leisair/codex-app-proxy/compare/v1.1.0...v1.1.1
[1.1.0]: https://github.com/leisair/codex-app-proxy/compare/v1.0.0...v1.1.0
[1.0.0]: https://github.com/leisair/codex-app-proxy/releases/tag/v1.0.0
