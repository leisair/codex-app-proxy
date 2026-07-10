#include "common/config.h"
#include "common/logging.h"
#include "common/process_utils.h"
#include "common/string_utils.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include <windows.h>

using namespace codex_proxy;

namespace {

struct Args {
  std::wstring config_path = DefaultConfigPath();
  std::wstring codex_path;
};

Args ParseArgs(int argc, wchar_t** argv) {
  Args args;
  for (int i = 1; i < argc; ++i) {
    std::wstring arg = argv[i];
    if (arg == L"--config" && i + 1 < argc) {
      args.config_path = argv[++i];
    } else if (arg == L"--codex" && i + 1 < argc) {
      args.codex_path = argv[++i];
    }
  }
  return args;
}

std::wstring ProxyUrl(const AppConfig& config) {
  return Utf8ToWide(ProxyTypeToString(config.proxy.type)) + L"://" +
         Utf8ToWide(config.proxy.host) + L":" +
         std::to_wstring(config.proxy.port);
}

std::wstring JoinBypassList(const std::vector<std::string>& values) {
  std::wstring result;
  for (const auto& value : values) {
    if (!result.empty()) {
      result += L";";
    }
    result += Utf8ToWide(value);
  }
  return result;
}

std::wstring BuildCommandLine(const std::wstring& app_path, const AppConfig& config) {
  std::wstring command = L"\"" + app_path + L"\" " +
      L"--proxy-server=\"" + ProxyUrl(config) + L"\"";

  std::wstring bypass = JoinBypassList(config.bypass_list);
  if (!bypass.empty()) {
    command += L" --proxy-bypass-list=\"" + bypass + L"\"";
  }
  if (config.disable_quic) {
    command += L" --disable-quic";
  }
  return command;
}

int StartAppWithProxy(const std::wstring& app_path, const AppConfig& config) {
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};

  std::wstring command = BuildCommandLine(app_path, config);
  Logger::Instance().Info(L"Using app path: " + app_path);
  Logger::Instance().Info(L"Starting app with command: " + command);
  BOOL created = CreateProcessW(app_path.c_str(), command.data(), nullptr, nullptr,
                                FALSE, 0, nullptr, GetDirName(app_path).c_str(),
                                &si, &pi);
  if (!created) {
    Logger::Instance().Warn(FormatLastError(L"CreateProcessW failed", GetLastError()));
    std::wcerr << L"Unable to start ChatGPT/Codex desktop app.\n";
    return 10;
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

  std::wstring error;
  if (!EnsureDefaultConfig(args.config_path, &error)) {
    std::wcerr << L"Unable to create default config: " << error << L"\n";
    return 1;
  }

  AppConfig config;
  if (!LoadConfig(args.config_path, &config, &error)) {
    std::wcerr << L"Unable to load config: " << error << L"\n";
    return 1;
  }
  if (!SaveConfig(args.config_path, config, &error)) {
    Logger::Instance().Warn(L"Unable to normalize config: " + error);
  }

  std::wstring app_path = args.codex_path.empty() ? FindCodexAppPath() : args.codex_path;
  if (app_path.empty() || !FileExists(app_path)) {
    std::wcerr << L"Unable to locate ChatGPT.exe or Codex.exe. Use --codex <path>.\n";
    return 3;
  }

  if (AnyProcessRunningAtPath(app_path)) {
    std::wcerr << L"ChatGPT/Codex desktop app is already running. Close it first.\n";
    return 2;
  }

  return StartAppWithProxy(app_path, config);
}
