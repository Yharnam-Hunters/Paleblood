/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef RUNTIME_RENDER_OBSERVER_H
#define RUNTIME_RENDER_OBSERVER_H

/* The renderer's observer calls as the game library's replacements make them (docs/RENDERER.md,
   "How the hooks attach"): each replacement of a GPU library function writes its packets as the
   original does and also tells the renderer what it set. The arguments are those of the record
   (runtime/render_calls.h) without the error buffer: buffer is the command buffer's start, offset the
   bytes it held before the call.

   The record lives in the boot harness (pbboot), which exports it; the game library finds it there
   the first time it is needed. Under a host without it (the game-run scaffold) every call does
   nothing, so the replacements behave as before. A call the record refuses stops the process with
   the reason (the host's rt_fatal). */

#include "runtime/render_calls.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Whether the host has the renderer's record: 1 when observer calls reach it. */
int rt_render_observer_available(void);

void rt_observe_color_target(uint64_t buffer, uint32_t offset, unsigned slot,
                             const uint32_t words[RT_COLOR_TARGET_WORDS] /* NULL: slot off */);
void rt_observe_viewport(uint64_t buffer, uint32_t offset, unsigned index, float zmin, float zmax, const float scale[3],
                         const float translate[3]);
void rt_observe_draw_auto(uint64_t buffer, uint32_t offset, uint32_t index_count);
void rt_observe_vertex_shader(uint64_t buffer, uint32_t offset, const uint32_t words[RT_VERTEX_SHADER_WORDS]);
void rt_observe_pixel_shader(uint64_t buffer, uint32_t offset, const uint32_t words[RT_PIXEL_SHADER_WORDS]);
void rt_observe_user_data(uint64_t buffer, uint32_t offset, rt_shader_stage stage, unsigned slot, const uint32_t *words,
                          unsigned count);
void rt_observe_register(uint64_t buffer, uint32_t offset, uint32_t address, uint32_t value);
void rt_observe_instances(uint64_t buffer, uint32_t offset, uint32_t count);
void rt_observe_dispatch(uint64_t buffer, uint32_t offset, const uint32_t groups[3]);
void rt_observe_index_base(uint64_t buffer, uint32_t offset, uint64_t address);
void rt_observe_index_size(uint64_t buffer, uint32_t offset, uint32_t count);
void rt_observe_index_type(uint64_t buffer, uint32_t offset, uint32_t type);
void rt_observe_draw_index_offset(uint64_t buffer, uint32_t offset, uint32_t first, uint32_t count);
/* Final packet fields from replacement writers, after their last cursor publication. */
void rt_observe_draw_index_offset_packet(uint64_t buffer, uint32_t offset, uint32_t header, uint32_t max_size,
                                         uint32_t first, uint32_t count, uint32_t initiator);
void rt_observe_draw_indexed(uint64_t buffer, uint32_t offset, uint32_t header, uint32_t max_size,
                              uint64_t address, uint32_t count, uint32_t initiator);

#ifdef __cplusplus
}
#endif

#endif
