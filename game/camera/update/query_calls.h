// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "../../ai/hk_ai.h"
#include "../follow_camera.h"

namespace camera::query_calls
{

// SysV x86-64 assigns integer and SSE arguments independently. This spelling yields:
// RDI world; ESI filter; RDX origin; RCX displacement; XMM0.low radius;
// R8 optional position; R9 optional normal; caller [RSP] / callee [RBP+0x10] fraction.
// All vectors occupy 16 bytes. Origin and non-null vector outputs must be 16-aligned.
// The original returns only AL, and its miss path leaves all optional outputs untouched.
RT_ORIGINAL(0x01c090e0, sphere_query,
            uint8_t(QueryWorld *world, uint32_t filter, const Vec4 *origin, const Vec4 *displacement,
                    float radius, Vec4 *hit_position, Vec4 *hit_normal, float *hit_fraction));

constexpr uint32_t follow_camera_query_filter = 0x25; // only the consumed bits are named
static_assert(sizeof(Vec4) == 0x10 && alignof(Vec4) == 0x10);

// The original writes ESI=0xffffffff, which defines RSI=0x00000000ffffffff.
// The original parameter's C++ signedness/width cannot be recovered from that write alone.
// A uint64_t wire argument preserves the complete observed register value without assuming
// that the unknown target ignores its upper half. Do not pass signed int64_t(-1).
using DebugStateSlotCall = void (*)(ai::DebugRenderState *, uint64_t register_value);
inline constexpr size_t known_debug_slot_index = 5;
struct DebugStateVTable
{
    void (*unknown_slots[known_debug_slot_index])();
    DebugStateSlotCall call_0x28;
};
static_assert(sizeof(DebugStateSlotCall) == sizeof(uint64_t));
static_assert(offsetof(DebugStateVTable, call_0x28) == 0x28);
constexpr uint64_t debug_state_register_value = 0x00000000ffffffff;

// Existing AI layouts cover the shared prefix. Camera additionally reads/writes +0x70.
// This is a partial view; neither the complete class nor an enum meaning is asserted.
struct CameraDebugRenderState
{
    ai::DebugRenderState common;
    uint8_t unknown_0x40[0x30];
    uint32_t setting_0x70;
};
static_assert(sizeof(ai::DebugRenderState) == 0x40);
static_assert(offsetof(CameraDebugRenderState, common) == 0x0);
static_assert(offsetof(CameraDebugRenderState, setting_0x70) == 0x70);
static_assert(offsetof(ai::DebugRenderState, unknown_0x00) == 0x0);
static_assert(sizeof(ai::DebugRenderState::unknown_0x00) == sizeof(void *));
static_assert(offsetof(ai::DebugRenderState, dirty) == 0x08);
static_assert(offsetof(ai::DebugRenderState, setting_0x18) == 0x18);
static_assert(offsetof(ai::DebugRenderState, setting_0x1c) == 0x1c);
static_assert(offsetof(ai::DebugRenderState, vector_0x20) == 0x20);
static_assert(offsetof(ai::DebugRenderState, vector_0x30) == 0x30);
static_assert(offsetof(ai::RendMan, debug_renderer) == 0x20);
static_assert(offsetof(ai::DebugRenderer, slots) == 0x10);
static_assert(offsetof(ai::DebugRenderer, current_slot) == 0x20);
static_assert(offsetof(ai::RenderSlot, state) == 0x40);

// The existing current_slot is signed int32_t, matching MOVSXD at both original reads.
// This adds no null/bounds handling. The outer update must preserve its missing-RendMan
// diagnostic and singleton reload before dereferencing the manager, even if that call returns.
inline ai::DebugRenderState *selected_debug_state(ai::DebugRenderer *renderer)
{
    return renderer->slots[renderer->current_slot]->state;
}

inline void call_original_debug_state_slot(ai::DebugRenderState *state)
{
    const DebugStateVTable *vtable;
    // The existing AI prefix deliberately leaves its first eight bytes opaque.
    std::memcpy(&vtable, state->unknown_0x00, sizeof(vtable));
    vtable->call_0x28(state, debug_state_register_value);
}

struct TerritoryDebugSelection
{
    ai::DebugRenderer *renderer;
    ai::DebugRenderState *captured_state;
    CameraDebugRenderState *reselected_state;
};

// Preserve the original's two identities if the virtual call changes the selected slot or
// its state. Setting+0x70/dirty0x80 use the reselected state; the later vector/settings writes
// still use captured_state. Renderer itself is captured before the call and is not reloaded.
inline TerritoryDebugSelection begin_territory_debug(ai::RendMan *manager)
{
    ai::DebugRenderer *renderer = manager->debug_renderer;
    ai::DebugRenderState *captured = selected_debug_state(renderer);
    call_original_debug_state_slot(captured);
    ai::DebugRenderState *reselected = selected_debug_state(renderer);
    return {renderer, captured, reinterpret_cast<CameraDebugRenderState *>(reselected)};
}

constexpr uint32_t camera_setting_0x70 = 2;
constexpr uint8_t camera_setting_0x70_dirty = 0x80;
constexpr uint32_t camera_setting_0x1c = 1;
constexpr uint32_t camera_setting_0x18 = 2;

} // namespace camera::query_calls
