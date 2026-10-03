cmake_minimum_required(VERSION 3.24)
project(and_hook LANGUAGES C CXX)

# 32-bit only: the game is a 32-bit process.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "Configure from an x86 developer shell (-arch=x86); the game is 32-bit.")
endif()

include(FetchContent)
FetchContent_Declare(minhook
  GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
  GIT_TAG v1.3.3)
FetchContent_MakeAvailable(minhook)  # v1.3.3 has no CMakeLists: build its sources here (BSD-2-Clause)
add_library(minhook STATIC
  ${minhook_SOURCE_DIR}/src/buffer.c
  ${minhook_SOURCE_DIR}/src/hook.c
  ${minhook_SOURCE_DIR}/src/trampoline.c
  ${minhook_SOURCE_DIR}/src/hde/hde32.c)
target_include_directories(minhook PUBLIC ${minhook_SOURCE_DIR}/include)
set_property(TARGET minhook PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")  # static CRT: no DLL dependencies

# Recovered game code: every .cpp under src/ is linked into the hook DLL and registers its own HOOK() entries.
file(GLOB_RECURSE RECOVERED_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../src/*.cpp)

add_library(and_hook SHARED
  ${RECOVERED_SOURCES}
  src/main.cpp
  src/log.cpp
  src/config.cpp
  src/crash.cpp
  src/hooks.cpp
  src/d3d9_hooks.cpp
  src/testshot.cpp
  src/input_hooks.cpp
  src/testclock.cpp
  src/d3d_trace.cpp
  src/testscenario.cpp
  enhance/display.cpp
  enhance/startup.cpp
  enhance/render.cpp
  dinput8.def)

target_include_directories(and_hook PRIVATE src enhance ../src ${minhook_SOURCE_DIR}/include)
target_link_libraries(and_hook PRIVATE minhook d3d9 dbghelp dxguid)
set_target_properties(and_hook PROPERTIES OUTPUT_NAME dinput8)

# x87 and precise floating point while differential tests need exact agreement (handover s8).
target_compile_options(and_hook PRIVATE /arch:IA32 /fp:precise /W4 /permissive- /utf-8)
target_compile_definitions(and_hook PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
