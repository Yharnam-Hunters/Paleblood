# SPDX-License-Identifier: GPL-2.0-or-later
# Shared by the runtime and the independently configurable host-time tests.
get_filename_component(PB_HOST_TIME_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
if(NOT TARGET pbhost_time)
    add_library(pbhost_time STATIC "${PB_HOST_TIME_ROOT}/runtime/host_time.c")
    target_include_directories(pbhost_time PUBLIC "${PB_HOST_TIME_ROOT}/runtime/include")
endif()
