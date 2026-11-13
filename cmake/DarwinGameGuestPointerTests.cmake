# SPDX-License-Identifier: GPL-2.0-or-later
# These fixtures exercise the canonical provider without starting a guest executable.
if(TARGET guest_pointer_owned)
    # The existing plugin unit exercises initialization without calling a replacement. Make its
    # canonical provider discoverable too; no fake offset provider is needed in that test.
    target_sources(test_plugin PRIVATE runtime/process.c)
    target_link_libraries(test_plugin guest_pointer_owned)
    target_link_options(test_plugin PRIVATE "LINKER:-u,_rt_guest_pointer_offset")
    set_target_properties(test_plugin PROPERTIES ENABLE_EXPORTS ON)

    foreach(unit IN ITEMS correct wrong)
        # Exercise the real initializer/registry and its owned peripheral call-order seams.
        add_library(test_game_guest_pointer_${unit} MODULE test/game_guest_pointer_module.c
            runtime/plugin.c runtime/hooks.c)
        target_include_directories(test_game_guest_pointer_${unit} PRIVATE runtime/include)
        target_link_libraries(test_game_guest_pointer_${unit} PRIVATE ${CMAKE_DL_LIBS} pthread)
        if(unit STREQUAL "wrong")
            target_compile_definitions(test_game_guest_pointer_${unit} PRIVATE PB_WRONG_SLOT_CONTROL=1)
        endif()
    endforeach()
    add_executable(test_game_guest_pointer test/test_game_guest_pointer.c runtime/process.c)
    target_link_libraries(test_game_guest_pointer PRIVATE guest_pointer_owned ${CMAKE_DL_LIBS} pthread)
    set_target_properties(test_game_guest_pointer PROPERTIES ENABLE_EXPORTS ON)
    add_executable(test_game_guest_pointer_absent test/test_game_guest_pointer.c)
    target_include_directories(test_game_guest_pointer_absent PRIVATE runtime/include)
    target_compile_definitions(test_game_guest_pointer_absent PRIVATE PB_NO_CANONICAL_PROVIDER=1)
    target_link_libraries(test_game_guest_pointer_absent PRIVATE ${CMAKE_DL_LIBS} pthread)
    add_test(NAME darwin_game_guest_pointer
             COMMAND test_game_guest_pointer $<TARGET_FILE:test_game_guest_pointer_correct> correct)
    add_test(NAME darwin_game_guest_pointer_absent
             COMMAND test_game_guest_pointer_absent $<TARGET_FILE:test_game_guest_pointer_correct> absent)
    add_test(NAME darwin_game_guest_pointer_wrong_slot
             COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/test/test_game_guest_pointer_control.py
                     $<TARGET_FILE:test_game_guest_pointer>
                     $<TARGET_FILE:test_game_guest_pointer_correct>
                     $<TARGET_FILE:test_game_guest_pointer_wrong>)
    set_tests_properties(darwin_game_guest_pointer darwin_game_guest_pointer_absent PROPERTIES TIMEOUT 15)
    set_tests_properties(darwin_game_guest_pointer_wrong_slot PROPERTIES TIMEOUT 50)

    foreach(unit IN ITEMS correct wrong)
        add_executable(test_temporary_operations_guest_tls_${unit}
            game/engine/tests/temporary_operations_guest_tls.cpp runtime/process.c)
        target_link_libraries(test_temporary_operations_guest_tls_${unit} PRIVATE guest_pointer_owned ${CMAKE_DL_LIBS} pthread)
        target_compile_options(test_temporary_operations_guest_tls_${unit} PRIVATE -ffp-contract=off -fno-fast-math)
        set_target_properties(test_temporary_operations_guest_tls_${unit} PROPERTIES ENABLE_EXPORTS ON)
        if(unit STREQUAL "wrong")
            target_compile_definitions(test_temporary_operations_guest_tls_${unit} PRIVATE PB_TEST_WRONG_TEMPORARY_TCB=1)
        endif()
    endforeach()
    add_test(NAME darwin_temporary_operations_guest_tls COMMAND test_temporary_operations_guest_tls_correct)
    add_test(NAME darwin_temporary_operations_guest_tls_wrong_getter
        COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/game/engine/tests/temporary_operations_guest_tls_control.py
        $<TARGET_FILE:test_temporary_operations_guest_tls_correct> $<TARGET_FILE:test_temporary_operations_guest_tls_wrong>)
    set_tests_properties(darwin_temporary_operations_guest_tls PROPERTIES TIMEOUT 15)
    set_tests_properties(darwin_temporary_operations_guest_tls_wrong_getter PROPERTIES TIMEOUT 450)
endif()
