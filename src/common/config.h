#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace codex_proxy {

enum class ProxyType {
  Http,
  Socks5,
};

enum class UdpMode {
  Block,
  Direct,
};

enum class IpMode {
  Block,
  Direct,
  Proxy,
};

struct ProxyConfig {
  ProxyType type = ProxyType::Http;
  std::string host = "127.0.0.1";
  uint16_t port = 10808;
};

struct TimeoutConfig {
  int connect_ms = 5000;
  int send_ms = 5000;
  int recv_ms = 5000;
};

struct BypassConfig {
  bool localhost = true;
  bool private_networks = true;
  bool proxy_endpoint = true;
};

struct FakeIpConfig {
  bool enabled = false;
  std::string cidr = "198.18.0.0/15";
};

struct ProxyRules {
  std::string dns_mode = "proxy";
  IpMode ipv6_mode = IpMode::Block;
  UdpMode udp_mode = UdpMode::Block;
};

struct AppConfig {
  int version = 1;
  std::string log_level = "info";
  ProxyConfig proxy;
  std::vector<std::string> target_processes = {"Codex.exe", "codex.exe"};
  bool child_injection = true;
  std::string child_injection_mode = "filtered";
  BypassConfig bypass;
  FakeIpConfig fake_ip;
  ProxyRules proxy_rules;
  TimeoutConfig timeout;
};

AppConfig DefaultConfig();
std::wstring DefaultConfigPath();
std::wstring DefaultConfigDir();
std::string SerializeConfig(const AppConfig& config);
bool EnsureDefaultConfig(const std::wstring& path, std::wstring* error);
bool LoadConfig(const std::wstring& path, AppConfig* config, std::wstring* error);
bool SaveConfig(const std::wstring& path, const AppConfig& config, std::wstring* error);
bool IsAllowedProcess(const AppConfig& config, const std::wstring& process_name);
std::string ProxyTypeToString(ProxyType type);
ProxyType ProxyTypeFromString(const std::string& value);

}  // namespace codex_proxy
