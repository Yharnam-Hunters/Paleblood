// SPDX-License-Identifier: GPL-2.0-or-later
// Record initializers: every member of the zeroes_record_and_stores_first_argument family
// (symbols/families.csv) clears the fifteen 64-bit words at its second argument, stores its first
// argument in the word after them and returns zero (all of rax). The members are the same 126
// bytes: fifteen `mov qword [rsi+K], 0` from 0x70 down to 0, `mov [rsi+0x78], rdi`, `xor eax, eax`.
// Which objects they build is not traced here.
#include <cstdint>

namespace {

constexpr int cleared_words = 15;

struct RecordWithOwner {
    uint64_t cleared[cleared_words];
    void *owner;
};
static_assert(sizeof(RecordWithOwner) == 0x80);

}  // namespace

extern "C" uint64_t bb_leaf_zeroes_record_and_stores_first_argument(void *owner, RecordWithOwner *record)
{
    for (uint64_t &word : record->cleared)
        word = 0;
    record->owner = owner;
    return 0;
}
