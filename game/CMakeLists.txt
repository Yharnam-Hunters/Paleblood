# SPDX-License-Identifier: GPL-2.0-or-later
# The game library loaded by the scaffold (BB_GAME_LIB): every .c and .cpp under game/<system>/,
# the generated hook table and the runtime. Its bbgame_init (runtime/plugin.c) installs the hooks.
file(GLOB SYSTEM_DIRS LIST_DIRECTORIES true RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/*)
set(GAME_SOURCES)
foreach(dir ${SYSTEM_DIRS})
    if(IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${dir})
        file(GLOB SRCS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${dir}/*.c ${CMAKE_CURRENT_SOURCE_DIR}/${dir}/*.cpp)
        list(APPEND GAME_SOURCES ${SRCS})
    endif()
endforeach()

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(HOOKS_C ${CMAKE_CURRENT_BINARY_DIR}/hooks_table.c)
# Function families (docs/FAMILIES.md): their member tables feed the generated hook table too.
file(GLOB FAMILY_TABLES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/symbols/families/*.csv)
add_custom_command(
    OUTPUT ${HOOKS_C}
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/gen_hooks.py
            ${CMAKE_CURRENT_SOURCE_DIR}/hooks.csv ${PROJECT_SOURCE_DIR}/symbols/functions.csv ${HOOKS_C}
    DEPENDS ${PROJECT_SOURCE_DIR}/tools/gen_hooks.py ${PROJECT_SOURCE_DIR}/tools/families.py
            ${CMAKE_CURRENT_SOURCE_DIR}/hooks.csv ${PROJECT_SOURCE_DIR}/symbols/functions.csv
            ${PROJECT_SOURCE_DIR}/symbols/families.csv ${FAMILY_TABLES})

add_library(bbgame SHARED ${GAME_SOURCES} ${HOOKS_C})
target_link_libraries(bbgame PRIVATE bbrt)
target_link_options(bbgame PRIVATE -Wl,--no-undefined)
# Replacements reproduce the original's floating-point operations one by one: no fused
# multiply-add and none of the fast-math licences (reassociation, reciprocals, assuming no NaN);
# -fno-fast-math also overrides one that comes in through CXXFLAGS. tools/check_float_flags.py
# checks the flags every game source is built with (ctest, so CI).
target_compile_options(bbgame PRIVATE -ffp-contract=off -fno-fast-math)

# Test the canonical runtime bridge against real recorders, and with no recorders exported.
add_executable(test_color_target_observer render/tests/color_target_observer.cpp render/set_render_target.cpp
    ${PROJECT_SOURCE_DIR}/runtime/render_observer.c)
target_link_libraries(test_color_target_observer PRIVATE pbrender_calls ${CMAKE_DL_LIBS} pthread)
target_compile_options(test_color_target_observer PRIVATE -ffp-contract=off -fno-fast-math)
set_target_properties(test_color_target_observer PROPERTIES ENABLE_EXPORTS ON)
add_test(NAME color_target_observer COMMAND test_color_target_observer)

add_executable(test_color_target_observer_absent render/tests/color_target_observer.cpp render/set_render_target.cpp
    ${PROJECT_SOURCE_DIR}/runtime/render_observer.c)
target_include_directories(test_color_target_observer_absent PRIVATE ${PROJECT_SOURCE_DIR}/runtime/include)
target_compile_definitions(test_color_target_observer_absent PRIVATE PB_COLOR_OBSERVER_ABSENT=1)
target_compile_options(test_color_target_observer_absent PRIVATE -ffp-contract=off -fno-fast-math)
target_link_libraries(test_color_target_observer_absent PRIVATE ${CMAKE_DL_LIBS} pthread)
add_test(NAME color_target_observer_absent COMMAND test_color_target_observer_absent)

# Depth observation uses separate sources so the proven color closure stays unchanged.
add_executable(test_depth_target_observer render/tests/depth_target_observer.cpp render/set_depth_render_target.cpp
    ${PROJECT_SOURCE_DIR}/runtime/render_observer.c)
target_link_libraries(test_depth_target_observer PRIVATE pbrender_calls ${CMAKE_DL_LIBS} pthread)
target_compile_options(test_depth_target_observer PRIVATE -ffp-contract=off -fno-fast-math)
set_target_properties(test_depth_target_observer PROPERTIES ENABLE_EXPORTS ON)
add_test(NAME depth_target_observer COMMAND test_depth_target_observer)

add_executable(test_depth_target_observer_absent render/tests/depth_target_observer.cpp render/set_depth_render_target.cpp
    ${PROJECT_SOURCE_DIR}/runtime/render_observer.c)
target_include_directories(test_depth_target_observer_absent PRIVATE ${PROJECT_SOURCE_DIR}/runtime/include)
target_compile_definitions(test_depth_target_observer_absent PRIVATE PB_DEPTH_OBSERVER_ABSENT=1)
target_compile_options(test_depth_target_observer_absent PRIVATE -ffp-contract=off -fno-fast-math)
target_link_libraries(test_depth_target_observer_absent PRIVATE ${CMAKE_DL_LIBS} pthread)
add_test(NAME depth_target_observer_absent COMMAND test_depth_target_observer_absent)

# The camera fixture generator and verifier contracts are checked beside their game-owned inputs.
add_test(NAME camera_update_cases
         COMMAND ${Python3_EXECUTABLE}
                 "${CMAKE_CURRENT_SOURCE_DIR}/camera/tests/test_update_cases.py")

# The compute command buffer's user-data observers, against stubs of the record.
add_executable(test_compute_user_data_observer render/tests/compute_user_data_observer.c render/observe_gpu_library.c)
target_include_directories(test_compute_user_data_observer PRIVATE ${PROJECT_SOURCE_DIR}/runtime/include)
target_compile_options(test_compute_user_data_observer PRIVATE -ffp-contract=off -fno-fast-math)
add_test(NAME compute_user_data_observer COMMAND test_compute_user_data_observer)
