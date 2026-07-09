#include "hook/proxy_tunnel.h"

#include "common/logging.h"
#include "common/string_utils.h"

#include <array>
#include <sstream>
#include <vector>
#include <ws2tcpip.h>

namespace codex_proxy {
namespace {

thread_local bool g_inside_proxy = false;

bool IsLoopbackIpv4(uint32_t host_order) {
  return (host_order >> 24) == 127;
}

bool IsPrivateIpv4(uint32_t host_order) {
  return (host_order >> 24) == 10 ||
         (host_order >> 20) == 0xAC1 ||
         (host_order >> 16) == 0xC0A8 ||
         (host_order >> 16) == 0xA9FE;
}

bool IsProxyEndpointIpv4(uint32_t host_order, uint16_t port, const AppConfig& config) {
  in_addr proxy_addr{};
  if (InetPtonA(AF_INET, config.proxy.host.c_str(), &proxy_addr) != 1) {
    return false;
  }
  return ntohl(proxy_addr.s_addr) == host_order && port == config.proxy.port;
}

bool IsWouldBlock(int error) {
  return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS ||
         error == WSAEALREADY;
}

timeval TimeoutToTimeval(int timeout_ms) {
  timeval timeout{};
  timeout.tv_sec = timeout_ms / 1000;
  timeout.tv_usec = (timeout_ms % 1000) * 1000;
  return timeout;
}

bool WaitForSocket(SOCKET socket, bool wait_write, int timeout_ms, int* wsa_error) {
  fd_set read_set;
  fd_set write_set;
  fd_set except_set;
  FD_ZERO(&read_set);
  FD_ZERO(&write_set);
  FD_ZERO(&except_set);
  if (wait_write) {
    FD_SET(socket, &write_set);
  } else {
    FD_SET(socket, &read_set);
  }
  FD_SET(socket, &except_set);

  timeval timeout = TimeoutToTimeval(timeout_ms);
  int rc = select(0, wait_write ? nullptr : &read_set,
                  wait_write ? &write_set : nullptr, &except_set, &timeout);
  if (rc > 0) {
    if (FD_ISSET(socket, &except_set)) {
      int socket_error = 0;
      int length = sizeof(socket_error);
      if (getsockopt(socket, SOL_SOCKET, SO_ERROR,
                     reinterpret_cast<char*>(&socket_error), &length) == 0 &&
          socket_error != 0) {
        if (wsa_error) {
          *wsa_error = socket_error;
        }
        return false;
      }
      if (wsa_error) {
        *wsa_error = WSAECONNRESET;
      }
      return false;
    }
    return true;
  }

  if (wsa_error) {
    *wsa_error = rc == 0 ? WSAETIMEDOUT : WSAGetLastError();
  }
  return false;
}

bool WaitForConnect(SOCKET socket, int timeout_ms, int* wsa_error) {
  if (!WaitForSocket(socket, true, timeout_ms, wsa_error)) {
    return false;
  }

  int socket_error = 0;
  int length = sizeof(socket_error);
  if (getsockopt(socket, SOL_SOCKET, SO_ERROR,
                 reinterpret_cast<char*>(&socket_error), &length) != 0) {
    if (wsa_error) {
      *wsa_error = WSAGetLastError();
    }
    return false;
  }
  if (socket_error != 0) {
    if (wsa_error) {
      *wsa_error = socket_error;
    }
    return false;
  }
  return true;
}

bool ConnectWithWait(SOCKET socket, const sockaddr* addr, int addr_len,
                     int timeout_ms, int* wsa_error) {
  if (connect(socket, addr, addr_len) == 0) {
    return true;
  }

  int error = WSAGetLastError();
  if (!IsWouldBlock(error)) {
    if (wsa_error) {
      *wsa_error = error;
    }
    return false;
  }
  return WaitForConnect(socket, timeout_ms, wsa_error);
}

bool SendAll(SOCKET socket, const char* data, int length, int timeout_ms,
             int* wsa_error) {
  int sent = 0;
  while (sent < length) {
    int rc = send(socket, data + sent, length - sent, 0);
    if (rc > 0) {
      sent += rc;
      continue;
    }
    int error = WSAGetLastError();
    if (IsWouldBlock(error)) {
      if (!WaitForSocket(socket, true, timeout_ms, wsa_error)) {
        return false;
      }
      continue;
    }
    if (wsa_error) {
      *wsa_error = error;
    }
    return false;
  }
  return true;
}

bool RecvSome(SOCKET socket, char* data, int length, int timeout_ms, int* wsa_error,
              int* received) {
  while (true) {
    int rc = recv(socket, data, length, 0);
    if (rc > 0) {
      if (received) {
        *received = rc;
      }
      return true;
    }
    if (rc == 0) {
      if (wsa_error) {
        *wsa_error = WSAECONNRESET;
      }
      return false;
    }
    int error = WSAGetLastError();
    if (IsWouldBlock(error)) {
      if (!WaitForSocket(socket, false, timeout_ms, wsa_error)) {
        return false;
      }
      continue;
    }
    if (wsa_error) {
      *wsa_error = error;
    }
    return false;
  }
}

bool RecvExact(SOCKET socket, char* data, int length, int timeout_ms, int* wsa_error) {
  int received = 0;
  while (received < length) {
    int chunk = 0;
    if (!RecvSome(socket, data + received, length - received, timeout_ms,
                  wsa_error, &chunk)) {
      return false;
    }
    received += chunk;
  }
  return true;
}

bool ReadHttpResponse(SOCKET socket, int timeout_ms, int* wsa_error) {
  std::string response;
  std::array<char, 512> buffer{};
  while (response.find("\r\n\r\n") == std::string::npos && response.size() < 8192) {
    int rc = 0;
    if (!RecvSome(socket, buffer.data(), static_cast<int>(buffer.size()),
                  timeout_ms, wsa_error, &rc)) {
      return false;
    }
    response.append(buffer.data(), rc);
  }
  return response.rfind("HTTP/1.1 200", 0) == 0 || response.rfind("HTTP/1.0 200", 0) == 0;
}

void SetSocketBlockingMode(SOCKET socket, bool blocking) {
  u_long mode = blocking ? 0 : 1;
  ioctlsocket(socket, FIONBIO, &mode);
}

void SetSocketTimeouts(SOCKET socket, const TimeoutConfig& timeout) {
  setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO,
             reinterpret_cast<const char*>(&timeout.send_ms), sizeof(timeout.send_ms));
  setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
             reinterpret_cast<const char*>(&timeout.recv_ms), sizeof(timeout.recv_ms));
}

