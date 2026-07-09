#include "hook/hooks.h"

#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>

#include "common/config.h"
#include "common/logging.h"
#include "common/process_utils.h"
#include "common/string_utils.h"
#include "hook/proxy_tunnel.h"

#include <MinHook.h>
#include <atomic>
#include <map>
#include <mutex>
#include <mswsock.h>
#include <string>
#include <tlhelp32.h>
#include <vector>

namespace codex_proxy {
namespace {

using ConnectFn = int(WSAAPI*)(SOCKET, const sockaddr*, int);
using WSAConnectFn = int(WSAAPI*)(SOCKET, const sockaddr*, int, LPWSABUF, LPWSABUF,
                                  LPQOS, LPQOS);
using GetAddrInfoFn = int(WSAAPI*)(PCSTR, PCSTR, const ADDRINFOA*, PADDRINFOA*);
using GetAddrInfoWFn = int(WSAAPI*)(PCWSTR, PCWSTR, const ADDRINFOW*, PADDRINFOW*);
using FreeAddrInfoFn = void(WSAAPI*)(PADDRINFOA);
using FreeAddrInfoWFn = void(WSAAPI*)(PADDRINFOW);
using CreateProcessWFn = BOOL(WINAPI*)(LPCWSTR, LPWSTR, LPSECURITY_ATTRIBUTES,
                                        LPSECURITY_ATTRIBUTES, BOOL, DWORD, LPVOID,
                                        LPCWSTR, LPSTARTUPINFOW, LPPROCESS_INFORMATION);
using WSAIoctlFn = int(WSAAPI*)(SOCKET, DWORD, LPVOID, DWORD, LPVOID, DWORD, LPDWORD,
                                LPWSAOVERLAPPED, LPWSAOVERLAPPED_COMPLETION_ROUTINE);
using IoctlSocketFn = int(WSAAPI*)(SOCKET, long, u_long*);
using ConnectExFn = BOOL(PASCAL*)(SOCKET, const sockaddr*, int, PVOID, DWORD, LPDWORD,
                                  LPOVERLAPPED);

ConnectFn g_connect = nullptr;
WSAConnectFn g_wsa_connect = nullptr;
GetAddrInfoFn g_getaddrinfo = nullptr;
GetAddrInfoWFn g_getaddrinfo_w = nullptr;
FreeAddrInfoFn g_freeaddrinfo = nullptr;
FreeAddrInfoWFn g_freeaddrinfo_w = nullptr;
CreateProcessWFn g_create_process_w = nullptr;
WSAIoctlFn g_wsa_ioctl = nullptr;
IoctlSocketFn g_ioctlsocket = nullptr;
ConnectExFn g_connect_ex = nullptr;

AppConfig g_config;
std::wstring g_dll_path;
std::mutex g_fake_ip_mutex;
std::mutex g_socket_state_mutex;
uint32_t g_next_fake_ip = 0xC6120001u;
std::map<uint32_t, std::string> g_fake_ip_to_host;
std::map<void*, bool> g_fake_addrinfo_allocations;
std::map<SOCKET, bool> g_socket_nonblocking;

bool CurrentProcessAllowed() {
  wchar_t path[MAX_PATH]{};
  GetModuleFileNameW(nullptr, path, MAX_PATH);
  bool allowed = IsAllowedProcess(g_config, path);
  Logger::Instance().Info(std::wstring(L"DLL loaded in process path=") + path +
                          L" allowed=" + (allowed ? L"true" : L"false"));
  return allowed;
}

std::wstring CommandProcessName(LPCWSTR application_name, LPWSTR command_line) {
  if (application_name && application_name[0]) {
    return GetBaseName(application_name);
  }
  if (!command_line) {
    return {};
  }
  std::wstring cmd = Trim(command_line);
  if (cmd.empty()) {
    return {};
  }
  if (cmd.front() == L'"') {
    size_t end = cmd.find(L'"', 1);
    if (end != std::wstring::npos) {
      return GetBaseName(cmd.substr(1, end - 1));
    }
  }
  size_t space = cmd.find(L' ');
  return GetBaseName(space == std::wstring::npos ? cmd : cmd.substr(0, space));
}

bool RewriteFakeIp(TargetEndpoint* endpoint) {
  if (!endpoint || !endpoint->is_fake_ip || endpoint->family != AF_INET) {
    return false;
  }
  in_addr addr{};
  if (InetPtonA(AF_INET, endpoint->host.c_str(), &addr) != 1) {
    return false;
  }
  std::lock_guard<std::mutex> lock(g_fake_ip_mutex);
  auto it = g_fake_ip_to_host.find(ntohl(addr.s_addr));
  if (it == g_fake_ip_to_host.end()) {
    return false;
  }
  endpoint->host = it->second;
  return true;
}

void MarkSocketNonBlocking(SOCKET socket, bool nonblocking) {
  std::lock_guard<std::mutex> lock(g_socket_state_mutex);
  g_socket_nonblocking[socket] = nonblocking;
}

bool IsSocketMarkedNonBlocking(SOCKET socket) {
  std::lock_guard<std::mutex> lock(g_socket_state_mutex);
  auto it = g_socket_nonblocking.find(socket);
  return it != g_socket_nonblocking.end() && it->second;
}

int ProxyAwareConnect(SOCKET socket, const sockaddr* name, int namelen) {
  if (!IsTcpSocket(socket)) {
    if (g_config.proxy_rules.udp_mode == UdpMode::Block) {
      WSASetLastError(WSAEHOSTUNREACH);
      Logger::Instance().Warn(L"Blocked non-TCP socket connect");
      return SOCKET_ERROR;
    }
    return g_connect(socket, name, namelen);
  }

  TargetEndpoint endpoint;
  if (!EndpointFromSockAddr(name, namelen, g_config, &endpoint)) {
    return g_connect(socket, name, namelen);
  }
  RewriteFakeIp(&endpoint);
  if (endpoint.family == AF_INET6 && g_config.proxy_rules.ipv6_mode == IpMode::Block) {
    WSASetLastError(WSAEHOSTUNREACH);
    Logger::Instance().Warn(L"Blocked IPv6 connection");
    return SOCKET_ERROR;
  }
  if (endpoint.family == AF_INET6 && g_config.proxy_rules.ipv6_mode == IpMode::Direct) {
    return g_connect(socket, name, namelen);
  }
  if (ShouldBypass(g_config, endpoint)) {
    Logger::Instance().Info(L"Bypass connect " + Utf8ToWide(endpoint.host) +
                            L":" + std::to_wstring(endpoint.port));
    return g_connect(socket, name, namelen);
  }

  int wsa_error = 0;
  bool was_nonblocking = IsSocketMarkedNonBlocking(socket);
  Logger::Instance().Info(L"Proxy connect " + Utf8ToWide(endpoint.host) +
                          L":" + std::to_wstring(endpoint.port) +
                          L" nonblocking=" + (was_nonblocking ? L"true" : L"false"));
  bool ok = EstablishProxyTunnel(socket, g_config, endpoint, was_nonblocking, &wsa_error);
  if (!ok) {
    WSASetLastError(wsa_error ? wsa_error : WSAECONNRESET);
    Logger::Instance().Warn(L"Proxy tunnel failed for " + Utf8ToWide(endpoint.host) +
                            L":" + std::to_wstring(endpoint.port) +
                            L" wsa=" + std::to_wstring(wsa_error));
    return SOCKET_ERROR;
  }
  Logger::Instance().Info(L"Proxy tunnel established for " + Utf8ToWide(endpoint.host) +
                          L":" + std::to_wstring(endpoint.port));
  return 0;
}

int WSAAPI HookConnect(SOCKET socket, const sockaddr* name, int namelen) {
  return ProxyAwareConnect(socket, name, namelen);
}

int WSAAPI HookWSAConnect(SOCKET socket, const sockaddr* name, int namelen,
                          LPWSABUF caller_data, LPWSABUF callee_data,
                          LPQOS sqos, LPQOS gqos) {
  (void)caller_data;
  (void)callee_data;
  (void)sqos;
  (void)gqos;
  int result = ProxyAwareConnect(socket, name, namelen);
  return result;
}

BOOL PASCAL HookConnectEx(SOCKET socket, const sockaddr* name, int namelen,
                          PVOID send_buffer, DWORD send_data_length,
                          LPDWORD bytes_sent, LPOVERLAPPED overlapped) {
  int result = ProxyAwareConnect(socket, name, namelen);
  if (result != 0) {
    return FALSE;
  }
  if (send_buffer && send_data_length > 0) {
    int sent = send(socket, static_cast<const char*>(send_buffer),
                    static_cast<int>(send_data_length), 0);
    if (bytes_sent) {
      *bytes_sent = sent > 0 ? static_cast<DWORD>(sent) : 0;
    }
    return sent >= 0;
  }
  if (bytes_sent) {
    *bytes_sent = 0;
  }
  (void)overlapped;
  SetLastError(0);
  return TRUE;
}

uint32_t AllocateFakeIp(const std::string& host) {
  std::lock_guard<std::mutex> lock(g_fake_ip_mutex);
  uint32_t ip = g_next_fake_ip++;
  g_fake_ip_to_host[ip] = host;
  return ip;
}

int WSAAPI HookGetAddrInfo(PCSTR node, PCSTR service, const ADDRINFOA* hints,
                           PADDRINFOA* result) {
  if (!g_config.fake_ip.enabled || !node || !result) {
    return g_getaddrinfo(node, service, hints, result);
  }
  std::string host = node;
  if (host == "localhost" || host == "127.0.0.1") {
    return g_getaddrinfo(node, service, hints, result);
  }
  auto* info = static_cast<ADDRINFOA*>(calloc(1, sizeof(ADDRINFOA)));
  auto* addr = static_cast<sockaddr_in*>(calloc(1, sizeof(sockaddr_in)));
  if (!info || !addr) {
    free(info);
    free(addr);
    return EAI_MEMORY;
  }
  uint32_t fake = AllocateFakeIp(host);
  addr->sin_family = AF_INET;
  addr->sin_addr.s_addr = htonl(fake);
  addr->sin_port = service ? htons(static_cast<uint16_t>(atoi(service))) : 0;
  info->ai_family = AF_INET;
  info->ai_socktype = hints ? hints->ai_socktype : SOCK_STREAM;
  info->ai_protocol = hints ? hints->ai_protocol : IPPROTO_TCP;
  info->ai_addrlen = sizeof(sockaddr_in);
  info->ai_addr = reinterpret_cast<sockaddr*>(addr);
  *result = info;
  {
    std::lock_guard<std::mutex> lock(g_fake_ip_mutex);
    g_fake_addrinfo_allocations[info] = false;
  }
  return 0;
}

int WSAAPI HookGetAddrInfoW(PCWSTR node, PCWSTR service, const ADDRINFOW* hints,
                            PADDRINFOW* result) {
  if (!g_config.fake_ip.enabled || !node || !result) {
    return g_getaddrinfo_w(node, service, hints, result);
  }
  std::string host = WideToUtf8(node);
  std::string service_utf8 = service ? WideToUtf8(service) : std::string();
  ADDRINFOA hints_a{};
  if (hints) {
    hints_a.ai_socktype = hints->ai_socktype;
    hints_a.ai_protocol = hints->ai_protocol;
  }
  PADDRINFOA result_a = nullptr;
  int rc = HookGetAddrInfo(host.c_str(), service ? service_utf8.c_str() : nullptr,
                           hints ? &hints_a : nullptr, &result_a);
  if (rc != 0) {
    return rc;
  }
  auto* info_w = static_cast<ADDRINFOW*>(calloc(1, sizeof(ADDRINFOW)));
  auto* addr = static_cast<sockaddr_in*>(calloc(1, sizeof(sockaddr_in)));
  if (!info_w || !addr || !result_a) {
    free(info_w);
    free(addr);
    if (result_a) {
      free(result_a->ai_addr);
      free(result_a);
    }
    return EAI_MEMORY;
  }
  memcpy(addr, result_a->ai_addr, sizeof(sockaddr_in));
  info_w->ai_family = result_a->ai_family;
  info_w->ai_socktype = result_a->ai_socktype;
  info_w->ai_protocol = result_a->ai_protocol;
  info_w->ai_addrlen = sizeof(sockaddr_in);
  info_w->ai_addr = reinterpret_cast<sockaddr*>(addr);
  *result = info_w;
  {
    std::lock_guard<std::mutex> lock(g_fake_ip_mutex);
    g_fake_addrinfo_allocations[info_w] = true;
    g_fake_addrinfo_allocations.erase(result_a);
  }
  free(result_a->ai_addr);
  free(result_a);
  return 0;
}

void WSAAPI HookFreeAddrInfo(PADDRINFOA info) {
  if (!info) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(g_fake_ip_mutex);
    auto it = g_fake_addrinfo_allocations.find(info);
    if (it != g_fake_addrinfo_allocations.end()) {
      free(info->ai_addr);
      free(info);
      g_fake_addrinfo_allocations.erase(it);
      return;
    }
  }
  g_freeaddrinfo(info);
}

void WSAAPI HookFreeAddrInfoW(PADDRINFOW info) {
  if (!info) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(g_fake_ip_mutex);
    auto it = g_fake_addrinfo_allocations.find(info);
    if (it != g_fake_addrinfo_allocations.end()) {
      free(info->ai_addr);
      free(info);
      g_fake_addrinfo_allocations.erase(it);
      return;
    }
  }
  g_freeaddrinfo_w(info);
}

