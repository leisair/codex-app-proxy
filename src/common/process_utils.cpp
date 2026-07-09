#include "common/process_utils.h"

#include "common/string_utils.h"

#include <algorithm>
#include <windows.h>
#include <appmodel.h>
#include <filesystem>
#include <set>
#include <tlhelp32.h>
#include <vector>

namespace codex_proxy {
namespace {

using NtQueryInformationProcessFn = LONG(WINAPI*)(HANDLE, int, PVOID, ULONG, PULONG);

struct UnicodeStringLocal {
  USHORT length = 0;
  USHORT maximum_length = 0;
  PWSTR buffer = nullptr;
};

struct RtlUserProcessParametersLocal {
  BYTE reserved1[16];
  PVOID reserved2[10];
  UnicodeStringLocal image_path_name;
  UnicodeStringLocal command_line;
};

struct PebLocal {
  BYTE reserved1[2];
  BYTE being_debugged = 0;
  BYTE reserved2[1];
  PVOID reserved3[2];
  PVOID ldr = nullptr;
  RtlUserProcessParametersLocal* process_parameters = nullptr;
};

struct ProcessBasicInformationLocal {
  PVOID reserved1 = nullptr;
  PebLocal* peb_base_address = nullptr;
  PVOID reserved2[2];
  ULONG_PTR unique_process_id = 0;
  PVOID reserved3 = nullptr;
};

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

bool IsCodexAppProcessName(const std::wstring& name) {
  std::wstring lower = ToLower(GetBaseName(name));
  return lower == L"codex.exe";
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

std::wstring QueryProcessCommandLine(DWORD pid) {
  HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ,
                               FALSE, pid);
  if (!process) {
    return {};
  }

  HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  auto query_process = reinterpret_cast<NtQueryInformationProcessFn>(
      GetProcAddress(ntdll, "NtQueryInformationProcess"));
  if (!query_process) {
    CloseHandle(process);
    return {};
  }

  ProcessBasicInformationLocal basic{};
  LONG status = query_process(process, 0, &basic, sizeof(basic), nullptr);
  if (status < 0 || !basic.peb_base_address) {
    CloseHandle(process);
    return {};
  }

  PebLocal peb{};
  SIZE_T bytes_read = 0;
  if (!ReadProcessMemory(process, basic.peb_base_address, &peb, sizeof(peb),
                         &bytes_read) ||
      !peb.process_parameters) {
    CloseHandle(process);
    return {};
  }

  RtlUserProcessParametersLocal parameters{};
  if (!ReadProcessMemory(process, peb.process_parameters, &parameters,
                         sizeof(parameters), &bytes_read) ||
      !parameters.command_line.buffer || parameters.command_line.length == 0) {
    CloseHandle(process);
    return {};
  }

  std::wstring command_line(parameters.command_line.length / sizeof(wchar_t), L'\0');
  if (!ReadProcessMemory(process, parameters.command_line.buffer,
                         command_line.data(), parameters.command_line.length,
                         &bytes_read)) {
    CloseHandle(process);
    return {};
  }
  CloseHandle(process);
  return command_line;
}

std::vector<std::wstring> FindWindowsAppsCodexCandidates() {
  std::vector<std::wstring> candidates;
  std::wstring pattern = L"C:\\Program Files\\WindowsApps\\OpenAI.Codex_*";
  WIN32_FIND_DATAW data{};
  HANDLE find = FindFirstFileW(pattern.c_str(), &data);
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
    rc = GetPackagePathByFullName(package_names[i], &path_length,
                                  package_path.data());
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

}  // namespace

std::wstring GetUserProfileDir() {
  std::wstring value = GetEnvVar(L"USERPROFILE");
  return value.empty() ? L"." : value;
}

std::wstring GetLocalAppDataDir() {
  std::wstring value = GetEnvVar(L"LOCALAPPDATA");
  return value.empty() ? GetUserProfileDir() + L"\\AppData\\Local" : value;
}

std::wstring GetCurrentModulePath() {
  std::wstring buffer(MAX_PATH, L'\0');
  DWORD size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
  while (size == buffer.size()) {
    buffer.resize(buffer.size() * 2);
    size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
  }
  buffer.resize(size);
  return buffer;
}

std::wstring GetModuleDir(HMODULE module) {
  std::wstring buffer(MAX_PATH, L'\0');
  DWORD size = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
  while (size == buffer.size()) {
    buffer.resize(buffer.size() * 2);
    size = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
  }
  buffer.resize(size);
  return GetDirName(buffer);
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

  auto candidates = FindWindowsAppsCodexCandidates();
  if (!candidates.empty()) {
    return candidates.back();
  }
  return {};
}

bool InjectDllIntoProcess(HANDLE process, const std::wstring& dll_path, std::wstring* error) {
  if (!FileExists(dll_path)) {
    if (error) {
      *error = L"DLL not found: " + dll_path;
    }
    return false;
  }

  size_t bytes = (dll_path.size() + 1) * sizeof(wchar_t);
  LPVOID remote = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  if (!remote) {
    if (error) {
      *error = L"VirtualAllocEx failed";
    }
    return false;
  }

  bool ok = WriteProcessMemory(process, remote, dll_path.c_str(), bytes, nullptr) != FALSE;
  if (!ok) {
    if (error) {
      *error = L"WriteProcessMemory failed";
    }
    VirtualFreeEx(process, remote, 0, MEM_RELEASE);
    return false;
  }

  HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
  auto load_library = reinterpret_cast<LPTHREAD_START_ROUTINE>(
      GetProcAddress(kernel32, "LoadLibraryW"));
  HANDLE thread = CreateRemoteThread(process, nullptr, 0, load_library, remote, 0, nullptr);
  if (!thread) {
    if (error) {
      *error = L"CreateRemoteThread failed";
    }
    VirtualFreeEx(process, remote, 0, MEM_RELEASE);
    return false;
  }

  WaitForSingleObject(thread, 10000);
  DWORD exit_code = 0;
  GetExitCodeThread(thread, &exit_code);
  CloseHandle(thread);
  VirtualFreeEx(process, remote, 0, MEM_RELEASE);

  if (exit_code == 0) {
    if (error) {
      *error = L"LoadLibraryW returned null in remote process";
    }
    return false;
  }
  // Let the DLL's init thread install hooks before a suspended target resumes.
  Sleep(750);
  return true;
}

DWORD FindNewestProcessIdByName(const std::wstring& process_name, DWORD after_pid) {
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) {
    return 0;
  }
  std::wstring target = ToLower(process_name);
  DWORD best = 0;
  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  if (Process32FirstW(snapshot, &entry)) {
    do {
      if (ToLower(entry.szExeFile) == target && entry.th32ProcessID > after_pid &&
          entry.th32ProcessID > best) {
        best = entry.th32ProcessID;
      }
    } while (Process32NextW(snapshot, &entry));
  }
  CloseHandle(snapshot);
  return best;
}

DWORD FindNewestProcessIdByPath(const std::wstring& process_path, DWORD after_pid) {
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
      if (entry.th32ProcessID <= after_pid || entry.th32ProcessID <= best) {
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

bool AnyProcessRunningAtPath(const std::wstring& process_path) {
  return FindNewestProcessIdByPath(process_path) != 0;
}

std::vector<ProcessInfo> EnumerateCodexAppProcesses(const std::wstring& app_dir) {
  std::vector<ProcessInfo> processes;
  std::wstring normalized_app_dir = NormalizeComparablePath(app_dir);
  if (!normalized_app_dir.empty() && normalized_app_dir.back() != L'\\') {
    normalized_app_dir += L"\\";
  }

  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) {
    return processes;
  }

  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  if (Process32FirstW(snapshot, &entry)) {
    do {
      if (!IsCodexAppProcessName(entry.szExeFile)) {
        continue;
      }
      std::wstring path = QueryProcessImagePath(entry.th32ProcessID);
      std::wstring lower_path = NormalizeComparablePath(path);
      if (lower_path.rfind(normalized_app_dir, 0) != 0) {
        continue;
      }
      ProcessInfo info;
      info.pid = entry.th32ProcessID;
      info.parent_pid = entry.th32ParentProcessID;
      info.name = entry.szExeFile;
      info.path = path;
      info.command_line = QueryProcessCommandLine(entry.th32ProcessID);
      processes.push_back(info);
    } while (Process32NextW(snapshot, &entry));
  }
  CloseHandle(snapshot);
  return processes;
}

std::vector<ProcessInfo> EnumerateCodexProcessTree(DWORD root_pid) {
  std::vector<ProcessInfo> processes;
  if (root_pid == 0) {
    return processes;
  }

  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) {
    return processes;
  }

  std::vector<PROCESSENTRY32W> entries;
  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  if (Process32FirstW(snapshot, &entry)) {
    do {
      entries.push_back(entry);
      entry.dwSize = sizeof(entry);
    } while (Process32NextW(snapshot, &entry));
  }
  CloseHandle(snapshot);

  std::set<DWORD> tree;
  tree.insert(root_pid);
  bool changed = true;
  while (changed) {
    changed = false;
    for (const auto& item : entries) {
      if (tree.count(item.th32ProcessID) != 0) {
        continue;
      }
      if (tree.count(item.th32ParentProcessID) == 0) {
        continue;
      }
      tree.insert(item.th32ProcessID);
      changed = true;
    }
  }

  for (const auto& item : entries) {
    if (tree.count(item.th32ProcessID) == 0 ||
        !IsCodexAppProcessName(item.szExeFile)) {
      continue;
    }
    ProcessInfo info;
    info.pid = item.th32ProcessID;
    info.parent_pid = item.th32ParentProcessID;
    info.name = item.szExeFile;
    info.path = QueryProcessImagePath(item.th32ProcessID);
    info.command_line = QueryProcessCommandLine(item.th32ProcessID);
    processes.push_back(info);
  }

  return processes;
}

}  // namespace codex_proxy
