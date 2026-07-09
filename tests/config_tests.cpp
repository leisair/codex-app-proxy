#include "common/config.h"

#include <cassert>
#include <iostream>

using namespace codex_proxy;

int main() {
  AppConfig config = DefaultConfig();
  assert(config.proxy.host == "127.0.0.1");
  assert(config.proxy.port == 10808);
  assert(config.proxy.type == ProxyType::Http);
  assert(IsAllowedProcess(config, L"Codex.exe"));
  assert(IsAllowedProcess(config, L"C:\\x\\codex.exe"));
  assert(!IsAllowedProcess(config, L"chrome.exe"));

  std::string json = SerializeConfig(config);
  assert(json.find("\"port\": 10808") != std::string::npos);
  assert(json.find("\"Codex.exe\"") != std::string::npos);

  std::cout << "config tests passed\n";
  return 0;
}
