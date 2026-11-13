// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the wide string's constructor from a null-terminated text (wide_string.h).
//
// engine_wide_string_construct_02bc0e00 (0x02bc0e00) puts the string on the engine's default
// allocator (created on first use and kept in its global), checks that heap the way every container
// does (an invalid one is reported and used anyway), leaves the string empty in its inline buffer,
// then assigns the text through the counted assign (0x02a2a720) with its length: wcslen, which is
// not called for an empty text. The first word and the owns byte are left alone; the caller sets
// them.
#include <cstdint>

#include "container.h"
#include "runtime/original.h"
#include "static_wide_string.h"
#include "wide_string.h"
#include "wide_string_assign.h"

namespace {

using engine::WideString;

// The wide string's assign from counted text (it allows text inside the string itself).
RT_ORIGINAL(0x02a2a720, wide_string_assign_counted, WideString *(WideString *, const char16_t *text, uint64_t length));
RT_ORIGINAL(0x02fbf228, c_wcslen, uint64_t(const char16_t *));

// The engine's default allocator, created the first time it is asked for.
engine::Allocator *default_allocator()
{
    namespace string = engine::static_wide_string;
    engine::Allocator *allocator = string::default_allocator.get();
    if (!allocator) {
        allocator = string::acquire_default_allocator();
        string::default_allocator.get() = allocator;
    }
    return allocator;
}

}  // namespace

extern "C" void bb_engine_wide_string_construct_02bc0e00(WideString *s, const char16_t *text)
{
    engine::Allocator *allocator = default_allocator();
    engine::check_container_heap(allocator);
    engine::wide_string_begin(s, allocator);
    const uint64_t length = text[0] != u'\0' ? c_wcslen(text) : 0;
    wide_string_assign_counted(s, text, length);
}
