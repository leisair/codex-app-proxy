#pragma once

#include <mutex>
#include <string>

namespace codex_proxy {

class Logger {
 public:
  static Logger& Instance();

  void Init(const std::wstring& component);
  void Info(const std::wstring& message);
  void Warn(const std::wstring& message);
  void Error(const std::wstring& message);
  std::wstring LogPath() const;

 private:
  void Write(const wchar_t* level, const std::wstring& message);

  mutable std::mutex mutex_;
  std::wstring component_ = L"codex-proxy";
  std::wstring log_path_;
};

std::wstring FormatLastError(const std::wstring& prefix, unsigned long error_code);

}  // namespace codex_proxy
