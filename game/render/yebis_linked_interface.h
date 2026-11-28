// SPDX-License-Identifier: GPL-2.0-or-later
// Call boundary for a derived YEBIS interface whose full class is unknown.
#pragma once

#include "yebis_interface.h"
#include "runtime/original.h"

namespace yebis {

// The pointer names the common prefix; the concrete cleanup uses additional storage.
RT_ORIGINAL(0x00fb6fe0, cleanup_linked_interface, void(InterfaceStorage *));

}  // namespace yebis
