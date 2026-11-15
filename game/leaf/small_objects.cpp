// SPDX-License-Identifier: GPL-2.0-or-later
// Small fixed-layout leaf families (symbols/families.csv); every member of each is the same bytes.
//
// returns_all_bits is `mov rax, -1; ret`. resets_first_qword_returning_object clears the first
// qword and returns the object (`mov qword [rdi], 0; mov rax, rdi`).
//
// adjusts_pointer_at_8_unless_null returns the pointer at +8 plus 0xd0 (a part 0xd0 bytes into
// the object it points at), or null when the pointer is null.
//
// reads_wide_string_text_at_0x30 returns the text of the wide string the pointer at +0x30 points
// at: the string keeps eight 16-bit units inline at +8 and its capacity at +0x20, and once the
// capacity is 8 or more, +8 holds the heap text instead (`cmp qword [rax+0x20], 8; jb inline`).
//
// The bit-field setters write parts of the first dword: bits 28..31 from the low 4 bits of the
// second argument, bits 0..9 from its low 10 bits, or both at once with bits 10..27 cleared.
// They return nothing (the originals leave a scratch value in eax).
#include <cstddef>
#include <cstdint>

namespace {

struct ObjectStartingWithQword {
    uint64_t first;
};

// The owner's layout is not known; only the offset of its base-class subobject is: every member
// adds 0xd0 to a non-null owner pointer and keeps null, the adjustment a compiler emits when it
// converts a pointer to a base class that does not start the object.
struct Owner;
constexpr std::ptrdiff_t owner_base_subobject = 0xd0;

struct ObjectPointingAtOwner {
    uint64_t unknown_before_pointer;
    Owner *owner;
};

constexpr uint64_t inline_capacity = 8;

struct WideString {
    uint64_t unknown_before_text;
    union {
        char16_t inline_text[inline_capacity];
        char16_t *heap_text;
    };
    uint64_t length;
    uint64_t capacity;
};

struct ObjectWithWideString {
    uint8_t unknown_before_string[0x30];
    WideString *string;
};

constexpr int low_bits = 10;
constexpr int middle_bits = 18;
constexpr int top_bits = 4;

struct PackedBits {
    uint32_t low : low_bits;
    uint32_t middle : middle_bits;
    uint32_t top : top_bits;
};

}  // namespace

extern "C" uint64_t bb_leaf_returns_all_bits(void)
{
    return UINT64_MAX;
}

extern "C" ObjectStartingWithQword *bb_leaf_resets_first_qword_returning_object(ObjectStartingWithQword *object)
{
    object->first = 0;
    return object;
}

extern "C" void *bb_leaf_adjusts_pointer_at_8_unless_null(const ObjectPointingAtOwner *object)
{
    if (object->owner == nullptr)
        return nullptr;
    return reinterpret_cast<uint8_t *>(object->owner) + owner_base_subobject;
}

extern "C" const char16_t *bb_leaf_reads_wide_string_text_at_0x30(const ObjectWithWideString *object)
{
    const WideString *string = object->string;
    return string->capacity < inline_capacity ? string->inline_text : string->heap_text;
}

extern "C" void bb_leaf_writes_top_four_bits_of_first_dword(PackedBits *bits, uint32_t value)
{
    bits->top = value;
}

extern "C" void bb_leaf_writes_low_ten_bits_of_first_dword(PackedBits *bits, uint32_t value)
{
    bits->low = value;
}

extern "C" void bb_leaf_packs_first_dword_from_two_bit_fields(PackedBits *bits, uint32_t low, uint32_t top)
{
    *bits = PackedBits{low, 0, top};
}
