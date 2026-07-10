# Codex Proxy Launcher

Windows-only per-app proxy launcher for the Codex desktop app.

This project packages the launch command that has proven to work for Codex
desktop: start `Codex.exe` with Chromium proxy flags. It does not change the
Windows system proxy, does not require TUN mode, and does not modify Codex
process memory.

The default target is V2RayN mixed proxy on `127.0.0.1:10808`.

## What It Does

- Locates the Microsoft Store Codex app executable.
- Starts Codex with `--proxy-server=...`.
- Adds a Chromium `--proxy-bypass-list=...` for localhost and private networks.
- Adds `--disable-quic` by default.
- Optionally sets `HTTP_PROXY`, `HTTPS_PROXY`, and `ALL_PROXY` only for the
  Codex process when `set_proxy_environment` is enabled.

It is intentionally not a transparent proxy, system proxy manager, TUN tool,
tray app, or background service.

## Equivalent Manual Command

The launcher is equivalent to this PowerShell flow with default config:

```powershell
$codex = Join-Path (Get-AppxPackage -Name OpenAI.Codex).InstallLocation "app\Codex.exe"
& $codex --proxy-server="http://127.0.0.1:10808" --proxy-bypass-list="<-loopback>;localhost;127.0.0.1;::1;10.*;172.16.*;172.17.*;172.18.*;172.19.*;172.20.*;172.21.*;172.22.*;172.23.*;172.24.*;172.25.*;172.26.*;172.27.*;172.28.*;172.29.*;172.30.*;172.31.*;192.168.*" --disable-quic
```

The value of this project is making that command repeatable, configurable, and
less error-prone.

## Use

1. Keep V2RayN running with mixed port `127.0.0.1:10808`.
2. Close existing Codex desktop app processes.
3. Run `CodexProxyLauncher.exe`.

On first run, the launcher creates:

```text
%USERPROFILE%\.codex-proxy\config.json
```

You can edit the JSON directly or open `config-web.html` locally to import,
edit, validate, and export the same config format. The HTML file is static and
does not start a local server.

## Config

```json
{
  "_comment": "Codex Proxy Launcher configuration",
  "_version": 1,
  "log_level": "info",
  "proxy": {
    "type": "http",
    "host": "127.0.0.1",
    "port": 10808
  },
  "bypass_list": [
    "<-loopback>",
    "localhost",
    "127.0.0.1",
    "::1",
    "10.*",
    "172.16.*",
    "172.17.*",
    "172.18.*",
    "172.19.*",
    "172.20.*",
    "172.21.*",
    "172.22.*",
    "172.23.*",
    "172.24.*",
    "172.25.*",
    "172.26.*",
    "172.27.*",
    "172.28.*",
    "172.29.*",
    "172.30.*",
    "172.31.*",
    "192.168.*"
  ],
  "disable_quic": true,
  "set_proxy_environment": false
}
```

## Build

Requirements:

- Windows x64
- Visual Studio 2022 C++ build tools
- CMake 3.20+

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Or:

```powershell
.\build.ps1
```

The packaged output contains:

- `CodexProxyLauncher.exe`
- `config-web.html`
- `config.json`

## Verify

After launching Codex through the launcher:

```powershell
$ids = Get-Process | Where-Object { $_.ProcessName -match '^Codex$|^codex$' } | Select-Object -ExpandProperty Id
Get-NetTCPConnection -OwningProcess $ids -ErrorAction SilentlyContinue |
  Select-Object OwningProcess,LocalAddress,LocalPort,RemoteAddress,RemotePort,State |
  Sort-Object OwningProcess,RemoteAddress
```

Codex traffic should primarily connect to `127.0.0.1:10808` instead of direct
remote `:443` connections.

Logs are written to:

```text
%LOCALAPPDATA%\CodexProxy\logs\proxy-YYYYMMDD.log
```
