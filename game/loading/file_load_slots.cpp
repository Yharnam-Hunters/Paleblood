// SPDX-License-Identifier: GPL-2.0-or-later
// Loading: building the file load event queue's 32 slots (structs in resource_queue.h).
//
// loading_file_load_slots_construct_02a6b520 (0x02a6b520) runs the slot constructor (0x024d63f0)
// on each of the 32 slots in order, first to last; the file load event queue (0x02a6a9e0,
// 0x02a6ad50) calls it on the slot array it holds.
#include "resource_queue.h"

using namespace loading::resource_queue;

extern "C" void bb_loading_file_load_slots_construct_02a6b520(FileLoadSlots *slots)
{
    for (FileLoadSlot &slot : slots->slots)
        file_load_slot_construct(&slot);
}
