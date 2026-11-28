// SPDX-License-Identifier: GPL-2.0-or-later
// destroys_owner_of_wide_string_then_base_0x0139b630 and
// deletes_owner_of_wide_string_after_base_0x0139b630 (symbols/families.csv): the copies of a
// destructor of an object that may own a heap wide string (engine/wide_string.h), in place and
// deleting. The object gets its vtable back; when it owns its string (the flag at +0x44) and has
// one (+0x30), the heap owning the string object is looked up first, the string's long text is
// freed through the string's allocator, the string is left empty (inline, capacity 7), the string
// object goes back to its heap and the pointer is cleared. Then the base destructor 0x0139b630
// runs (a tail call in place; the deleting copies then hand the object to operator delete).
#include <cstddef>
#include <cstdint>

#include "allocator.h"
#include "buffer_release.h"
#include "runtime/original.h"
#include "wide_string.h"

namespace {

struct WideStringOwner {
    const void *vtable;
    uint8_t unknown_0x08[0x28];
    engine::WideString *string;
    uint8_t unknown_0x38[0xc];
    uint8_t owns_string;
};
static_assert(offsetof(WideStringOwner, string) == 0x30 && offsetof(WideStringOwner, owns_string) == 0x44);

// The class's vtable: the address point is 0x10 bytes into its block.
RT_GLOBAL(0x056f3b30, wide_string_owner_vtable_block, const uint8_t);
constexpr std::ptrdiff_t vtable_address_point = 0x10;

RT_ORIGINAL(0x0139b630, destroy_base, void(WideStringOwner *));
RT_ORIGINAL(0x02fbe588, operator_delete, void(void *));

// An empty string as its destructor leaves it: inline, one character less than the inline room.
constexpr uint64_t empty_capacity = engine::wide_string_inline_capacity - 1;

void release_owned_string(WideStringOwner *owner)
{
    owner->vtable = wide_string_owner_vtable_block.address() + vtable_address_point;
    if (!owner->owns_string)
        return;
    engine::WideString *string = owner->string;
    if (!string)
        return;
    engine::Allocator *heap = engine::heap_owning(string);
    engine::wide_string_release(string);
    string->capacity = empty_capacity;
    string->size = 0;
    string->inline_text[0] = u'\0';
    heap->free(string);
    owner->string = nullptr;
}

}  // namespace

extern "C" void bb_engine_destroys_owner_of_wide_string_then_base_0x0139b630(WideStringOwner *owner)
{
    release_owned_string(owner);
    destroy_base(owner);
}

extern "C" void bb_engine_deletes_owner_of_wide_string_after_base_0x0139b630(WideStringOwner *owner)
{
    release_owned_string(owner);
    destroy_base(owner);
    operator_delete(owner);
}
