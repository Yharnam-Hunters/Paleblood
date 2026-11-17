// SPDX-License-Identifier: GPL-2.0-or-later
// Tpfbnd binder resource (0xa8 bytes): derives from the SprjFile-backed binder resource base
// (0x017ea230) and installs the tpfbnd vtable (0x0576ac10).
#pragma once

#include <cstddef>
#include <cstdint>

#include "runtime/original.h"

namespace loading::tpfbnd {

// A tpfbnd resource entry (0xa8 bytes, sizeof returned by vtable slot 0x60).
// Offsets 0x08..0x88 hold the loading resource base (loading_resource_base_construct_013e6ff0),
// and offsets 0x88..0xa8 hold the binder base's fields and SprjFile entry pointer.
struct TpfbndResource {
    const uint8_t *vtable;
    uint8_t unknown_0x08[0xa0];
};
static_assert(offsetof(TpfbndResource, vtable) == 0 && sizeof(TpfbndResource) == 0xa8);

// Base binder resource constructor (initializes the resource base, clears +0x88..+0xa0, and
// registers a 0xa8-byte entry in SprjFile for type_name).
RT_ORIGINAL(0x017ea230, binder_resource_construct, void(TpfbndResource *, const char16_t *, uint64_t));

// TpfbndResource vtable address point.
RT_GLOBAL(0x0576ac10, tpfbnd_resource_vtable, const uint8_t);

}  // namespace loading::tpfbnd
