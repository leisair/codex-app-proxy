#include "common/config.h"

#include "common/process_utils.h"
#include "common/string_utils.h"

#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <system_error>
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
  std::error_code ec;
  std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
  if (ec) {
    return false;
  }
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
  try {
    *out = std::stoi(match[1].str());
  } catch (...) {
    return false;
  }
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
  return GetExecutableDir();
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
  out << "  \"_comment\": \"ChatGPT/Codex Proxy Launcher configuration\",\n";
  out << "  \"_version\": " << config.version << ",\n";
  out << "  \"proxy\": {\n";
  out << "    \"type\": \"" << ProxyTypeToString(config.proxy.type) << "\",\n";
  out << "    \"host\": \"" << JsonEscape(config.proxy.host) << "\",\n";
  out << "    \"port\": " << config.proxy.port << "\n";
  out << "  },\n";
  out << "  \"bypass_list\": [";
  for (size_t i = 0; i < config.bypass_list.size(); ++i) {
    if (i != 0) {
      out << ", ";
    }
    out << "\"" << JsonEscape(config.bypass_list[i]) << "\"";
  }
  out << "],\n";
  out << "  \"disable_quic\": " << (config.disable_quic ? "true" : "false") << "\n";
  out << "}\n";
  return out.str();
}

bool SaveConfig(const std::wstring& path, const AppConfig& config, std::wstring* error) {
  if (!WriteFileUtf8(path, SerializeConfig(config))) {
    if (error) {
      *error = L"无法写入配置文件：" + path;
    }
    return false;
  }
  return true;
}

bool EnsureDefaultConfig(const std::wstring& path, std::wstring* error) {
  std::error_code ec;
  if (std::filesystem::exists(path, ec)) {
    return true;
  }
  if (ec) {
    if (error) {
      *error = L"无法访问配置路径：" + path;
    }
    return false;
  }
  return SaveConfig(path, DefaultConfig(), error);
}

bool ValidateConfig(const AppConfig& config, std::wstring* error) {
  if (Trim(config.proxy.host).empty()) {
    if (error) {
      *error = L"proxy.host（代理地址）不能为空";
    }
    return false;
  }
  if (config.proxy.port == 0) {
    if (error) {
      *error = L"proxy.port（本地监听端口）必须在 1 到 65535 之间";
    }
    return false;
  }
  if (config.bypass_list.empty()) {
    if (error) {
      *error = L"bypass_list（直连列表）至少需要保留一项";
    }
    return false;
  }
  return true;
}

bool LoadConfig(const std::wstring& path, AppConfig* config, std::wstring* error) {
  if (!config) {
    return false;
  }
  if (!EnsureDefaultConfig(path, error)) {
    return false;
  }
  std::string json = ReadFileUtf8(path);
  if (json.empty()) {
    if (error) {
      *error = L"配置文件为空或无法读取：" + path;
    }
    return false;
  }

  AppConfig parsed = DefaultConfig();
  std::string value;
  int int_value = 0;
  bool bool_value = false;

  std::string proxy = ExtractObject(json, "proxy");
  if (proxy.empty()) {
    if (error) {
      *error = L"缺少 proxy 配置对象，或其 JSON 结构无效";
    }
    return false;
  }
  if (!ExtractString(proxy, "type", &value) ||
      (ToLower(Trim(value)) != "http" && ToLower(Trim(value)) != "socks5")) {
    if (error) {
      *error = L"proxy.type（连接方式）只能是 http 或 socks5";
    }
    return false;
  }
  parsed.proxy.type = ProxyTypeFromString(value);
  if (!ExtractString(proxy, "host", &value) || Trim(value).empty()) {
    if (error) {
      *error = L"proxy.host（代理地址）不能为空";
    }
    return false;
  }
  parsed.proxy.host = Trim(value);
  if (!ExtractInt(proxy, "port", &int_value) || int_value < 1 || int_value > 65535) {
    if (error) {
      *error = L"proxy.port（本地监听端口）必须在 1 到 65535 之间";
    }
    return false;
  }
  parsed.proxy.port = static_cast<uint16_t>(int_value);

  auto bypass_list = ExtractStringArray(json, "bypass_list");
  if (bypass_list.empty()) {
    if (error) {
      *error = L"bypass_list（直连列表）至少需要保留一项";
    }
    return false;
  }
  parsed.bypass_list = bypass_list;

  if (!ExtractBool(json, "disable_quic", &bool_value)) {
    if (error) {
      *error = L"disable_quic（禁用 QUIC）必须是 true 或 false";
    }
    return false;
  }
  parsed.disable_quic = bool_value;

  if (!ValidateConfig(parsed, error)) {
    return false;
  }
  *config = parsed;
  return true;
}

}  // namespace codex_proxy
