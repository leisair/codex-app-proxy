#include "common/config.h"

#include "common/process_utils.h"
#include "common/string_utils.h"

#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <windows.h>

namespace codex_proxy {
namespace {

std::string ReadFileUtf8(const std::wstring& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return {};
  }
  std::ostringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

bool WriteFileUtf8(const std::wstring& path, const std::string& content) {
  std::filesystem::create_directories(std::filesystem::path(path).parent_path());
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file) {
    return false;
  }
  file << content;
  return true;
}

std::string ExtractObject(const std::string& json, const std::string& key) {
  std::regex key_re("\"" + key + "\"\\s*:\\s*\\{");
  std::smatch match;
  if (!std::regex_search(json, match, key_re)) {
    return {};
  }
  size_t start = static_cast<size_t>(match.position(0) + match.length(0));
  int depth = 1;
  for (size_t i = start; i < json.size(); ++i) {
    if (json[i] == '{') {
      ++depth;
    } else if (json[i] == '}') {
      --depth;
      if (depth == 0) {
        return json.substr(start, i - start);
      }
    }
  }
  return {};
}

std::string ExtractArray(const std::string& json, const std::string& key) {
  std::regex key_re("\"" + key + "\"\\s*:\\s*\\[");
  std::smatch match;
  if (!std::regex_search(json, match, key_re)) {
    return {};
  }
  size_t start = static_cast<size_t>(match.position(0) + match.length(0));
  int depth = 1;
  bool in_string = false;
  for (size_t i = start; i < json.size(); ++i) {
    char ch = json[i];
    if (ch == '"' && (i == 0 || json[i - 1] != '\\')) {
      in_string = !in_string;
    }
    if (in_string) {
      continue;
    }
    if (ch == '[') {
      ++depth;
    } else if (ch == ']') {
      --depth;
      if (depth == 0) {
        return json.substr(start, i - start);
      }
    }
  }
  return {};
}

bool ExtractString(const std::string& json, const std::string& key, std::string* out) {
  std::regex re("\"" + key + "\"\\s*:\\s*\"([^\"]*)\"");
  std::smatch match;
  if (!std::regex_search(json, match, re)) {
    return false;
  }
  *out = match[1].str();
  return true;
}

bool ExtractInt(const std::string& json, const std::string& key, int* out) {
  std::regex re("\"" + key + "\"\\s*:\\s*(-?\\d+)");
  std::smatch match;
  if (!std::regex_search(json, match, re)) {
    return false;
  }
  *out = std::stoi(match[1].str());
  return true;
}

bool ExtractBool(const std::string& json, const std::string& key, bool* out) {
  std::regex re("\"" + key + "\"\\s*:\\s*(true|false)");
  std::smatch match;
  if (!std::regex_search(json, match, re)) {
    return false;
  }
  *out = match[1].str() == "true";
  return true;
}

std::vector<std::string> ExtractStringArray(const std::string& json, const std::string& key) {
  std::vector<std::string> values;
  std::string array = ExtractArray(json, key);
  if (array.empty()) {
    return values;
  }
  std::regex item_re("\"([^\"]*)\"");
  for (std::sregex_iterator it(array.begin(), array.end(), item_re), end; it != end; ++it) {
    values.push_back((*it)[1].str());
  }
  return values;
}

std::string JsonEscape(const std::string& value) {
  std::string out;
  for (char ch : value) {
    switch (ch) {
      case '\\':
        out += "\\\\";
        break;
      case '"':
        out += "\\\"";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        out.push_back(ch);
        break;
    }
  }
  return out;
}

}  // namespace

AppConfig DefaultConfig() {
  return {};
}

std::wstring DefaultConfigDir() {
  return GetUserProfileDir() + L"\\.codex-proxy";
}

std::wstring DefaultConfigPath() {
  return DefaultConfigDir() + L"\\config.json";
}

std::string ProxyTypeToString(ProxyType type) {
  return type == ProxyType::Socks5 ? "socks5" : "http";
}

ProxyType ProxyTypeFromString(const std::string& value) {
  std::string lower = ToLower(Trim(value));
  return lower == "socks5" ? ProxyType::Socks5 : ProxyType::Http;
}

std::string SerializeConfig(const AppConfig& config) {
  std::ostringstream out;
  out << "{\n";
  out << "  \"_comment\": \"Codex Proxy Launcher configuration\",\n";
  out << "  \"_version\": " << config.version << ",\n";
  out << "  \"log_level\": \"" << JsonEscape(config.log_level) << "\",\n";
  out << "  \"proxy\": {\n";
  out << "    \"type\": \"" << ProxyTypeToString(config.proxy.type) << "\",\n";
  out << "    \"host\": \"" << JsonEscape(config.proxy.host) << "\",\n";
  out << "    \"port\": " << config.proxy.port << "\n";
  out << "  },\n";
  out << "  \"target_processes\": [";
  for (size_t i = 0; i < config.target_processes.size(); ++i) {
    if (i != 0) {
      out << ", ";
    }
    out << "\"" << JsonEscape(config.target_processes[i]) << "\"";
  }
  out << "],\n";
  out << "  \"child_injection\": " << (config.child_injection ? "true" : "false") << ",\n";
  out << "  \"child_injection_mode\": \"" << JsonEscape(config.child_injection_mode) << "\",\n";
  out << "  \"bypass\": {\n";
  out << "    \"localhost\": " << (config.bypass.localhost ? "true" : "false") << ",\n";
  out << "    \"private_networks\": " << (config.bypass.private_networks ? "true" : "false") << ",\n";
  out << "    \"proxy_endpoint\": " << (config.bypass.proxy_endpoint ? "true" : "false") << "\n";
  out << "  },\n";
  out << "  \"fake_ip\": {\n";
  out << "    \"enabled\": " << (config.fake_ip.enabled ? "true" : "false") << ",\n";
  out << "    \"cidr\": \"" << JsonEscape(config.fake_ip.cidr) << "\"\n";
  out << "  },\n";
  out << "  \"proxy_rules\": {\n";
  out << "    \"dns_mode\": \"" << JsonEscape(config.proxy_rules.dns_mode) << "\",\n";
  out << "    \"ipv6_mode\": \"" << (config.proxy_rules.ipv6_mode == IpMode::Proxy ? "proxy" : config.proxy_rules.ipv6_mode == IpMode::Direct ? "direct" : "block") << "\",\n";
  out << "    \"udp_mode\": \"" << (config.proxy_rules.udp_mode == UdpMode::Direct ? "direct" : "block") << "\"\n";
  out << "  },\n";
  out << "  \"timeout\": {\n";
  out << "    \"connect\": " << config.timeout.connect_ms << ",\n";
  out << "    \"send\": " << config.timeout.send_ms << ",\n";
  out << "    \"recv\": " << config.timeout.recv_ms << "\n";
  out << "  }\n";
  out << "}\n";
  return out.str();
}

