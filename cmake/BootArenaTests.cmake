# SPDX-License-Identifier: GPL-2.0-or-later
get_filename_component(PB_ARENA_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
add_executable(test_boot_arena "${PB_ARENA_ROOT}/test/test_boot_arena.c"
    "${PB_ARENA_ROOT}/runtime/boot_arena.c" "${PB_ARENA_ROOT}/runtime/memory.c")
target_include_directories(test_boot_arena PRIVATE "${PB_ARENA_ROOT}/runtime/include")
add_test(NAME boot_arena COMMAND test_boot_arena)
add_executable(test_boot_arena_failures "${PB_ARENA_ROOT}/test/test_boot_arena_failures.c")
target_include_directories(test_boot_arena_failures PRIVATE "${PB_ARENA_ROOT}/runtime/include")
add_test(NAME boot_arena_failures COMMAND test_boot_arena_failures)
set_tests_properties(boot_arena boot_arena_failures PROPERTIES TIMEOUT 15)
