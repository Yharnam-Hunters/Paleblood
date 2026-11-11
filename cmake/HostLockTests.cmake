# SPDX-License-Identifier: GPL-2.0-or-later
get_filename_component(PB_HOST_LOCK_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
include("${CMAKE_CURRENT_LIST_DIR}/HostTime.cmake")
find_package(Threads REQUIRED)
add_executable(test_syslib_locks
    "${PB_HOST_LOCK_ROOT}/test/test_syslib_locks.c"
    "${PB_HOST_LOCK_ROOT}/test/syslib/timed_locks.c"
    "${PB_HOST_LOCK_ROOT}/runtime/syslib/kernel_mutex.c"
    "${PB_HOST_LOCK_ROOT}/runtime/syslib/kernel_rwlock.c"
    "${PB_HOST_LOCK_ROOT}/runtime/syslib/table.c"
    "${PB_HOST_LOCK_ROOT}/runtime/process.c")
target_include_directories(test_syslib_locks PRIVATE "${PB_HOST_LOCK_ROOT}/runtime/include")
target_link_libraries(test_syslib_locks PRIVATE pbhost_time Threads::Threads)
add_test(NAME syslib_locks COMMAND test_syslib_locks)
set_tests_properties(syslib_locks PROPERTIES TIMEOUT 10)
add_executable(test_host_lock_wait "${PB_HOST_LOCK_ROOT}/test/test_host_lock_wait.c")
target_include_directories(test_host_lock_wait PRIVATE "${PB_HOST_LOCK_ROOT}/runtime/include")
add_test(NAME host_lock_wait COMMAND test_host_lock_wait)
set_tests_properties(host_lock_wait PROPERTIES TIMEOUT 10)
if(APPLE)
    add_executable(test_host_cond_wait
        "${PB_HOST_LOCK_ROOT}/test/test_host_cond_wait.c"
        "${PB_HOST_LOCK_ROOT}/runtime/syslib/kernel_mutex.c"
        "${PB_HOST_LOCK_ROOT}/runtime/syslib/table.c"
        "${PB_HOST_LOCK_ROOT}/runtime/process.c")
    target_include_directories(test_host_cond_wait PRIVATE "${PB_HOST_LOCK_ROOT}/runtime/include")
    target_compile_definitions(test_host_cond_wait PRIVATE
        clock_gettime=rt_test_clock_gettime pthread_cond_timedwait_relative_np=rt_test_cond_wait)
    target_link_libraries(test_host_cond_wait PRIVATE pbhost_time Threads::Threads)
    add_test(NAME host_cond_wait COMMAND test_host_cond_wait)
    set_tests_properties(host_cond_wait PROPERTIES TIMEOUT 10)
endif()
