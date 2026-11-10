// SPDX-License-Identifier: GPL-2.0-or-later
// The one thing a field getter knows about its object: a field of type Field, Offset bytes from
// the start. The function families of field getters (game/leaf/reads_*_field.cpp,
// field_address.cpp) read through this layout; the rest of the object is unknown. Packed, so the
// field sits exactly at Offset whatever its alignment, as the original's load does.
#ifndef GAME_LEAF_OBJECT_FIELD_H
#define GAME_LEAF_OBJECT_FIELD_H

#include <cstddef>
#include <cstdint>

template <std::size_t Offset, typename Field>
struct [[gnu::packed]] ObjectWithFieldAt {
    uint8_t unknown_before_field[Offset];
    Field field;
};

#endif
