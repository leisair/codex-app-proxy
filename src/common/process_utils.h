#pragma once

#include <string>

namespace codex_proxy {

std::wstring GetExecutablePath();
std::wstring GetExecutableDir();
std::wstring GetBaseName(const std::wstring& path);
std::wstring GetDirName(const std::wstring& path);
bool FileExists(const std::wstring& path);
std::wstring FindCodexAppPath();
bool AnyDesktopAppProcessRunning(const std::wstring& app_path);

}  // namespace codex_proxy
