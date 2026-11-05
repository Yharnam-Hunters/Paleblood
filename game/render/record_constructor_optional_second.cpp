// SPDX-License-Identifier: GPL-2.0-or-later
// Delegate through a zeroed geometry request and scratch result. Even a failed
// geometry calculation publishes its zero/partial scratch fields to the outputs.
#include <cstring>

#include "record_constructor_helpers.h"

extern "C" uint32_t bb_render_record_constructor_optional_second(uint64_t *total, uint32_t *unit,
                                                                uint32_t *first, uint32_t *second,
                                                                uint32_t first_value, uint32_t second_value,
                                                                uint32_t count, uint32_t selector,
                                                                uint32_t encoding)
{
    constexpr uint32_t scratch_alignment = 32;
    constexpr uint32_t request_flags = 0x40;
    alignas(scratch_alignment) render::RecordGeometryDescriptor descriptor{};
    descriptor.flags_24.set(request_flags);
    descriptor.metadata_28.set(render::read_record_tuple_encoding_metadata(
        reinterpret_cast<const uint8_t *>(&encoding)));
    descriptor.second_0c.set(second_value);
    descriptor.count_14.set(1);
    descriptor.third_10.set(1);
    descriptor.classification_00.set(selector);
    descriptor.first_08.set(first_value);

    alignas(scratch_alignment) render::RecordGeometryResult result{};
    const uint32_t status = render::calculate_record_geometry(&result, &descriptor);
    // Do not branch on status: the native wrapper copies scratch on error too.
    if (total != nullptr) {
        const uint64_t value = uint64_t(count) * render::read_geometry_total(result.total_10);
        std::memcpy(total, &value, sizeof(value));
    }
    if (unit != nullptr) {
        const uint32_t value = result.value_1c.get();
        std::memcpy(unit, &value, sizeof(value));
    }
    if (first != nullptr) {
        const uint32_t value = result.first_00.get();
        std::memcpy(first, &value, sizeof(value));
    }
    if (second != nullptr) {
        const uint32_t value = result.second_04.get();
        std::memcpy(second, &value, sizeof(value));
    }
    return status;
}
