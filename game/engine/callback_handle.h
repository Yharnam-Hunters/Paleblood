// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the handle a registered callback keeps (LoadCallback::handle in load.h, at +0x10). The
// callback base class's destructor lets go of it when it is still set, then clears it; the load's
// destructors (load.cpp) inline that, and member_release_destructors.cpp replaces the destructor's
// copies. Declared here once for both.
#pragma once

#include "runtime/original.h"

namespace engine {

// Lets go of a callback's registration handle (callers skip a null one).
RT_ORIGINAL(0x02450560, callback_release_handle, void(void *handle));

}  // namespace engine
