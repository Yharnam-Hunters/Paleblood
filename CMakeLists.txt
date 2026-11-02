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

option(PB_METAL_ONLY "Build the standalone macOS Metal platform unit" ${APPLE})
option(PB_ENABLE_METAL_SMOKE_TEST "Build/register the real-device offscreen Metal test" OFF)
if(PB_METAL_ONLY)
    if(NOT APPLE OR NOT CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        message(FATAL_ERROR "PB_METAL_ONLY requires macOS and an Apple SDK")
    endif()
    enable_language(OBJCXX)
    include(cmake/Metal.cmake)
    return()
elseif(APPLE)
    message(FATAL_ERROR "The full guest runtime currently requires Linux x86-64. Use PB_METAL_ONLY=ON for the native macOS Metal unit.")
elseif(PB_ENABLE_METAL_SMOKE_TEST)
    message(FATAL_ERROR "PB_ENABLE_METAL_SMOKE_TEST requires PB_METAL_ONLY=ON on macOS")
endif()

enable_testing()
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
add_executable(test_syslib test/test_syslib.c runtime/process.c ${PB_TEST_SYSLIB})
target_include_directories(test_syslib PRIVATE runtime/include)
add_test(NAME syslib COMMAND test_syslib)

add_test(NAME tools COMMAND ${Python3_EXECUTABLE} -m unittest discover -s ${PROJECT_SOURCE_DIR}/test -p "test_*.py" -v)
set_tests_properties(tools PROPERTIES ENVIRONMENT "BB_LOADER=$<TARGET_FILE:pbloader>;BB_PBBOOT=$<TARGET_FILE:pbboot>")
add_test(NAME verify_self_test COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/verify.py --self-test)
add_test(NAME validate_functions COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/validate_functions.py)
add_test(NAME check_agnostic COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/check_agnostic.py)
add_test(NAME float_flags COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/check_float_flags.py ${CMAKE_BINARY_DIR})
add_test(NAME progress_readme COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/progress.py --check)
