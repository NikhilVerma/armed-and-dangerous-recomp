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
set(ENABLE_HLSL ON CACHE BOOL "" FORCE)  # XeGTAO (third_party/XeGTAO) is HLSL
set(BUILD_EXTERNAL OFF CACHE BOOL "" FORCE)
set(ENABLE_SPVREMAPPER OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(vulkan_headers volk glslang)

# Jolt Physics (MIT; docs/decision.md 2026-10-06): the open-source replacement for Havok, staged behind [physics]
# engine in and.ini (hook/enhance/physics_jolt.cpp; default: the original Havok). Its CMake project is in Build/.
FetchContent_Declare(jolt
  GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
  GIT_TAG v5.6.0
  SOURCE_SUBDIR Build)
set(TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(TARGET_HELLO_WORLD OFF CACHE BOOL "" FORCE)
set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
set(TARGET_SAMPLES OFF CACHE BOOL "" FORCE)
set(TARGET_VIEWER OFF CACHE BOOL "" FORCE)
set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
set(USE_STATIC_MSVC_RUNTIME_LIBRARY ON CACHE BOOL "" FORCE)
set(CROSS_PLATFORM_DETERMINISTIC ON CACHE BOOL "" FORCE)  # reproducible runs for tools/compare.py
set(DEBUG_RENDERER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
set(PROFILER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
set(FLOATING_POINT_EXCEPTIONS_ENABLED OFF CACHE BOOL "" FORCE)  # never unmask FP exceptions inside the game
set(ENABLE_OBJECT_STREAM OFF CACHE BOOL "" FORCE)
# GPU compute backends (hair, soft-body acceleration): not used.
set(JPH_USE_DX12 OFF CACHE BOOL "" FORCE)
set(JPH_USE_VK OFF CACHE BOOL "" FORCE)
set(JPH_USE_MTL OFF CACHE BOOL "" FORCE)
set(JPH_USE_CPU_COMPUTE OFF CACHE BOOL "" FORCE)
# SSE2 only: the bridge (and_physics) is compiled /arch:SSE2 and Jolt picks its SIMD paths from the compiler's
# instruction-set macros, so library and bridge must agree; also runs on any x86 CPU the game supports.
foreach(isa USE_SSE4_1 USE_SSE4_2 USE_AVX USE_AVX2 USE_AVX512 USE_LZCNT USE_TZCNT USE_F16C USE_FMADD)
  set(${isa} OFF CACHE BOOL "" FORCE)
endforeach()
FetchContent_MakeAvailable(jolt)
# The bridge: the only code that includes Jolt, compiled with SSE2 (Jolt's math types depend on the instruction set;
# the hook DLL itself is /arch:IA32). Everything else uses the plain C interface in physics/jolt_bridge.h.
add_library(and_physics STATIC physics/jolt_bridge.cpp)
target_link_libraries(and_physics PRIVATE Jolt)
target_compile_options(and_physics PRIVATE /arch:SSE2 /utf-8 /Zi)
target_compile_options(Jolt PRIVATE /Zi)  # line info for crash symbolisation (tools/symbolize.py)
set_property(TARGET and_physics PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
# AMD FidelityFX Super Resolution 3.1 upscaler (temporal anti-aliasing at native resolution; docs/subsystems/
# remaster-aa.md), built from the FidelityFX SDK v1.1.4 sources (MIT; the last release with a Vulkan backend) into
# the hook DLL: the game is 32-bit, so this replaces AMD's prebuilt 64-bit DLL. Only the FSR3 upscaler component, the
# Vulkan backend and their shaders (compiled at build time with the SDK's FidelityFX_SC.exe + glslang, as the SDK
# does). Vulkan calls go through volk (forced in; VK_NO_PROTOTYPES), so nothing links against vulkan-1.
FetchContent_Declare(ffx_sdk
  GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK.git
  GIT_TAG v1.1.4
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR not-a-cmake-project)  # sources only
FetchContent_MakeAvailable(ffx_sdk)
set(FFXSDK ${ffx_sdk_SOURCE_DIR}/sdk)
set(FFX_SHADER_OUT ${CMAKE_BINARY_DIR}/ffx_shaders)
file(MAKE_DIRECTORY ${FFX_SHADER_OUT})
set(FFX_GPU ${FFXSDK}/include/FidelityFX/gpu)
set(FFX_SC_ARGS -reflection -deps=gcc -DFFX_GPU=1
  -DFFX_FSR3UPSCALER_OPTION_UPSAMPLE_SAMPLERS_USE_DATA_HALF=0 -DFFX_FSR3UPSCALER_OPTION_ACCUMULATE_SAMPLERS_USE_DATA_HALF=0
  -DFFX_FSR3UPSCALER_OPTION_REPROJECT_SAMPLERS_USE_DATA_HALF=1
  -DFFX_FSR3UPSCALER_OPTION_POSTPROCESSLOCKSTATUS_SAMPLERS_USE_DATA_HALF=0
  -DFFX_FSR3UPSCALER_OPTION_UPSAMPLE_USE_LANCZOS_TYPE=2
  -compiler=glslang -e CS --target-env vulkan1.2 -S comp -Os -DFFX_GLSL=1
  "-DFFX_FSR3UPSCALER_OPTION_REPROJECT_USE_LANCZOS_TYPE={0,1}" "-DFFX_FSR3UPSCALER_OPTION_HDR_COLOR_INPUT={0,1}"
  "-DFFX_FSR3UPSCALER_OPTION_LOW_RESOLUTION_MOTION_VECTORS={0,1}"
  "-DFFX_FSR3UPSCALER_OPTION_JITTERED_MOTION_VECTORS={0,1}" "-DFFX_FSR3UPSCALER_OPTION_INVERTED_DEPTH={0,1}"
  "-DFFX_FSR3UPSCALER_OPTION_APPLY_SHARPENING={0,1}"
  -I${FFX_GPU} -I${FFX_GPU}/fsr3upscaler -output=${FFX_SHADER_OUT})
file(GLOB FFX_FSR3_GLSL ${FFXSDK}/src/backends/vk/shaders/fsr3upscaler/*.glsl)
set(FFX_SHADER_HEADERS)
foreach(shader ${FFX_FSR3_GLSL})
  get_filename_component(name ${shader} NAME_WE)
  foreach(variant "|0" "_wave64|0" "_16bit|1" "_wave64_16bit|1")
    string(REPLACE "|" ";" v "${variant}")
    list(GET v 0 suffix)
    list(GET v 1 half)
    set(out ${FFX_SHADER_OUT}/${name}${suffix}_permutations.h)
    add_custom_command(OUTPUT ${out}
      COMMAND ${FFXSDK}/tools/binary_store/FidelityFX_SC.exe ${FFX_SC_ARGS} -name=${name}${suffix} -DFFX_HALF=${half}
              ${shader}
      WORKING_DIRECTORY ${FFX_SHADER_OUT}
      DEPENDS ${shader}
      COMMENT "FSR3 shader ${name}${suffix}"
      VERBATIM)
    list(APPEND FFX_SHADER_HEADERS ${out})
  endforeach()
endforeach()
# The SDK assumes 64-bit builds: in 32-bit Vulkan, VkBuffer / VkPipelineCache are 64-bit integers, not pointers.
# Three lines of ffx_vk.cpp are rewritten in a build-tree copy (the handles go through uintptr_t; fsr_bridge.cpp
# refuses handles that do not fit in 32 bits, which the 32-bit drivers' pointer-sized handles always do).
file(READ ${FFXSDK}/src/backends/vk/ffx_vk.cpp FFX_VK_SRC)
foreach(pair
    "allocation.resource.resource = uniformBuffer|allocation.resource.resource = reinterpret_cast<void*>(static_cast<uintptr_t>(uniformBuffer))"
    "vkCreateComputePipelines(backendContext->device, nullptr, 1,|vkCreateComputePipelines(backendContext->device, VK_NULL_HANDLE, 1,"
    "static_cast<VkBuffer>(allocation.resource.resource)|static_cast<VkBuffer>(reinterpret_cast<uintptr_t>(allocation.resource.resource))")
  string(REPLACE "|" ";" p "${pair}")
  list(GET p 0 from)
  list(GET p 1 to)
  string(FIND "${FFX_VK_SRC}" "${from}" at)
  if(at EQUAL -1)
    message(FATAL_ERROR "ffx_vk.cpp 32-bit patch: '${from}' not found")
  endif()
  string(REPLACE "${from}" "${to}" FFX_VK_SRC "${FFX_VK_SRC}")
endforeach()
# Pointer -> 64-bit handle casts sign-extend on x86 (a handle at 0x8xxxxxxx became 0xffffffff8xxxxxxx and the driver
# crashed in vkCmdFillBuffer): every such cast goes through uintptr_t (zero extension).
foreach(type VkBuffer VkImage VkPipeline VkSwapchainKHR)
  string(REPLACE "reinterpret_cast<${type}>(" "(${type})(uintptr_t)(" FFX_VK_SRC "${FFX_VK_SRC}")
endforeach()
foreach(type VkBuffer VkImage)
  string(REPLACE "(${type})inFfxResource->resource" "(${type})(uintptr_t)inFfxResource->resource" FFX_VK_SRC
                 "${FFX_VK_SRC}")
  string(REPLACE "(${type})blockData->" "(${type})(uintptr_t)blockData->" FFX_VK_SRC "${FFX_VK_SRC}")
  string(REPLACE "(${type})gpuBuffer" "(${type})(uintptr_t)gpuBuffer" FFX_VK_SRC "${FFX_VK_SRC}")
endforeach()
if(AND_FSR_DEBUG)  # diagnosis: FSR backend logs (and-fsr-*.log)
  string(REPLACE
    "backendContext->vkFunctionTable.vkCmdFillBuffer(vkCommandBuffer, vkResource, 0, VK_WHOLE_SIZE,"
    "{ static FILE* dbg = fopen(\"and-fsr-debug.log\", \"w\"); fprintf(dbg, \"fill cb %p buffer %llx size %llu usage %x\\n\", (void*)vkCommandBuffer, (unsigned long long)vkResource, (unsigned long long)ffxResource.resourceDescription.width, (unsigned)ffxResource.resourceDescription.usage); fflush(dbg); }\n        backendContext->vkFunctionTable.vkCmdFillBuffer(vkCommandBuffer, vkResource, 0, VK_WHOLE_SIZE,"
    FFX_VK_SRC "${FFX_VK_SRC}")
  string(REPLACE "outResource->internalIndex = effectContext.nextStaticResource++;" "outResource->internalIndex = effectContext.nextStaticResource++; { static FILE* dbg2 = fopen(\"and-fsr-res.log\", \"w\"); fprintf(dbg2, \"create %u ctx %u %ls w %u h %u type %u\\n\", outResource->internalIndex, effectContextId, createResourceDescription->name, createResourceDescription->resourceDescription.width, createResourceDescription->resourceDescription.height, (unsigned)createResourceDescription->resourceDescription.type); fflush(dbg2); }" FFX_VK_SRC "${FFX_VK_SRC}")
  string(REPLACE "deviceCapabilities->fp16Supported = (bool)shaderFloat18Int8Features.shaderFloat16;" "deviceCapabilities->fp16Supported = false;" FFX_VK_SRC "${FFX_VK_SRC}")
  string(REPLACE "uint32_t idx = job->clearJobDescriptor.target.internalIndex;" "uint32_t idx = job->clearJobDescriptor.target.internalIndex; { static FILE* dbg3 = fopen(\"and-fsr-clear.log\", \"w\"); fprintf(dbg3, \"clear idx %u\\n\", idx); fflush(dbg3); }" FFX_VK_SRC "${FFX_VK_SRC}")
  string(REPLACE "FfxTextureSRV& textureSRV = job->computeJobDescriptor.srvTextures[currentPipelineSrvIndex];" "FfxTextureSRV& textureSRV = job->computeJobDescriptor.srvTextures[currentPipelineSrvIndex]; { static FILE* dbg4 = fopen(\"and-fsr-srv.log\", \"w\"); static int n4; if (n4++ < 200) { fprintf(dbg4, \"%ls srv %u slot %u idx %u\\n\", job->jobLabel, currentPipelineSrvIndex, job->computeJobDescriptor.pipeline.srvTextureBindings[currentPipelineSrvIndex].slotIndex, textureSRV.resource.internalIndex); fflush(dbg4); } }" FFX_VK_SRC "${FFX_VK_SRC}")
endif()
set(FFX_VK_PATCHED ${CMAKE_BINARY_DIR}/ffx_vk_x86.cpp)
file(WRITE ${FFX_VK_PATCHED}.tmp "${FFX_VK_SRC}")
configure_file(${FFX_VK_PATCHED}.tmp ${FFX_VK_PATCHED} COPYONLY)
file(GLOB FFX_SHARED_CPP ${FFXSDK}/src/shared/*.cpp)
add_library(and_fsr STATIC ${FFX_SHARED_CPP}
  ${FFXSDK}/src/components/fsr3upscaler/ffx_fsr3upscaler.cpp
  ${FFX_VK_PATCHED}
  ${FFXSDK}/src/backends/shared/ffx_shader_blobs.cpp
  ${FFXSDK}/src/backends/shared/blob_accessors/ffx_fsr3upscaler_shaderblobs.cpp
  fsr/fsr_bridge.cpp
  ${FFX_SHADER_HEADERS})
target_include_directories(and_fsr PUBLIC ${FFXSDK}/include ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${FFXSDK}/src/components ${FFXSDK}/src/shared ${FFXSDK}/src/backends/shared ${FFXSDK}/src/backends/vk
          ${FFX_SHADER_OUT} ${FFXSDK}/libs
          ${volk_SOURCE_DIR})
target_compile_definitions(and_fsr PRIVATE FFX_FSR3UPSCALER VK_NO_PROTOTYPES VK_USE_PLATFORM_WIN32_KHR _UNICODE UNICODE
  _DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_compile_options(and_fsr PRIVATE /arch:SSE2 /utf-8 /W0 /FIvolk.h)
target_link_libraries(and_fsr PRIVATE Vulkan::Headers)
set_property(TARGET and_fsr PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Headless test of the bridge, no game: pwsh tools/build.ps1 -Target jolt_bridge_test, then run it.
add_executable(jolt_bridge_test EXCLUDE_FROM_ALL physics/jolt_bridge_test.cpp)
target_link_libraries(jolt_bridge_test PRIVATE and_physics)
set_property(TARGET jolt_bridge_test PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Shader sources compiled at run time, embedded as byte arrays (the game folder has no source files):
# XeGTAO (MIT, unmodified) and its entry points (src/rhi/vulkan/shaders/gtao.hlsl). Header: xegtao_embed.h.
set(EMBED_DIR ${CMAKE_BINARY_DIR}/generated)
set(EMBED_HEADER ${EMBED_DIR}/xegtao_embed.h)
set(EMBED_FILES
  kXeGtaoH ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/XeGTAO/XeGTAO.h
  kXeGtaoHlsli ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/XeGTAO/XeGTAO.hlsli
  kGtaoEntries ${CMAKE_CURRENT_SOURCE_DIR}/../src/rhi/vulkan/shaders/gtao.hlsl)
file(MAKE_DIRECTORY ${EMBED_DIR})
file(WRITE ${EMBED_HEADER} "#pragma once\n// Generated by hook/CMakeLists.txt from the files named in EMBED_FILES.\n")
list(LENGTH EMBED_FILES embed_count)
math(EXPR embed_last "${embed_count} - 1")
foreach(i RANGE 0 ${embed_last} 2)
  math(EXPR j "${i} + 1")
  list(GET EMBED_FILES ${i} embed_name)
  list(GET EMBED_FILES ${j} embed_file)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${embed_file})
  file(READ ${embed_file} embed_hex HEX)
  string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," embed_bytes "${embed_hex}")
  file(APPEND ${EMBED_HEADER} "static const char ${embed_name}[] = {${embed_bytes}0};\n")
endforeach()

# Recovered game code: every .cpp under src/ is linked into the hook DLL and registers its own HOOK() entries.
file(GLOB_RECURSE RECOVERED_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../src/*.cpp)
list(FILTER RECOVERED_SOURCES EXCLUDE REGEX "/tests/")  # standalone test programs, built below
# Sources still being written (parallel recovery agents): tools/build.ps1 -Exclude passes regexes here.
foreach(pattern IN LISTS AND_EXCLUDE)
  list(FILTER RECOVERED_SOURCES EXCLUDE REGEX "${pattern}")
endforeach()

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
  enhance/physics_jolt.cpp
  enhance/remaster_menu.cpp
  dinput8.def)

target_include_directories(and_hook PRIVATE src enhance ../src ${CMAKE_CURRENT_SOURCE_DIR} ${minhook_SOURCE_DIR}/include ${EMBED_DIR})
target_link_libraries(and_hook PRIVATE minhook and_physics and_fsr d3d9 dbghelp winmm dxguid ole32 windowscodecs dsound
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
foreach(f vk_shader_translate.cpp vk_fixed_function.cpp vk_pipeline_input.cpp vk_tessellation.cpp vk_fog.cpp
          vk_gtao.cpp vk_rt.cpp)
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../src/rhi/vulkan/${f})
    list(APPEND VK_TEST_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../src/rhi/vulkan/${f})
  endif()
endforeach()
add_executable(vk_shader_test EXCLUDE_FROM_ALL ${VK_TEST_SOURCES})  # build with -Target vk_shader_test
target_include_directories(vk_shader_test PRIVATE ../src ${volk_SOURCE_DIR} ${EMBED_DIR})
target_compile_definitions(vk_shader_test PRIVATE RHI_SHADER_TEST VK_NO_PROTOTYPES WIN32_LEAN_AND_MEAN NOMINMAX
  _CRT_SECURE_NO_WARNINGS)
target_link_libraries(vk_shader_test PRIVATE Vulkan::Headers glslang::glslang glslang::SPIRV
  glslang::glslang-default-resource-limits)

# Headless end-to-end test of ray-traced shadows through the 64-bit helper (src/rhi/vulkan/tests/rt_helper_test.cpp):
# rt_helper_test.exe with rt_helper.exe next to it; no game, no display.
add_executable(rt_helper_test EXCLUDE_FROM_ALL
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/rhi/vulkan/tests/rt_helper_test.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/rhi/vulkan/vk_context.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/rhi/vulkan/vk_rt_client.cpp)
target_include_directories(rt_helper_test PRIVATE ../src src ${volk_SOURCE_DIR})
target_compile_definitions(rt_helper_test PRIVATE VK_NO_PROTOTYPES VK_USE_PLATFORM_WIN32_KHR WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_link_libraries(rt_helper_test PRIVATE Vulkan::Headers)