BOOL WINAPI HookCreateProcessW(LPCWSTR application_name, LPWSTR command_line,
                               LPSECURITY_ATTRIBUTES process_attributes,
                               LPSECURITY_ATTRIBUTES thread_attributes,
                               BOOL inherit_handles, DWORD creation_flags,
                               LPVOID environment, LPCWSTR current_directory,
                               LPSTARTUPINFOW startup_info,
                               LPPROCESS_INFORMATION process_information) {
  std::wstring name = CommandProcessName(application_name, command_line);
  bool should_inject = g_config.child_injection && IsAllowedProcess(g_config, name);
  DWORD flags = creation_flags;
  if (should_inject) {
    flags |= CREATE_SUSPENDED;
  }

  BOOL ok = g_create_process_w(application_name, command_line, process_attributes,
                               thread_attributes, inherit_handles, flags, environment,
                               current_directory, startup_info, process_information);
  if (!ok || !should_inject || !process_information) {
    return ok;
  }

  std::wstring error;
  if (InjectDllIntoProcess(process_information->hProcess, g_dll_path, &error)) {
    Logger::Instance().Info(L"Injected child process " + name);
  } else {
    Logger::Instance().Warn(L"Child injection failed for " + name + L": " + error);
  }

  if ((creation_flags & CREATE_SUSPENDED) == 0) {
    ResumeThread(process_information->hThread);
  }
  return ok;
}

