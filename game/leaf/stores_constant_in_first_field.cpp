// SPDX-License-Identifier: GPL-2.0-or-later
// First-field constant setters: every member of these families (symbols/families.csv) stores a
// fixed value in the field at the very start of its object and returns nothing; rax is left as it
// was. clears_first_qword is `mov qword [rdi], 0; ret`, clears_first_dword `mov dword [rdi], 0`,
// clears_first_byte `mov byte [rdi], 0` and stores_one_in_first_byte `mov byte [rdi], 1`.
#include <cstdint>

namespace {

struct ObjectStartingWithQword {
    uint64_t first;
};

struct ObjectStartingWithDword {
    uint32_t first;
};

struct ObjectStartingWithByte {
    uint8_t first;
};

}  // namespace

extern "C" void bb_leaf_clears_first_qword(ObjectStartingWithQword *object)
{
    object->first = 0;
}

extern "C" void bb_leaf_clears_first_dword(ObjectStartingWithDword *object)
{
    object->first = 0;
}

extern "C" void bb_leaf_clears_first_byte(ObjectStartingWithByte *object)
{
    object->first = 0;
}

extern "C" void bb_leaf_stores_one_in_first_byte(ObjectStartingWithByte *object)
{
    object->first = 1;
}
