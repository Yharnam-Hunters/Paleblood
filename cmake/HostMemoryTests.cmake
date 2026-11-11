# SPDX-License-Identifier: GPL-2.0-or-later
get_filename_component(PB_HOST_MEMORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
find_package(Threads REQUIRED)
set(PB_HOST_MEMORY_SOURCES
    "${PB_HOST_MEMORY_ROOT}/runtime/memory.c"
    "${PB_HOST_MEMORY_ROOT}/runtime/syslib/kernel_memory.c"
    "${PB_HOST_MEMORY_ROOT}/runtime/syslib/kernel.c"
    "${PB_HOST_MEMORY_ROOT}/runtime/syslib/table.c"
    "${PB_HOST_MEMORY_ROOT}/runtime/process.c")
add_executable(test_direct_memory_bounds "${PB_HOST_MEMORY_ROOT}/test/test_direct_memory_bounds.c" ${PB_HOST_MEMORY_SOURCES})
target_include_directories(test_direct_memory_bounds PRIVATE "${PB_HOST_MEMORY_ROOT}/runtime/include")
target_link_libraries(test_direct_memory_bounds PRIVATE Threads::Threads)
add_test(NAME direct_memory_bounds COMMAND test_direct_memory_bounds)
set_tests_properties(direct_memory_bounds PROPERTIES TIMEOUT 20)
add_executable(test_syslib_memory_host "${PB_HOST_MEMORY_ROOT}/test/test_syslib_memory_host.c"
    "${PB_HOST_MEMORY_ROOT}/test/syslib/memory_reserve_batch.c"
    "${PB_HOST_MEMORY_ROOT}/test/syslib/memory_unmap.c" ${PB_HOST_MEMORY_SOURCES})
target_include_directories(test_syslib_memory_host PRIVATE "${PB_HOST_MEMORY_ROOT}/runtime/include")
target_link_libraries(test_syslib_memory_host PRIVATE Threads::Threads)
add_test(NAME syslib_memory_host COMMAND test_syslib_memory_host)
set_tests_properties(syslib_memory_host PROPERTIES TIMEOUT 20)
if(APPLE AND CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    add_executable(test_direct_memory_darwin "${PB_HOST_MEMORY_ROOT}/test/test_direct_memory_darwin.c")
    target_include_directories(test_direct_memory_darwin PRIVATE "${PB_HOST_MEMORY_ROOT}/runtime/include")
    target_link_libraries(test_direct_memory_darwin PRIVATE Threads::Threads)
    target_sources(test_direct_memory_darwin PRIVATE "${PB_HOST_MEMORY_ROOT}/runtime/process.c"
        "${PB_HOST_MEMORY_ROOT}/runtime/syslib/kernel.c")
    add_test(NAME direct_memory_darwin COMMAND test_direct_memory_darwin)
    set_tests_properties(direct_memory_darwin PROPERTIES TIMEOUT 20)
endif()
if(TARGET pbmetal_executor AND PB_ENABLE_METAL_SMOKE_TEST)
    add_executable(test_direct_memory_metal "${PB_HOST_MEMORY_ROOT}/test/test_direct_memory_metal.c" ${PB_HOST_MEMORY_SOURCES})
    target_include_directories(test_direct_memory_metal PRIVATE "${PB_HOST_MEMORY_ROOT}/runtime/include")
    target_compile_options(test_direct_memory_metal PRIVATE -ffp-contract=off -fno-fast-math)
    target_link_libraries(test_direct_memory_metal PRIVATE pbmetal_executor Threads::Threads)
    add_test(NAME direct_memory_metal COMMAND test_direct_memory_metal)
    set_tests_properties(direct_memory_metal PROPERTIES TIMEOUT 120 LABELS "gpu;metal")
endif()
