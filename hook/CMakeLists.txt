cmake_minimum_required(VERSION 3.24)
project(and_hook LANGUAGES C CXX)

# 32-bit only: the game is a 32-bit process.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "Configure from an x86 developer shell (-arch=x86); the game is 32-bit.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")  # static CRT: no DLL dependencies

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

# Vulkan backend (src/rhi/vulkan, docs/decision.md 2026-10-04): open-source Khronos sources at pinned tags. The
# driver provides vulkan-1.dll; volk loads it at run time, so nothing links against it.
FetchContent_Declare(vulkan_headers
  GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
  GIT_TAG vulkan-sdk-1.3.296.0)
FetchContent_Declare(volk
  GIT_REPOSITORY https://github.com/zeux/volk.git
  GIT_TAG vulkan-sdk-1.3.296.0)
FetchContent_Declare(glslang
  GIT_REPOSITORY https://github.com/KhronosGroup/glslang.git
  GIT_TAG vulkan-sdk-1.3.296.0)
set(VOLK_PULL_IN_VULKAN OFF CACHE BOOL "" FORCE)
set(VOLK_HEADERS_ONLY ON CACHE BOOL "" FORCE)  # compiled in src/rhi/vulkan (VOLK_IMPLEMENTATION)
set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)
set(BUILD_EXTERNAL OFF CACHE BOOL "" FORCE)
set(ENABLE_SPVREMAPPER OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(vulkan_headers volk glslang)

# Recovered game code: every .cpp under src/ is linked into the hook DLL and registers its own HOOK() entries.
file(GLOB_RECURSE RECOVERED_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../src/*.cpp)
list(FILTER RECOVERED_SOURCES EXCLUDE REGEX "/tests/")  # standalone test programs, built below

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
  src/profiler.cpp
  src/d3d_trace.cpp
  src/testscenario.cpp
  enhance/display.cpp
  enhance/startup.cpp
  enhance/render.cpp
  enhance/overrides.cpp
  enhance/camera.cpp
  enhance/persistence.cpp
  enhance/audio.cpp
  enhance/qol.cpp
  enhance/ui_aspect.cpp
  enhance/settings.cpp
  enhance/shadows.cpp
  enhance/weather.cpp
  enhance/remaster_menu.cpp
  dinput8.def)

target_include_directories(and_hook PRIVATE src enhance ../src ${minhook_SOURCE_DIR}/include)
target_link_libraries(and_hook PRIVATE minhook d3d9 dbghelp winmm dxguid ole32 windowscodecs dsound
  Vulkan::Headers glslang::glslang glslang::SPIRV glslang::glslang-default-resource-limits)
target_include_directories(and_hook PRIVATE ${volk_SOURCE_DIR})
target_compile_definitions(and_hook PRIVATE VK_NO_PROTOTYPES VK_USE_PLATFORM_WIN32_KHR)
set_target_properties(and_hook PROPERTIES OUTPUT_NAME dinput8)

# x87 and precise floating point while differential tests need exact agreement (handover s8).
target_compile_options(and_hook PRIVATE /arch:IA32 /fp:precise /W4 /permissive- /utf-8 /Zi)
# Symbols for the sampling profiler (hook/src/profiler.cpp) and crash reports; optimisation and code are unchanged.
target_link_options(and_hook PRIVATE /DEBUG /OPT:REF /OPT:ICF)
target_compile_definitions(and_hook PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)


# Offline test for the Vulkan backend's shader generators (tools/check.ps1): vk_shader_test <corpus dir>.
set(VK_TEST_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/rhi/vulkan/tests/shader_test.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/rhi/vulkan/vk_glsl.cpp)
foreach(f vk_shader_translate.cpp vk_fixed_function.cpp vk_pipeline_input.cpp vk_tessellation.cpp)
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../src/rhi/vulkan/${f})
    list(APPEND VK_TEST_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../src/rhi/vulkan/${f})
  endif()
endforeach()
add_executable(vk_shader_test EXCLUDE_FROM_ALL ${VK_TEST_SOURCES})  # build with -Target vk_shader_test
target_include_directories(vk_shader_test PRIVATE ../src ${volk_SOURCE_DIR})
target_compile_definitions(vk_shader_test PRIVATE RHI_SHADER_TEST VK_NO_PROTOTYPES WIN32_LEAN_AND_MEAN NOMINMAX
  _CRT_SECURE_NO_WARNINGS)
target_link_libraries(vk_shader_test PRIVATE Vulkan::Headers glslang::glslang glslang::SPIRV
  glslang::glslang-default-resource-limits)