bool Socks5Tunnel(SOCKET socket, const TargetEndpoint& endpoint, int timeout_ms,
                  int* wsa_error) {
  const unsigned char greeting[] = {0x05, 0x01, 0x00};
  if (!SendAll(socket, reinterpret_cast<const char*>(greeting),
               static_cast<int>(sizeof(greeting)), timeout_ms, wsa_error)) {
    return false;
  }
  unsigned char auth[2]{};
  if (!RecvExact(socket, reinterpret_cast<char*>(auth), 2, timeout_ms, wsa_error) ||
      auth[0] != 0x05 ||
      auth[1] != 0x00) {
    return false;
  }

  std::string host = endpoint.host;
  std::vector<unsigned char> request = {0x05, 0x01, 0x00};
  in_addr ipv4{};
  in6_addr ipv6{};
  if (InetPtonA(AF_INET, host.c_str(), &ipv4) == 1) {
    request.push_back(0x01);
    auto* bytes = reinterpret_cast<unsigned char*>(&ipv4.s_addr);
    request.insert(request.end(), bytes, bytes + 4);
  } else if (InetPtonA(AF_INET6, host.c_str(), &ipv6) == 1) {
    request.push_back(0x04);
    auto* bytes = reinterpret_cast<unsigned char*>(&ipv6);
    request.insert(request.end(), bytes, bytes + 16);
  } else {
    if (host.size() > 255) {
      return false;
    }
    request.push_back(0x03);
    request.push_back(static_cast<unsigned char>(host.size()));
    request.insert(request.end(), host.begin(), host.end());
  }
  request.push_back(static_cast<unsigned char>((endpoint.port >> 8) & 0xFF));
  request.push_back(static_cast<unsigned char>(endpoint.port & 0xFF));

  if (!SendAll(socket, reinterpret_cast<const char*>(request.data()),
               static_cast<int>(request.size()), timeout_ms, wsa_error)) {
    return false;
  }
  unsigned char reply[4]{};
  if (!RecvExact(socket, reinterpret_cast<char*>(reply), 4, timeout_ms, wsa_error) ||
      reply[0] != 0x05 ||
      reply[1] != 0x00) {
    return false;
  }
  int to_read = 0;
  if (reply[3] == 0x01) {
    to_read = 4 + 2;
  } else if (reply[3] == 0x04) {
    to_read = 16 + 2;
  } else if (reply[3] == 0x03) {
    unsigned char len = 0;
    if (!RecvExact(socket, reinterpret_cast<char*>(&len), 1, timeout_ms, wsa_error)) {
      return false;
    }
    to_read = len + 2;
  } else {
    return false;
  }
  std::vector<char> discard(static_cast<size_t>(to_read));
  return RecvExact(socket, discard.data(), to_read, timeout_ms, wsa_error);
}

std::string FormatHostPortForHttpConnect(const TargetEndpoint& endpoint) {
  bool needs_brackets = endpoint.host.find(':') != std::string::npos &&
                        endpoint.host.front() != '[';
  std::ostringstream out;
  if (needs_brackets) {
    out << "[" << endpoint.host << "]";
  } else {
    out << endpoint.host;
  }
  out << ":" << endpoint.port;
  return out.str();
}

}  // namespace

bool InitWinsockForHook() {
  WSADATA data{};
  return WSAStartup(MAKEWORD(2, 2), &data) == 0;
}

