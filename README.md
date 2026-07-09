# Codex Proxy Launcher

Windows-only transparent proxy launcher for the Codex desktop app.

The launcher starts the Codex Microsoft Store app executable, injects
`codex_proxy_hook.dll`, performs a short startup sweep to inject Codex child
processes, and then exits. The DLL hooks the Codex process tree so non-local TCP
traffic is tunneled through a local HTTP/SOCKS5 proxy such as V2RayN mixed port
`127.0.0.1:10808`.

## Status

This is an early v1 implementation. It is intentionally scoped to Codex:

- Allowed process names are `Codex.exe` and `codex.exe` by default.
- No service, no tray process, no auto-start, and no global proxy changes.
- Localhost and private network destinations are bypassed by default.

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
- `codex_proxy_hook.dll`
- `config-web.html`
- `config.json`

## Use

1. Keep V2RayN running with a mixed HTTP/SOCKS port on `127.0.0.1:10808`.
2. Run `CodexProxyLauncher.exe`.
3. If `%USERPROFILE%\.codex-proxy\config.json` does not exist, the launcher
   creates it with safe defaults and continues.

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
