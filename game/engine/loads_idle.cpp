// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the check the load code asks before it starts work. It has 15 callers and appears in
// vtable slots; the constructor of the localize job (menu/localize_job.cpp) is one of them. It
// is true when none of three signed 32-bit counters is above 0. What they count is not
// established.
#include <cstdint>

#include "loads_idle.h"
#include "runtime/original.h"

namespace {

RT_GLOBAL(0x05978fd8, counter_0x05978fd8, int32_t);
RT_GLOBAL(0x05978a38, counter_0x05978a38, int32_t);
RT_GLOBAL(0x05978d08, counter_0x05978d08, int32_t);

}  // namespace

// The counters are checked in this order and the first one above 0 decides. Signed compares:
// a negative counter does not count as busy.
extern "C" bool bb_engine_loads_idle_01ee9b40()
{
    if (counter_0x05978fd8.get() > 0)
        return false;
    if (counter_0x05978a38.get() > 0)
        return false;
    return counter_0x05978d08.get() <= 0;
}
