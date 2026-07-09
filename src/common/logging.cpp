#include "common/logging.h"

#include "common/process_utils.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <windows.h>

namespace codex_proxy {
namespace {

std::wstring CurrentDate() {
  SYSTEMTIME st{};
  GetLocalTime(&st);
  wchar_t buffer[32]{};
  swprintf_s(buffer, L"%04u%02u%02u", st.wYear, st.wMonth, st.wDay);
  return buffer;
}

std::wstring CurrentTimestamp() {
  SYSTEMTIME st{};
  GetLocalTime(&st);
  wchar_t buffer[64]{};
  swprintf_s(buffer, L"%04u-%02u-%02u %02u:%02u:%02u.%03u",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
             st.wMilliseconds);
  return buffer;
}

}  // namespace

Logger& Logger::Instance() {
  static Logger logger;
  return logger;
}

void Logger::Init(const std::wstring& component) {
  std::lock_guard<std::mutex> lock(mutex_);
  component_ = component;
  std::wstring dir = GetLocalAppDataDir() + L"\\CodexProxy\\logs";
  std::filesystem::create_directories(dir);
  log_path_ = dir + L"\\proxy-" + CurrentDate() + L".log";
}

void Logger::Info(const std::wstring& message) {
  Write(L"info", message);
}

void Logger::Warn(const std::wstring& message) {
  Write(L"warn", message);
}

void Logger::Error(const std::wstring& message) {
  Write(L"error", message);
}

std::wstring Logger::LogPath() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return log_path_;
}

void Logger::Write(const wchar_t* level, const std::wstring& message) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (log_path_.empty()) {
    std::wstring dir = GetLocalAppDataDir() + L"\\CodexProxy\\logs";
    std::filesystem::create_directories(dir);
    log_path_ = dir + L"\\proxy-" + CurrentDate() + L".log";
  }
  std::wofstream file(log_path_, std::ios::app);
  if (!file) {
    return;
  }
  file << CurrentTimestamp() << L" [" << level << L"] [" << component_ << L"] "
       << message << L"\n";
}

std::wstring FormatLastError(const std::wstring& prefix, unsigned long error_code) {
  wchar_t* buffer = nullptr;
  DWORD size = FormatMessageW(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
          FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, error_code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
      reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
  std::wstring result = prefix + L" error=" + std::to_wstring(error_code);
  if (size && buffer) {
    result += L" ";
    result += buffer;
  }
  if (buffer) {
    LocalFree(buffer);
  }
  return result;
}

}  // namespace codex_proxy
