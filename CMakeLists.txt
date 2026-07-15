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

add_library(codex_proxy_common STATIC
  src/common/config.cpp
  src/common/launch_args.cpp
  src/common/logging.cpp
  src/common/network_utils.cpp
  src/common/process_utils.cpp
  src/common/string_utils.cpp
)
target_include_directories(codex_proxy_common PUBLIC src)
target_link_libraries(codex_proxy_common PUBLIC ws2_32)

add_executable(CodexProxyLauncher
  src/launcher/main.cpp
  resources/windows/launcher.rc
)
target_link_libraries(CodexProxyLauncher PRIVATE codex_proxy_common ole32 shell32 user32)

if(CODEX_PROXY_BUILD_TESTS)
  enable_testing()
  add_executable(codex_proxy_tests
    tests/config_tests.cpp
  )
  target_link_libraries(codex_proxy_tests PRIVATE codex_proxy_common)
  add_test(NAME codex_proxy_tests COMMAND codex_proxy_tests)
endif()

install(TARGETS CodexProxyLauncher RUNTIME DESTINATION .)
install(FILES resources/START-HERE.html resources/default_config.json README.md DESTINATION .)
