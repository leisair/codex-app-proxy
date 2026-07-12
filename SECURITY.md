# Security Policy

## 支持版本

安全修复优先应用于最新 Release 和 `main` 分支。旧版本可能不会单独维护。

## 报告问题

请通过 GitHub 的私密 Security Advisory 渠道报告可能导致代码执行、任意文件写入、敏感信息泄露或发布供应链风险的问题：

<https://github.com/leisair/codex-app-proxy/security/advisories/new>

不要在公开 Issue 中粘贴账号信息、代理订阅、访问令牌、完整诊断日志或其他敏感数据。

普通配置问题、兼容性问题和功能建议请使用公开 Issue，并附上可公开的 Windows、ChatGPT/Codex App、代理类型和复现步骤。

## 设计边界

本项目不会读取账号或对话内容，不提供代理节点，不修改 Windows 系统代理，不创建 TUN 网卡，也不会常驻后台。诊断日志可能包含路径、代理地址和错误信息，分享前请自行检查。
