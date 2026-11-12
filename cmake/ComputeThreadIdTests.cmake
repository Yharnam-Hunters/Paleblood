# SPDX-License-Identifier: GPL-2.0-or-later
if(NOT TARGET test_compute_thread_ids)
    get_filename_component(PB_THREAD_ID_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
    add_executable(test_compute_thread_ids "${PB_THREAD_ID_ROOT}/test/test_compute_thread_ids.c")
    target_link_libraries(test_compute_thread_ids PRIVATE pbgcn_spirv)
    add_test(NAME compute_thread_ids COMMAND test_compute_thread_ids)
    set_tests_properties(compute_thread_ids PROPERTIES TIMEOUT 15)
endif()
