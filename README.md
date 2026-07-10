# ChatGPT/Codex Proxy Launcher

一个适用于 Windows 的便携启动器，让 Microsoft Store 版 ChatGPT/Codex 只使用指定的本地代理，无需开启 Windows 系统代理或 TUN 模式。

它不会注入进程、修改系统设置或常驻后台。本质上，它会找到 `ChatGPT.exe`，并使用经过验证的 Chromium 代理参数启动它。

## 最简单的使用方法

如果使用 V2RayN，且 mixed 端口是默认的 `10808`：

1. 解压下载的 Artifact，只需解压一次。
2. 启动 V2RayN，保持节点可用。
3. 完全退出已经运行的 ChatGPT/Codex，包括后台进程。
4. 双击 `CodexProxyLauncher.exe`。

不需要开启 V2RayN TUN，也不需要开启 Windows 系统代理。

## 使用其他代理软件

1. 双击打开 `config-web.html`。
2. 选择代理软件预设，确认本地监听端口。
3. 点击“保存 config.json”。
4. 将文件保存到 `CodexProxyLauncher.exe` 所在文件夹，并覆盖原来的 `config.json`。
5. 启动代理软件，完全退出已有 ChatGPT/Codex，然后双击启动器。

配置页提供以下快捷预设：

| 代理软件 | 配置页预设 | 常见默认地址 |
| --- | --- | --- |
| V2RayN | HTTP / mixed | `127.0.0.1:10808` |
| Clash Verge Rev | HTTP / mixed | `127.0.0.1:7897` |
| Clash / Mihomo | HTTP / mixed | `127.0.0.1:7890` |
| 通用 SOCKS5 | SOCKS5 | `127.0.0.1:1080` |

端口都可以在代理软件中修改。请以软件当前显示的 HTTP、mixed 或 SOCKS5 监听端口为准。

## 文件说明

发布目录中的文件都有明确用途：

- `CodexProxyLauncher.exe`：读取配置并启动 ChatGPT/Codex。
- `config.json`：当前代理配置，必须与启动器放在同一目录。
- `config-web.html`：本地可视化配置页，不启动 Web 服务。
- `README.md`：使用说明。
- `logs`：运行后自动生成的诊断日志目录。

启动器不会创建或读取 `%USERPROFILE%\.codex-proxy`。

## 启动器会做什么

- 从 Microsoft Store 包中优先定位 `app\ChatGPT.exe`，旧版本则回退到 `app\Codex.exe`。
- 检查 `config.json` 是否有效。
- 检查配置的代理地址和端口是否正在监听。
- 检查 ChatGPT/Codex 是否已经运行，防止启动参数不生效。
- 添加 `--proxy-server`、`--proxy-bypass-list` 和可选的 `--disable-quic`。
- 启动完成后立即退出，不常驻内存。

如果同目录没有 `config.json`，启动器会创建 V2RayN `127.0.0.1:10808` 默认配置。

## 常见问题

### 提示代理不可连接

确认代理软件正在运行，并检查配置页中的端口是否与代理软件显示的本地监听端口一致。不要填写远程节点端口。

### 提示 ChatGPT/Codex 已经运行

完全退出桌面 App 后再运行启动器。Chromium 启动参数通常只对第一个 App 进程生效。

### 配置保存后没有变化

确认新的 `config.json` 与 `CodexProxyLauncher.exe` 在同一目录，而不是只留在浏览器的“下载”目录。

### 无法定位桌面 App

从 Microsoft Store 安装或更新 ChatGPT。当前商店包名可能仍是 `OpenAI.Codex`，但新版入口是 `app\ChatGPT.exe`。

日志位于启动器同目录：

```text
logs\proxy-YYYYMMDD.log
```

## 配置格式

项目只保留会转化为实际启动参数的设置：

```json
{
  "_comment": "ChatGPT/Codex Proxy Launcher configuration",
  "_version": 1,
  "proxy": {
    "type": "http",
    "host": "127.0.0.1",
    "port": 10808
  },
  "bypass_list": ["<-loopback>", "localhost", "127.0.0.1", "::1", "10.*", "172.16.*", "192.168.*"],
  "disable_quic": true
}
```

## 等价手动命令

默认配置等价于：

```powershell
& (Join-Path (Get-AppxPackage -Name OpenAI.Codex).InstallLocation "app\ChatGPT.exe") --proxy-server="http://127.0.0.1:10808" --proxy-bypass-list="<-loopback>;localhost;127.0.0.1;::1;10.*;172.16.*;172.17.*;172.18.*;172.19.*;172.20.*;172.21.*;172.22.*;172.23.*;172.24.*;172.25.*;172.26.*;172.27.*;172.28.*;172.29.*;172.30.*;172.31.*;192.168.*" --disable-quic
```

启动器的价值是自动定位更新后的 App、检查常见错误，并免去每次输入这条长命令。

## 本地构建

需要 Windows x64、Visual Studio 2022 C++ Build Tools 和 CMake 3.20+：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

也可以运行：

```powershell
.\build.ps1
```
