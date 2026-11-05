// SPDX-License-Identifier: GPL-2.0-or-later
// Scalar triplets and register storage shared by presentation parameter writers.
#pragma once
#include "byte_field.h"
namespace render {
struct RecordConfigurationFloat3 {
    Field<float> first;
    Field<float> second;
    Field<float> third;
};
static_assert(offsetof(RecordConfigurationFloat3, first) == 0x00);
static_assert(offsetof(RecordConfigurationFloat3, second) == 0x04);
static_assert(offsetof(RecordConfigurationFloat3, third) == 0x08);
static_assert(sizeof(RecordConfigurationFloat3) == 0x0c);

struct RecordConfigurationRegister {
    Field<uint32_t> word;
    uint8_t unknown_0x04[0x04];
};
static_assert(offsetof(RecordConfigurationRegister, word) == 0x00);
static_assert(sizeof(RecordConfigurationRegister) == 0x08);
}  // namespace render
