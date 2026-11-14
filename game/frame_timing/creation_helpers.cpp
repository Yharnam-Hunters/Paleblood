// SPDX-License-Identifier: GPL-2.0-or-later
// Frame timing: the two helpers the Chalice Dungeon gate's handle creations call
// (fixed_step_states.cpp declares them as originals; it is left as it is).
// - frame_timing_fill_creation_part1_0231fbc0: packs a decimal number into one 32-bit word as four
//   two-digit groups, highest first, one per byte (12345678 packs as 12 << 24 | 34 << 16 |
//   56 << 8 | 78). The number is the source record's word at +0x18 when it is not negative; when it
//   is, it is built from the record's 16-bit value at +2 and its count at +5 (one less, at most the
//   caller's limit). With no record the word is left all ones. Returns the block.
// - frame_timing_create_handle_from_levels_02265580: when FrpgNetMan's four bytes allow it, makes
//   a handle object (0x130 bytes on the heap at 0x05940438) stamped with the local date, adds it to
//   the list of FrpgNetMan's object at +0xca8, starts a new entry of that object with the handle and
//   the four flag levels, and hands the caller a reference to it. Otherwise the caller gets null.
//   Returns the caller's slot.
#include <cstddef>
#include <cstdint>

#include "../engine/allocator.h"
#include "../engine/engine.h"
#include "../engine/ref_object.h"
#include "runtime/original.h"

namespace {

// ---- fill_creation_part1 ------------------------------------------------------------------------

// The record the source points at: a number at +0x18, used as it is when not negative, and the two
// values a negative one is replaced with. What they stand for is not known.
struct CreationRecord {
    uint8_t unknown_0x000[0x2];
    uint16_t value_002;
    uint8_t unknown_0x004[0x1];
    uint8_t count_005;
    uint8_t unknown_0x006[0x12];
    int32_t number;
};
static_assert(offsetof(CreationRecord, value_002) == 0x2 && offsetof(CreationRecord, count_005) == 0x5 &&
              offsetof(CreationRecord, number) == 0x18);

struct CreationSource {
    uint8_t unknown_0x000[0x8];
    const CreationRecord *record;  // may be null
};
static_assert(offsetof(CreationSource, record) == 0x8);

// The block filled: one 32-bit word (the callers' slot is larger; nothing past the word is written).
struct CreationPart1 {
    uint32_t packed;
};

constexpr uint32_t no_record_packed = 0xffffffff;
// A negative number is replaced with base + value_002 * scale + (count_005 - 1, at most the limit).
constexpr uint32_t replaced_number_base = 29000000, value_002_scale = 1000;
// The four two-digit groups, highest first, each in its own byte.
constexpr uint32_t decimal_group = 100;
constexpr uint32_t group_divisors[] = {1000000, 10000, 100, 1};
constexpr uint32_t bits_per_byte = 8, group_count = sizeof group_divisors / sizeof group_divisors[0];

// count_005 less one (0 for a count of 0 or 1), at most the limit (both unsigned).
uint32_t count_index(const CreationRecord *record, uint32_t limit)
{
    const uint32_t count = record->count_005;
    const uint32_t index = count > 1 ? count - 1 : 0;
    return index < limit ? index : limit;
}

uint32_t pack_decimal_groups(uint32_t number)
{
    uint32_t packed = 0;
    for (uint32_t group = 0; group < group_count; ++group) {
        const uint32_t shift = (group_count - 1 - group) * bits_per_byte;
        packed |= (number / group_divisors[group] % decimal_group) << shift;
    }
    return packed;
}

// ---- create_handle_from_levels ------------------------------------------------------------------

// FrpgNetMan, the online manager: the four bytes that gate the creation (as fixed_step_states.cpp
// reads them; their meaning is not known) and the object whose list and entries it uses.
struct NetHandles;
struct FrpgNetMan {
    uint8_t unknown_0x000[0x8];
    uint8_t flag_008;  // must be nonzero
    uint8_t unknown_0x009[0x9ed];
    uint8_t flag_9f6;  // must be zero
    uint8_t unknown_0x9f7[0x1];
    uint8_t flag_9f8;  // must be zero
    uint8_t unknown_0x9f9[0x57];
    uint8_t flag_a50;  // must be zero
    uint8_t unknown_0xa51[0x257];
    NetHandles *handles;
};
static_assert(offsetof(FrpgNetMan, flag_008) == 0x8 && offsetof(FrpgNetMan, flag_9f6) == 0x9f6 &&
              offsetof(FrpgNetMan, flag_9f8) == 0x9f8 && offsetof(FrpgNetMan, flag_a50) == 0xa50 &&
              offsetof(FrpgNetMan, handles) == 0xca8);
RT_GLOBAL(0x0593b120, frpg_net_man_instance, FrpgNetMan *);
RT_GLOBAL(0x04d3ad45, frpg_net_man_name, const char);

// FrpgNetMan's object at +0xca8: a list of handle pointers at +0x50 (a vector: begin, end and
// capacity from its +0x8).
struct LevelHandle;
struct HandleList {
    uint8_t unknown_0x00[0x8];
    LevelHandle **begin;
    LevelHandle **end;
    LevelHandle **capacity;
};
struct NetHandles {
    uint8_t unknown_0x000[0x50];
    HandleList list;
};
static_assert(offsetof(HandleList, begin) == 0x8 && offsetof(HandleList, capacity) == 0x18 &&
              offsetof(NetHandles, list) == 0x50);

// The four flag levels (fixed_step_states.cpp reads them); passed on as they are.
constexpr size_t level_columns = 4;
struct FlagLevels {
    int32_t level[level_columns];
};

// The handle object: reference counted, with two words at +0x110 the creation sets to 1 (their
// meaning is not known).
struct LevelHandle {
    engine::RefObject base;
    uint8_t unknown_0x010[0x100];
    uint32_t word_110;
    uint32_t word_114;
    uint8_t unknown_0x118[0x18];
};
static_assert(offsetof(LevelHandle, word_110) == 0x110 && sizeof(LevelHandle) == 0x130);
constexpr uint64_t handle_alignment = 8;
constexpr uint32_t handle_word_initial = 1;
RT_GLOBAL(0x05940438, handle_heap, engine::Allocator *);

// The date stamp the handle is made with: the top bit always set, below it the date packed by
// 0x024ee200 when the local clock could be read.
constexpr uint64_t stamp_flag = 0x8000000000000000;
// The date and time the clock fills (16 bytes, not read here) and the 16 bytes the packing fills
// (only the first 8 are used); the original zeroes the packing's block before the call, which reads it.
constexpr size_t rtc_date_time_bytes = 0x10;
struct RtcDateTime {
    uint8_t bytes[rtc_date_time_bytes];
};
struct PackedDate {
    uint64_t low;
    uint64_t high;
};
RT_ORIGINAL(0x02fc0908, rtc_get_current_clock_local_time, int32_t(RtcDateTime *now));
RT_ORIGINAL(0x02fbf7e8, rtc_get_tick, int32_t(const RtcDateTime *time, uint64_t *tick));
// Packs a tick into a date; false when the tick has no date in range (not replaced).
RT_ORIGINAL(0x024ee200, pack_date, bool(PackedDate *out, uint64_t tick));

// The handle's constructor (not replaced): it keeps the stamp at +0x10 and zeroes its count.
RT_ORIGINAL(0x02267200, construct_level_handle, void(LevelHandle *handle, const uint64_t *stamp));
// Appends the handle pointer to the list (a vector of pointers; not replaced).
RT_ORIGINAL(0x02266b50, append_handle, void(HandleList *list, LevelHandle *const *handle));
// Makes a new entry of FrpgNetMan's object (0xe0 bytes; not replaced, not analyzed further).
RT_ORIGINAL(0x02264640, make_handles_entry, void *(NetHandles *handles));
// Starts the entry with a reference to the handle and the levels (not replaced, not analyzed).
RT_ORIGINAL(0x0226a130, start_entry, void(void *entry, engine::RefObject **handle, const FlagLevels *levels));

bool net_allows(const FrpgNetMan *net)
{
    return net->flag_008 != 0 && net->flag_9f8 == 0 && net->flag_9f6 == 0 && net->flag_a50 == 0;
}

// The local date's stamp: only the flag when the clock fails, the tick is 0 or the packing fails.
// A tick the clock call leaves unwritten counts as 0 (the original would read its stack).
uint64_t date_stamp()
{
    RtcDateTime now;
    if (rtc_get_current_clock_local_time(&now) != 0) return stamp_flag;
    uint64_t tick = 0;
    rtc_get_tick(&now, &tick);
    PackedDate packed{};
    if (tick == 0 || !pack_date(&packed, tick)) return stamp_flag;
    return stamp_flag | packed.low;
}

}  // namespace