int WSAAPI HookWSAIoctl(SOCKET socket, DWORD io_control_code, LPVOID in_buffer,
                        DWORD in_buffer_size, LPVOID out_buffer, DWORD out_buffer_size,
                        LPDWORD bytes_returned, LPWSAOVERLAPPED overlapped,
                        LPWSAOVERLAPPED_COMPLETION_ROUTINE completion_routine) {
  int rc = g_wsa_ioctl(socket, io_control_code, in_buffer, in_buffer_size, out_buffer,
                       out_buffer_size, bytes_returned, overlapped, completion_routine);
  if (rc == 0 && io_control_code == FIONBIO && in_buffer &&
      in_buffer_size >= sizeof(u_long)) {
    MarkSocketNonBlocking(socket, *static_cast<u_long*>(in_buffer) != 0);
  }
  if (rc == 0 && io_control_code == SIO_GET_EXTENSION_FUNCTION_POINTER &&
      in_buffer && in_buffer_size == sizeof(GUID) && out_buffer &&
      out_buffer_size >= sizeof(void*)) {
    GUID connect_ex_guid = WSAID_CONNECTEX;
    if (memcmp(in_buffer, &connect_ex_guid, sizeof(GUID)) == 0) {
      g_connect_ex = *reinterpret_cast<ConnectExFn*>(out_buffer);
      *reinterpret_cast<ConnectExFn*>(out_buffer) = HookConnectEx;
      Logger::Instance().Info(L"ConnectEx pointer replaced");
    }
  }
  return rc;
}

