// SPDX-License-Identifier: GPL-2.0-or-later
// Small fixed-layout leaf families (symbols/families.csv): every member of each is the same bytes.
//
// atomically_increments_dword_at_8 is `mov eax, 1; lock xadd [rdi+8], eax; ret`: an atomic
// reference or use count 8 bytes into the object goes up by one, and the count before the
// increment comes back zero-extended (xadd leaves it in eax).
//
// clears_first_two_qwords, clears_first_four_qwords and clears_two_qwords_and_a_dword clear the
// first 16, 32 and 20 bytes of their object with `mov [rdi+K], 0` stores and return nothing;
// rax is left as it was.
#include <cstdint>

namespace {

struct ObjectWithCountAt8 {
    uint64_t unknown_before_count;
    uint32_t count;
};

struct TwoQwords {
    uint64_t first;
    uint64_t second;
};

struct FourQwords {
    uint64_t first;
    uint64_t second;
    uint64_t third;
    uint64_t fourth;
};

struct [[gnu::packed]] TwoQwordsAndADword {
    uint64_t first;
    uint64_t second;
    uint32_t third;
};

}  // namespace

extern "C" uint64_t bb_leaf_atomically_increments_dword_at_8(ObjectWithCountAt8 *object)
{
    return __atomic_fetch_add(&object->count, 1u, __ATOMIC_SEQ_CST);
}

extern "C" void bb_leaf_clears_first_two_qwords(TwoQwords *object)
{
    object->second = 0;
    object->first = 0;
}

extern "C" void bb_leaf_clears_first_four_qwords(FourQwords *object)
{
    object->fourth = 0;
    object->third = 0;
    object->second = 0;
    object->first = 0;
}

extern "C" void bb_leaf_clears_two_qwords_and_a_dword(TwoQwordsAndADword *object)
{
    object->third = 0;
    object->second = 0;
    object->first = 0;
}
