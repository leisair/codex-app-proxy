#include "common/logging.h"

#include "common/process_utils.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>
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

void Logger::Init(const std::wstring& component, LogMode mode) {
  std::lock_guard<std::mutex> lock(mutex_);
  component_ = component;
  mode_ = mode;
  log_path_.clear();
  buffered_entries_.clear();
}

void Logger::SetMode(LogMode mode) {
  std::lock_guard<std::mutex> lock(mutex_);
  mode_ = mode;
  if (mode_ == LogMode::Off) {
    buffered_entries_.clear();
    return;
  }
  if (mode_ == LogMode::Always) {
    for (const auto& entry : buffered_entries_) {
      AppendToFile(entry);
    }
    buffered_entries_.clear();
  }
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

LogMode Logger::Mode() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return mode_;
}

void Logger::EnsureLogPath() {
  if (!log_path_.empty()) {
    return;
  }
  std::wstring dir = GetExecutableDir() + L"\\logs";
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  if (!ec) {
    log_path_ = dir + L"\\proxy-" + CurrentDate() + L".log";
  }
}

void Logger::AppendToFile(const std::wstring& entry) {
  EnsureLogPath();
  if (log_path_.empty()) {
    return;
  }
  std::wofstream file(log_path_, std::ios::app);
  if (file) {
    file << entry << L"\n";
  }
}

void Logger::Write(const wchar_t* level, const std::wstring& message) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (mode_ == LogMode::Off) {
    return;
  }
  const std::wstring entry = CurrentTimestamp() + L" [" + level + L"] [" +
                             component_ + L"] " + message;
  if (mode_ == LogMode::Errors && std::wstring(level) != L"error") {
    buffered_entries_.push_back(entry);
    return;
  }
  if (mode_ == LogMode::Errors) {
    for (const auto& buffered : buffered_entries_) {
      AppendToFile(buffered);
    }
    buffered_entries_.clear();
  }
  AppendToFile(entry);
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
