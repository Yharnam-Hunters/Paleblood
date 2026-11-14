/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Table snapshots taken when a buffer is submitted (runtime/table_snapshot.h). */
#include "runtime/table_snapshot.h"

#include "runtime/gcn_spirv.h"
#include "runtime/render_executor.h"

#include <stdlib.h>
#include <string.h>

_Static_assert(RT_TABLE_WORDS == GCN_TABLE_SNAPSHOT_WORDS + 1, "a dispatch's words hold the mask and the snapshot's words");

enum { PLAN_SLOTS = 2048, PLAN_PROBES = 32, POINTER_HIGH_MASK = 0xffff };

/* What the calls set on one queue, as the executor keeps it (render_executor.c, swap_compute_state). */
typedef struct {
    rt_compute_shader compute;
    int compute_set;
    uint32_t user[RT_USER_DATA_SLOTS], user_set;
} queue_state;

/* A shader's plan, by its code address: open addressing, each address probing PLAN_PROBES slots from its
   hash, so a plan is made once per shader (the executor keeps at most 512 compute pipelines). */
typedef struct {
    uint64_t code;
    int planned;                     /* 1: plan holds the program's; -1: the program does not decode */
    gcn_table_snapshot plan;
} plan_slot;

struct rt_table_snapshots {
    queue_state queues[RT_EXECUTOR_QUEUES];
    plan_slot plans[PLAN_SLOTS];
};

rt_table_snapshots *rt_table_snapshots_new(void) { return calloc(1, sizeof(rt_table_snapshots)); }

void rt_table_snapshots_free(rt_table_snapshots *s) { free(s); }

static int inside(const rt_table_memory *memory, uint64_t address, uint64_t bytes)
{
    return address >= memory->base && address - memory->base <= memory->bytes && bytes <= memory->bytes - (address - memory->base);
}

/* The plan of the program at code, or NULL (outside the memory, or code that does not decode). */
static const gcn_table_snapshot *plan_for(rt_table_snapshots *s, uint64_t code, const rt_table_memory *memory)
{
    if (code % 4 || !inside(memory, code, 4)) return NULL;
    const unsigned home = (unsigned)((code >> 8) * 0x9e3779b1u) % PLAN_SLOTS;
    plan_slot *slot = &s->plans[home];
    for (unsigned i = 0; i < PLAN_PROBES; i++) {
        plan_slot *probe = &s->plans[(home + i) % PLAN_SLOTS];
        if (probe->code == code || !probe->planned) {
            slot = probe;
            break;
        }
    }
    if (slot->code != code || !slot->planned) {
        char reason[256];
        const size_t dwords = (size_t)((memory->base + memory->bytes - code) / 4);
        slot->code = code;
        slot->planned = gcn_table_snapshot_plan((const uint32_t *)(uintptr_t)code, dwords, &slot->plan, reason, sizeof reason) ? -1 : 1;
    }
    return slot->planned > 0 ? &slot->plan : NULL;
}

/* A dispatch's mask and words (runtime/table_snapshot.h) from the queue's state. */
static void snapshot(rt_table_snapshots *s, const queue_state *q, const rt_table_memory *memory, uint32_t tables[RT_TABLE_WORDS])
{
    const gcn_table_snapshot *plan = q->compute_set ? plan_for(s, q->compute.code, memory) : NULL;
    if (!plan) return;
    for (unsigned k = 0; k < plan->entry_count; k++) {
        const gcn_table_entry *e = &plan->entries[k];
        if (e->pointer_sgpr + 2 > RT_USER_DATA_SLOTS || (~q->user_set >> e->pointer_sgpr & 3)) continue;
        const uint64_t pointer = (uint64_t)(q->user[e->pointer_sgpr + 1] & POINTER_HIGH_MASK) << 32 | q->user[e->pointer_sgpr];
        const uint64_t address = pointer + e->offset, bytes = (uint64_t)e->dwords * 4;
        if (address % 4 || !inside(memory, address, bytes)) continue;
        memcpy(&tables[1 + e->first_word], (const void *)(uintptr_t)address, bytes);
        tables[0] |= 1u << k;
    }
}

int rt_table_snapshots_take(rt_table_snapshots *s, unsigned queue, const rt_render_call *calls, size_t count, const rt_table_memory *memory,
                            rt_table_words *tables)
{
    if (!s) return -1;
    memset(tables, 0, count * sizeof *tables);
    queue_state *q = &s->queues[queue < RT_EXECUTOR_QUEUES ? queue : RT_EXECUTOR_GRAPHICS_QUEUE];
    for (size_t i = 0; i < count; i++) {
        const rt_render_call *c = &calls[i];
        if (c->kind == RT_CALL_COMPUTE_SHADER) {
            q->compute = c->u.compute_shader, q->compute_set = 1;
        } else if (c->kind == RT_CALL_USER_DATA && c->u.user_data.stage == RT_STAGE_COMPUTE && c->u.user_data.slot < RT_USER_DATA_SLOTS &&
                   c->u.user_data.count <= RT_USER_DATA_SLOTS - c->u.user_data.slot) {
            memcpy(&q->user[c->u.user_data.slot], c->u.user_data.words, c->u.user_data.count * sizeof *q->user);
            q->user_set |= (uint32_t)(((1ull << c->u.user_data.count) - 1) << c->u.user_data.slot);
        } else if (c->kind == RT_CALL_DISPATCH || c->kind == RT_CALL_DISPATCH_INDIRECT) {
            snapshot(s, q, memory, tables[i].words);
        }
    }
    return 0;
}
