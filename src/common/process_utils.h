#pragma once

#include <string>
#include <windows.h>

namespace codex_proxy {

std::wstring GetUserProfileDir();
std::wstring GetLocalAppDataDir();
std::wstring GetCurrentModulePath();
std::wstring GetModuleDir(HMODULE module);
std::wstring GetBaseName(const std::wstring& path);
std::wstring GetDirName(const std::wstring& path);
bool FileExists(const std::wstring& path);
bool DirectoryExists(const std::wstring& path);
bool AnyCodexProcessRunning();
std::wstring FindCodexAppPath();
bool InjectDllIntoProcess(HANDLE process, const std::wstring& dll_path, std::wstring* error);
DWORD FindNewestProcessIdByName(const std::wstring& process_name, DWORD after_pid = 0);

}  // namespace codex_proxy
