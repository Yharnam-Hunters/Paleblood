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
add_library(bbrt STATIC hooks.c plugin.c host_hooks.c capture.c capture_trace.cpp capture_hooks.c capture_entry.S probe.c loader.c nid.c
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
# The host display (runtime/display/): a window when Vulkan and SDL3 are found, otherwise headless
# only (docs/RENDERER.md). CI builds without a window.
if(PkgConfig_FOUND)
    pkg_check_modules(PB_SDL3 QUIET IMPORTED_TARGET sdl3)
    pkg_check_modules(PB_VULKAN QUIET IMPORTED_TARGET vulkan)
endif()
if(PB_SDL3_FOUND AND PB_VULKAN_FOUND)
    set(PB_DISPLAY_SOURCES display/display.c display/display_vulkan.c renderer/renderer.c audio/audio_host_sdl.c)
    message(STATUS "display window: on (Vulkan ${PB_VULKAN_VERSION}, SDL ${PB_SDL3_VERSION})")
else()
    set(PB_DISPLAY_SOURCES display/display.c display/display_window_none.c renderer/renderer_none.c audio/audio_host_none.c)
    message(STATUS "display window: off (Vulkan or SDL3 not found)")
endif()
list(TRANSFORM PB_DISPLAY_SOURCES PREPEND ${CMAKE_CURRENT_SOURCE_DIR}/)
set(PB_DISPLAY_SOURCES ${PB_DISPLAY_SOURCES} PARENT_SCOPE)
add_executable(pbboot boot.c loader.c memory.c nid.c process.c host_hooks.c ${PB_SYSLIB} ${PB_DISPLAY_SOURCES})
if(PB_SDL3_FOUND AND PB_VULKAN_FOUND)
    target_link_libraries(pbboot PRIVATE PkgConfig::PB_SDL3 PkgConfig::PB_VULKAN pbrender_executor)
endif()
target_include_directories(pbboot PRIVATE include)
target_link_libraries(pbboot PRIVATE ${CMAKE_DL_LIBS} atrac9)
