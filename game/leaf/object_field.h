// SPDX-License-Identifier: GPL-2.0-or-later
// The one thing a field getter or setter knows about its object: a field of type Field, Offset
// bytes from the start. The function families of field getters and setters (game/leaf/) read
// and write through this layout; the rest of the object is unknown. Packed, so the field sits
// exactly at Offset whatever its alignment, as the original's load or store does; each generated
// replacement pins its offset with field_is_at_offset.
#ifndef GAME_LEAF_OBJECT_FIELD_H
#define GAME_LEAF_OBJECT_FIELD_H

#include <cstddef>
#include <cstdint>

template <std::size_t Offset, typename Field>
struct [[gnu::packed]] ObjectWithFieldAt {
    uint8_t unknown_before_field[Offset];
    Field field;
};

template <std::size_t Offset, typename Field>
constexpr std::size_t field_offset()
{
    using Object = ObjectWithFieldAt<Offset, Field>;
    return offsetof(Object, field);
}

template <std::size_t Offset, typename Field>
constexpr bool field_is_at_offset = field_offset<Offset, Field>() == Offset;

#endif
