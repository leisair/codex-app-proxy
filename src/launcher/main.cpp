#include "common/config.h"
#include "common/logging.h"
#include "common/process_utils.h"
#include "common/string_utils.h"

#include <algorithm>
#include <iostream>
#include <filesystem>
#include <set>
#include <shellapi.h>
#include <string>
#include <thread>
#include <vector>
#include <windows.h>

using namespace codex_proxy;

namespace {

struct Args {
  bool attach_existing = false;
  std::wstring config_path = DefaultConfigPath();
  std::wstring codex_path;
};

Args ParseArgs(int argc, wchar_t** argv) {
  Args args;
  for (int i = 1; i < argc; ++i) {
    std::wstring arg = argv[i];
    if (arg == L"--attach-existing") {
      args.attach_existing = true;
    } else if (arg == L"--config" && i + 1 < argc) {
      args.config_path = argv[++i];
    } else if (arg == L"--codex" && i + 1 < argc) {
      args.codex_path = argv[++i];
    }
  }
  return args;
}

std::wstring HookDllPath() {
  std::wstring dir = GetDirName(GetCurrentModulePath());
  std::wstring path = dir + L"\\codex_proxy_hook.dll";
  if (FileExists(path)) {
    return path;
  }
  return std::filesystem::current_path().wstring() + L"\\codex_proxy_hook.dll";
}

std::wstring AppUserModelIdFromCodexPath(const std::wstring& codex_path) {
  std::filesystem::path path(codex_path);
  for (const auto& part : path) {
    std::wstring name = part.wstring();
    if (name.rfind(L"OpenAI.Codex_", 0) != 0) {
      continue;
    }
    size_t publisher = name.rfind(L"__");
    if (publisher == std::wstring::npos) {
      continue;
    }
    size_t version_sep = name.find(L"_", std::wstring(L"OpenAI.Codex_").size());
    if (version_sep == std::wstring::npos || version_sep >= publisher) {
      continue;
    }
    std::wstring package_name = name.substr(0, std::wstring(L"OpenAI.Codex").size());
    std::wstring publisher_id = name.substr(publisher + 2);
    return package_name + L"_" + publisher_id + L"!App";
  }
  return L"OpenAI.Codex_2p2nqsd0c76g0!App";
}

int InjectProcessByPid(DWORD pid, const std::wstring& dll_path);

void AppendUniqueProcesses(std::vector<ProcessInfo>* target,
                           const std::vector<ProcessInfo>& source) {
  if (!target) {
    return;
  }
  std::set<DWORD> existing;
  for (const auto& process : *target) {
    existing.insert(process.pid);
  }
  for (const auto& process : source) {
    if (existing.insert(process.pid).second) {
      target->push_back(process);
    }
  }
}

bool IsTargetDesktopChildProcess(const ProcessInfo& process) {
  std::wstring path = ToLower(process.path);
  std::wstring command = ToLower(process.command_line);
  std::replace(path.begin(), path.end(), L'/', L'\\');
  std::replace(command.begin(), command.end(), L'/', L'\\');

  if (path.find(L"\\app\\resources\\codex.exe") != std::wstring::npos ||
      command.find(L"\\app\\resources\\codex.exe") != std::wstring::npos) {
    return command.find(L" app-server") != std::wstring::npos ||
           command.find(L"\\resources\\codex.exe\" app-server") != std::wstring::npos;
  }

  std::wstring base = path.empty() ? ToLower(process.name) : GetBaseName(path);
  if (base != L"codex.exe") {
    return false;
  }
  return command.find(L"--type=utility") != std::wstring::npos &&
         command.find(L"network.mojom.networkservice") != std::wstring::npos;
}

int AttachExisting(const std::wstring& codex_path, const std::wstring& dll_path) {
  std::wstring app_dir = GetDirName(codex_path);
  auto processes = EnumerateCodexAppProcesses(app_dir);
  if (processes.empty()) {
    std::wcerr << L"No running Codex desktop app processes found.\n";
    return 2;
  }

  int injected = 0;
  for (const auto& process_info : processes) {
    if (!IsTargetDesktopChildProcess(process_info)) {
      Logger::Instance().Info(L"Attach skipped PID " +
                              std::to_wstring(process_info.pid) + L" " +
                              process_info.command_line);
      continue;
    }
    if (InjectProcessByPid(process_info.pid, dll_path) == 0) {
      ++injected;
      Logger::Instance().Info(L"Attached existing PID " +
                              std::to_wstring(process_info.pid) + L" " +
                              process_info.path);
    }
  }
  std::wcout << L"Attached " << injected << L" Codex desktop app process(es).\n";
  return injected > 0 ? 0 : 4;
}

int StartSuspendedAndInject(const std::wstring& codex_path,
                            const std::wstring& dll_path,
                            DWORD* launched_pid) {
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  std::wstring command = L"\"" + codex_path + L"\"";

  BOOL created = CreateProcessW(codex_path.c_str(), command.data(), nullptr, nullptr,
                                FALSE, CREATE_SUSPENDED, nullptr,
                                GetDirName(codex_path).c_str(), &si, &pi);
  if (!created) {
    Logger::Instance().Warn(FormatLastError(L"CreateProcessW suspended failed", GetLastError()));
    return -1;
  }

  std::wstring error;
  if (!InjectDllIntoProcess(pi.hProcess, dll_path, &error)) {
    Logger::Instance().Error(L"Initial injection failed: " + error);
    TerminateProcess(pi.hProcess, 1);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 5;
  }

  ResumeThread(pi.hThread);
  Logger::Instance().Info(L"Started and injected Codex PID " + std::to_wstring(pi.dwProcessId));
  if (launched_pid) {
    *launched_pid = pi.dwProcessId;
  }
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return 0;
}

int ShellLaunchAndAttach(const std::wstring& codex_path, const std::wstring& dll_path,
                         DWORD* launched_pid) {
  Logger::Instance().Warn(
      L"Using fallback launch + attach. Earliest startup requests may not be covered.");
  DWORD before = FindNewestProcessIdByPath(codex_path);
  HINSTANCE result = ShellExecuteW(nullptr, L"open", codex_path.c_str(), nullptr,
                                   GetDirName(codex_path).c_str(), SW_SHOWNORMAL);
  if (reinterpret_cast<INT_PTR>(result) <= 32) {
    std::wstring app_uri = L"shell:AppsFolder\\" + AppUserModelIdFromCodexPath(codex_path);
    Logger::Instance().Warn(L"Direct ShellExecuteW failed; trying App activation " + app_uri);
    result = ShellExecuteW(nullptr, L"open", app_uri.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32) {
      std::wcerr << L"ShellExecuteW and App activation failed.\n";
      return 6;
    }
  }

  DWORD pid = 0;
  for (int i = 0; i < 50; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    pid = FindNewestProcessIdByPath(codex_path, before);
    if (pid != 0) {
      break;
    }
  }
  if (pid == 0) {
    std::wcerr << L"Codex launched, but no new Codex.exe PID was detected.\n";
    return 7;
  }

  HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                                   PROCESS_VM_OPERATION | PROCESS_VM_WRITE |
                                   PROCESS_VM_READ,
                               FALSE, pid);
  if (!process) {
    std::wcerr << L"OpenProcess failed for fallback PID " << pid << L".\n";
    return 8;
  }
  std::wstring error;
  bool ok = InjectDllIntoProcess(process, dll_path, &error);
  CloseHandle(process);
  if (!ok) {
    std::wcerr << L"Fallback injection failed: " << error << L"\n";
    return 9;
  }
  if (launched_pid) {
    *launched_pid = pid;
  }
  return 0;
}

