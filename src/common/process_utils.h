#pragma once

#include <string>

namespace codex_proxy {

std::wstring GetUserProfileDir();
std::wstring GetLocalAppDataDir();
std::wstring GetBaseName(const std::wstring& path);
std::wstring GetDirName(const std::wstring& path);
bool FileExists(const std::wstring& path);
bool DirectoryExists(const std::wstring& path);
std::wstring FindCodexAppPath();
bool AnyProcessRunningAtPath(const std::wstring& process_path);

}  // namespace codex_proxy
