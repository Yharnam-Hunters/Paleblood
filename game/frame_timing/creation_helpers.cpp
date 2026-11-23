// SPDX-License-Identifier: GPL-2.0-or-later
// Frame timing: the two helpers the Chalice Dungeon gate's handle creations call
// (fixed_step_states.cpp calls them through their original addresses).
// - frame_timing_fill_creation_part1_0231fbc0: packs a decimal number into one 32-bit word as four
//   two-digit groups, highest first, one per byte (12345678 packs as 12 << 24 | 34 << 16 |
//   56 << 8 | 78). The number is the source record's word at +0x18 when it is not negative; when it
//   is, it is built from the record's 16-bit value at +2 and its count at +5 (one less, at most the
//   caller's limit). With no record the word is left all ones. Returns the block.
// - frame_timing_create_handle_from_levels_02265580: when FrpgNetMan's four bytes allow it, makes
//   a gate handle (dungeon_gate.h) whose id is a stamp of the local date, adds it to the list of
//   FrpgNetMan's handle service, starts a new request of the service with the handle and the four
//   flag levels, and hands the caller a reference to the handle. Otherwise the caller gets null.
//   Returns the caller's slot.
#include <cstdint>

#include "../engine/allocator.h"
#include "../engine/engine.h"
#include "../engine/ref_object.h"
#include "dungeon_gate.h"
#include "runtime/original.h"

using namespace frame_timing::dungeon_gate;

namespace {

// ---- fill_creation_part1 ------------------------------------------------------------------------

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

// What the creation sets the handle's words at +0x110 and +0x114 to (their meaning is not known).
constexpr uint32_t handle_word_initial = 1;

// The date stamp the handle is made with (as its id): the top bit always set, below it the date packed by
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

// Starts the request with a reference to the handle and the levels (not replaced, not analyzed).
RT_ORIGINAL(0x0226a130, start_request,
            void(HandleRequest *request, engine::RefObject **handle, const FlagLevels *levels));

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
    FrpgNetMan *net = require_frpg_net_man();
    if (!net_allows(net)) {
        *out = nullptr;
        return out;
    }
    HandleService *const service = net->handle_service;
    const SessionId id{static_cast<int64_t>(date_stamp())};

    // A failed allocation is not checked past the constructor: the original then writes through null.
    void *const memory = gate_handle_heap.get()->allocate(sizeof(GateHandle), gate_handle_alignment);
    GateHandle *handle = static_cast<GateHandle *>(memory);
    if (handle) construct_gate_handle(handle, &id);
    handle->word_110 = handle_word_initial;
    handle->word_114 = handle_word_initial;
    append_gate_handle(&service->handles, &handle);

    // The request gets its own reference for the call; the one dropped after it is the one taken,
    // whatever the call leaves in the slot.
    HandleRequest *const request = make_handle_request(service);
    engine::RefObject *const taken = handle ? &handle->base : nullptr;
    engine::RefObject *passed = taken;
    if (taken) engine::retain(taken);
    start_request(request, &passed, levels);
    if (taken) engine::release(taken);

    *out = taken;
    if (taken) engine::retain(taken);
    return out;
}
