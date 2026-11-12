# SPDX-License-Identifier: GPL-2.0-or-later
# Compile the real CLI implementation with test-only call/signal observations. Use the same
# providers and link dependencies as pbboot, so a new provider cannot silently escape the test.
if(APPLE AND TARGET guest_pointer_owned)
    get_target_property(PB_BOOT_DIRECTORY pbboot SOURCE_DIR)
    get_target_property(PB_BOOT_SOURCES pbboot SOURCES)
    get_target_property(PB_BOOT_LIBRARIES pbboot LINK_LIBRARIES)
    get_target_property(PB_BOOT_INCLUDES pbboot INCLUDE_DIRECTORIES)
    set(PB_SCOPE_SOURCES)
    foreach(source IN LISTS PB_BOOT_SOURCES)
        get_filename_component(name "${source}" NAME)
        if(name STREQUAL "boot.c")
            continue()
        endif()
        if(NOT IS_ABSOLUTE "${source}")
            set(source "${PB_BOOT_DIRECTORY}/${source}")
        endif()
        list(APPEND PB_SCOPE_SOURCES "${source}")
    endforeach()
    add_executable(test_boot_scope_darwin test/test_boot_scope_darwin.c ${PB_SCOPE_SOURCES})
    target_include_directories(test_boot_scope_darwin PRIVATE ${PB_BOOT_INCLUDES})
    target_link_libraries(test_boot_scope_darwin PRIVATE ${PB_BOOT_LIBRARIES})
    add_test(NAME darwin_boot_scope COMMAND test_boot_scope_darwin)
    set_tests_properties(darwin_boot_scope PROPERTIES TIMEOUT 15)
endif()

add_test(NAME boot_owned_startup COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/test/test_boot_startup.py" -v)
set_tests_properties(boot_owned_startup PROPERTIES TIMEOUT 45 ENVIRONMENT "BB_PBBOOT=$<TARGET_FILE:pbboot>")

# Exercise hook installation order through a host-compiled fixture on both operating systems.
add_test(NAME boot_game_library COMMAND "${Python3_EXECUTABLE}" -m unittest discover -s "${PROJECT_SOURCE_DIR}/test" -p test_boot_game_library.py -v)
set_tests_properties(boot_game_library PROPERTIES TIMEOUT 90 ENVIRONMENT "BB_PBBOOT=$<TARGET_FILE:pbboot>")
