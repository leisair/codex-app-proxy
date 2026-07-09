cmake_minimum_required(VERSION 3.20)

project(codex-proxy LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(CODEX_PROXY_STATIC_RUNTIME "Use the static MSVC runtime" ON)
option(CODEX_PROXY_BUILD_TESTS "Build lightweight unit tests" ON)

if(MSVC)
  add_compile_options(/utf-8 /W4 /permissive-)
  add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
  if(CODEX_PROXY_STATIC_RUNTIME)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  endif()
endif()

include(FetchContent)
FetchContent_Declare(
  minhook
  GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
  GIT_TAG v1.3.4
)
FetchContent_MakeAvailable(minhook)

add_library(codex_proxy_common STATIC
  src/common/config.cpp
  src/common/logging.cpp
  src/common/process_utils.cpp
  src/common/string_utils.cpp
)
target_include_directories(codex_proxy_common PUBLIC src)

add_executable(CodexProxyLauncher
  src/launcher/main.cpp
)
target_link_libraries(CodexProxyLauncher PRIVATE codex_proxy_common shell32)

add_library(codex_proxy_hook SHARED
  src/hook/dllmain.cpp
  src/hook/hooks.cpp
  src/hook/proxy_tunnel.cpp
)
target_link_libraries(codex_proxy_hook PRIVATE codex_proxy_common minhook ws2_32)
target_compile_definitions(codex_proxy_hook PRIVATE CODEX_PROXY_HOOK_EXPORTS)

set_target_properties(codex_proxy_hook PROPERTIES
  PREFIX ""
  OUTPUT_NAME "codex_proxy_hook"
)

if(CODEX_PROXY_BUILD_TESTS)
  enable_testing()
  add_executable(codex_proxy_tests
    tests/config_tests.cpp
  )
  target_link_libraries(codex_proxy_tests PRIVATE codex_proxy_common ws2_32)
  add_test(NAME codex_proxy_tests COMMAND codex_proxy_tests)
endif()

install(TARGETS CodexProxyLauncher codex_proxy_hook RUNTIME DESTINATION . LIBRARY DESTINATION .)
install(FILES resources/config-web.html resources/default_config.json DESTINATION .)