bool EndpointFromSockAddr(const sockaddr* addr, int addr_len, const AppConfig& config,
                          TargetEndpoint* endpoint) {
  if (!addr || !endpoint) {
    return false;
  }

  char host[NI_MAXHOST]{};
  char service[NI_MAXSERV]{};
  if (getnameinfo(addr, addr_len, host, sizeof(host), service, sizeof(service),
                  NI_NUMERICHOST | NI_NUMERICSERV) != 0) {
    return false;
  }

  endpoint->family = addr->sa_family;
  endpoint->host = host;
  endpoint->port = static_cast<uint16_t>(std::stoi(service));

  if (addr->sa_family == AF_INET) {
    auto* in = reinterpret_cast<const sockaddr_in*>(addr);
    uint32_t ip = ntohl(in->sin_addr.s_addr);
    endpoint->is_local = IsLoopbackIpv4(ip) || ip == 0;
    endpoint->is_private = IsPrivateIpv4(ip);
    endpoint->is_proxy_endpoint = IsProxyEndpointIpv4(ip, endpoint->port, config);
    endpoint->is_fake_ip = (ip & 0xFFFE0000u) == 0xC6120000u;  // 198.18.0.0/15
  } else if (addr->sa_family == AF_INET6) {
    auto* in6 = reinterpret_cast<const sockaddr_in6*>(addr);
    endpoint->is_local = IN6_IS_ADDR_LOOPBACK(&in6->sin6_addr) ||
                         IN6_IS_ADDR_UNSPECIFIED(&in6->sin6_addr);
    endpoint->is_private =
        IN6_IS_ADDR_LINKLOCAL(&in6->sin6_addr) || IN6_IS_ADDR_SITELOCAL(&in6->sin6_addr);
  }
  return true;
}

bool ShouldBypass(const AppConfig& config, const TargetEndpoint& endpoint) {
  if (endpoint.family != AF_INET && endpoint.family != AF_INET6) {
    return true;
  }
  if (endpoint.family == AF_INET6 && config.proxy_rules.ipv6_mode == IpMode::Block) {
    WSASetLastError(WSAEHOSTUNREACH);
    return false;
  }
  if (config.bypass.localhost && endpoint.is_local) {
    return true;
  }
  if (config.bypass.private_networks && endpoint.is_private) {
    return true;
  }
  if (config.bypass.proxy_endpoint && endpoint.is_proxy_endpoint) {
    return true;
  }
  return false;
}

bool IsTcpSocket(SOCKET socket) {
  int type = 0;
  int length = sizeof(type);
  if (getsockopt(socket, SOL_SOCKET, SO_TYPE, reinterpret_cast<char*>(&type), &length) != 0) {
    return true;
  }
  return type == SOCK_STREAM;
}

bool EstablishProxyTunnel(SOCKET socket, const AppConfig& config,
                          const TargetEndpoint& endpoint, bool restore_nonblocking,
                          int* wsa_error) {
  if (g_inside_proxy) {
    return false;
  }
  g_inside_proxy = true;
  SetSocketBlockingMode(socket, true);
  SetSocketTimeouts(socket, config.timeout);

  auto finish = [&](bool ok) {
    if (restore_nonblocking) {
      SetSocketBlockingMode(socket, false);
    }
    g_inside_proxy = false;
    return ok;
  };

  addrinfo hints{};
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_family = AF_UNSPEC;
  addrinfo* results = nullptr;
  std::string port = std::to_string(config.proxy.port);
  int gai = getaddrinfo(config.proxy.host.c_str(), port.c_str(), &hints, &results);
  if (gai != 0 || !results) {
    if (wsa_error) {
      *wsa_error = WSAHOST_NOT_FOUND;
    }
    return finish(false);
  }

  bool connected = false;
  int last_connect_error = 0;
  for (addrinfo* item = results; item; item = item->ai_next) {
    int connect_error = 0;
    if (ConnectWithWait(socket, item->ai_addr, static_cast<int>(item->ai_addrlen),
                        config.timeout.connect_ms, &connect_error)) {
      connected = true;
      break;
    }
    last_connect_error = connect_error;
  }
  freeaddrinfo(results);
  if (!connected) {
    if (wsa_error) {
      *wsa_error = last_connect_error != 0 ? last_connect_error : WSAGetLastError();
    }
    return finish(false);
  }
  if (wsa_error) {
    *wsa_error = 0;
  }

  bool tunnel_ok = false;
  if (config.proxy.type == ProxyType::Http) {
    std::string authority = FormatHostPortForHttpConnect(endpoint);
    std::ostringstream request;
    request << "CONNECT " << authority << " HTTP/1.1\r\n"
            << "Host: " << authority << "\r\n"
            << "Proxy-Connection: Keep-Alive\r\n\r\n";
    std::string payload = request.str();
    tunnel_ok = SendAll(socket, payload.data(), static_cast<int>(payload.size()),
                        config.timeout.send_ms, wsa_error) &&
                ReadHttpResponse(socket, config.timeout.recv_ms, wsa_error);
  } else {
    tunnel_ok = Socks5Tunnel(socket, endpoint, config.timeout.recv_ms, wsa_error);
  }

  if (!tunnel_ok && wsa_error && *wsa_error == 0) {
    *wsa_error = WSAECONNRESET;
  }
  return finish(tunnel_ok);
}

}  // namespace codex_proxy
