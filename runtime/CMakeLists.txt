# SPDX-License-Identifier: GPL-2.0-or-later
add_library(bbrt STATIC hooks.c plugin.c capture.c probe.c loader.c nid.c)
target_include_directories(bbrt PUBLIC include)
# The executable loader on its own, for the verification harness (tools/harness.py loads it).
add_library(pbloader SHARED loader.c nid.c)
target_include_directories(pbloader PUBLIC include)
# The boot harness: runs the executable's entry on our runtime (runtime/boot.c). System library
# implementations are compiled in directly, so the linker keeps every RT_SYSLIB entry.
file(GLOB PB_SYSLIB CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/syslib/*.c)
add_executable(pbboot boot.c loader.c nid.c process.c ${PB_SYSLIB})
target_include_directories(pbboot PRIVATE include)
