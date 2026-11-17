// SPDX-License-Identifier: GPL-2.0-or-later
// The family deleting_destructor_freeing_0x28_bytes (symbols/families.csv): copies of one deleting
// destructor. It stores its base class's vtable (the address points past the table's two header
// entries) and hands the object, 0x28 bytes, to the thread allocator's sized free.
#include "runtime/guest.h"
#include "thread_allocator_free.h"

namespace {

constexpr uint32_t base_class_vtable = 0x0566fa90;
constexpr std::size_t vtable_header = 0x10;   // offset to top and type info before the entries
constexpr uint32_t object_size = 0x28;

}  // namespace

extern "C" uint64_t bb_engine_deleting_destructor_freeing_0x28_bytes(void *object)
{
    *static_cast<const void **>(object) = static_cast<const uint8_t *>(rt_guest(base_class_vtable)) + vtable_header;
    return engine::thread_allocator_free::free_sized(object, object_size);
}
