/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef RUNTIME_RENDER_OBSERVER_HOOKS_H
#define RUNTIME_RENDER_OBSERVER_HOOKS_H

/* Observer calls that only the GPU library's observe hooks make (game/render/observe_gpu_library.c),
   as runtime/render_observer.h describes them: the record's arguments without the error buffer.
   They live apart from runtime/render_observer.h, which the library's replacements include, so that
   adding one leaves the replacements' sources (and their verification records) unchanged. */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* DRAW_INDEX_2 as written: its header, MAX_SIZE, index address (INDEX_BASE_LO and _HI), INDEX_COUNT
   and DRAW_INITIATOR (rt_render_record_draw_indexed). */
void rt_observe_draw_indexed(uint64_t buffer, uint32_t offset, uint32_t header, uint32_t max_size, uint64_t address, uint32_t count,
                             uint32_t initiator);

#ifdef __cplusplus
}
#endif

#endif
