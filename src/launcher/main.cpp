#include "common/config.h"
#include "common/logging.h"
#include "common/process_utils.h"
#include "common/string_utils.h"

#include <algorithm>
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

std::wstring BuildEnvironmentBlockWithProxy(const AppConfig& config) {
  LPWCH current = GetEnvironmentStringsW();
  if (!current) {
    return {};
  }

  std::vector<std::wstring> entries;
  for (LPWCH item = current; *item; item += wcslen(item) + 1) {
    std::wstring entry = item;
    std::wstring lower = ToLower(entry);
    if (lower.rfind(L"http_proxy=", 0) == 0 ||
        lower.rfind(L"https_proxy=", 0) == 0 ||
        lower.rfind(L"all_proxy=", 0) == 0 ||
        lower.rfind(L"no_proxy=", 0) == 0) {
      continue;
    }
    entries.push_back(entry);
  }
  FreeEnvironmentStringsW(current);

  std::wstring proxy = ProxyUrl(config);
  entries.push_back(L"HTTP_PROXY=" + proxy);
  entries.push_back(L"HTTPS_PROXY=" + proxy);
  entries.push_back(L"ALL_PROXY=" + proxy);
  entries.push_back(L"http_proxy=" + proxy);
  entries.push_back(L"https_proxy=" + proxy);
  entries.push_back(L"all_proxy=" + proxy);
  entries.push_back(L"NO_PROXY=localhost,127.0.0.1,::1");
  entries.push_back(L"no_proxy=localhost,127.0.0.1,::1");

  std::sort(entries.begin(), entries.end(),
            [](const std::wstring& left, const std::wstring& right) {
              return ToLower(left) < ToLower(right);
            });

  std::wstring block;
  for (const auto& entry : entries) {
    block += entry;
    block.push_back(L'\0');
  }
  block.push_back(L'\0');
  return block;
}

std::wstring BuildCommandLine(const std::wstring& codex_path, const AppConfig& config) {
  std::wstring command = L"\"" + codex_path + L"\" " +
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

int StartCodexWithProxy(const std::wstring& codex_path, const AppConfig& config) {
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};

  std::wstring command = BuildCommandLine(codex_path, config);
  std::wstring environment;
  LPVOID environment_ptr = nullptr;
  DWORD creation_flags = 0;
  if (config.set_proxy_environment) {
    environment = BuildEnvironmentBlockWithProxy(config);
    if (!environment.empty()) {
      environment_ptr = environment.data();
      creation_flags |= CREATE_UNICODE_ENVIRONMENT;
    }
  }

  Logger::Instance().Info(L"Using Codex path: " + codex_path);
  Logger::Instance().Info(L"Starting Codex with command: " + command);
  BOOL created = CreateProcessW(codex_path.c_str(), command.data(), nullptr, nullptr,
                                FALSE, creation_flags, environment_ptr,
                                GetDirName(codex_path).c_str(), &si, &pi);
  if (!created) {
    Logger::Instance().Warn(FormatLastError(L"CreateProcessW failed", GetLastError()));
    std::wcerr << L"Unable to start Codex.\n";
    return 10;
  }

  Logger::Instance().Info(L"Started Codex PID " + std::to_wstring(pi.dwProcessId));
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  std::wcout << L"Codex launched with app proxy. Log: "
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

  std::wstring codex_path = args.codex_path.empty() ? FindCodexAppPath() : args.codex_path;
  if (codex_path.empty() || !FileExists(codex_path)) {
    std::wcerr << L"Unable to locate Codex.exe. Use --codex <path>.\n";
    return 3;
  }

  if (AnyProcessRunningAtPath(codex_path)) {
    std::wcerr << L"Codex desktop app is already running. Close Codex desktop first.\n";
    return 2;
  }

  return StartCodexWithProxy(codex_path, config);
}
