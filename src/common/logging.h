#pragma once

#include <mutex>
#include <string>
#include <vector>

#include "common/config.h"

namespace codex_proxy {

class Logger {
 public:
  static Logger& Instance();

  void Init(const std::wstring& component, LogMode mode = LogMode::Errors);
  void SetMode(LogMode mode);
  void Info(const std::wstring& message);
  void Warn(const std::wstring& message);
  void Error(const std::wstring& message);
  std::wstring LogPath() const;
  LogMode Mode() const;

 private:
  void Write(const wchar_t* level, const std::wstring& message);
  void AppendToFile(const std::wstring& entry);
  void EnsureLogPath();

  mutable std::mutex mutex_;
  std::wstring component_ = L"codex-proxy";
  std::wstring log_path_;
  LogMode mode_ = LogMode::Errors;
  std::vector<std::wstring> buffered_entries_;
};

std::wstring FormatLastError(const std::wstring& prefix, unsigned long error_code);

}  // namespace codex_proxy
