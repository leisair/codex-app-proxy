#pragma once

#include <cstdint>
#include <string>

namespace codex_proxy {

bool CanConnectTcp(const std::string& host, uint16_t port, int timeout_ms,
                   std::wstring* error);

}  // namespace codex_proxy
