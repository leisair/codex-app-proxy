#include "common/config.h"

#include <cassert>
#include <iostream>

using namespace codex_proxy;

int main() {
  AppConfig config = DefaultConfig();
  assert(config.proxy.host == "127.0.0.1");
  assert(config.proxy.port == 10808);
  assert(config.proxy.type == ProxyType::Http);
  assert(!config.bypass_list.empty());
  assert(config.disable_quic);
  assert(!config.set_proxy_environment);

  std::string json = SerializeConfig(config);
  assert(json.find("\"port\": 10808") != std::string::npos);
  assert(json.find("\"disable_quic\": true") != std::string::npos);
  assert(json.find("\"set_proxy_environment\": false") != std::string::npos);

  std::cout << "config tests passed\n";
  return 0;
}
