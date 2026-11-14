// SPDX-License-Identifier: GPL-2.0-or-later
// Menu: more of the item dialogs' fixed-vector lists (gaitem_dialog.h): the select dialog's
// pair of narrow texts, put into a slot of its vector, and the status dialog's two-slot vector
// of labelled wide strings, appended to.
//
// Strings are copied the way the engine's containers copy them: built empty on the source's
// allocator, then assigned all of the source's text, and given the source's owns byte. The first
// word of each string object is left as it was.
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "../engine/engine.h"
#include "../engine/narrow_string.h"
#include "../engine/wide_string_assign.h"
#include "gaitem_dialog.h"

namespace {

using engine::NarrowString;
using engine::WideString;
using namespace menu::gaitem_dialog;

// Assigns `count` characters of `source` from `position` (all of the rest with
// narrow_string_rest), std::string::assign(source, position, count).
constexpr uint64_t narrow_string_rest = ~uint64_t{0};
RT_ORIGINAL(0x02b27840, narrow_string_assign_from,
            NarrowString *(NarrowString *target, const NarrowString *source, uint64_t position, uint64_t count));

void copy_string(NarrowString *target, const NarrowString &source)
{
    engine::narrow_string_begin(target, source.allocator);
    narrow_string_assign_from(target, &source, 0, narrow_string_rest);
    target->owns = source.owns;
}

void copy_string(WideString *target, const WideString &source)
{
    engine::wide_string_begin(target, source.allocator);
    engine::wide_string_assign_from(target, &source, 0, engine::wide_string_rest);
    target->owns = source.owns;
}

// An entry of the select dialog's text vector: two narrow texts.
struct TextPair {
    NarrowString first;
    NarrowString second;
};
static_assert(offsetof(TextPair, second) == 0x38 && sizeof(TextPair) == 0x70);

// The vector's count of slots in use follows its slots.
constexpr size_t text_vector_count_offset = 0x708;

// An entry of the status dialog's label vector.
struct Label {
    uint8_t kind;
    uint8_t unknown_0x01[0x7];   // not copied
    uint64_t word_0x08;
    WideString text;
};
static_assert(offsetof(Label, word_0x08) == 0x8 && offsetof(Label, text) == 0x10 && sizeof(Label) == 0x48);

constexpr uint64_t label_capacity = 2;
struct LabelVector {
    uint8_t slots[label_capacity * sizeof(Label) + slot_alignment];   // from first_slot()
    uint64_t count;
};
static_assert(offsetof(LabelVector, count) == 0x98);

}  // namespace

// Copies `source` into slot `slot` of the select dialog's text vector (null: nothing). A slot at
// or past the slots in use is reported, then copied into all the same; a slot before the vector
// is not checked.
extern "C" void bb_menu_gaitem_select_text_pair_copy_into_vector_01ed7950(const uint8_t *vector, TextPair *slot,
                                                                         const TextPair *source)
{
    uint64_t in_use;
    std::memcpy(&in_use, vector + text_vector_count_offset, sizeof(in_use));
    const uintptr_t end = first_slot(vector) + sizeof(TextPair) * in_use;
    if (end <= reinterpret_cast<uintptr_t>(slot))
        engine::fatal_error(pool_check_file.address(), pool_check_line, pool_check_message.address());
    if (slot == nullptr) return;
    copy_string(&slot->first, source->first);
    copy_string(&slot->second, source->second);
}

// Appends a copy of `source` to the status dialog's label vector. A full vector is reported and
// the copy goes into the slot past its end all the same. The test is count + 1 < 3 unsigned, so
// a count of all ones (one less than zero) passes it.
extern "C" void bb_menu_gaitem_status_label_push_back_01f2c200(LabelVector *vector, const Label *source)
{
    if (vector->count + 1 > label_capacity)
        engine::fatal_error(pool_check_file.address(), pool_full_line, pool_full_message.address());
    const uint64_t index = vector->count++;
    Label *slot = reinterpret_cast<Label *>(first_slot(vector)) + index;
    slot->kind = source->kind;
    slot->word_0x08 = source->word_0x08;
    // (The original stores the kind and the word a second time after the text; the text copy
    // cannot change them, so the second store is left out.)
    copy_string(&slot->text, source->text);
}
