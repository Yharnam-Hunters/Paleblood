// SPDX-License-Identifier: GPL-2.0-or-later
// Prefix views used while preparing a presentation source. Fields with unproved meanings
// retain offset-qualified names; these views do not establish pixel or tiling formats.
#pragma once

#include <cstddef>
#include <cstdint>

#include "runtime/original.h"

namespace render {
struct PresentSourcePreparation;
struct PresentSourceContext;

struct PresentSourcePayloadView {
    uint8_t unknown_0x00[0x2c];
    uint16_t count_input_2c;
    uint16_t count_input_2e;
    uint16_t record_parameter_30;
    uint8_t record_count_32;
    uint8_t record_selector_33;
    uint8_t unknown_0x34[4];
};
static_assert(offsetof(PresentSourcePayloadView, count_input_2c) == 0x2c);
static_assert(offsetof(PresentSourcePayloadView, count_input_2e) == 0x2e);
static_assert(offsetof(PresentSourcePayloadView, record_parameter_30) == 0x30);
static_assert(offsetof(PresentSourcePayloadView, record_count_32) == 0x32);
static_assert(offsetof(PresentSourcePayloadView, record_selector_33) == 0x33);
static_assert(offsetof(PresentSourcePayloadView, unknown_0x34) == 0x34);
static_assert(sizeof(PresentSourcePayloadView) == 0x38);
static_assert(alignof(PresentSourcePayloadView) == 2);

struct PresentSourceView {
    uint8_t unknown_0x00[0x18];
    PresentSourcePayloadView payload;
};
static_assert(offsetof(PresentSourceView, payload) == 0x18);

// The native caller reserves 48 bytes. Builder helpers reach the 44-byte record
// prefix; the final four scratch bytes have no initialized-value promise.
struct alignas(16) PresentSourceRecord {
    uint8_t unknown_0x00[0x30];
};
static_assert(sizeof(PresentSourceRecord) == 0x30);
static_assert(alignof(PresentSourceRecord) == 0x10);

RT_ORIGINAL(0x02ad1f80, render_present_source_flush_state,
            void(PresentSourcePreparation *, PresentSourceContext *, PresentSourceView *));
RT_ORIGINAL(0x02565130, render_present_record_bit_width,
            uint32_t(uint32_t first, uint32_t second, uint32_t third));
RT_ORIGINAL(0x0256e1d0, build_present_source_record,
            uint32_t(PresentSourceRecord *, PresentSourcePayloadView *, uint32_t selector,
                     uint32_t index, uintptr_t range_begin, uint32_t parameter));
RT_ORIGINAL(0x02ab5a70, consume_present_source_record,
            void(PresentSourceContext *, PresentSourceRecord *, PresentSourcePayloadView *,
                 uint32_t zero, uint32_t parameter, uint32_t trailing_zero));
}  // namespace render
