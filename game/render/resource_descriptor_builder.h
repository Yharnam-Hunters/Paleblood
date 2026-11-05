// SPDX-License-Identifier: GPL-2.0-or-later
// Descriptor preparation used by resource-info initialization. Lower functions
// remain originals; byte views describe only fields consumed by these callers.
#pragma once

#include "byte_field.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "runtime/original.h"

namespace render {
constexpr size_t resource_descriptor_parameter_bytes = 0x30;
constexpr size_t resource_descriptor_storage_bytes = 0x20;
constexpr size_t resource_descriptor_level_bytes = 0x30;
struct ResourceDescriptorParameters { uint8_t bytes[resource_descriptor_parameter_bytes]; };
struct ResourceDescriptorOptional { Field<uint32_t> value; };
struct ResourceDescriptorStorage { uint8_t bytes[resource_descriptor_storage_bytes]; };
struct ResourceDescriptorLevel { uint8_t bytes[resource_descriptor_level_bytes]; };
struct ResourceDescriptorRequirements { Field<uint64_t> packed; };
struct ResourceDescriptorPair {
    Field<uint32_t> quantity;
    Field<uint32_t> alignment;
    void set_packed(uint64_t value) { std::memcpy(this, &value, sizeof(value)); }
};
struct ResourceDescriptorNormalized {
    Field<uint32_t> first;
    Field<uint32_t> second;
    Field<uint32_t> level_count;
    Field<uint32_t> quantity;
    Field<uint32_t> selector;
    Field<uint32_t> first_parameter;
    Field<uint32_t> second_parameter;
    uint8_t unknown_1c[4];
    Field<uint32_t> flags;
    uint8_t unknown_24[4];
    Field<uint32_t> secondary_flags;
    uint8_t unknown_2c[4];
    Field<uint32_t> first_derived;
    Field<uint32_t> second_derived;
    uint8_t unknown_38[8];
    Field<uint8_t> option_byte;
    uint8_t unknown_41[3];
    Field<uint32_t> status;
};
struct ResourceDescriptorGeometry {
    Field<uint64_t> first_pair;
    Field<uint32_t> third;
    uint8_t unknown_0c[0x14];
    Field<uint64_t> constructor_tail_pair;
    Field<uint32_t> constructor_tail_word;
    uint8_t unknown_2c[0xc];
};
// Outputs whose addresses are passed to descriptor and auxiliary helpers.
struct ResourceDescriptorScratch {
    ResourceDescriptorPair constructor_pair;
    ResourceDescriptorPair first_auxiliary_pair;
    ResourceDescriptorNormalized normalized;
    ResourceDescriptorLevel second_level;
    Field<uint32_t> optional_second;
    Field<uint32_t> optional_first;
    Field<uint32_t> optional_unit;
    Field<uint64_t> optional_total;
    ResourceDescriptorGeometry geometry;
    ResourceDescriptorLevel first_level;
    ResourceDescriptorStorage second_temporary;
    ResourceDescriptorStorage first_temporary;
};
static_assert(sizeof(uintptr_t) == 8);
static_assert(sizeof(ResourceDescriptorParameters) == 0x30);
static_assert(sizeof(ResourceDescriptorRequirements) == 8);
static_assert(sizeof(ResourceDescriptorPair) == 8);
static_assert(sizeof(ResourceDescriptorStorage) == 0x20);
static_assert(sizeof(ResourceDescriptorLevel) == 0x30);
static_assert(offsetof(ResourceDescriptorNormalized, flags) == 0x20);
static_assert(offsetof(ResourceDescriptorNormalized, secondary_flags) == 0x28);
static_assert(offsetof(ResourceDescriptorNormalized, first_derived) == 0x30);
static_assert(offsetof(ResourceDescriptorNormalized, second_derived) == 0x34);
static_assert(offsetof(ResourceDescriptorNormalized, option_byte) == 0x40);
static_assert(offsetof(ResourceDescriptorNormalized, status) == 0x44);
static_assert(sizeof(ResourceDescriptorNormalized) == 0x48);
static_assert(offsetof(ResourceDescriptorGeometry, constructor_tail_pair) == 0x20);
static_assert(offsetof(ResourceDescriptorGeometry, constructor_tail_word) == 0x28);
static_assert(sizeof(ResourceDescriptorGeometry) == 0x38);

namespace resource_descriptor_calls {
// Resource-info initialization forwards its owner values to normalization.
RT_ORIGINAL(0x025711d0, normalize,
            void(ResourceDescriptorNormalized *, const ResourceDescriptorParameters *,
                 const ResourceDescriptorOptional *, uint32_t, uintptr_t, uintptr_t));
// The first native call enters with RAX=0x40, so every returned bit above
// AL is zero; the later call reads only AL. Consume a byte here so compiler
// scratch-register contents cannot enter the saved 32-bit predicate word.
RT_ORIGINAL(0x02abb4f0, record_auxiliary_predicate, uint8_t(uint32_t, uint32_t));
RT_ORIGINAL(0x02ab7a20, choose_constructor_parameter, uint32_t(uint32_t, uint32_t));
RT_ORIGINAL(0x02ab7ab0, combine_record_parameters, uint32_t(uint32_t, uint32_t, uint32_t));
RT_ORIGINAL(0x01485f90, construct_record,
            uint64_t(ResourceDescriptorGeometry *, uint32_t, uint32_t, uint32_t, uint32_t,
                     uint32_t, uint32_t, uint32_t, ResourceDescriptorPair *, ResourceDescriptorPair *));
RT_ORIGINAL(0x01488d90, convert_constructor_record,
            void(ResourceDescriptorStorage *, ResourceDescriptorGeometry *, uint32_t));
RT_ORIGINAL(0x01487ee0, initialize_pair_descriptor,
            uint64_t(ResourceDescriptorStorage *, uint32_t, uint32_t, uint32_t, uint32_t,
                     uint32_t, uint32_t, uint32_t, uint32_t));
RT_ORIGINAL(0x014a1e80, initialize_record_level_descriptor,
            uint32_t(ResourceDescriptorLevel *, ResourceDescriptorStorage *, uint32_t, uint32_t));
RT_ORIGINAL(0x0149ce10, calculate_record_geometry,
            uint32_t(ResourceDescriptorGeometry *, ResourceDescriptorLevel *));
RT_ORIGINAL(0x0149e2d0, record_constructor_optional_first,
            uint32_t(Field<uint64_t> *, Field<uint32_t> *,
                     Field<uint32_t> *, Field<uint32_t> *,
                     uint64_t, uint64_t, uint32_t, uint32_t));
// Auxiliary helpers publish the total and alignment used in the final fold.
RT_ORIGINAL(0x0149e570, calculate_second_auxiliary,
            uint32_t(Field<uint64_t> *, Field<uint32_t> *,
                     Field<uint32_t> *, Field<uint32_t> *,
                     uint64_t, uint64_t, uint32_t, uint32_t));
RT_ORIGINAL(0x02abb4a0, report_record_normalization_error, uintptr_t(uint32_t));
}  // namespace resource_descriptor_calls
}  // namespace render

// Prepare descriptors and return the low 32-bit status, zero-extended to 64 bits.
// The two owner values are forwarded to the normalizer.
extern "C" uint64_t bb_render_prepare_resource_descriptor(
    render::ResourceDescriptorRequirements *, render::ResourceDescriptorStorage *,
    const render::ResourceDescriptorParameters *, const render::ResourceDescriptorOptional *,
    uintptr_t inherited_initial, uintptr_t inherited_owner);
