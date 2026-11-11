# SPDX-License-Identifier: GPL-2.0-or-later
include_guard(GLOBAL)
get_filename_component(PB_GUEST_POINTER_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
if(NOT TARGET guest_tls_owned)
    include("${CMAKE_CURRENT_LIST_DIR}/DarwinGuestTLS.cmake")
endif()
add_library(guest_pointer_owned STATIC "${PB_GUEST_POINTER_ROOT}/runtime/guest_pointer_darwin.c")
target_include_directories(guest_pointer_owned PUBLIC "${PB_GUEST_POINTER_ROOT}/runtime/include")
target_link_libraries(guest_pointer_owned PUBLIC guest_tls_owned)
