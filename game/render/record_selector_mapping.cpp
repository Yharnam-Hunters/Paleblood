// SPDX-License-Identifier: GPL-2.0-or-later
// Translate the selector used by record normalization into its packed encoding.
// Rows reconstruct the finite original switch; no unproved format names are assigned.
#include <cstdint>
#include <iterator>

namespace {
constexpr uint32_t selector_not_mapped = 12;
constexpr uint32_t unmapped_encoding = 0xffffffff;
// Indexed by selector. Sentinel rows are unsupported holes in the original switch.
constexpr uint32_t mapping_by_selector[] = {
    0x00000000, 0x00fac40e, 0x00fac70e, 0x00fac40e, 0x00fac50e, 0x001ac40d, 0x003ac70d, 0x001ac40d,  // selectors 0x00–0x07
    0x001ac50d, 0x00fac40c, 0x00fac70c, 0x00fac00c, 0x00fac40c, 0x00fac10c, 0x00fac50c, 0x0002c40b,  // selectors 0x08–0x0f
    0x0022c70b, 0x0002c40b, 0x0002c50b, 0x00004404, 0x00204704, 0x00204704, 0x00020401, 0x00fac409,  // selectors 0x10–0x17
    0x00fac009, 0x00fac409, 0x003ac706, 0x00fac40a, 0x00fac00a, 0x00fac90a, 0x00fac40a, 0x00fac10a,  // selectors 0x18–0x1f
    0x00fac50a, 0x0002c405, 0x0022c705, 0x0022c005, 0x0002c405, 0x0022c105, 0x0002c505, 0x00004404,  // selectors 0x20–0x27
    0x00204704, 0x00204704, 0x00004404, 0x00004504, unmapped_encoding, unmapped_encoding, unmapped_encoding, unmapped_encoding,  // selectors 0x28–0x2f
    0x0002c403, 0x0022c003, 0x0002c403, 0x0022c103, 0x0002c503, 0x00004402, 0x00204702, 0x00204002,  // selectors 0x30–0x37
    0x00204002, 0x00004402, 0x00204102, 0x00004502, 0x00004401, 0x00204001, 0x00004401, 0x00204101,  // selectors 0x38–0x3f
    0x00004501, 0x00800001, unmapped_encoding, unmapped_encoding, unmapped_encoding, unmapped_encoding, 0x00fac023, 0x00fac023,  // selectors 0x40–0x47
    0x00fac923, 0x00fac024, 0x00fac024, 0x00fac924, 0x00fac025, 0x00fac025, 0x00fac925, 0x00204026,  // selectors 0x48–0x4f
    0x00204026, 0x00204126, 0x0022c027, 0x0022c027, 0x0022c127, 0x0032e010, 0x00f2e011, 0x00f2e00a,  // selectors 0x50–0x57
    0x0032e00a, unmapped_encoding, 0x00f2e90a, 0x00f2e90a, 0x0032e90a, 0x0032e90a, 0x003ac028, 0x003ac028,  // selectors 0x58–0x5f
    0x003ac128, 0x00fac029, 0x00fac029, 0x00fac929,  // selectors 0x60–0x63
};
}  // namespace

extern "C" uint32_t bb_render_record_translate_selector(uint32_t *mapping, uint32_t selector)
{
    if (selector >= std::size(mapping_by_selector) || mapping_by_selector[selector] == unmapped_encoding) {
        *mapping = 0;
        return selector_not_mapped;
    }
    *mapping = mapping_by_selector[selector];
    return 0;
}
