# SPDX-License-Identifier: GPL-2.0-or-later
# The disk consumer shares the generic canonical-provider fixtures registered separately.
if(TARGET guest_pointer_owned)
    add_executable(test_disk_guest_tls game/loading/tests/disk_guest_tls.cpp runtime/process.c)
    target_link_libraries(test_disk_guest_tls PRIVATE guest_pointer_owned ${CMAKE_DL_LIBS} pthread)
    target_compile_options(test_disk_guest_tls PRIVATE -ffp-contract=off -fno-fast-math)
    set_target_properties(test_disk_guest_tls PROPERTIES ENABLE_EXPORTS ON)
    add_test(NAME darwin_disk_guest_tls COMMAND test_disk_guest_tls)
    set_tests_properties(darwin_disk_guest_tls PROPERTIES TIMEOUT 15)
endif()
