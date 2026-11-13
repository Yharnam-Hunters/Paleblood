// SPDX-License-Identifier: GPL-2.0-or-later
// Loading: a resource info's reference count (structs in resource_queue.h).
//
// loading_resource_info_reference_count_02a3a1b0 (0x02a3a1b0) reads the count under the info's
// own mutex (the engine's PthreadMutex lock and unlock, 0x02484110 and 0x02484290), waiting
// forever. loading_resource_info_drop_reference_02a3a2a0 (0x02a3a2a0) takes one reference away
// under the same mutex; a count already at zero stays zero. The lock's result is not looked at.
#include <cstdint>

#include "resource_queue.h"

using namespace loading::resource_queue;

namespace {

RT_ORIGINAL(0x02484110, engine_mutex_lock, int32_t(uintptr_t *mutex, int32_t timeout_us));
RT_ORIGINAL(0x02484290, engine_mutex_unlock, int32_t(uintptr_t *mutex));

constexpr int32_t wait_forever = -1;

}  // namespace

extern "C" int32_t bb_loading_resource_info_reference_count_02a3a1b0(ResourceInfo *info)
{
    engine_mutex_lock(&info->mutex, wait_forever);
    const int32_t count = info->reference_count;
    engine_mutex_unlock(&info->mutex);
    return count;
}

extern "C" void bb_loading_resource_info_drop_reference_02a3a2a0(ResourceInfo *info)
{
    engine_mutex_lock(&info->mutex, wait_forever);
    if (info->reference_count != 0)
        --info->reference_count;
    engine_mutex_unlock(&info->mutex);
}
