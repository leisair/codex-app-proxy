#include "common/process_utils.h"

#include "common/string_utils.h"

#include <algorithm>
#include <windows.h>
#include <appmodel.h>
#include <filesystem>
#include <tlhelp32.h>
#include <vector>

namespace codex_proxy {
namespace {

std::wstring NormalizeComparablePath(std::wstring path) {
  std::replace(path.begin(), path.end(), L'/', L'\\');
  if (path.rfind(L"\\\\?\\", 0) == 0) {
    path.erase(0, 4);
  } else if (path.rfind(L"\\??\\", 0) == 0) {
    path.erase(0, 4);
  }
  return ToLower(path);
}

std::wstring QueryProcessImagePath(DWORD pid) {
  HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!process) {
    return {};
  }

  std::wstring path(MAX_PATH, L'\0');
  DWORD size = static_cast<DWORD>(path.size());
  while (!QueryFullProcessImageNameW(process, 0, path.data(), &size)) {
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
      CloseHandle(process);
      return {};
    }
    path.resize(path.size() * 2);
    size = static_cast<DWORD>(path.size());
  }

  CloseHandle(process);
  path.resize(size);
  return path;
}

void AddAppExecutables(const std::wstring& app_dir, std::vector<std::wstring>* candidates) {
  if (!candidates) {
    return;
  }
  const wchar_t* exe_names[] = {L"ChatGPT.exe", L"Codex.exe"};
  for (const wchar_t* exe_name : exe_names) {
    std::wstring candidate = app_dir + L"\\" + exe_name;
    if (FileExists(candidate)) {
      candidates->push_back(candidate);
    }
  }
}

std::vector<std::wstring> FindWindowsAppsCandidates(const std::wstring& package_pattern) {
  std::vector<std::wstring> candidates;
  WIN32_FIND_DATAW data{};
  std::wstring pattern = L"C:\\Program Files\\WindowsApps\\" + package_pattern;
  HANDLE find = FindFirstFileW(pattern.c_str(), &data);
  if (find == INVALID_HANDLE_VALUE) {
    return candidates;
  }

  do {
    if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
      std::wstring path = L"C:\\Program Files\\WindowsApps\\";
      path += data.cFileName;
      path += L"\\app";
      AddAppExecutables(path, &candidates);
    }
  } while (FindNextFileW(find, &data));

  FindClose(find);
  std::sort(candidates.begin(), candidates.end());
  return candidates;
}

std::vector<std::wstring> FindPackagedCandidates(const wchar_t* package_family) {
  std::vector<std::wstring> candidates;
  UINT32 count = 0;
  UINT32 buffer_length = 0;
  LONG rc = GetPackagesByPackageFamily(package_family, &count, nullptr,
                                       &buffer_length, nullptr);
  if (rc != ERROR_INSUFFICIENT_BUFFER && rc != ERROR_SUCCESS) {
    return candidates;
  }
  if (count == 0 || buffer_length == 0) {
    return candidates;
  }

  std::vector<PWSTR> package_names(count);
  std::vector<WCHAR> package_buffer(buffer_length);
  rc = GetPackagesByPackageFamily(package_family, &count, package_names.data(),
                                  &buffer_length, package_buffer.data());
  if (rc != ERROR_SUCCESS) {
    return candidates;
  }

  for (UINT32 i = 0; i < count; ++i) {
    UINT32 path_length = 0;
    rc = GetPackagePathByFullName(package_names[i], &path_length, nullptr);
    if (rc != ERROR_INSUFFICIENT_BUFFER || path_length == 0) {
      continue;
    }

    std::wstring package_path(path_length, L'\0');
    rc = GetPackagePathByFullName(package_names[i], &path_length, package_path.data());
    if (rc != ERROR_SUCCESS) {
      continue;
    }
    if (!package_path.empty() && package_path.back() == L'\0') {
      package_path.pop_back();
    }

    AddAppExecutables(package_path + L"\\app", &candidates);
  }

  std::sort(candidates.begin(), candidates.end());
  return candidates;
}