bool SaveConfig(const std::wstring& path, const AppConfig& config, std::wstring* error) {
  if (!WriteFileUtf8(path, SerializeConfig(config))) {
    if (error) {
      *error = L"Failed to write config: " + path;
    }
    return false;
  }
  return true;
}

bool EnsureDefaultConfig(const std::wstring& path, std::wstring* error) {
  if (std::filesystem::exists(path)) {
    return true;
  }
  return SaveConfig(path, DefaultConfig(), error);
}

bool LoadConfig(const std::wstring& path, AppConfig* config, std::wstring* error) {
  if (!config) {
    return false;
  }
  EnsureDefaultConfig(path, error);
  std::string json = ReadFileUtf8(path);
  if (json.empty()) {
    if (error) {
      *error = L"Config is empty or unreadable: " + path;
    }
    return false;
  }

  AppConfig parsed = DefaultConfig();
  std::string value;
  int int_value = 0;
  bool bool_value = false;

  if (ExtractString(json, "log_level", &value)) {
    parsed.log_level = ToLower(Trim(value));
  }

  std::string proxy = ExtractObject(json, "proxy");
  if (!proxy.empty()) {
    if (ExtractString(proxy, "type", &value)) {
      parsed.proxy.type = ProxyTypeFromString(value);
    }
    if (ExtractString(proxy, "host", &value)) {
      parsed.proxy.host = Trim(value);
    }
    if (ExtractInt(proxy, "port", &int_value) && int_value > 0 && int_value <= 65535) {
      parsed.proxy.port = static_cast<uint16_t>(int_value);
    }
  }

  auto targets = ExtractStringArray(json, "target_processes");
  if (!targets.empty()) {
    parsed.target_processes = targets;
  }

  if (ExtractBool(json, "child_injection", &bool_value)) {
    parsed.child_injection = bool_value;
  }
  if (ExtractString(json, "child_injection_mode", &value)) {
    parsed.child_injection_mode = ToLower(Trim(value));
  }

  std::string bypass = ExtractObject(json, "bypass");
  if (!bypass.empty()) {
    if (ExtractBool(bypass, "localhost", &bool_value)) {
      parsed.bypass.localhost = bool_value;
    }
    if (ExtractBool(bypass, "private_networks", &bool_value)) {
      parsed.bypass.private_networks = bool_value;
    }
    if (ExtractBool(bypass, "proxy_endpoint", &bool_value)) {
      parsed.bypass.proxy_endpoint = bool_value;
    }
  }

  std::string fake_ip = ExtractObject(json, "fake_ip");
  if (!fake_ip.empty()) {
    if (ExtractBool(fake_ip, "enabled", &bool_value)) {
      parsed.fake_ip.enabled = bool_value;
    }
    if (ExtractString(fake_ip, "cidr", &value)) {
      parsed.fake_ip.cidr = Trim(value);
    }
  }

  std::string rules = ExtractObject(json, "proxy_rules");
  if (!rules.empty()) {
    if (ExtractString(rules, "dns_mode", &value)) {
      parsed.proxy_rules.dns_mode = ToLower(Trim(value));
    }
    if (ExtractString(rules, "ipv6_mode", &value)) {
      value = ToLower(Trim(value));
      parsed.proxy_rules.ipv6_mode =
          value == "proxy" ? IpMode::Proxy : value == "direct" ? IpMode::Direct : IpMode::Block;
    }
    if (ExtractString(rules, "udp_mode", &value)) {
      parsed.proxy_rules.udp_mode = ToLower(Trim(value)) == "direct" ? UdpMode::Direct : UdpMode::Block;
    }
  }

  std::string timeout = ExtractObject(json, "timeout");
  if (!timeout.empty()) {
    if (ExtractInt(timeout, "connect", &int_value) && int_value > 0) {
      parsed.timeout.connect_ms = int_value;
    }
    if (ExtractInt(timeout, "send", &int_value) && int_value > 0) {
      parsed.timeout.send_ms = int_value;
    }
    if (ExtractInt(timeout, "recv", &int_value) && int_value > 0) {
      parsed.timeout.recv_ms = int_value;
    }
  }

  *config = parsed;
  return true;
}

bool IsAllowedProcess(const AppConfig& config, const std::wstring& process_name) {
  std::wstring lower = ToLower(GetBaseName(process_name));
  for (const auto& item : config.target_processes) {
    if (lower == ToLower(Utf8ToWide(item))) {
      return true;
    }
  }
  return false;
}

}  // namespace codex_proxy
