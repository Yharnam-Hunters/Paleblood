# SPDX-License-Identifier: GPL-2.0-or-later
cmake_minimum_required(VERSION 3.20)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(macos_input C)
    enable_testing()
endif()
if(NOT APPLE)
    message(FATAL_ERROR "This fixture exercises the macOS keyboard provider")
endif()
get_filename_component(PB_INPUT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
add_executable(test_macos_input test_macos_input.c "${PB_INPUT_ROOT}/runtime/input/input.c")
target_include_directories(test_macos_input PRIVATE "${PB_INPUT_ROOT}/runtime/include")
set_target_properties(test_macos_input PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_compile_options(test_macos_input PRIVATE -Wall -Wextra -Werror -ffp-contract=off -fno-fast-math)
target_link_libraries(test_macos_input PRIVATE pthread)
add_test(NAME macos_input COMMAND test_macos_input)
set_tests_properties(macos_input PROPERTIES TIMEOUT 15 LABELS "cpu")

# Native event-loop qualification stays opt-in with the existing window tests.
if(PB_ENABLE_METAL_PRESENTATION_TEST AND TARGET pbmetal)
    add_executable(test_macos_input_window "${PB_INPUT_ROOT}/test/test_macos_input_window.mm"
        "${PB_INPUT_ROOT}/runtime/input/input_host_macos.c" "${PB_INPUT_ROOT}/runtime/input/input.c"
        "${PB_INPUT_ROOT}/runtime/display/window_mailbox.c")
    target_include_directories(test_macos_input_window PRIVATE "${PB_INPUT_ROOT}/runtime/include")
    target_compile_options(test_macos_input_window PRIVATE -Wall -Wextra -Werror -ffp-contract=off -fno-fast-math
        "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc;-fobjc-exceptions;-fblocks;-fno-modules>")
    target_link_libraries(test_macos_input_window PRIVATE pbmetal ${PB_APPKIT_FRAMEWORK} pthread)
    add_test(NAME macos_input_window COMMAND test_macos_input_window)
    set_tests_properties(macos_input_window PROPERTIES TIMEOUT 30 LABELS "window;metal")
endif()