int InjectProcessByPid(DWORD pid, const std::wstring& dll_path) {
  HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                                   PROCESS_VM_OPERATION | PROCESS_VM_WRITE |
                                   PROCESS_VM_READ,
                               FALSE, pid);
  if (!process) {
    Logger::Instance().Warn(FormatLastError(L"OpenProcess failed for PID " +
                                                std::to_wstring(pid),
                                            GetLastError()));
    return 1;
  }
  std::wstring error;
  bool ok = InjectDllIntoProcess(process, dll_path, &error);
  CloseHandle(process);
  if (!ok) {
    Logger::Instance().Warn(L"Startup sweep injection failed for PID " +
                            std::to_wstring(pid) + L": " + error);
    return 2;
  }
  return 0;
}

void StartupInjectionSweep(const std::wstring& codex_path,
                           const std::wstring& dll_path,
                           DWORD launched_pid) {
  std::wstring app_dir = GetDirName(codex_path);
  std::set<DWORD> seen;

  Logger::Instance().Info(L"Starting 180s startup injection sweep for " + app_dir);
  if (launched_pid != 0) {
    Logger::Instance().Info(L"Startup sweep launched PID hint " +
                            std::to_wstring(launched_pid));
  }
  size_t last_candidate_count = static_cast<size_t>(-1);
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(180);
  while (std::chrono::steady_clock::now() < deadline) {
    auto processes = EnumerateCodexAppProcesses(app_dir);
    if (launched_pid != 0) {
      AppendUniqueProcesses(&processes, EnumerateCodexProcessTree(launched_pid));
    }
    if (processes.size() != last_candidate_count) {
      last_candidate_count = processes.size();
      Logger::Instance().Info(L"Startup sweep sees " +
                              std::to_wstring(processes.size()) +
                              L" Codex app process candidate(s)");
      for (const auto& process : processes) {
        Logger::Instance().Info(L"Startup sweep candidate PID " +
                                std::to_wstring(process.pid) + L" parent " +
                                std::to_wstring(process.parent_pid) + L" " +
                                process.command_line);
      }
    }

    for (const auto& process : processes) {
      if (seen.count(process.pid) != 0) {
        continue;
      }
      if (!IsTargetDesktopChildProcess(process)) {
        continue;
      }
      seen.insert(process.pid);
      if (InjectProcessByPid(process.pid, dll_path) == 0) {
        Logger::Instance().Info(L"Startup sweep injected PID " +
                                std::to_wstring(process.pid) + L" " + process.path);
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
  }
  Logger::Instance().Info(L"Startup injection sweep complete");
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

  std::wstring dll_path = HookDllPath();
  if (!FileExists(dll_path)) {
    std::wcerr << L"Missing codex_proxy_hook.dll next to launcher: " << dll_path << L"\n";
    return 1;
  }

  std::wstring codex_path = args.codex_path.empty() ? FindCodexAppPath() : args.codex_path;
  if (codex_path.empty() || !FileExists(codex_path)) {
    std::wcerr << L"Unable to locate Codex.exe. Use --codex <path>.\n";
    return 3;
  }

  if (args.attach_existing) {
    return AttachExisting(codex_path, dll_path);
  }

  if (AnyProcessRunningAtPath(codex_path)) {
    std::wcerr << L"Codex desktop app is already running. Close Codex desktop first, "
                  L"or use --attach-existing for diagnostic attach mode.\n";
    return 2;
  }

  Logger::Instance().Info(L"Using Codex path: " + codex_path);
  DWORD launched_pid = 0;
  int result = StartSuspendedAndInject(codex_path, dll_path, &launched_pid);
  if (result == -1) {
    result = ShellLaunchAndAttach(codex_path, dll_path, &launched_pid);
  }
  if (result == 0) {
    StartupInjectionSweep(codex_path, dll_path, launched_pid);
    std::wcout << L"Codex launched through proxy hook. Log: "
               << Logger::Instance().LogPath() << L"\n";
  }
  return result;
}
