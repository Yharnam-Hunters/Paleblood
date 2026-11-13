// SPDX-License-Identifier: GPL-2.0-or-later
// Frame timing: two helpers of the Chalice Dungeon gate (SprjDungeonGateIns) that
// fixed_step_states.cpp calls through their original addresses.
//   gate_state_query (0x01ea5570): whether the gate's state allows a ritual reset. It is the same
//     test the two ritual resets of fixed_step_states.cpp (0x01ea5670, 0x01ea5850) open with; the
//     method that waits for a dungeon mode asks it and ignores the answer.
//   step_machine_count_step (0x01ec3000): the gate's step machine counts a step; when the next
//     step would be past the gate's last one, it formats a debug message and passes it on.
#include <cstddef>
#include <cstdint>

#include "../engine/allocator.h"
#include "../engine/engine.h"
#include "../engine/wide_string.h"
#include "runtime/original.h"

namespace {

// The two ritual modes of the gate's state (fixed_step_states.cpp's GateMode).
enum GateMode : uint32_t {
    mode_local_ritual = 2,
    mode_server_ritual = 3,
};

// The owner's handle, as far as the query reads it: bit 0 of the byte at +0x10c stops the reset in
// the server ritual mode.
struct HandleObject {
    uint8_t unknown_0x000[0x10c];
    uint8_t flags_10c;
};
static_assert(offsetof(HandleObject, flags_10c) == 0x10c);
constexpr uint8_t handle_blocks_reset = 1;

// The gate's state (SprjDungeonGateIns +0x108), as far as the query reads it.
struct StateOwner {
    uint8_t unknown_0x000[0xd0];
    const HandleObject *handle;
    uint8_t unknown_0x0d8[0x184];
    uint32_t state;  // a GateMode
    uint32_t request;
    int32_t busy;  // nonzero while the state is advancing
};
static_assert(offsetof(StateOwner, handle) == 0xd0 && offsetof(StateOwner, state) == 0x25c &&
              offsetof(StateOwner, busy) == 0x264);

// FrpgNetMan, the online manager: four of its bytes, each tested for zero (their meaning is not
// known; fixed_step_states.cpp reads the same four).
struct FrpgNetMan {
    uint8_t unknown_0x000[0x8];
    uint8_t flag_008;  // must be nonzero
    uint8_t unknown_0x009[0x9ed];
    uint8_t flag_9f6;  // must be zero
    uint8_t unknown_0x9f7[0x1];
    uint8_t flag_9f8;  // must be zero
    uint8_t unknown_0x9f9[0x57];
    uint8_t flag_a50;  // must be zero
};
static_assert(offsetof(FrpgNetMan, flag_008) == 0x8 && offsetof(FrpgNetMan, flag_9f6) == 0x9f6 &&
              offsetof(FrpgNetMan, flag_9f8) == 0x9f8 && offsetof(FrpgNetMan, flag_a50) == 0xa50);
RT_GLOBAL(0x0593b120, frpg_net_man_instance, FrpgNetMan *);
RT_GLOBAL(0x04d3ad45, frpg_net_man_name, const char);

// FrpgNetMan's four bytes, in the original's order. A missing FrpgNetMan is reported and the
// singleton read again (engine::require).
bool net_allows()
{
    const FrpgNetMan *net = engine::require(frpg_net_man_instance, frpg_net_man_name.address());
    return net->flag_008 != 0 && net->flag_9f8 == 0 && net->flag_9f6 == 0 && net->flag_a50 == 0;
}

// The gate's step machine (its base): the index of the current step, and a count of steps taken.
struct StepMachine {
    uint8_t unknown_0x00[0x50];
    uint32_t step;
    uint32_t steps_counted;  // what reads it is not known
};
static_assert(offsetof(StepMachine, step) == 0x50 && offsetof(StepMachine, steps_counted) == 0x54);
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

// Whether the gate's state allows a ritual reset: idle in the local ritual mode, or idle in the
// server ritual mode with FrpgNetMan's bytes allowing it and no handle or one whose flag bit 0 is
// clear. The original checks FrpgNetMan again before reading the handle, with the pointer the
// bytes were just read through (so it cannot fail), and sets only al.
extern "C" bool bb_frame_timing_gate_state_query_01ea5570(const StateOwner *owner)
{
    if (owner->busy != 0) return false;
    if (owner->state == mode_local_ritual) return true;
    if (owner->state != mode_server_ritual || !net_allows()) return false;
    return !owner->handle || (owner->handle->flags_10c & handle_blocks_reset) == 0;
}

// Counts a step; when the step after the current one is past the gate's last step, reports it with
// a debug message. The gate's steps call it before their method at vtable +0x38.
extern "C" void bb_frame_timing_step_machine_count_step_01ec3000(StepMachine *machine)
{
    ++machine->steps_counted;
    if (machine->step + 1 < gate_step_count) return;
    report_next_step_missing();
}
