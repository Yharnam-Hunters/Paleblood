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
add_custom_command(
    OUTPUT ${HOOKS_C}
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/gen_hooks.py
            ${CMAKE_CURRENT_SOURCE_DIR}/hooks.csv ${PROJECT_SOURCE_DIR}/symbols/functions.csv ${HOOKS_C}
    DEPENDS ${PROJECT_SOURCE_DIR}/tools/gen_hooks.py ${CMAKE_CURRENT_SOURCE_DIR}/hooks.csv
            ${PROJECT_SOURCE_DIR}/symbols/functions.csv)

add_library(bbgame SHARED ${GAME_SOURCES} ${HOOKS_C})
target_link_libraries(bbgame PRIVATE bbrt)
target_link_options(bbgame PRIVATE -Wl,--no-undefined)
# Replacements reproduce the original's floating-point operations one by one: no fused multiply-add.
target_compile_options(bbgame PRIVATE -ffp-contract=off)
