// SPDX-License-Identifier: GPL-2.0-or-later
// Original operations used when releasing the last YEBIS interface reference.
#pragma once

#include "yebis_context.h"
#include "yebis_interface.h"
#include "runtime/original.h"

namespace yebis {

RT_ORIGINAL(0x02fbf178, c_assert, void(const char *message, const char *function));
RT_GLOBAL(0x04bfeae0, release_count_message, const char);
RT_GLOBAL(0x04bfeb43, release_function_name, const char);
RT_GLOBAL(0x056b3618, current_allocator, AllocatorInterface *);
RT_ORIGINAL(0x00fb6f50, free_interface_storage, void(AllocatorInterface *, InterfaceStorage *));

}  // namespace yebis
