#pragma once

#include <string>
#include <vector>
#include <windows.h>

namespace codex_proxy {

struct ProcessInfo {
  DWORD pid = 0;
  DWORD parent_pid = 0;
  std::wstring name;
  std::wstring path;
};

std::wstring GetUserProfileDir();
std::wstring GetLocalAppDataDir();
std::wstring GetCurrentModulePath();
std::wstring GetModuleDir(HMODULE module);
std::wstring GetBaseName(const std::wstring& path);
std::wstring GetDirName(const std::wstring& path);
bool FileExists(const std::wstring& path);
bool DirectoryExists(const std::wstring& path);
std::wstring FindCodexAppPath();
bool InjectDllIntoProcess(HANDLE process, const std::wstring& dll_path, std::wstring* error);
DWORD FindNewestProcessIdByName(const std::wstring& process_name, DWORD after_pid = 0);
DWORD FindNewestProcessIdByPath(const std::wstring& process_path, DWORD after_pid = 0);
bool AnyProcessRunningAtPath(const std::wstring& process_path);
std::vector<ProcessInfo> EnumerateCodexAppProcesses(const std::wstring& app_dir);

}  // namespace codex_proxy
