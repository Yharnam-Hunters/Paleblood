// SPDX-License-Identifier: GPL-2.0-or-later
// Constructor-only record clearing and original encoding publication.
#pragma once
#include <cstring>
#include "record_constructor_layout.h"
#include "runtime/original.h"
namespace render {
template <typename Pair> inline void clear_record_constructor_pair(Pair &pair)
{
    static_assert(sizeof(Pair) == sizeof(uint64_t));
    const uint64_t zero = 0;
    std::memcpy(&pair, &zero, sizeof(zero));
}
RT_ORIGINAL(0x01486540, set_record_constructor_encoding,
            void(RecordConstructorPrefix *record, uint32_t encoding));
}  // namespace render
