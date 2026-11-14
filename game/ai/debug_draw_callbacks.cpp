// SPDX-License-Identifier: GPL-2.0-or-later
// AI: two small debug-draw helpers the Havok AI frame update uses (hk_ai.h).
// - ai_debug_draw_axes_0135d810: draws an axis frame with the debug renderer. It takes the
//   renderer's lock (waiting without a time limit), lets the renderer prepare, draws the frame
//   into the current render slot (the slot index is read after the prepare call) and releases
//   the lock as its last call.
// - ai_draw_layer_color_02232a60: the colour callback of the AI draw descriptor. With no
//   requested bits it returns the default colour; otherwise it averages the palette colours of
//   the bits both masks set (bits 0-19), then sets alpha to one. When no bit is shared the
//   average divides zero by zero and every lane but alpha is NaN, as in the original.
#include <cstddef>
#include <cstdint>

#include "../engine/vector.h"
#include "../engine/vector_operations.h"
#include "hk_ai.h"
#include "runtime/original.h"

namespace ai {
namespace {

// The renderer's lock (+0x28): an object whose table has acquire at slot 3 and release at 5.
constexpr int lock_acquire_slot = 3, renderer_prepare_slot = 5;
struct DebugRendererLock;
struct DebugRendererLockVtable {
    void (*unknown_slots_0_2[lock_acquire_slot])();
    void (*acquire)(DebugRendererLock *, uint32_t timeout);
    void (*unknown_slot_4)();
    void (*release)(DebugRendererLock *);
};
static_assert(offsetof(DebugRendererLockVtable, acquire) == 0x18);
static_assert(offsetof(DebugRendererLockVtable, release) == 0x28);
struct DebugRendererLock {
    const DebugRendererLockVtable *vtable;
};

// DebugRenderer (hk_ai.h) with its table and lock: slot 5 of the table is called before every
// draw, its meaning is unknown.
struct LockedDebugRenderer;
struct LockedDebugRendererVtable {
    void (*unknown_slots_0_4[renderer_prepare_slot])();
    void (*prepare_draw)(LockedDebugRenderer *);
};
static_assert(offsetof(LockedDebugRendererVtable, prepare_draw) == 0x28);
struct LockedDebugRenderer {
    const LockedDebugRendererVtable *vtable;
    uint8_t unknown_0x08[0x8];
    RenderSlot *slots[render_slot_count];
    int32_t current_slot;
    DebugRendererLock lock;
};
static_assert(offsetof(LockedDebugRenderer, slots) == offsetof(DebugRenderer, slots));
static_assert(offsetof(LockedDebugRenderer, current_slot) == offsetof(DebugRenderer, current_slot));
static_assert(offsetof(LockedDebugRenderer, lock) == 0x28);

namespace ops = engine::vector_operations;

constexpr uint32_t wait_forever = 0xffffffff;

RT_ORIGINAL(0x0135ec80, render_slot_draw_axes, void(RenderSlot *, AxisFrame *));

// The draw colour palette: one colour per layer bit (read-only data).
constexpr int palette_size = 20;
RT_GLOBAL(0x04b35040, layer_palette, const Vec4[palette_size]);
RT_GLOBAL(0x04d17e50, default_layer_color, const Vec4);
RT_GLOBAL(0x04d17e60, all_ones, const Vec4);
RT_GLOBAL(0x04d28ff0, opaque_alpha_source, const Vec4);   // only its w (1.0) is used

}  // namespace
}  // namespace ai

using namespace ai;

extern "C" void bb_ai_debug_draw_axes_0135d810(DebugRenderer *shared, AxisFrame *frame)
{
    auto *renderer = reinterpret_cast<LockedDebugRenderer *>(shared);
    DebugRendererLock *lock = &renderer->lock;
    lock->vtable->acquire(lock, wait_forever);
    renderer->vtable->prepare_draw(renderer);
    render_slot_draw_axes(renderer->slots[renderer->current_slot], frame);
    lock->vtable->release(lock);
}

// The first argument is not read. Built for SSE4.1 so that the alpha blend is inlined: the
// original leaves the per-colour scale in xmm1, and a call to the blend would pass the alpha
// source there instead.
extern "C" __attribute__((target("sse4.1"))) Vec4 bb_ai_draw_layer_color_02232a60(void *, uint32_t requested,
                                                                                   uint32_t present)
{
    if (requested == 0)
        return default_layer_color.get();
    const uint32_t shared_layers = present & requested;
    const Vec4 *palette = layer_palette.get();
    Vec4 sum = Vec4::zero();
    int64_t count = 0;
    for (int layer = 0; layer < palette_size; layer++) {
        if ((shared_layers >> layer & 1) == 0)
            continue;
        sum = ops::add_in_order(sum, palette[layer]);
        count++;
    }
    const Vec4 scale = ops::divide(all_ones.get(), ops::broadcast(static_cast<float>(count)));
    return ops::multiply_in_order(sum, scale).with_w_of(opaque_alpha_source.get());
}
