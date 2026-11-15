// SPDX-License-Identifier: GPL-2.0-or-later
// The family releases_member_at_0xc0_then_destroys_base (symbols/families.csv): copies of one
// scaleform destructor. It stores its class's vtable (the address points past the table's two
// header entries), releases the member it holds at +0xc0, then tail-calls the base class's
// destructor. The member is either a tagged value (bit 0 set), which is decremented in place, or
// a pointer to a counted object: when the 22-bit count at +0x20 is not 0, it is decremented and
// the release function is called with the object.
#include <cstddef>
#include <cstdint>

#include "runtime/guest.h"
#include "runtime/original.h"

namespace {

RT_ORIGINAL(0x027ee1e0, release_counted, void(void *object));
RT_ORIGINAL(0x006f92f0, destroy_base, void(void *object));

constexpr uint32_t class_vtable = 0x055b8100;
constexpr std::size_t vtable_header = 0x10;   // offset to top and type info before the entries
constexpr uintptr_t tag_bit = 1;
constexpr uint32_t count_mask = 0x3fffff;      // the low 22 bits of the dword at +0x20

struct Counted {
    uint8_t unknown_0x0[0x20];
    uint32_t count;
};
static_assert(offsetof(Counted, count) == 0x20);

struct Holder {
    const void *vtable;
    uint8_t unknown_0x8[0xb8];
    uintptr_t member;
};
static_assert(offsetof(Holder, member) == 0xc0);

}  // namespace

extern "C" void bb_engine_releases_member_at_0xc0_then_destroys_base(void *object)
{
    auto *holder = static_cast<Holder *>(object);
    holder->vtable = static_cast<const uint8_t *>(rt_guest(class_vtable)) + vtable_header;
    const uintptr_t member = holder->member;
    if (member != 0) {
        if (member & tag_bit) {
            holder->member = member - 1;
        } else {
            auto *counted = reinterpret_cast<Counted *>(member);
            if ((counted->count & count_mask) != 0) {
                counted->count -= 1;
                release_counted(counted);
            }
        }
    }
    destroy_base(object);
}
