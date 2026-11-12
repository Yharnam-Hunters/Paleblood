// SPDX-License-Identifier: GPL-2.0-or-later
// Loading: the resource manager's release of a resource info (structs in resource_queue.h).
//
// loading_resource_manager_release_024c1530 (0x024c1530), called by the resource info's release
// (0x024bd930) once the info has no references left. A manager that was never initialized is
// reported (the report returns, and the release goes on). An info without flag 0x40 is taken out
// of the file load queue it waits in, if any, and the manager's condition signal is set. Then,
// under the manager's lock, the info is marked released (0x80) and handed to 0x024c3260; the
// unlock is the original's tail call.
#include <cstdint>

#include "../engine/engine.h"
#include "resource_queue.h"

using namespace loading::resource_queue;
using frame_timing::job::JobLock;
using frame_timing::job::wait_forever;

extern "C" void bb_loading_resource_manager_release_024c1530(ResourceManager *manager, ResourceInfo *info)
{
    if ((manager->flags & resource_manager_initialized) == 0)
        engine::fatal_error(resource_manager_source_file.address(), resource_manager_release_line,
                            resource_manager_not_initialized.address());
    if ((info->flags & resource_flag_0x40) == 0) {
        if (info->file_queue)
            file_queue_remove(info->file_queue, info);
        condition_signal_set(&manager->signal, true);
    }
    JobLock *lock = &manager->lock;
    lock->vtable->lock(lock, wait_forever);
    info->flags |= resource_released;
    resource_manager_remove_released(manager, info);
    lock->vtable->unlock(lock);
}
