#include "common/config.h"
#include "common/launch_args.h"
#include "common/logging.h"
#include "common/network_utils.h"
#include "common/process_utils.h"

#include <iostream>
#include <string>
#include <windows.h>
#include <shellapi.h>

using namespace codex_proxy;

namespace {

struct Args {
  std::wstring codex_path;
};

Args ParseArgs(int argc, wchar_t** argv) {
  Args args;
  for (int i = 1; i < argc; ++i) {
    std::wstring arg = argv[i];
    if (arg == L"--codex" && i + 1 < argc) {
      args.codex_path = argv[++i];
    }
  }
  return args;
}

void OpenGuide() {
  const std::wstring guide = GetExecutableDir() + L"\\START-HERE.html";
  if (FileExists(guide)) {
    ShellExecuteW(nullptr, L"open", guide.c_str(), nullptr,
                  GetExecutableDir().c_str(), SW_SHOWNORMAL);
  }
}

int ShowError(const std::wstring& code, const std::wstring& title,
              const std::wstring& detail, int exit_code) {
  const std::wstring log_path = Logger::Instance().LogPath();
  const std::wstring guide = GetExecutableDir() + L"\\START-HERE.html";
  const bool has_guide = FileExists(guide);
  const std::wstring message =
      code + L"：" + title + L"\n\n" + detail +
      L"\n\n详细日志：\n" + log_path +
      (has_guide ? L"\n\n是否打开 START-HERE 配置与排错向导？" : L"");
  Logger::Instance().Error(code + L" " + title + L" | " + detail);
  std::wcerr << message << L"\n";
  const int answer = MessageBoxW(nullptr, message.c_str(),
                                 L"ChatGPT/Codex 代理启动器",
                                 (has_guide ? MB_YESNO : MB_OK) |
                                     MB_ICONERROR | MB_DEFBUTTON1);
  if (has_guide && answer == IDYES) {
    OpenGuide();
  }
  return exit_code;
}

int StartAppWithProxy(const std::wstring& app_path, const AppConfig& config) {
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};

  std::wstring command = BuildAppCommandLine(app_path, config);
  Logger::Instance().Info(L"Using app path: " + app_path);
  Logger::Instance().Info(L"Starting app with command: " + command);
  BOOL created = CreateProcessW(app_path.c_str(), command.data(), nullptr, nullptr,
                                FALSE, 0, nullptr, GetDirName(app_path).c_str(),
                                &si, &pi);
  if (!created) {
    const std::wstring detail = FormatLastError(L"CreateProcessW failed", GetLastError());
    return ShowError(
        L"E_LAUNCH_FAILED", L"无法启动 ChatGPT/Codex",
        L"Windows 未能启动桌面 App。请确认商店版 App 能够正常打开，"
        L"然后重试。\n\n系统信息：" + detail,
        10);
  }

  Logger::Instance().Info(L"Started app PID " + std::to_wstring(pi.dwProcessId));
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  std::wcout << L"ChatGPT/Codex launched with app proxy. Log: "
             << Logger::Instance().LogPath() << L"\n";
  return 0;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  Logger::Instance().Init(L"launcher");
  Args args = ParseArgs(argc, argv);
  const std::wstring config_path = DefaultConfigPath();
  Logger::Instance().Info(L"Using portable config: " + config_path);

  std::wstring error;
  if (!EnsureDefaultConfig(config_path, &error)) {
    return ShowError(
        L"E_CONFIG_CREATE", L"无法创建默认配置",
        L"启动器需要在自身目录写入 config.json。请将整个工具解压到"
        L"普通可写目录（例如桌面或 D 盘文件夹），不要直接在 ZIP 内运行。"
        L"\n\n系统信息：" + error,
        1);
  }

  AppConfig config;
  if (!LoadConfig(config_path, &config, &error)) {
    return ShowError(
        L"E_CONFIG_INVALID", L"config.json 配置无效",
        L"当前配置文件：\n" + config_path +
        L"\n\n请打开 START-HERE.html，恢复出厂配置或重新保存。"
        L"\n\n具体问题：" + error,
        1);
  }
  Logger::Instance().Info(L"Configured proxy: " + ProxyUrl(config));

  std::wstring app_path = args.codex_path.empty() ? FindCodexAppPath() : args.codex_path;
  if (app_path.empty() || !FileExists(app_path)) {
    return ShowError(
        L"E_APP_NOT_FOUND", L"未找到 Microsoft Store 版 ChatGPT",
        L"本工具仅支持 Windows x64 的 Microsoft Store 版 ChatGPT/Codex。"
        L"请先从 Microsoft Store 安装或更新 App，然后重试。",
        3);
  }

  if (AnyDesktopAppProcessRunning(app_path)) {
    return ShowError(
        L"E_APP_RUNNING", L"ChatGPT/Codex 已在运行",
        L"代理启动参数只会在 App 第一次启动时生效。请从托盘或任务管理器"
        L"完全退出 ChatGPT/Codex（包括后台进程），再双击启动器。",
        2);
  }

  if (!CanConnectTcp(config.proxy.host, config.proxy.port, 2000, &error)) {
    return ShowError(
        L"E_PROXY_UNREACHABLE", L"无法连接本地代理",
        L"当前配置：" + ProxyUrl(config) +
        L"\n\n请依次检查：\n"
        L"1. 代理软件是否正在运行；\n"
        L"2. 填写的是否是本地 HTTP、mixed 或 SOCKS5 监听端口；\n"
        L"3. 端口是否与代理软件界面显示的一致。\n\n"
        L"不要填写节点服务器端口、订阅端口或远程 443。\n\n系统信息：" + error,
        4);
  }

  return StartAppWithProxy(app_path, config);
}
