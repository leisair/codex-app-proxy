#include "common/string_utils.h"

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <windows.h>

namespace codex_proxy {

std::wstring Utf8ToWide(const std::string& value) {
  if (value.empty()) {
    return {};
  }
  int size = MultiByteToWideChar(CP_UTF8, 0, value.data(),
                                 static_cast<int>(value.size()), nullptr, 0);
  std::wstring result(size, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                      result.data(), size);
  return result;
}

std::string WideToUtf8(const std::wstring& value) {
  if (value.empty()) {
    return {};
  }
  int size = WideCharToMultiByte(CP_UTF8, 0, value.data(),
                                static_cast<int>(value.size()), nullptr, 0,
                                nullptr, nullptr);
  std::string result(size, '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                      result.data(), size, nullptr, nullptr);
  return result;
}

std::wstring ToLower(std::wstring value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
  return value;
}

std::string ToLower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return value;
}

std::wstring Trim(std::wstring value) {
  auto is_space = [](wchar_t ch) { return std::iswspace(ch) != 0; };
  value.erase(value.begin(), std::find_if_not(value.begin(), value.end(), is_space));
  value.erase(std::find_if_not(value.rbegin(), value.rend(), is_space).base(),
              value.end());
  return value;
}

std::string Trim(std::string value) {
  auto is_space = [](unsigned char ch) { return std::isspace(ch) != 0; };
  value.erase(value.begin(), std::find_if_not(value.begin(), value.end(), is_space));
  value.erase(std::find_if_not(value.rbegin(), value.rend(), is_space).base(),
              value.end());
  return value;
}

std::vector<std::string> SplitCsv(const std::string& value) {
  std::vector<std::string> parts;
  std::string current;
  for (char ch : value) {
    if (ch == ',') {
      parts.push_back(Trim(current));
      current.clear();
    } else {
      current.push_back(ch);
    }
  }
  parts.push_back(Trim(current));
  return parts;
}

}  // namespace codex_proxy
