// SPDX-License-Identifier: GPL-2.0-or-later
// Menu: copying the entries of the item dialogs' lists (the game's GaitemSelectDialog and
// GaitemStatusDialog, going by the source file names their code reports). Each entry holds two
// wide strings with a word before each; the status dialog's entry adds a shared, reference-counted
// object and a byte.
//
// The strings are copied as the engine's containers copy them: built empty on the source's
// allocator, then assigned all of the source's text (wide_string_assign.h), and given the
// source's owns byte. The first word of each string object is left as it was.
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "../engine/engine.h"
#include "../engine/wide_string_assign.h"

namespace {

using engine::WideString;

// An entry of the select dialog's list.
struct SelectEntry {
    int32_t key;
    uint8_t unknown_0x04[0x4];  // not copied
    uint64_t word_0x08;
    WideString first;
    uint64_t word_0x48;
    WideString second;
};
static_assert(offsetof(SelectEntry, first) == 0x10 && offsetof(SelectEntry, word_0x48) == 0x48);
static_assert(offsetof(SelectEntry, second) == 0x50 && sizeof(SelectEntry) == 0x88);

// The select dialog keeps its entries in a fixed pool (a Dantelion container): the slots start
// at the pool's address rounded up to 8, and the pool's count says how many are in use.
constexpr size_t select_pool_count_offset = 0x888;
constexpr uintptr_t slot_alignment = 8;

// The pool's out-of-range report (Dantelion's Core/Util header and its message).
RT_GLOBAL(0x04d39639, pool_check_file, const char);
RT_GLOBAL(0x04d40099, pool_check_message, const char);
constexpr int32_t pool_check_line = 0x27;

// An entry of the status dialog's list.
struct SharedObject {
    uint8_t unknown_0x00[0x8];
    int32_t references;
};
static_assert(offsetof(SharedObject, references) == 0x8);

struct StatusEntry {
    uint64_t word_0x00;
    WideString first;
    uint64_t word_0x40;
    WideString second;
    SharedObject *shared;  // its reference count goes up for the copy
    uint8_t byte_0x88;
};
static_assert(offsetof(StatusEntry, first) == 0x8 && offsetof(StatusEntry, word_0x40) == 0x40);
static_assert(offsetof(StatusEntry, second) == 0x48 && offsetof(StatusEntry, shared) == 0x80);
static_assert(offsetof(StatusEntry, byte_0x88) == 0x88 && sizeof(StatusEntry) == 0x90);

void copy_string(WideString *target, const WideString &source)
{
    engine::wide_string_begin(target, source.allocator);
    engine::wide_string_assign_from(target, &source, 0, engine::wide_string_rest);
    target->owns = source.owns;
}

}  // namespace

// Copies `source` into pool slot `slot` (null: nothing). A slot at or past the pool's in-use
// slots is reported, then copied into all the same; a slot before the pool is not checked.
extern "C" void bb_menu_gaitem_select_entry_copy_into_pool(const uint8_t *pool, SelectEntry *slot,
                                                           const SelectEntry *source)
{
    const uintptr_t first_slot = (reinterpret_cast<uintptr_t>(pool) + slot_alignment - 1) & ~(slot_alignment - 1);
    uint64_t in_use;
    std::memcpy(&in_use, pool + select_pool_count_offset, sizeof(in_use));
    const uintptr_t end = first_slot + sizeof(SelectEntry) * in_use;
    if (end <= reinterpret_cast<uintptr_t>(slot))
        engine::fatal_error(pool_check_file.address(), pool_check_line, pool_check_message.address());
    if (slot == nullptr) return;
    slot->key = source->key;
    slot->word_0x08 = source->word_0x08;
    copy_string(&slot->first, source->first);
    slot->word_0x48 = source->word_0x48;
    copy_string(&slot->second, source->second);
}

// Copies entry `index` of `entries` into `target`; returns `target`.
extern "C" StatusEntry *bb_menu_gaitem_status_entry_copy(StatusEntry *target, const StatusEntry *entries, int32_t index)
{
    const StatusEntry &source = entries[index];
    target->word_0x00 = source.word_0x00;
    copy_string(&target->first, source.first);
    target->word_0x40 = source.word_0x40;
    copy_string(&target->second, source.second);
    target->shared = source.shared;
    if (target->shared != nullptr)
        __atomic_add_fetch(&target->shared->references, 1, __ATOMIC_SEQ_CST);
    target->byte_0x88 = source.byte_0x88;
    return target;
}
