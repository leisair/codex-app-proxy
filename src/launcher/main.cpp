#include "common/config.h"
#include "common/launch_args.h"
#include "common/logging.h"
#include "common/network_utils.h"
#include "common/process_utils.h"

#include <iostream>
#include <string>
#include <windows.h>

using namespace codex_proxy;

namespace {

struct Args {
  std::wstring codex_path;
};

Args ParseArgs(int argc, wchar_t** argv) {
  Args args;
  for (int i = 1; i < argc; ++i) {
    std::wstring arg = argv[i];
    if (arg == L"--codex" && i + 1 < argc) {
      args.codex_path = argv[++i];
    }
  }
  return args;
}

int ShowError(const std::wstring& message, int exit_code) {
  Logger::Instance().Error(message);
  std::wcerr << message << L"\n";
  MessageBoxW(nullptr, message.c_str(), L"ChatGPT/Codex Proxy Launcher",
              MB_OK | MB_ICONERROR);
  return exit_code;
}

int StartAppWithProxy(const std::wstring& app_path, const AppConfig& config) {
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};

  std::wstring command = BuildAppCommandLine(app_path, config);
  Logger::Instance().Info(L"Using app path: " + app_path);
  Logger::Instance().Info(L"Starting app with command: " + command);
  BOOL created = CreateProcessW(app_path.c_str(), command.data(), nullptr, nullptr,
                                FALSE, 0, nullptr, GetDirName(app_path).c_str(),
                                &si, &pi);
  if (!created) {
    const std::wstring detail = FormatLastError(L"CreateProcessW failed", GetLastError());
    return ShowError(L"Unable to start ChatGPT/Codex desktop app.\n\n" + detail, 10);
  }

  Logger::Instance().Info(L"Started app PID " + std::to_wstring(pi.dwProcessId));
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  std::wcout << L"ChatGPT/Codex launched with app proxy. Log: "
             << Logger::Instance().LogPath() << L"\n";
  return 0;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  Logger::Instance().Init(L"launcher");
  Args args = ParseArgs(argc, argv);
  const std::wstring config_path = DefaultConfigPath();
  Logger::Instance().Info(L"Using portable config: " + config_path);

  std::wstring error;
  if (!EnsureDefaultConfig(config_path, &error)) {
    return ShowError(L"Unable to create config.json beside the launcher.\n\n" + error, 1);
  }

  AppConfig config;
  if (!LoadConfig(config_path, &config, &error)) {
    return ShowError(L"config.json is invalid.\n\n" + error +
                         L"\n\nOpen config-web.html to create a valid configuration.",
                     1);
  }
  Logger::Instance().Info(L"Configured proxy: " + ProxyUrl(config));

  std::wstring app_path = args.codex_path.empty() ? FindCodexAppPath() : args.codex_path;
  if (app_path.empty() || !FileExists(app_path)) {
    return ShowError(
        L"Unable to locate the Microsoft Store ChatGPT/Codex desktop app.\n\n"
        L"Install or update it from Microsoft Store, then try again.",
        3);
  }

  if (AnyDesktopAppProcessRunning(app_path)) {
    return ShowError(
        L"ChatGPT/Codex desktop app is already running.\n\n"
        L"Fully quit it, including any background process, then run the launcher again.",
        2);
  }

  if (!CanConnectTcp(config.proxy.host, config.proxy.port, 2000, &error)) {
    return ShowError(L"The configured proxy is not reachable at " + ProxyUrl(config) +
                         L".\n\nStart your proxy app and verify its local/mixed port.\n\n" + error,
                     4);
  }

  return StartAppWithProxy(app_path, config);
}
