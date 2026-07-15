#include "common/config.h"
#include "common/launch_args.h"
#include "common/logging.h"
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
  assert(config.log_mode == LogMode::Errors);
  assert(DefaultConfigDir() == GetExecutableDir());
  assert(DefaultConfigPath() == GetExecutableDir() + L"\\config.json");

  std::string json = SerializeConfig(config);
  assert(json.find("\"port\": 10808") != std::string::npos);
  assert(json.find("\"disable_quic\": true") != std::string::npos);
  assert(json.find("\"log_mode\": \"errors\"") != std::string::npos);
  assert(json.find("set_proxy_environment") == std::string::npos);
  assert(json.find("log_level") == std::string::npos);

  std::wstring error;
  assert(ValidateConfig(config, &error));
  AppConfig invalid = config;
  invalid.proxy.host.clear();
  assert(!ValidateConfig(invalid, &error));

  config.log_mode = LogMode::Always;
  assert(SerializeConfig(config).find("\"log_mode\": \"always\"") != std::string::npos);
  config.log_mode = LogMode::Off;
  assert(SerializeConfig(config).find("\"log_mode\": \"off\"") != std::string::npos);
  config = DefaultConfig();
  invalid = config;
  invalid.bypass_list.clear();
  assert(!ValidateConfig(invalid, &error));

  std::wstring command = BuildAppCommandLine(L"C:\\Program Files\\ChatGPT.exe", config);
  assert(command.find(L"--proxy-server=\"http://127.0.0.1:10808\"") !=
         std::wstring::npos);
  assert(command.find(L"--proxy-bypass-list=") != std::wstring::npos);
  assert(command.find(L"--disable-quic") != std::wstring::npos);
  std::wstring arguments = BuildAppArguments(config);
  assert(arguments.find(L"C:\\Program Files\\ChatGPT.exe") == std::wstring::npos);
  assert(arguments.find(L"--proxy-server=\"http://127.0.0.1:10808\"") !=
         std::wstring::npos);

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

  const std::filesystem::path legacy_path =
      std::filesystem::temp_directory_path() / "codex-proxy-legacy-config.json";
  {
    std::ofstream legacy_file(legacy_path, std::ios::binary | std::ios::trunc);
    legacy_file << "{\"proxy\":{\"type\":\"http\",\"host\":\"127.0.0.1\",\"port\":10808},"
                   "\"bypass_list\":[\"localhost\"],\"disable_quic\":true}";
  }
  assert(LoadConfig(legacy_path.wstring(), &loaded, &error));
  assert(loaded.log_mode == LogMode::Errors);
  assert(LoadLogModePreference(legacy_path.wstring()) == LogMode::Errors);
  std::filesystem::remove(legacy_path, remove_error);

  Logger::Instance().Init(L"test", LogMode::Errors);
  Logger::Instance().Info(L"buffered context");
  assert(Logger::Instance().LogPath().empty());
  Logger::Instance().Error(L"test error");
  assert(!Logger::Instance().LogPath().empty());
  assert(std::filesystem::exists(Logger::Instance().LogPath()));

  Logger::Instance().Init(L"test", LogMode::Always);
  Logger::Instance().Info(L"continuous context");
  assert(!Logger::Instance().LogPath().empty());

  Logger::Instance().Init(L"test", LogMode::Off);
  Logger::Instance().Error(L"must not be written");
  assert(Logger::Instance().LogPath().empty());

  std::cout << "config tests passed\n";
  return 0;
}
