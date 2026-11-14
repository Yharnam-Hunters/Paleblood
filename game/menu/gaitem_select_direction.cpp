// SPDX-License-Identifier: GPL-2.0-or-later
// Menu: the item select dialog's direction readers (the game's GaitemSelectDialog). Each is a
// virtual function of a small reader object that turns four of the key assign's keys into one
// step on the dialog's grid: the first key held wins, in the order up, down, left, right.
//
// The step {x, y} is returned in one 64-bit register. Up and down give y -1 and +1, left
// gives x -1; for right the key assign's answer itself becomes x (1 when held, 0 when not), so
// a right step is +1 and no key held is {0, 0}.
#include <cstdint>

#include "runtime/original.h"

namespace {

struct KeyAssign;

// The key assign's state of key `key`: nonzero while it is held.
RT_ORIGINAL(0x01391c60, key_held, uint8_t(KeyAssign *keys, int32_t key));

// A step {x, y} as the readers return it: x in the low half of the register, y in the high half.
constexpr uint64_t step(int32_t x, int32_t y)
{
    return static_cast<uint32_t>(x) | uint64_t{static_cast<uint32_t>(y)} << 32;
}

constexpr uint64_t step_up = step(0, -1);
constexpr uint64_t step_down = step(0, 1);
constexpr uint64_t step_left = step(-1, 0);

// The keys a reader looks at, in the order it tries them.
struct DirectionKeys {
    int32_t up;
    int32_t down;
    int32_t left;
    int32_t right;
};
constexpr DirectionKeys cursor_keys{0x20, 0x21, 0x22, 0x23};
constexpr DirectionKeys second_keys{0x24, 0x25, 0x26, 0x27};

}  // namespace

// The reader of keys 0x20-0x23. `reader` (the object) is not used; `keys` points at the key assign.
extern "C" uint64_t bb_menu_gaitem_select_direction_keys_20_01ee6080(void * /*reader*/, KeyAssign *const *keys)
{
    if (key_held(*keys, cursor_keys.up))
        return step_up;
    if (key_held(*keys, cursor_keys.down))
        return step_down;
    if (key_held(*keys, cursor_keys.left))
        return step_left;
    return step(key_held(*keys, cursor_keys.right), 0);
}

// The reader of keys 0x24-0x27. Without a key assign it skips up and down and asks for left and
// right all the same (with a null key assign).
extern "C" uint64_t bb_menu_gaitem_select_direction_keys_24_01ee61f0(void * /*reader*/, KeyAssign *const *keys)
{
    if (*keys) {
        if (key_held(*keys, second_keys.up))
            return step_up;
        if (key_held(*keys, second_keys.down))
            return step_down;
    }
    if (key_held(*keys, second_keys.left))
        return step_left;
    return step(key_held(*keys, second_keys.right), 0);
}
