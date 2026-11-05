// SPDX-License-Identifier: GPL-2.0-or-later
// Input and result layouts for the three presentation-record normalization modes.
// Trailing input words and result padding retain their original initialized state.
#pragma once

#include <cstddef>
#include <cstdint>

namespace render {
struct RecordMode2Input {
    uint32_t component_00;
    uint32_t count_04;
    uint32_t parameter_08;
    uint32_t selector;
    uint32_t parameter_10;
    uint32_t parameter_14;
    uint32_t parameter_18;
    uint32_t flags;
    uint32_t extra_20;
};
static_assert(offsetof(RecordMode2Input, selector) == 0xc);
static_assert(offsetof(RecordMode2Input, parameter_10) == 0x10);
static_assert(offsetof(RecordMode2Input, parameter_14) == 0x14);
static_assert(offsetof(RecordMode2Input, flags) == 0x1c);
static_assert(sizeof(RecordMode2Input) == 0x24);

struct NormalizedRecordMode2 {
    RecordMode2Input input;
    uint32_t mapping;
    uint32_t supplied;
    uint32_t secondary;
    uint32_t tertiary;
    uint8_t mode;
    uint8_t unknown_0x35[0x3];
    uint32_t status;
};
static_assert(offsetof(NormalizedRecordMode2, input) == 0x0);
static_assert(offsetof(NormalizedRecordMode2, mapping) == 0x24);
static_assert(offsetof(NormalizedRecordMode2, supplied) == 0x28);
static_assert(offsetof(NormalizedRecordMode2, secondary) == 0x2c);
static_assert(offsetof(NormalizedRecordMode2, tertiary) == 0x30);
static_assert(offsetof(NormalizedRecordMode2, mode) == 0x34);
static_assert(offsetof(NormalizedRecordMode2, status) == 0x38);
static_assert(sizeof(NormalizedRecordMode2) == 0x3c);

struct RecordMode4Input {
    uint32_t component_00;
    uint32_t component_04;
    uint32_t component_08;
    uint32_t count_0c;
    uint32_t selector;
    uint32_t parameter_14;
    uint32_t parameter_18;
    uint32_t parameter_1c;
    uint32_t flags;
    uint32_t extra_24;
};
static_assert(offsetof(RecordMode4Input, selector) == 0x10);
static_assert(offsetof(RecordMode4Input, parameter_14) == 0x14);
static_assert(offsetof(RecordMode4Input, parameter_18) == 0x18);
static_assert(offsetof(RecordMode4Input, flags) == 0x20);
static_assert(sizeof(RecordMode4Input) == 0x28);
struct NormalizedRecordMode4 {
    RecordMode4Input input;
    uint32_t mapping;
    uint32_t supplied;
    uint32_t secondary;
    uint32_t tertiary;
    uint8_t mode;
    uint8_t unknown_0x39[0x3];
    uint32_t status;
};
static_assert(offsetof(NormalizedRecordMode4, input) == 0x0);
static_assert(offsetof(NormalizedRecordMode4, mapping) == 0x28);
static_assert(offsetof(NormalizedRecordMode4, supplied) == 0x2c);
static_assert(offsetof(NormalizedRecordMode4, secondary) == 0x30);
static_assert(offsetof(NormalizedRecordMode4, tertiary) == 0x34);
static_assert(offsetof(NormalizedRecordMode4, mode) == 0x38);
static_assert(offsetof(NormalizedRecordMode4, status) == 0x3c);
static_assert(sizeof(NormalizedRecordMode4) == 0x40);

struct RecordMode3Input {
    uint32_t component_00;
    uint32_t component_04;
    uint32_t count_08;
    uint32_t parameter_0c;
    uint32_t selector;
    uint32_t parameter_14;
    uint32_t parameter_18;
    uint32_t parameter_1c;
    uint32_t parameter_20;
    uint32_t parameter_24;
    uint32_t flags;
    uint32_t extra_2c;
};
static_assert(offsetof(RecordMode3Input, selector) == 0x10);
static_assert(offsetof(RecordMode3Input, parameter_14) == 0x14);
static_assert(offsetof(RecordMode3Input, parameter_18) == 0x18);
static_assert(offsetof(RecordMode3Input, parameter_1c) == 0x1c);
static_assert(offsetof(RecordMode3Input, parameter_20) == 0x20);
static_assert(offsetof(RecordMode3Input, flags) == 0x28);
static_assert(sizeof(RecordMode3Input) == 0x30);
struct NormalizedRecordMode3 {
    RecordMode3Input input;
    uint32_t mapping;
    uint32_t supplied;
    uint32_t secondary;
    uint32_t tertiary;
    uint8_t mode;
    uint8_t unknown_0x41[0x3];
    uint32_t status;
};
static_assert(offsetof(NormalizedRecordMode3, input) == 0x0);
static_assert(offsetof(NormalizedRecordMode3, mapping) == 0x30);
static_assert(offsetof(NormalizedRecordMode3, supplied) == 0x34);
static_assert(offsetof(NormalizedRecordMode3, secondary) == 0x38);
static_assert(offsetof(NormalizedRecordMode3, tertiary) == 0x3c);
static_assert(offsetof(NormalizedRecordMode3, mode) == 0x40);
static_assert(offsetof(NormalizedRecordMode3, status) == 0x44);
static_assert(sizeof(NormalizedRecordMode3) == 0x48);

}  // namespace render
