#include "common/config.h"
#include "common/launch_args.h"
#include "common/process_utils.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace codex_proxy;

int main() {
  AppConfig config = DefaultConfig();
  assert(config.proxy.host == "127.0.0.1");
  assert(config.proxy.port == 10808);
  assert(config.proxy.type == ProxyType::Http);
  assert(!config.bypass_list.empty());
  assert(config.disable_quic);
  assert(DefaultConfigDir() == GetExecutableDir());
  assert(DefaultConfigPath() == GetExecutableDir() + L"\\config.json");

  std::string json = SerializeConfig(config);
  assert(json.find("\"port\": 10808") != std::string::npos);
  assert(json.find("\"disable_quic\": true") != std::string::npos);
  assert(json.find("set_proxy_environment") == std::string::npos);
  assert(json.find("log_level") == std::string::npos);

  std::wstring error;
  assert(ValidateConfig(config, &error));
  AppConfig invalid = config;
  invalid.proxy.host.clear();
  assert(!ValidateConfig(invalid, &error));
  invalid = config;
  invalid.bypass_list.clear();
  assert(!ValidateConfig(invalid, &error));

  std::wstring command = BuildAppCommandLine(L"C:\\Program Files\\ChatGPT.exe", config);
  assert(command.find(L"--proxy-server=\"http://127.0.0.1:10808\"") !=
         std::wstring::npos);
  assert(command.find(L"--proxy-bypass-list=") != std::wstring::npos);
  assert(command.find(L"--disable-quic") != std::wstring::npos);

  AppConfig socks = config;
  socks.proxy.type = ProxyType::Socks5;
  socks.proxy.port = 1080;
  command = BuildAppCommandLine(L"C:\\ChatGPT.exe", socks);
  assert(command.find(L"--proxy-server=\"socks5://127.0.0.1:1080\"") !=
         std::wstring::npos);

  const std::filesystem::path invalid_path =
      std::filesystem::temp_directory_path() / "codex-proxy-invalid-config.json";
  {
    std::ofstream invalid_file(invalid_path, std::ios::binary | std::ios::trunc);
    invalid_file << "{}";
  }
  AppConfig loaded;
  assert(!LoadConfig(invalid_path.wstring(), &loaded, &error));
  std::error_code remove_error;
  std::filesystem::remove(invalid_path, remove_error);

  std::cout << "config tests passed\n";
  return 0;
}
