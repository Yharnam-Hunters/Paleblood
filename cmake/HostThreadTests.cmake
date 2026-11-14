# SPDX-License-Identifier: GPL-2.0-or-later
get_filename_component(PB_HOST_THREAD_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
find_package(Threads REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
# Actual x86 guest execution, not an ARM-only compilation of the host APIs.
if(NOT APPLE OR TARGET guest_pointer_owned)
    add_executable(test_syslib_threads_host
        "${PB_HOST_THREAD_ROOT}/test/test_syslib_threads_host.c"
        "${PB_HOST_THREAD_ROOT}/test/syslib/thread_join.c"
        "${PB_HOST_THREAD_ROOT}/runtime/syslib/kernel_thread.c"
        "${PB_HOST_THREAD_ROOT}/runtime/syslib/kernel_key.c"
        "${PB_HOST_THREAD_ROOT}/runtime/syslib/table.c"
        "${PB_HOST_THREAD_ROOT}/runtime/process.c"
        "${PB_HOST_THREAD_ROOT}/runtime/memory.c"
        "${PB_HOST_THREAD_ROOT}/runtime/loader.c"
        "${PB_HOST_THREAD_ROOT}/runtime/nid.c")
    target_include_directories(test_syslib_threads_host PRIVATE "${PB_HOST_THREAD_ROOT}/runtime/include")
    target_link_libraries(test_syslib_threads_host PRIVATE Threads::Threads)
    if(TARGET guest_pointer_owned)
        target_link_libraries(test_syslib_threads_host PRIVATE guest_pointer_owned)
    endif()
    add_test(NAME syslib_threads_host COMMAND test_syslib_threads_host)
    set_tests_properties(syslib_threads_host PROPERTIES TIMEOUT 25)
endif()

if(TARGET test_syslib_threads_host)
    add_test(NAME syslib_threads_elf
        COMMAND "${Python3_EXECUTABLE}" "${PB_HOST_THREAD_ROOT}/test/test_guest_threads.py"
                "$<TARGET_FILE:test_syslib_threads_host>")
    set_tests_properties(syslib_threads_elf PROPERTIES TIMEOUT 30)
endif()

if(TARGET guest_pointer_owned)
    add_executable(test_guest_pointer_failures "${PB_HOST_THREAD_ROOT}/test/test_guest_pointer_failures.c"
        "${PB_HOST_THREAD_ROOT}/runtime/process.c")
    target_include_directories(test_guest_pointer_failures PRIVATE "${PB_HOST_THREAD_ROOT}/runtime/include")
    target_link_libraries(test_guest_pointer_failures PRIVATE guest_tls_owned Threads::Threads)
    add_test(NAME guest_pointer_failures COMMAND test_guest_pointer_failures)
    set_tests_properties(guest_pointer_failures PROPERTIES TIMEOUT 15)
    add_executable(test_guest_thread_cleanup "${PB_HOST_THREAD_ROOT}/test/test_guest_thread_cleanup.c"
        "${PB_HOST_THREAD_ROOT}/runtime/syslib/kernel_key.c"
        "${PB_HOST_THREAD_ROOT}/runtime/syslib/table.c"
        "${PB_HOST_THREAD_ROOT}/runtime/process.c" "${PB_HOST_THREAD_ROOT}/runtime/memory.c")
    target_include_directories(test_guest_thread_cleanup PRIVATE "${PB_HOST_THREAD_ROOT}/runtime/include")
    target_link_libraries(test_guest_thread_cleanup PRIVATE guest_pointer_owned Threads::Threads)
    add_test(NAME guest_thread_cleanup COMMAND test_guest_thread_cleanup)
    set_tests_properties(guest_thread_cleanup PROPERTIES TIMEOUT 15)
endif()
