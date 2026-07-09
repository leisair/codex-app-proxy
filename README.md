# Codex Proxy Launcher

Windows-only per-app proxy launcher for the Codex desktop app.

The launcher starts the Codex Microsoft Store app executable with Chromium
proxy flags and process-local proxy environment variables. It does not change
the Windows system proxy and does not require TUN mode. The default target is a
local HTTP/SOCKS mixed proxy such as V2RayN on `127.0.0.1:10808`.

## Status

This is an early v1 implementation. It is intentionally scoped to Codex:

- Default mode does not inject DLLs.
- No service, no tray process, no auto-start, and no global proxy changes.
- Localhost and private network destinations are bypassed by default.
- `--hook` keeps the older experimental DLL hook path for diagnostics only.

## Build

Requirements:

- Windows x64
- Visual Studio 2022 C++ build tools
- CMake 3.20+

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Outputs are under `build\Release`.

### GitHub Actions build

Pushing to `main` runs `.github/workflows/windows-build.yml` on
`windows-latest`. The workflow uploads `codex-app-proxy-windows-x64.zip`
containing:

- `CodexProxyLauncher.exe`
- `codex_proxy_hook.dll` for optional `--hook` diagnostics
- `config-web.html`
- `config.json`

## Use

1. Keep V2RayN running with a mixed HTTP/SOCKS port on `127.0.0.1:10808`.
2. Run `CodexProxyLauncher.exe`.
3. If `%USERPROFILE%\.codex-proxy\config.json` does not exist, the launcher
   creates it with safe defaults and continues.

The launcher passes these settings only to the Codex process:

- `--proxy-server=...`
- `--proxy-bypass-list=...`
- `--disable-quic`
- `HTTP_PROXY`, `HTTPS_PROXY`, `ALL_PROXY`, and lowercase variants

Legacy hook mode is available for diagnostics:

```powershell
.\CodexProxyLauncher.exe --hook
```

Open `resources/config-web.html` locally to import, edit, and export
`config.json`. The HTML file does not start a local server.

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
