#include "common/network_utils.h"

#include "common/string_utils.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <string>

namespace codex_proxy {
namespace {

std::wstring SocketErrorMessage(const wchar_t* prefix, int error_code) {
  return std::wstring(prefix) + L" (Winsock " + std::to_wstring(error_code) + L")";
}

}  // namespace

bool CanConnectTcp(const std::string& host, uint16_t port, int timeout_ms,
                   std::wstring* error) {
  WSADATA data{};
  int rc = WSAStartup(MAKEWORD(2, 2), &data);
  if (rc != 0) {
    if (error) {
      *error = SocketErrorMessage(L"Unable to initialize network check", rc);
    }
    return false;
  }

  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;
  addrinfo* addresses = nullptr;
  const std::string service = std::to_string(port);
  rc = getaddrinfo(host.c_str(), service.c_str(), &hints, &addresses);
  if (rc != 0) {
    if (error) {
      *error = L"Unable to resolve proxy host " + Utf8ToWide(host) + L" (" +
               std::to_wstring(rc) + L")";
    }
    WSACleanup();
    return false;
  }

  int last_error = WSAECONNREFUSED;
  bool connected = false;
  for (addrinfo* address = addresses; address; address = address->ai_next) {
    SOCKET socket_handle = socket(address->ai_family, address->ai_socktype,
                                  address->ai_protocol);
    if (socket_handle == INVALID_SOCKET) {
      last_error = WSAGetLastError();
      continue;
    }

    u_long non_blocking = 1;
    ioctlsocket(socket_handle, FIONBIO, &non_blocking);
    rc = connect(socket_handle, address->ai_addr,
                 static_cast<int>(address->ai_addrlen));
    if (rc == 0) {
      connected = true;
    } else {
      last_error = WSAGetLastError();
      if (last_error == WSAEWOULDBLOCK || last_error == WSAEINPROGRESS ||
          last_error == WSAEINVAL) {
        fd_set writable;
        FD_ZERO(&writable);
        FD_SET(socket_handle, &writable);
        timeval timeout{};
        timeout.tv_sec = timeout_ms / 1000;
        timeout.tv_usec = (timeout_ms % 1000) * 1000;
        rc = select(0, nullptr, &writable, nullptr, &timeout);
        if (rc > 0 && FD_ISSET(socket_handle, &writable)) {
          int socket_error = 0;
          int size = sizeof(socket_error);
          if (getsockopt(socket_handle, SOL_SOCKET, SO_ERROR,
                         reinterpret_cast<char*>(&socket_error), &size) == 0 &&
              socket_error == 0) {
            connected = true;
          } else {
            last_error = socket_error == 0 ? WSAGetLastError() : socket_error;
          }
        } else if (rc == 0) {
          last_error = WSAETIMEDOUT;
        }
      }
    }

    closesocket(socket_handle);
    if (connected) {
      break;
    }
  }

  freeaddrinfo(addresses);
  WSACleanup();
  if (!connected && error) {
    *error = SocketErrorMessage(L"Unable to connect to the configured proxy", last_error);
  }
  return connected;
}

}  // namespace codex_proxy
