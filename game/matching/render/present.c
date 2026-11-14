// SPDX-License-Identifier: GPL-2.0-or-later
// Matching source (docs/MATCHING.md) for small presentation helpers: the pool counter read, the
// encoding unit table, the record bit width and the swap chain cleanup records.
#include <stdint.h>

// Read the presentation pool counter atomically (a locked or-with-zero writeback).
uint64_t render_read_present_pool_counter(uint64_t *counter)
{
    return __sync_fetch_and_or(counter, 0);
}

// DXGI format numbers, as the encoding argument uses them (the ranges below follow DXGI's order).
enum {
    DXGI_R32G32B32A32_TYPELESS = 1,
    DXGI_R32G32B32A32_SINT = 4,
    DXGI_R32G32B32_TYPELESS = 5,
    DXGI_R32G32B32_SINT = 8,
    DXGI_R16G16B16A16_TYPELESS = 9,
    DXGI_X32_TYPELESS_G8X24_UINT = 22,
    DXGI_R10G10B10A2_TYPELESS = 23,
    DXGI_X24_TYPELESS_G8_UINT = 47,
    DXGI_R8G8_TYPELESS = 48,
    DXGI_R16_SINT = 59,
    DXGI_R8_TYPELESS = 60,
    DXGI_R1_UNORM = 66,
    DXGI_R9G9B9E5_SHAREDEXP = 67,
    DXGI_G8R8_G8B8_UNORM = 69,
    DXGI_BC1_TYPELESS = 70,
    DXGI_BC1_UNORM_SRGB = 72,
    DXGI_BC2_TYPELESS = 73,
    DXGI_BC3_UNORM_SRGB = 78,
    DXGI_BC4_TYPELESS = 79,
    DXGI_BC4_SNORM = 81,
    DXGI_BC5_TYPELESS = 82,
    DXGI_BC5_SNORM = 84,
    DXGI_B5G6R5_UNORM = 85,
    DXGI_B5G5R5A1_UNORM = 86,
    DXGI_B8G8R8A8_UNORM = 87,
    DXGI_B8G8R8X8_UNORM_SRGB = 93,
    DXGI_BC6H_TYPELESS = 94,
    DXGI_BC7_UNORM_SRGB = 99,
};

enum {
    UNITS_2 = 2,
    UNITS_4 = 4,
    UNITS_8 = 8,
    UNITS_12 = 12,
    UNITS_16 = 16,
};

// The bytes per element (per 4x4 block for the BC formats) of a DXGI format; 0 for any format
// outside 1..99. The original is this switch: clang turns it into the lookup table at the
// table address the replacement reads.
uint64_t render_present_encoding_units(uint32_t encoding)
{
    switch (encoding) {
    case DXGI_R32G32B32A32_TYPELESS ... DXGI_R32G32B32A32_SINT:
        return UNITS_16;
    case DXGI_R32G32B32_TYPELESS ... DXGI_R32G32B32_SINT:
        return UNITS_12;
    case DXGI_R16G16B16A16_TYPELESS ... DXGI_X32_TYPELESS_G8X24_UINT:
        return UNITS_8;
    case DXGI_R10G10B10A2_TYPELESS ... DXGI_X24_TYPELESS_G8_UINT:
        return UNITS_4;
    case DXGI_R8G8_TYPELESS ... DXGI_R16_SINT:
        return UNITS_2;
    case DXGI_R8_TYPELESS ... DXGI_R1_UNORM:
        return 1;
    case DXGI_R9G9B9E5_SHAREDEXP ... DXGI_G8R8_G8B8_UNORM:
        return UNITS_4;
    case DXGI_BC1_TYPELESS ... DXGI_BC1_UNORM_SRGB:
        return UNITS_8;
    case DXGI_BC2_TYPELESS ... DXGI_BC3_UNORM_SRGB:
        return UNITS_16;
    case DXGI_BC4_TYPELESS ... DXGI_BC4_SNORM:
        return UNITS_8;
    case DXGI_BC5_TYPELESS ... DXGI_BC5_SNORM:
        return UNITS_16;
    case DXGI_B5G6R5_UNORM ... DXGI_B5G5R5A1_UNORM:
        return UNITS_2;
    case DXGI_B8G8R8A8_UNORM ... DXGI_B8G8R8X8_UNORM_SRGB:
        return UNITS_4;
    case DXGI_BC6H_TYPELESS ... DXGI_BC7_UNORM_SRGB:
        return UNITS_16;
    default:
        return 0;
    }
}

enum { WORD_BITS = 32 };

// The leading zero bits of a word. The original uses the LZCNT instruction (BMI/ABM on Jaguar),
// which gives 32 for zero; __builtin_clz compiles to that same instruction with -march=btver2.
static inline uint32_t leading_zeros(uint32_t value)
{
    return __builtin_clz(value);
}

// The number of bits needed to hold the widest of three components (0 when all are zero).
uint32_t render_present_record_bit_width(uint32_t first, uint32_t second, uint32_t third)
{
    uint32_t first_zeros = leading_zeros(first);
    uint32_t second_zeros = leading_zeros(second);
    uint32_t fewest = leading_zeros(third);
    if (second_zeros < fewest)
        fewest = second_zeros;
    if (first_zeros < fewest)
        fewest = first_zeros;
    return WORD_BITS - fewest;
}

typedef struct SwapChainCleanupOwner SwapChainCleanupOwner;

// The owner's virtual table; only the release callback is known.
typedef struct {
    uint8_t unknown_0x00[0x70];
    void (*release)(SwapChainCleanupOwner *owner, uintptr_t resource);
} SwapChainCleanupVtable;

struct SwapChainCleanupOwner {
    SwapChainCleanupVtable *vtable;
};

typedef struct {
    SwapChainCleanupOwner *owner;
    uintptr_t resource;
} SwapChainCleanupRecord;

// Hand the record's resource back to its owner and clear the resource slot.
static inline void clear_cleanup_record(SwapChainCleanupRecord *record)
{
    SwapChainCleanupOwner *owner = record->owner;
    if (owner && record->resource) {
        owner->vtable->release(owner, record->resource);
        record->resource = 0;
    }
}

// Release the swap chain's curve resource.
void render_clear_swap_chain_curve(SwapChainCleanupRecord *record)
{
    clear_cleanup_record(record);
}

// Release the swap chain's cached record resource.
void render_clear_swap_chain_cached_record(SwapChainCleanupRecord *record)
{
    clear_cleanup_record(record);
}
