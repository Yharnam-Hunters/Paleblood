// SPDX-License-Identifier: GPL-2.0-or-later
// First-field setters: the members of writes_first_qword (`mov [rdi], rsi; ret`) and
// writes_first_dword (`mov [rdi], esi; ret`) store their second argument in the field at the very
// start of their object and return nothing; rax is left as it was (symbols/families.csv).
#include <cstdint>

struct ObjectStartingWithQword {
    uint64_t first;
};

struct ObjectStartingWithDword {
    uint32_t first;
};

extern "C" void bb_leaf_writes_first_qword(ObjectStartingWithQword *object, uint64_t value)
{
    object->first = value;
}

extern "C" void bb_leaf_writes_first_dword(ObjectStartingWithDword *object, uint32_t value)
{
    object->first = value;
}
