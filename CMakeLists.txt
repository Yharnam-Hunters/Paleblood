# SPDX-License-Identifier: GPL-2.0-or-later
cmake_minimum_required(VERSION 3.20)
project(paleblood C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)   # editors: ln -s build/compile_commands.json .
add_compile_options(-Wall -Wextra -Werror)

enable_testing()
add_subdirectory(runtime)
add_subdirectory(game)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_executable(test_hooks test/test_hooks.c)
target_link_libraries(test_hooks bbrt)
add_test(NAME hooks COMMAND test_hooks)
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
add_test(NAME progress_readme COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/progress.py --check)
