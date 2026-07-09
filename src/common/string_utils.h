#pragma once

#include <string>
#include <vector>

namespace codex_proxy {

std::wstring Utf8ToWide(const std::string& value);
std::string WideToUtf8(const std::wstring& value);
std::wstring ToLower(std::wstring value);
std::string ToLower(std::string value);
std::wstring Trim(std::wstring value);
std::string Trim(std::string value);
std::vector<std::string> SplitCsv(const std::string& value);

}  // namespace codex_proxy
