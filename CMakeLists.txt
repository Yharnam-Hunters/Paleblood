# SPDX-License-Identifier: GPL-2.0-or-later
cmake_minimum_required(VERSION 3.20)
project(paleblood C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)   # editors: ln -s build/compile_commands.json .
# Verification proves the code as it is built, so every build is an optimized one unless asked
# otherwise (CI builds Release too).
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
add_compile_options(-Wall -Wextra -Werror)

option(PB_METAL_ONLY "Build only the standalone macOS Metal platform unit" OFF)
option(PB_ENABLE_METAL_SMOKE_TEST "Build/register the real-device offscreen Metal test" OFF)
if(APPLE AND CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    enable_language(OBJCXX)
    include(cmake/Metal.cmake)
    if(PB_METAL_ONLY)
        return()
    endif()
elseif(PB_METAL_ONLY OR PB_ENABLE_METAL_SMOKE_TEST)
    message(FATAL_ERROR "Metal options require macOS and an Apple SDK")
endif()

enable_testing()
if(APPLE AND CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    if(CMAKE_OSX_ARCHITECTURES)
        set(PB_GUEST_TLS_ARCH "${CMAKE_OSX_ARCHITECTURES}")
    else()
        set(PB_GUEST_TLS_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
    endif()
    if(PB_GUEST_TLS_ARCH STREQUAL "x86_64")
        include(cmake/DarwinGuestTLS.cmake)
    else()
        message(STATUS "owned Darwin guest TLS: configure x86_64 to build its GS-slot qualification tests")
    endif()
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_subdirectory(runtime)
add_subdirectory(game)

add_executable(test_hooks test/test_hooks.c)
target_link_libraries(test_hooks bbrt)
add_test(NAME hooks COMMAND test_hooks)
add_executable(test_plugin test/test_plugin.c runtime/plugin.c runtime/hooks.c)
target_include_directories(test_plugin PRIVATE runtime/include)
target_link_libraries(test_plugin bbrt)
add_test(NAME plugin COMMAND test_plugin)

add_executable(test_host_hooks test/test_host_hooks.c runtime/host_hooks.c)
target_include_directories(test_host_hooks PRIVATE runtime/include)
add_test(NAME host_hooks COMMAND test_host_hooks)
add_executable(test_capture test/test_capture.cpp)
target_link_libraries(test_capture bbrt)
add_test(NAME capture COMMAND test_capture)
add_executable(test_capture_trace test/test_capture_trace.cpp test/capture_trace_guest.S)
target_link_libraries(test_capture_trace bbrt)
add_test(NAME capture_trace COMMAND test_capture_trace --self-test)
add_test(NAME capture_trace_replay
         COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/test/capture_trace_integration.py $<TARGET_FILE:test_capture_trace>)
set_tests_properties(capture_trace_replay PROPERTIES
    ENVIRONMENT "CC=${CMAKE_C_COMPILER};BB_LOADER=$<TARGET_FILE:pbloader>")
add_executable(test_capture_hooks test/test_capture_hooks.cpp test/capture_trace_guest.S)
target_link_libraries(test_capture_hooks bbrt)
add_test(NAME capture_hooks COMMAND test_capture_hooks --self-test)
add_test(NAME capture_hooks_replay
         COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/test/capture_trace_integration.py $<TARGET_FILE:test_capture_hooks>
                 capture_trace_scale capture_trace_twice capture_trace_copy capture_trace_use_import)
set_tests_properties(capture_hooks_replay PROPERTIES
    ENVIRONMENT "CC=${CMAKE_C_COMPILER};BB_LOADER=$<TARGET_FILE:pbloader>")
add_executable(test_capture_poc test/test_capture_poc.cpp)
add_test(NAME capture_poc_fail_closed COMMAND test_capture_poc --self-test)
add_test(NAME capture_poc_replay
         COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/test/capture_poc_integration.py $<TARGET_FILE:test_capture_poc>)
set_tests_properties(capture_poc_replay PROPERTIES
    ENVIRONMENT "CC=${CMAKE_C_COMPILER};BB_LOADER=$<TARGET_FILE:pbloader>")
# System library implementations, compiled in directly so the linker keeps every RT_SYSLIB entry
file(GLOB PB_TEST_SYSLIB CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/runtime/syslib/*.c)
# The syslib tests use the display's headless mode only, so they build without its window.
add_executable(test_syslib test/test_syslib.c runtime/process.c ${PB_TEST_SYSLIB}
               runtime/display/display.c runtime/display/display_window_none.c runtime/renderer/renderer_none.c)
target_include_directories(test_syslib PRIVATE runtime/include)
add_test(NAME syslib COMMAND test_syslib)
# The renderer's GCN shader decoder (docs/RENDERER.md), its test, and pbgcn for the shader census.
add_library(pbgcn_decode STATIC runtime/renderer/gcn_decode.c)
target_include_directories(pbgcn_decode PUBLIC runtime/include)
add_executable(test_gcn test/test_gcn.c)
target_link_libraries(test_gcn PRIVATE pbgcn_decode)
add_test(NAME gcn COMMAND test_gcn)
add_executable(pbgcn runtime/renderer/pbgcn.c)
target_link_libraries(pbgcn PRIVATE pbgcn_decode)
# The shader translator (GCN to SPIR-V) and its tests: test_gcn_spirv checks modules with spirv-val
# when it is installed; test_gcn_compute and test_gcn_draw run them on a Vulkan device when there is one.
# The renderer's record of the GPU library's calls (docs/RENDERER.md, phase 3).
add_library(pbrender_calls STATIC runtime/renderer/render_calls.c)
target_include_directories(pbrender_calls PUBLIC runtime/include)
target_link_libraries(pbrender_calls PUBLIC pthread)
add_executable(test_render_calls test/test_render_calls.c)
target_link_libraries(test_render_calls PRIVATE pbrender_calls)
add_test(NAME render_calls COMMAND test_render_calls)
add_library(pbgcn_spirv STATIC runtime/renderer/spirv.c runtime/renderer/gcn_spirv.c)
target_include_directories(pbgcn_spirv PUBLIC runtime/include)
target_link_libraries(pbgcn_spirv PUBLIC pbgcn_decode)
add_executable(test_gcn_spirv test/test_gcn_spirv.c)
target_link_libraries(test_gcn_spirv PRIVATE pbgcn_spirv)
add_test(NAME gcn_spirv COMMAND test_gcn_spirv)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(PB_TEST_VULKAN QUIET IMPORTED_TARGET vulkan)
endif()
if(PB_TEST_VULKAN_FOUND)
    add_executable(test_gcn_compute test/test_gcn_compute.c)
    target_link_libraries(test_gcn_compute PRIVATE pbgcn_spirv PkgConfig::PB_TEST_VULKAN m)
    add_test(NAME gcn_compute COMMAND test_gcn_compute)
    set_tests_properties(gcn_compute PROPERTIES SKIP_RETURN_CODE 77)
    # The draw executor (docs/RENDERER.md, phase 3) runs recorded calls on a Vulkan GPU.
    add_library(pbrender_executor STATIC runtime/renderer/render_executor.c)
    target_include_directories(pbrender_executor PUBLIC runtime/include)
    target_link_libraries(pbrender_executor PUBLIC pbrender_calls pbgcn_spirv PkgConfig::PB_TEST_VULKAN)
    add_executable(test_render_executor test/test_render_executor.c)
    target_link_libraries(test_render_executor PRIVATE pbrender_executor m)
    add_test(NAME render_executor COMMAND test_render_executor)
    set_tests_properties(render_executor PROPERTIES SKIP_RETURN_CODE 77)
    add_executable(test_renderer test/test_renderer.c runtime/renderer/renderer.c)
    target_include_directories(test_renderer PRIVATE runtime/include)
    target_link_libraries(test_renderer PRIVATE pbrender_executor m)
    add_test(NAME renderer COMMAND test_renderer)
    set_tests_properties(renderer PROPERTIES SKIP_RETURN_CODE 77)
    add_executable(test_gcn_draw test/test_gcn_draw.c)
    target_link_libraries(test_gcn_draw PRIVATE pbgcn_spirv PkgConfig::PB_TEST_VULKAN m)
    add_test(NAME gcn_draw COMMAND test_gcn_draw)
    set_tests_properties(gcn_draw PROPERTIES SKIP_RETURN_CODE 77)
endif()

add_test(NAME tools COMMAND ${Python3_EXECUTABLE} -m unittest discover -s ${PROJECT_SOURCE_DIR}/test -p "test_*.py" -v)
set_tests_properties(tools PROPERTIES ENVIRONMENT "BB_LOADER=$<TARGET_FILE:pbloader>;BB_PBBOOT=$<TARGET_FILE:pbboot>")
add_test(NAME verify_self_test COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/verify.py --self-test)
add_test(NAME validate_functions COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/validate_functions.py)
add_test(NAME check_agnostic COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/check_agnostic.py)
add_test(NAME float_flags COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/check_float_flags.py ${CMAKE_BINARY_DIR})
add_test(NAME progress_readme COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/progress.py --check)

# Compiled-SDK wire validation over the loader-owned process parameters.
add_executable(test_compiled_sdk test/test_compiled_sdk.c
               runtime/syslib/table.c runtime/syslib/kernel_sdk.c)
target_include_directories(test_compiled_sdk PRIVATE runtime/include)
add_test(NAME compiled_sdk COMMAND test_compiled_sdk)
set_tests_properties(compiled_sdk PROPERTIES TIMEOUT 10)

# Exercise the registered direct-memory calls, including rejected ranges before host mapping.
add_executable(test_direct_memory_bounds test/test_direct_memory_bounds.c
               runtime/syslib/kernel_memory.c runtime/syslib/table.c runtime/process.c)
target_include_directories(test_direct_memory_bounds PRIVATE runtime/include)
add_test(NAME direct_memory_bounds COMMAND test_direct_memory_bounds)
set_tests_properties(direct_memory_bounds PROPERTIES TIMEOUT 20)

# The table is immutable in the original image. This unit uses owned values so
# the last valid entry and a one-past access are independently observable.
add_executable(test_present_encoding_units game/render/tests/test_present_encoding_units.cpp
               game/render/present_encoding_units.cpp)
target_include_directories(test_present_encoding_units PRIVATE runtime/include)
target_compile_options(test_present_encoding_units PRIVATE
                       -ffp-contract=off -fno-fast-math)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(test_present_encoding_units PRIVATE
                           -fsanitize=bounds -fsanitize-undefined-trap-on-error)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(test_present_encoding_units PRIVATE
                           -fsanitize=bounds -fsanitize-trap=bounds)
endif()
add_test(NAME present_encoding_units COMMAND test_present_encoding_units)
# Owned per-thread module blocks and the actual TLS registry entry.
find_package(Threads REQUIRED)
add_executable(test_module_tls test/test_module_tls.c runtime/syslib/kernel_tls.c
               runtime/syslib/table.c)
target_include_directories(test_module_tls PRIVATE runtime/include)
target_link_libraries(test_module_tls PRIVATE Threads::Threads)
add_test(NAME module_tls COMMAND test_module_tls)
set_tests_properties(module_tls PROPERTIES TIMEOUT 10)

add_test(NAME module_tls_boot
         COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/test/test_module_tls_boot.py -v)
set_tests_properties(module_tls_boot PROPERTIES TIMEOUT 30
    ENVIRONMENT "BB_PBBOOT=$<TARGET_FILE:pbboot>")
