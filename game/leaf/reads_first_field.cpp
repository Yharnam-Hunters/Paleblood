// SPDX-License-Identifier: GPL-2.0-or-later
// First-field getters: the members of reads_first_qword (`mov rax, [rdi]; ret`) and
// reads_first_dword (`mov eax, [rdi]; ret`) return the field at the very start of their object
// (symbols/families.csv). The 32-bit load clears the upper half of rax, so both replacements
// return a 64-bit value and the families' cases compare all of rax.
#include <cstdint>

struct ObjectStartingWithQword {
    uint64_t first;
};

struct ObjectStartingWithDword {
    uint32_t first;
};

extern "C" uint64_t bb_leaf_reads_first_qword(const ObjectStartingWithQword *object)
{
    return object->first;
}

extern "C" uint64_t bb_leaf_reads_first_dword(const ObjectStartingWithDword *object)
{
    return object->first;
}
