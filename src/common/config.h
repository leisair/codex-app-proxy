#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace codex_proxy {

enum class ProxyType {
  Http,
  Socks5,
};

enum class LogMode {
  Errors,
  Always,
  Off,
};

struct ProxyConfig {
  ProxyType type = ProxyType::Http;
  std::string host = "127.0.0.1";
  uint16_t port = 10808;
};

struct AppConfig {
  int version = 1;
  ProxyConfig proxy;
  std::vector<std::string> bypass_list = {
      "<-loopback>", "localhost", "127.0.0.1", "::1",
      "10.*", "172.16.*", "172.17.*", "172.18.*", "172.19.*",
      "172.20.*", "172.21.*", "172.22.*", "172.23.*", "172.24.*",
      "172.25.*", "172.26.*", "172.27.*", "172.28.*", "172.29.*",
      "172.30.*", "172.31.*", "192.168.*"};
  bool disable_quic = true;
  LogMode log_mode = LogMode::Errors;
};

AppConfig DefaultConfig();
std::wstring DefaultConfigPath();
std::wstring DefaultConfigDir();
std::string SerializeConfig(const AppConfig& config);
bool EnsureDefaultConfig(const std::wstring& path, std::wstring* error);
bool LoadConfig(const std::wstring& path, AppConfig* config, std::wstring* error);
bool SaveConfig(const std::wstring& path, const AppConfig& config, std::wstring* error);
bool ValidateConfig(const AppConfig& config, std::wstring* error);
std::string ProxyTypeToString(ProxyType type);
ProxyType ProxyTypeFromString(const std::string& value);
std::string LogModeToString(LogMode mode);
LogMode LogModeFromString(const std::string& value);
LogMode LoadLogModePreference(const std::wstring& path);

}  // namespace codex_proxy
