# ChatGPT/Codex Proxy Launcher

Windows-only per-app proxy launcher for the Microsoft Store ChatGPT desktop app
that now contains Codex.

The current Microsoft Store update may still use the package identity
`OpenAI.Codex`, but the app directory contains the new `app\ChatGPT.exe` entry.
This launcher starts that desktop app with Chromium proxy flags. It does not
change the Windows system proxy, does not require TUN mode, and does not modify
the app process.

The default target is V2RayN mixed proxy on `127.0.0.1:10808`.

## What It Does

- Locates the Microsoft Store app package.
- Prefers `app\ChatGPT.exe`, then falls back to `app\Codex.exe`.
- Starts the app with `--proxy-server=...`.
- Adds a Chromium `--proxy-bypass-list=...` for localhost and private networks.
- Adds `--disable-quic` by default.

It is intentionally not a transparent proxy, system proxy manager, TUN tool,
tray app, background service, or environment-variable shim.

## Equivalent Manual Command

The launcher is equivalent to this PowerShell command with default config:

```powershell
& (Join-Path (Get-AppxPackage -Name OpenAI.Codex).InstallLocation "app\ChatGPT.exe") --proxy-server="http://127.0.0.1:10808" --proxy-bypass-list="<-loopback>;localhost;127.0.0.1;::1;10.*;172.16.*;172.17.*;172.18.*;172.19.*;172.20.*;172.21.*;172.22.*;172.23.*;172.24.*;172.25.*;172.26.*;172.27.*;172.28.*;172.29.*;172.30.*;172.31.*;192.168.*" --disable-quic
```

The value of this project is making that long command repeatable,
configurable, and less error-prone.

## Quick Start

Use the launcher when you want ChatGPT/Codex desktop to use V2RayN without
turning on Windows system proxy or TUN mode.

1. Keep V2RayN running with mixed port `127.0.0.1:10808`.
2. Keep Windows system proxy off.
3. Keep V2RayN TUN mode off.
4. Close existing ChatGPT/Codex desktop app windows.
5. Run `CodexProxyLauncher.exe`.

With the default config, the launcher starts the Microsoft Store app through:

```text
http://127.0.0.1:10808
```

It automatically prefers the new `ChatGPT.exe` entry from the updated Microsoft
Store app. If only the older `Codex.exe` entry exists, it falls back to that.

## Config File

On first run, the launcher creates:

```text
%USERPROFILE%\.codex-proxy\config.json
```

You can edit the JSON directly or open `config-web.html` locally to import,
edit, validate, and export the same config format. The HTML file is static and
does not start a local server.

If an older config contains removed fields such as `log_level` or
`set_proxy_environment`, the launcher rewrites the file using the current
minimal schema after it loads successfully.

## Config

Only settings that affect the proven launch command are kept:

```json
{
  "_comment": "ChatGPT/Codex Proxy Launcher configuration",
  "_version": 1,
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
  "disable_quic": true
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

After launching through the launcher:

```powershell
$ids = Get-Process | Where-Object { $_.ProcessName -match '^ChatGPT$|^Codex$|^codex$' } | Select-Object -ExpandProperty Id
Get-NetTCPConnection -OwningProcess $ids -ErrorAction SilentlyContinue |
  Select-Object OwningProcess,LocalAddress,LocalPort,RemoteAddress,RemotePort,State |
  Sort-Object OwningProcess,RemoteAddress
```

The app should primarily connect to `127.0.0.1:10808` instead of direct remote
`:443` connections.

Logs are written to:

```text
%LOCALAPPDATA%\CodexProxy\logs\proxy-YYYYMMDD.log
```
