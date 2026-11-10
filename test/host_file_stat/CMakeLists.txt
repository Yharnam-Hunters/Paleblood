# SPDX-License-Identifier: GPL-2.0-or-later
cmake_minimum_required(VERSION 3.20)
project(paleblood_host_file_stat C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra -Werror)
enable_testing()
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/HostFileStatTests.cmake")
