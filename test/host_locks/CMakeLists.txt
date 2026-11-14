# SPDX-License-Identifier: GPL-2.0-or-later
cmake_minimum_required(VERSION 3.20)
project(paleblood_host_locks C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
add_compile_options(-Wall -Wextra -Werror)
enable_testing()
include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/HostLockTests.cmake")
