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

std::wstring GetEnvVar(const wchar_t* name) {
  DWORD size = GetEnvironmentVariableW(name, nullptr, 0);
  if (size == 0) {
    return {};
  }
  std::wstring value(size, L'\0');
  GetEnvironmentVariableW(name, value.data(), size);
  if (!value.empty() && value.back() == L'\0') {
    value.pop_back();
  }
  return value;
}

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

std::vector<std::wstring> FindWindowsAppsCodexCandidates() {
  std::vector<std::wstring> candidates;
  WIN32_FIND_DATAW data{};
  HANDLE find = FindFirstFileW(L"C:\\Program Files\\WindowsApps\\OpenAI.Codex_*", &data);
  if (find == INVALID_HANDLE_VALUE) {
    return candidates;
  }

  do {
    if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
      std::wstring path = L"C:\\Program Files\\WindowsApps\\";
      path += data.cFileName;
      path += L"\\app\\Codex.exe";
      if (FileExists(path)) {
        candidates.push_back(path);
      }
    }
  } while (FindNextFileW(find, &data));

  FindClose(find);
  std::sort(candidates.begin(), candidates.end());
  return candidates;
}

std::vector<std::wstring> FindPackagedCodexCandidates() {
  std::vector<std::wstring> candidates;
  UINT32 count = 0;
  UINT32 buffer_length = 0;
  LONG rc = GetPackagesByPackageFamily(L"OpenAI.Codex_2p2nqsd0c76g0", &count,
                                       nullptr, &buffer_length, nullptr);
  if (rc != ERROR_INSUFFICIENT_BUFFER && rc != ERROR_SUCCESS) {
    return candidates;
  }
  if (count == 0 || buffer_length == 0) {
    return candidates;
  }

  std::vector<PWSTR> package_names(count);
  std::vector<WCHAR> package_buffer(buffer_length);
  rc = GetPackagesByPackageFamily(L"OpenAI.Codex_2p2nqsd0c76g0", &count,
                                  package_names.data(), &buffer_length,
                                  package_buffer.data());
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

    std::wstring candidate = package_path + L"\\app\\Codex.exe";
    if (FileExists(candidate)) {
      candidates.push_back(candidate);
    }
  }

  std::sort(candidates.begin(), candidates.end());
  return candidates;
}

DWORD FindNewestProcessIdByPath(const std::wstring& process_path) {
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) {
    return 0;
  }

  std::wstring target = NormalizeComparablePath(process_path);
  DWORD best = 0;
  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  if (Process32FirstW(snapshot, &entry)) {
    do {
      if (entry.th32ProcessID <= best) {
        continue;
      }
      std::wstring path = QueryProcessImagePath(entry.th32ProcessID);
      if (NormalizeComparablePath(path) == target) {
        best = entry.th32ProcessID;
      }
    } while (Process32NextW(snapshot, &entry));
  }

  CloseHandle(snapshot);
  return best;
}

}  // namespace

std::wstring GetUserProfileDir() {
  std::wstring value = GetEnvVar(L"USERPROFILE");
  return value.empty() ? L"." : value;
}

std::wstring GetLocalAppDataDir() {
  std::wstring value = GetEnvVar(L"LOCALAPPDATA");
  return value.empty() ? GetUserProfileDir() + L"\\AppData\\Local" : value;
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

bool DirectoryExists(const std::wstring& path) {
  DWORD attrs = GetFileAttributesW(path.c_str());
  return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

std::wstring FindCodexAppPath() {
  wchar_t found[MAX_PATH]{};
  if (SearchPathW(nullptr, L"codex.exe", nullptr, MAX_PATH, found, nullptr) > 0) {
    std::filesystem::path resource_path(found);
    std::filesystem::path maybe_app = resource_path.parent_path().parent_path() / L"Codex.exe";
    if (FileExists(maybe_app.wstring())) {
      return maybe_app.wstring();
    }
  }

  auto packaged_candidates = FindPackagedCodexCandidates();
  if (!packaged_candidates.empty()) {
    return packaged_candidates.back();
  }

  auto windows_apps_candidates = FindWindowsAppsCodexCandidates();
  if (!windows_apps_candidates.empty()) {
    return windows_apps_candidates.back();
  }

  return {};
}

bool AnyProcessRunningAtPath(const std::wstring& process_path) {
  return FindNewestProcessIdByPath(process_path) != 0;
}

}  // namespace codex_proxy
