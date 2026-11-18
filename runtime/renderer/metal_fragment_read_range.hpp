/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PB_METAL_FRAGMENT_READ_RANGE_HPP
#define PB_METAL_FRAGMENT_READ_RANGE_HPP
#include "metal_fragment_ir.hpp"
#include "runtime/gcn_spirv.h"

namespace pb {
/* This certifies only a read already proved to be a canonical entry-user pair plus a
   constant offset. It neither infers shader provenance nor reads or snapshots its value. */
inline bool metal_fragment_read_in_pool(const MetalFragmentRead &read, const uint32_t *user,
    unsigned declared, uint32_t written, uint64_t guest_base, uint64_t gpu_base, uint64_t pool_size)
{
    if (!user || declared > GCN_USER_SGPR_MAX || read.pointer_sgpr >= declared ||
        declared - read.pointer_sgpr < 2 || read.bytes != 4 || read.offset % 4 ||
        read.offset > UINT64_MAX - read.bytes) return false;
    const uint32_t pair = 3u << read.pointer_sgpr;
    if ((written & pair) != pair) return false;
    const uint64_t pointer = uint64_t(user[read.pointer_sgpr]) |
        uint64_t(user[read.pointer_sgpr + 1] & 0xffffu) << 32;
    if (pointer > UINT64_MAX - read.offset) return false;
    const uint64_t address = pointer + read.offset;
    if (address % 4 || address < guest_base || address - guest_base > pool_size ||
        read.bytes > pool_size - (address - guest_base)) return false;
    // The translation anchor is GPU base minus guest base modulo uint64. Its subtraction
    // may wrap legitimately; the actual native address and its complete read must not.
    const uint64_t offset = address - guest_base;
    if (offset > UINT64_MAX - gpu_base) return false;
    const uint64_t native = gpu_base + offset;
    return native % 4 == 0 && read.bytes <= UINT64_MAX - native;
}
} // namespace pb
#endif
