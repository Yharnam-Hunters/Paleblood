// SPDX-License-Identifier: GPL-2.0-or-later
// Frame timing: two helpers of the Chalice Dungeon gate (SprjDungeonGateIns) that
// fixed_step_states.cpp calls through their original addresses.
//   gate_state_query (0x01ea5570): whether the gate's state allows a ritual reset. It is the same
//     test the two ritual resets of fixed_step_states.cpp (0x01ea5670, 0x01ea5850) open with; the
//     method that waits for a dungeon mode asks it and ignores the answer.
//   step_machine_count_step (0x01ec3000): the gate's step machine counts a step; when the next
//     step would be past the gate's last one, it formats a debug message and passes it on.
#include <cstdint>

#include "../engine/allocator.h"
#include "../engine/engine.h"
#include "../engine/wide_string.h"
#include "dungeon_gate.h"
#include "runtime/original.h"

using namespace frame_timing::dungeon_gate;

namespace {

// The gate's steps: the step after the current one must be below this (the comparison is unsigned).
constexpr uint32_t gate_step_count = 27;

// The heap the message is built on: the default container heap, made on first use.
RT_GLOBAL(0x05940670, default_heap, engine::Allocator *);
RT_ORIGINAL(0x02fa2b10, make_default_heap, engine::Allocator *());
RT_GLOBAL(0x04b37dc0, message_incompatible_heap, const char);
constexpr int32_t line_incompatible_heap = 0x3e;

// Formats into a wide string (printf style, wide format); its result is not used here.
RT_ORIGINAL(0x02a295d0, wide_string_format, void(engine::WideString *out, const char16_t *format, ...));
// Formats a debug message from an id and a wide format (not analyzed further: the step counter
// passes its text as the format, with no arguments).
RT_ORIGINAL(0x01385b10, debug_message, void(uint32_t id, const char16_t *format, ...));
// The message's wide-character format in the image and the id it is passed with.
RT_GLOBAL(0x04dc0d20, next_step_missing_format, const char16_t);
constexpr uint32_t next_step_missing_message_id = 0x59682f01;

const char16_t *text_of(const engine::WideString &s)
{
    return s.capacity >= engine::wide_string_inline_capacity ? s.heap : s.inline_text;
}

// The heap the original builds its string on: made when missing, then checked (a heap that may
// not hold containers is reported, and used anyway).
engine::Allocator *message_heap()
{
    engine::Allocator *&heap = default_heap.get();
    if (!heap) heap = make_default_heap();
    engine::Allocator *const allocator = heap;
    engine::AllocatorInfo info{};
    allocator->vtable->info(&info, allocator, 0);
    if (!(info.flags & engine::allocator_info_valid))
        engine::fatal_error(nullptr, line_incompatible_heap, message_incompatible_heap.address());
    return allocator;
}

// Builds the message in an empty wide string on that heap, passes its text on, and frees the text
// if it went to the heap (with the allocator the string holds then).
void report_next_step_missing()
{
    engine::Allocator *const allocator = message_heap();
    engine::WideString message;
    message.unknown_0x00 = 0;  // the original leaves it as the stack had it; nothing here reads it
    message.allocator = allocator;
    message.capacity = engine::wide_string_inline_capacity - 1;
    message.size = 0;
    message.inline_text[0] = 0;
    message.owns = 1;
    wide_string_format(&message, next_step_missing_format.address());
    debug_message(next_step_missing_message_id, text_of(message));
    engine::wide_string_release(&message);
}

}  // namespace

// Whether the gate's state allows a ritual reset (ritual_reset_allowed in dungeon_gate.h). The
// original sets only al.
extern "C" bool bb_frame_timing_gate_state_query_01ea5570(const StateOwner *owner)
{
    return ritual_reset_allowed(owner);
}

// Counts a step; when the step after the current one is past the gate's last step, reports it with
// a debug message. The gate's steps call it before their method at vtable +0x38.
extern "C" void bb_frame_timing_step_machine_count_step_01ec3000(DungeonGate *gate)
{
    ++gate->steps_counted;
    if (gate->step + 1 < gate_step_count) return;
    report_next_step_missing();
}
