/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PB_METAL_FRAGMENT_WQM_SOURCE_HPP
#define PB_METAL_FRAGMENT_WQM_SOURCE_HPP
#include <string>
namespace pb {
/* Boolean quad permutation: every successful read observes all four invocation bits in the same
   reduction as their Boolean data. Production callers must materialize each call. */
inline std::string metal_fragment_wqm_source(const std::string &name)
{
    return "namespace " + name + " {\n" + R"(using namespace metal;
static inline bool swap(bool value, uint source_xor, uint lane, device atomic_uint *fault)
{
    uint bit = 1u << lane;
    uint2 acquired = quad_or(uint2(bit, value ? bit : 0u));
    if (acquired.x != 15u) {
        atomic_fetch_or_explicit(fault, 1u, memory_order_relaxed);
        discard_fragment();
        return false;
    }
    return (acquired.y & (1u << (lane ^ source_xor))) != 0u;
}
)" + "\n}";
}
}
#endif
