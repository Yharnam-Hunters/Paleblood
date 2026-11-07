# SPDX-License-Identifier: GPL-2.0-or-later
# Shared by the root x86-64 macOS build and the isolated qualification project.
include_guard(GLOBAL)
get_filename_component(PB_GUEST_TLS_SOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
find_package(Threads REQUIRED)

add_library(guest_tls_owned STATIC "${PB_GUEST_TLS_SOURCE_ROOT}/runtime/guest_tls_darwin.c")
target_include_directories(guest_tls_owned PUBLIC "${PB_GUEST_TLS_SOURCE_ROOT}/runtime/include")
target_link_libraries(guest_tls_owned PUBLIC Threads::Threads)

add_executable(test_guest_tls_darwin "${PB_GUEST_TLS_SOURCE_ROOT}/test/test_guest_tls_darwin.c")
target_link_libraries(test_guest_tls_darwin PRIVATE guest_tls_owned)
# The failure test supplies fake public APIs around the implementation it includes.
add_executable(test_guest_tls_failures "${PB_GUEST_TLS_SOURCE_ROOT}/test/test_guest_tls_failures.c")
target_include_directories(test_guest_tls_failures PRIVATE "${PB_GUEST_TLS_SOURCE_ROOT}/runtime/include")
target_link_libraries(test_guest_tls_failures PRIVATE Threads::Threads)
set_target_properties(guest_tls_owned test_guest_tls_darwin test_guest_tls_failures PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
add_test(NAME darwin_guest_tls_owned COMMAND test_guest_tls_darwin)
add_test(NAME darwin_guest_tls_failures COMMAND test_guest_tls_failures)
set_tests_properties(darwin_guest_tls_owned darwin_guest_tls_failures PROPERTIES TIMEOUT 10)
