# SPDX-License-Identifier: GPL-2.0-or-later
get_filename_component(PB_HOST_FILE_STAT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
find_package(Threads REQUIRED)
# The fixture includes files.c to exercise its exact private wire conversion.
add_executable(test_host_file_stat "${PB_HOST_FILE_STAT_ROOT}/test/test_host_file_stat.c"
    "${PB_HOST_FILE_STAT_ROOT}/runtime/process.c" "${PB_HOST_FILE_STAT_ROOT}/runtime/syslib/table.c")
target_include_directories(test_host_file_stat PRIVATE "${PB_HOST_FILE_STAT_ROOT}/runtime/include")
target_link_libraries(test_host_file_stat PRIVATE Threads::Threads)
add_test(NAME host_file_stat COMMAND test_host_file_stat)
set_tests_properties(host_file_stat PROPERTIES TIMEOUT 10)
