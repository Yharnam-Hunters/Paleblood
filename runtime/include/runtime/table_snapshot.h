/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef RUNTIME_TABLE_SNAPSHOT_H
#define RUNTIME_TABLE_SNAPSHOT_H

/* Table snapshots taken when a buffer is submitted (runtime/renderer/table_snapshot.c; the shader's
   side is gcn_spirv.h, "Table snapshots").

   The game hands some compute shaders a pointer to a 16-byte table in a per-frame ring and writes
   that ring slot again soon after it submits the buffer: the console's GPU starts on a buffer soon
   after its submission, so it has read the table by then. The renderer's thread records a buffer
   up to RENDER_LAG_LIMIT buffers later (docs/RENDERER.md), by which time the slot may already hold
   another table's words (floats, read as a descriptor, put the LUT pass at 0x20d1b00800 outside the
   guest memory about one clinic boot in six). So each dispatch's tables are copied on the
   submitting thread, from the compute shader and user data the calls before it set on its queue:
   the state the executor keeps across buffers, followed here the same way. The executor takes them
   with the calls (rt_executor_record_calls_with_tables).

   rt_table_snapshots_take fills tables[i] for call i of a buffer submitted to `queue`
   (RT_EXECUTOR_GRAPHICS_QUEUE or a compute queue's id): for an RT_CALL_DISPATCH or
   RT_CALL_DISPATCH_INDIRECT, word 0 a mask, bit k for entry k of the shader's plan
   (gcn_table_snapshot_plan), entry k's words from word 1 + first_word; all 0 for other calls. An entry
   whose pointer the calls have not set, or whose table or the shader's code lies outside `memory`, is
   left out. The buffers of all queues go through one tracker in submission order. Returns 0, or -1
   when there is no tracker. */

#include "runtime/render_calls.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint64_t base, bytes; /* the guest memory the GPU reaches: the tables and code are read there in place */
} rt_table_memory;

enum { RT_TABLE_WORDS = 16 }; /* a dispatch's mask and up to 15 words */
typedef struct {
    uint32_t words[RT_TABLE_WORDS];
} rt_table_words;

typedef struct rt_table_snapshots rt_table_snapshots;

rt_table_snapshots *rt_table_snapshots_new(void);
void rt_table_snapshots_free(rt_table_snapshots *s);
int rt_table_snapshots_take(rt_table_snapshots *s, unsigned queue, const rt_render_call *calls, size_t count, const rt_table_memory *memory,
                            rt_table_words *tables);

#ifdef __cplusplus
}
#endif

#endif