int WSAAPI HookIoctlSocket(SOCKET socket, long cmd, u_long* argp) {
  int rc = g_ioctlsocket(socket, cmd, argp);
  if (rc == 0 && cmd == FIONBIO && argp) {
    MarkSocketNonBlocking(socket, *argp != 0);
  }
  return rc;
}

bool HookApi(const wchar_t* module, const char* name, void* hook, void** original) {
  MH_STATUS status = MH_CreateHookApi(module, name, hook, original);
  if (status != MH_OK) {
    Logger::Instance().Warn(L"MH_CreateHookApi failed for " + Utf8ToWide(name));
    return false;
  }
  return true;
}

}  // namespace

bool InstallHooks(void* module) {
  Logger::Instance().Init(L"dll");
  g_dll_path = GetModuleDir(static_cast<HMODULE>(module)) + L"\\codex_proxy_hook.dll";

  std::wstring error;
  if (!LoadConfig(DefaultConfigPath(), &g_config, &error)) {
    Logger::Instance().Warn(L"Failed to load config, using defaults: " + error);
  }
  Logger::Instance().Info(L"Hook config proxy=" + Utf8ToWide(g_config.proxy.host) +
                          L":" + std::to_wstring(g_config.proxy.port) +
                          L" type=" + Utf8ToWide(ProxyTypeToString(g_config.proxy.type)));
  if (!CurrentProcessAllowed()) {
    Logger::Instance().Warn(L"Process not allowed by config; hooks skipped");
    return true;
  }

  InitWinsockForHook();
  if (MH_Initialize() != MH_OK) {
    Logger::Instance().Error(L"MinHook initialization failed");
    return false;
  }

  HookApi(L"ws2_32.dll", "connect", reinterpret_cast<void*>(HookConnect),
          reinterpret_cast<void**>(&g_connect));
  HookApi(L"ws2_32.dll", "WSAConnect", reinterpret_cast<void*>(HookWSAConnect),
          reinterpret_cast<void**>(&g_wsa_connect));
  HookApi(L"ws2_32.dll", "getaddrinfo", reinterpret_cast<void*>(HookGetAddrInfo),
          reinterpret_cast<void**>(&g_getaddrinfo));
  HookApi(L"ws2_32.dll", "GetAddrInfoW", reinterpret_cast<void*>(HookGetAddrInfoW),
          reinterpret_cast<void**>(&g_getaddrinfo_w));
  HookApi(L"ws2_32.dll", "freeaddrinfo", reinterpret_cast<void*>(HookFreeAddrInfo),
          reinterpret_cast<void**>(&g_freeaddrinfo));
  HookApi(L"ws2_32.dll", "FreeAddrInfoW", reinterpret_cast<void*>(HookFreeAddrInfoW),
          reinterpret_cast<void**>(&g_freeaddrinfo_w));
  HookApi(L"ws2_32.dll", "WSAIoctl", reinterpret_cast<void*>(HookWSAIoctl),
          reinterpret_cast<void**>(&g_wsa_ioctl));
  HookApi(L"ws2_32.dll", "ioctlsocket", reinterpret_cast<void*>(HookIoctlSocket),
          reinterpret_cast<void**>(&g_ioctlsocket));
  HookApi(L"kernel32.dll", "CreateProcessW", reinterpret_cast<void*>(HookCreateProcessW),
          reinterpret_cast<void**>(&g_create_process_w));

  MH_EnableHook(MH_ALL_HOOKS);
  Logger::Instance().Info(L"All hooks installed");
  return true;
}

}  // namespace codex_proxy
