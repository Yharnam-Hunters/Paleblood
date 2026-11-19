// SPDX-License-Identifier: GPL-2.0-or-later
// Three leaf families of forwarding thunks whose every member is the same bytes
// (symbols/families.csv; the parameterized thunk families are generated next to this file).
//
// passes_part_at_0xe8_to_virtual_0x68 tail-calls slot 0x68 of its object's virtual table with
// 0xe8 added to the second argument, the part 0xe8 into the object that argument points at;
// adjusts_base_and_passes_part_at_0xe8 is the same call reached through a base 0x20 into the
// object. exports_member_at_0x20_and_tail_calls_it stores the pointer at +0x20 through its second
// argument and tail-calls slot 8 of the object it points at.
#include "virtual_call.h"

constexpr std::size_t part_slot = 0x68;
constexpr uint64_t part_offset = 0xe8;
constexpr std::size_t base_offset = 0x20;
constexpr std::size_t exported_member_offset = 0x20;
constexpr std::size_t exported_slot = 8;

extern "C" uint64_t bb_leaf_passes_part_at_0xe8_to_virtual_0x68(void *object, uint64_t second, uint64_t third, uint64_t fourth, uint64_t fifth, uint64_t sixth)
{
    BB_TAIL_CALL return virtual_function<part_slot>(object)(object, second + part_offset, third, fourth, fifth, sixth);
}

extern "C" uint64_t bb_leaf_adjusts_base_and_passes_part_at_0xe8(void *object, uint64_t second, uint64_t third, uint64_t fourth, uint64_t fifth, uint64_t sixth)
{
    void *whole = static_cast<uint8_t *>(object) - base_offset;
    BB_TAIL_CALL return virtual_function<part_slot>(whole)(whole, second + part_offset, third, fourth, fifth, sixth);
}

extern "C" uint64_t bb_leaf_exports_member_at_0x20_and_tail_calls_it(void *object, uint64_t second, uint64_t third, uint64_t fourth, uint64_t fifth, uint64_t sixth)
{
    void *member = member_at<exported_member_offset>(object);
    *reinterpret_cast<void **>(second) = member;
    BB_TAIL_CALL return virtual_function<exported_slot>(member)(member, second, third, fourth, fifth, sixth);
}
