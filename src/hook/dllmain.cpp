#include "hook/hooks.h"

#include <windows.h>

namespace {

DWORD WINAPI InitThread(LPVOID param) {
  codex_proxy::InstallHooks(param);
  return 0;
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(module);
    HANDLE thread = CreateThread(nullptr, 0, InitThread, module, 0, nullptr);
    if (thread) {
      CloseHandle(thread);
    }
  }
  return TRUE;
}