std::wstring PreferredAppExecutable(std::vector<std::wstring> candidates) {
  if (candidates.empty()) {
    return {};
  }
  std::sort(candidates.begin(), candidates.end());
  for (auto it = candidates.rbegin(); it != candidates.rend(); ++it) {
    if (ToLower(GetBaseName(*it)) == L"chatgpt.exe") {
      return *it;
    }
  }
  return candidates.back();
}

}  // namespace

std::wstring GetExecutablePath() {
  std::wstring path(MAX_PATH, L'\0');
  for (;;) {
    DWORD size = GetModuleFileNameW(nullptr, path.data(),
                                    static_cast<DWORD>(path.size()));
    if (size == 0) {
      return {};
    }
    if (size < path.size() - 1) {
      path.resize(size);
      return path;
    }
    path.resize(path.size() * 2);
  }
}

std::wstring GetExecutableDir() {
  std::wstring path = GetExecutablePath();
  return path.empty() ? L"." : GetDirName(path);
}

std::wstring GetBaseName(const std::wstring& path) {
  return std::filesystem::path(path).filename().wstring();
}

std::wstring GetDirName(const std::wstring& path) {
  return std::filesystem::path(path).parent_path().wstring();
}

bool FileExists(const std::wstring& path) {
  DWORD attrs = GetFileAttributesW(path.c_str());
  return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::wstring FindCodexAppPath() {
  wchar_t found[MAX_PATH]{};
  if (SearchPathW(nullptr, L"chatgpt.exe", nullptr, MAX_PATH, found, nullptr) > 0) {
    std::filesystem::path app_path(found);
    if (FileExists(app_path.wstring())) {
      return app_path.wstring();
    }
  }

  if (SearchPathW(nullptr, L"codex.exe", nullptr, MAX_PATH, found, nullptr) > 0) {
    std::filesystem::path resource_path(found);
    std::filesystem::path maybe_app_dir = resource_path.parent_path().parent_path();
    const wchar_t* exe_names[] = {L"ChatGPT.exe", L"Codex.exe"};
    for (const wchar_t* exe_name : exe_names) {
      std::filesystem::path maybe_app = maybe_app_dir / exe_name;
      if (FileExists(maybe_app.wstring())) {
        return maybe_app.wstring();
      }
    }
  }

  auto packaged_candidates = FindPackagedCandidates(L"OpenAI.Codex_2p2nqsd0c76g0");
  if (!packaged_candidates.empty()) {
    return PreferredAppExecutable(packaged_candidates);
  }

  auto windows_apps_candidates = FindWindowsAppsCandidates(L"OpenAI.Codex_*");
  auto chatgpt_candidates = FindWindowsAppsCandidates(L"OpenAI.ChatGPT_*");
  windows_apps_candidates.insert(windows_apps_candidates.end(), chatgpt_candidates.begin(),
                                 chatgpt_candidates.end());
  if (!windows_apps_candidates.empty()) {
    return PreferredAppExecutable(windows_apps_candidates);
  }

  return {};
}

bool AnyDesktopAppProcessRunning(const std::wstring& app_path) {
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) {
    return false;
  }

  const std::wstring app_dir = NormalizeComparablePath(GetDirName(app_path));
  bool found = false;
  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  if (Process32FirstW(snapshot, &entry)) {
    do {
      std::wstring image_path = QueryProcessImagePath(entry.th32ProcessID);
      if (image_path.empty()) {
        continue;
      }
      const std::wstring name = ToLower(GetBaseName(image_path));
      if ((name == L"chatgpt.exe" || name == L"codex.exe") &&
          NormalizeComparablePath(GetDirName(image_path)) == app_dir) {
        found = true;
        break;
      }
    } while (Process32NextW(snapshot, &entry));
  }

  CloseHandle(snapshot);
  return found;
}

}  // namespace codex_proxy
