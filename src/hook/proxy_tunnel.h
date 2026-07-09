#pragma once

#include "common/config.h"

#include <string>
#include <winsock2.h>

namespace codex_proxy {

struct TargetEndpoint {
  int family = AF_UNSPEC;
  std::string host;
  uint16_t port = 0;
  bool is_local = false;
  bool is_private = false;
  bool is_proxy_endpoint = false;
  bool is_fake_ip = false;
};

bool InitWinsockForHook();
bool EndpointFromSockAddr(const sockaddr* addr, int addr_len, const AppConfig& config,
                          TargetEndpoint* endpoint);
bool ShouldBypass(const AppConfig& config, const TargetEndpoint& endpoint);
bool EstablishProxyTunnel(SOCKET socket, const AppConfig& config,
                          const TargetEndpoint& endpoint, int* wsa_error);
bool IsTcpSocket(SOCKET socket);

}  // namespace codex_proxy
