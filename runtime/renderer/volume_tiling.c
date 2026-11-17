/* SPDX-License-Identifier: GPL-2.0-or-later */
/* GFX7 ADDR_THICK, 1D_TILED_THICK: 8x8x4 microtiles, without macro pipe/bank
   interleave. Basis: AMD PAL AddrLib at 7ce51b199c72021b8d027c50b55da515fd9b2e0b,
   CiLib::HwlSetupTileInfo and EgBasedLib's microtile index/address rules.
   The fixed index-19 convention is an admitted profile, not a discovery of an
   arbitrary guest's programmed GB_TILE_MODE table. */
#include "runtime/volume_tiling.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

enum { TILE_WIDTH = 8, TILE_HEIGHT = 8, TILE_DEPTH = 4,
       TILE_TEXELS = TILE_WIDTH * TILE_HEIGHT * TILE_DEPTH };

static int failed(char *error, size_t size, const char *format, ...) __attribute__((format(printf, 3, 4)));
static int failed(char *error, size_t size, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    if (error && size) vsnprintf(error, size, format, args);
    va_end(args);
    return -1;
}

static int power_of_two(uint32_t value) { return value && !(value & (value - 1)); }
static int multiply_span(uint64_t *span, uint32_t count)
{
    if (*span && count > UINT64_MAX / *span) return -1;
    *span *= count;
    return 0;
}

int rt_volume_layout(unsigned tiling_index, unsigned element_bytes, uint32_t pitch,
                     uint32_t width, uint32_t height, uint32_t depth, int pow2_pad,
                     rt_volume_tiling *output, char *error, size_t error_size)
{
    if (!output) return failed(error, error_size, "a volume layout output is required");
    if (tiling_index != RT_VOLUME_1D_THICK)
        return failed(error, error_size, "volume tiling index %u: only the index-19 1D-thick profile is modeled", tiling_index);
    if (element_bytes != 1 && element_bytes != 2 && element_bytes != 4 && element_bytes != 8 && element_bytes != 16)
        return failed(error, error_size, "volume element size %u: only 1, 2, 4, 8 and 16 bytes are modeled", element_bytes);
    if (!width || !height || !depth || width > pitch || pitch % TILE_WIDTH)
        return failed(error, error_size, "volume dimensions require nonzero extents and an 8-aligned pitch covering width");
    if (pow2_pad != 0 && pow2_pad != 1)
        return failed(error, error_size, "volume POW2_PAD must be zero or one");
    if (pow2_pad && (!power_of_two(pitch) || !power_of_two(width) || !power_of_two(height) || !power_of_two(depth)))
        return failed(error, error_size, "volume POW2_PAD requires dimensions and pitch already padded to powers of two");
    if (height > UINT32_MAX - (TILE_HEIGHT - 1) || depth > UINT32_MAX - (TILE_DEPTH - 1))
        return failed(error, error_size, "volume height or depth padding overflows 32 bits");
    rt_volume_tiling result = {.element_bytes = element_bytes, .width = width, .height = height,
        .depth = depth, .pitch = pitch, .padded_height = (height + TILE_HEIGHT - 1) & ~(uint32_t)(TILE_HEIGHT - 1),
        .padded_depth = (depth + TILE_DEPTH - 1) & ~(uint32_t)(TILE_DEPTH - 1)};
    result.size_bytes = pitch;
    if (multiply_span(&result.size_bytes, result.padded_height) ||
        multiply_span(&result.size_bytes, result.padded_depth) || multiply_span(&result.size_bytes, element_bytes))
        return failed(error, error_size, "volume allocation size overflows 64 bits");
    *output = result;
    return 0;
}

static unsigned bit(uint32_t value, unsigned index) { return value >> index & 1u; }

uint64_t rt_volume_offset(const rt_volume_tiling *layout, uint32_t x, uint32_t y, uint32_t z)
{
    /* ADDR_THICK interleaves Z inside each microtile. Its lower bits depend on
       element width; the final two bits are always X2 and Y2. */
    unsigned pixel = bit(x, 0) | bit(y, 0) << 1;
    if (layout->element_bytes <= 2)
        pixel |= bit(x, 1) << 2 | bit(y, 1) << 3 | bit(z, 0) << 4 | bit(z, 1) << 5;
    else if (layout->element_bytes == 4)
        pixel |= bit(x, 1) << 2 | bit(z, 0) << 3 | bit(y, 1) << 4 | bit(z, 1) << 5;
    else
        pixel |= bit(z, 0) << 2 | bit(x, 1) << 3 | bit(y, 1) << 4 | bit(z, 1) << 5;
    pixel |= bit(x, 2) << 6 | bit(y, 2) << 7;
    const uint64_t tile = ((uint64_t)(z / TILE_DEPTH) * (layout->padded_height / TILE_HEIGHT) + y / TILE_HEIGHT) *
        (layout->pitch / TILE_WIDTH) + x / TILE_WIDTH;
    return (tile * TILE_TEXELS + pixel) * layout->element_bytes;
}

static void copy_volume(const rt_volume_tiling *layout, const uint8_t *source, uint8_t *destination, int to_tiled)
{
    uint64_t linear = 0;
    for (uint32_t z = 0; z < layout->depth; ++z)
        for (uint32_t y = 0; y < layout->height; ++y)
            for (uint32_t x = 0; x < layout->width; ++x) {
                const uint64_t tiled = rt_volume_offset(layout, x, y, z);
                memcpy(destination + (to_tiled ? tiled : linear), source + (to_tiled ? linear : tiled), layout->element_bytes);
                linear += layout->element_bytes;
            }
}

void rt_volume_detile(const rt_volume_tiling *layout, const uint8_t *tiled, uint8_t *linear)
{
    copy_volume(layout, tiled, linear, 0);
}

void rt_volume_tile(const rt_volume_tiling *layout, const uint8_t *linear, uint8_t *tiled)
{
    copy_volume(layout, linear, tiled, 1);
}
