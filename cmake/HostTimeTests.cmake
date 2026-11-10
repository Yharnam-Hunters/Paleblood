# SPDX-License-Identifier: GPL-2.0-or-later
include("${CMAKE_CURRENT_LIST_DIR}/HostTime.cmake")
find_package(Threads REQUIRED)
add_executable(test_host_time "${PB_HOST_TIME_ROOT}/test/test_host_time.c")
target_link_libraries(test_host_time PRIVATE pbhost_time)
add_test(NAME host_time COMMAND test_host_time)
set_tests_properties(host_time PROPERTIES TIMEOUT 10)
add_executable(test_host_time_cpp "${PB_HOST_TIME_ROOT}/test/test_host_time_cpp.cpp")
target_link_libraries(test_host_time_cpp PRIVATE pbhost_time)
add_test(NAME host_time_cpp COMMAND test_host_time_cpp)
set_tests_properties(host_time_cpp PROPERTIES TIMEOUT 10)
# Compile production consumers with deterministic clock aliases in this target only.
add_executable(test_host_time_pacing "${PB_HOST_TIME_ROOT}/test/test_host_time_pacing.c"
    "${PB_HOST_TIME_ROOT}/runtime/host_time.c" "${PB_HOST_TIME_ROOT}/runtime/syslib/audio_out.c"
    "${PB_HOST_TIME_ROOT}/runtime/syslib/video_out.c" "${PB_HOST_TIME_ROOT}/runtime/syslib/table.c"
    "${PB_HOST_TIME_ROOT}/runtime/audio/audio_host_none.c")
target_include_directories(test_host_time_pacing PRIVATE "${PB_HOST_TIME_ROOT}/runtime/include")
target_compile_definitions(test_host_time_pacing PRIVATE clock_gettime=rt_test_clock_gettime nanosleep=rt_test_nanosleep)
target_link_libraries(test_host_time_pacing PRIVATE Threads::Threads)
add_test(NAME host_time_pacing COMMAND test_host_time_pacing)
set_tests_properties(host_time_pacing PROPERTIES TIMEOUT 10)
