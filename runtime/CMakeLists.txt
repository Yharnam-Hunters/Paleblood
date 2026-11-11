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
add_library(bbrt STATIC hooks.c plugin.c host_hooks.c capture.c capture_trace.cpp capture_hooks.c capture_entry.S probe.c loader.c nid.c render_observer.c observe_hooks.c observe_entry.S
            ${PB_TRACE_SOURCES})
target_include_directories(bbrt PUBLIC include)
target_link_libraries(bbrt PUBLIC ${CMAKE_DL_LIBS} pthread)
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
# only (docs/RENDERER.md). CI builds without a window. Host sound and host controller input
# (runtime/audio/, runtime/input/) come with the window's SDL3, or are off.
if(PkgConfig_FOUND)
    pkg_check_modules(PB_SDL3 QUIET IMPORTED_TARGET sdl3)
    pkg_check_modules(PB_VULKAN QUIET IMPORTED_TARGET vulkan)
endif()
if(APPLE AND PB_ENABLE_METAL_EXECUTOR)
    set(PB_DISPLAY_SOURCES display/display.c display/display_window_none.c renderer/renderer.c audio/audio_host_none.c
        input/input_host_none.c)
    message(STATUS "renderer: Metal offscreen executor; window presentation remains separate")
elseif(PB_SDL3_FOUND AND PB_VULKAN_FOUND)
    set(PB_DISPLAY_SOURCES display/display.c display/display_vulkan.c renderer/renderer.c audio/audio_host_sdl.c
        input/input_host_sdl.c)
    message(STATUS "display window: on (Vulkan ${PB_VULKAN_VERSION}, SDL ${PB_SDL3_VERSION})")
else()
    set(PB_DISPLAY_SOURCES display/display.c display/display_window_none.c renderer/renderer_none.c audio/audio_host_none.c
        input/input_host_none.c)
    message(STATUS "display window: off (Vulkan or SDL3 not found)")
endif()
list(TRANSFORM PB_DISPLAY_SOURCES PREPEND ${CMAKE_CURRENT_SOURCE_DIR}/)
set(PB_DISPLAY_SOURCES ${PB_DISPLAY_SOURCES} PARENT_SCOPE)
# Movie decoding (runtime/media/, runtime/media_host.h): FFmpeg when it is found, otherwise none, and
# the title's movie player stops by name.
if(PkgConfig_FOUND)
    pkg_check_modules(PB_FFMPEG QUIET IMPORTED_TARGET GLOBAL libavformat libavcodec libavutil libswscale libswresample)
endif()
if(PB_FFMPEG_FOUND)
    set(PB_MEDIA_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/media/media_ffmpeg.c)
    message(STATUS "movie decoding: on (FFmpeg)")
else()
    set(PB_MEDIA_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/media/media_none.c)
    message(STATUS "movie decoding: off (FFmpeg not found)")
endif()
set(PB_MEDIA_SOURCES ${PB_MEDIA_SOURCES} PARENT_SCOPE)
set(PB_FFMPEG_FOUND ${PB_FFMPEG_FOUND} PARENT_SCOPE)
include(${PROJECT_SOURCE_DIR}/cmake/HostTime.cmake)
add_executable(pbboot boot.c loader.c memory.c nid.c process.c host_hooks.c input/input.c input/input_script.c ${PB_SYSLIB} ${PB_DISPLAY_SOURCES} ${PB_MEDIA_SOURCES})
if(PB_FFMPEG_FOUND)
    target_link_libraries(pbboot PRIVATE PkgConfig::PB_FFMPEG)
endif()
if(APPLE AND PB_ENABLE_METAL_EXECUTOR)
    target_link_libraries(pbboot PRIVATE pbmetal_executor)
elseif(PB_SDL3_FOUND AND PB_VULKAN_FOUND)
    target_link_libraries(pbboot PRIVATE PkgConfig::PB_SDL3 PkgConfig::PB_VULKAN pbrender_executor)
endif()
target_include_directories(pbboot PRIVATE include)
# The game library's replacements reach the renderer's record through the symbols pbboot exports
# (runtime/render_observer.h): only those, so the library's own copies of shared runtime code
# (loader.c, nid.c) keep binding to themselves. macOS executables export their symbols already.
if(NOT APPLE)
    foreach(symbol rt_fatal rt_render_record_color_target rt_render_record_viewport rt_render_record_draw_auto
                   rt_render_record_vertex_shader rt_render_record_pixel_shader rt_render_record_user_data
                   rt_render_record_register rt_render_record_instances rt_render_record_dispatch rt_render_record_index_base
                   rt_render_record_index_size rt_render_record_index_type rt_render_record_draw_index_offset
                   rt_render_record_draw_index_offset_packet rt_render_record_draw_indexed)
        target_link_options(pbboot PRIVATE "LINKER:--export-dynamic-symbol=${symbol}")
    endforeach()
endif()
target_link_libraries(pbboot PRIVATE ${CMAKE_DL_LIBS} atrac9 pbhost_time)

if(TARGET guest_pointer_owned)
    target_link_libraries(pbboot PRIVATE guest_pointer_owned)
endif()
