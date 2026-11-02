// SPDX-License-Identifier: GPL-2.0-or-later
// Release heap storage when selected by capacity, then reset the string to empty inline form.
// The ownership byte is not consulted or reset by this destructor.
#include "wide_string.h"

extern "C" void bb_engine_destroy_wide_string_02a44140(engine::WideString *value)
{
    if (value->capacity >= engine::wide_string_inline_capacity) {
        value->allocator->free(value->heap);
    }
    // These stores follow the callback, including when it changes the object itself.
    value->capacity = engine::wide_string_inline_capacity - 1;
    value->size = 0;
    value->inline_text[0] = u'\0';
}