extern "C" CreationPart1 *bb_frame_timing_fill_creation_part1_0231fbc0(CreationPart1 *out, const CreationSource *source,
                                                                        uint32_t limit)
{
    out->packed = no_record_packed;
    const CreationRecord *record = source->record;
    if (!record) return out;
    uint32_t number = static_cast<uint32_t>(record->number);
    if (record->number < 0)
        number = replaced_number_base + record->value_002 * value_002_scale + count_index(record, limit);
    out->packed = pack_decimal_groups(number);
    return out;
}

extern "C" engine::RefObject **bb_frame_timing_create_handle_from_levels_02265580(engine::RefObject **out,
                                                                                  const FlagLevels *levels)
{
    FrpgNetMan *net = engine::require(frpg_net_man_instance, frpg_net_man_name.address());
    if (!net_allows(net)) {
        *out = nullptr;
        return out;
    }
    NetHandles *const handles = net->handles;
    const uint64_t stamp = date_stamp();

    // A failed allocation is not checked past the constructor: the original then writes through null.
    LevelHandle *handle = static_cast<LevelHandle *>(handle_heap.get()->allocate(sizeof(LevelHandle), handle_alignment));
    if (handle) construct_level_handle(handle, &stamp);
    handle->word_110 = handle_word_initial;
    handle->word_114 = handle_word_initial;
    append_handle(&handles->list, &handle);

    // The entry gets its own reference for the call; the one dropped after it is the one taken,
    // whatever the call leaves in the slot.
    void *const entry = make_handles_entry(handles);
    engine::RefObject *const taken = handle ? &handle->base : nullptr;
    engine::RefObject *passed = taken;
    if (taken) engine::retain(taken);
    start_entry(entry, &passed, levels);
    if (taken) engine::release(taken);

    engine::RefObject *const result = handle ? &handle->base : nullptr;
    *out = result;
    if (result) engine::retain(result);
    return out;
}
