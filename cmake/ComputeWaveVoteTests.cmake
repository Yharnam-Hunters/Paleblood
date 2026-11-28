# SPDX-License-Identifier: GPL-2.0-or-later
if(NOT TARGET test_compute_wave_votes)
    get_filename_component(PB_WAVE_TEST_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
    add_executable(test_compute_wave_votes "${PB_WAVE_TEST_ROOT}/test/test_compute_wave_votes.c")
    target_link_libraries(test_compute_wave_votes PRIVATE pbgcn_spirv)
    add_test(NAME compute_wave_votes COMMAND test_compute_wave_votes)
    set_tests_properties(compute_wave_votes PROPERTIES TIMEOUT 15)
endif()
