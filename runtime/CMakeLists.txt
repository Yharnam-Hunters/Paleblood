# SPDX-License-Identifier: GPL-2.0-or-later
# Recording a hooked call by tracing the original (trace.c) needs Capstone to decode
# instructions. Without it the runtime builds with trace_none.c: tracing is unavailable and
# nothing is recorded (docs/capture-architecture.md).
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(CAPSTONE QUIET IMPORTED_TARGET capstone)
endif()
if(CAPSTONE_FOUND)
    set(PB_TRACE_SOURCES trace.c trace_entry.S)
    message(STATUS "capture tracing: on (Capstone ${CAPSTONE_VERSION})")
else()
    set(PB_TRACE_SOURCES trace_none.c)
    message(STATUS "capture tracing: off (Capstone not found)")
endif()
add_library(bbrt STATIC hooks.c plugin.c host_hooks.c capture.c capture_trace.cpp probe.c loader.c nid.c
            ${PB_TRACE_SOURCES})
target_include_directories(bbrt PUBLIC include)
if(CAPSTONE_FOUND)
    target_link_libraries(bbrt PUBLIC PkgConfig::CAPSTONE pthread)
endif()
# The executable loader on its own, for the verification harness (tools/harness.py loads it).
add_library(pbloader SHARED loader.c nid.c)
target_include_directories(pbloader PUBLIC include)
# The boot harness: runs the executable's entry on our runtime (runtime/boot.c). System library
# implementations are compiled in directly, so the linker keeps every RT_SYSLIB entry.
file(GLOB PB_SYSLIB CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/syslib/*.c)
add_executable(pbboot boot.c loader.c nid.c process.c ${PB_SYSLIB})
target_include_directories(pbboot PRIVATE include)
