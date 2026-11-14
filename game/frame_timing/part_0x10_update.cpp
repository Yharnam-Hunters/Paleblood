// SPDX-License-Identifier: GPL-2.0-or-later
// Frame timing: the update of the per-frame task's part at +0x10 (game/frame_timing/task_013d3520.cpp).
//
// frame_timing_part_0x10_update_013d5440 (0x013d5440) does nothing unless the seconds are above 0
// (so not for 0, negatives or NaN). Otherwise it keeps the seconds (+0x144), steps its source
// (+0x18, when present) with them, and records whether its generator (+0x10) is idle (+0x140,
// set when the generator's flag is clear; an idle generator is also refreshed). Then:
// - with the output switch (+0x30) on, it marks the output used (+0x141), asks the output
//   (+0x28, virtual slot 9) for a count n, sizes the generator's buffer for 2n + 1 entries,
//   allocates that buffer from the thread's allocator and keeps it (+0x138), has the generator
//   fill it for the target (+0x20) and the seconds, and hands the target to the output (virtual
//   slot 5, mode 0xf) as a tail call. The buffer is not freed here.
// - with the switch off, it sizes the generator's buffer for one entry, allocates it, has the
//   generator step into it with the seconds and frees it again (a tail call; the thread's
//   allocator is read again for the free).
//
// Three of the generator's calls are here too (the generator's layout below):
// - frame_timing_generator_buffer_size_00b68770: the buffer size for a number of entries, from
//   the generator's layout (0x00b685f0, which also reports a flag that is not used).
// - frame_timing_generator_fill_00b69450: first clears the pending flag of every kind-1 node
//   each binding's group lists (the count read once, the group's list read again per node), then
//   fills the buffer (0x00b694d0, a tail call with the same arguments).
// - frame_timing_generator_refresh_00b6a030: for each binding, hands each record of its group's
//   two record lists to the binding's object the record names: the short records (16 bytes) to
//   slot 7, the long ones (32 bytes) to slot 5, with the record's data after its index. Every
//   count and list is read again on each step.
#include <cstddef>
#include <cstdint>

#include "../engine/thread.h"
#include "runtime/original.h"

namespace {

// A node of a binding set: the fill clears the pending flag of kind 1 nodes.
struct GeneratorNode {
    uint8_t unknown_0x00[0x18];
    int32_t kind;
    uint8_t unknown_0x1c[0x1c];
    uint8_t pending;
};
static_assert(offsetof(GeneratorNode, kind) == 0x18 && offsetof(GeneratorNode, pending) == 0x38);
constexpr int32_t node_kind_cleared_on_fill = 1;

// The records a group hands to the binding's objects: an index, then the data passed on.
constexpr size_t short_record_data_size = 12;
constexpr size_t long_record_data_size = 24;
struct ShortRecord {
    int32_t index;
    uint8_t data[short_record_data_size];
};
static_assert(sizeof(ShortRecord) == 0x10);
struct LongRecord {
    int32_t index;
    uint8_t unknown_0x04[0x4];
    uint8_t data[long_record_data_size];
};
static_assert(offsetof(LongRecord, data) == 0x8 && sizeof(LongRecord) == 0x20);

struct GeneratorGroup {
    uint8_t unknown_0x00[0x18];
    int32_t *node_ids;
    int32_t node_count;
    uint8_t unknown_0x24[0x4];
    ShortRecord *short_records;
    int32_t short_count;
    uint8_t unknown_0x34[0x4];
    LongRecord *long_records;
    int32_t long_count;
};
static_assert(offsetof(GeneratorGroup, node_ids) == 0x18 && offsetof(GeneratorGroup, node_count) == 0x20);
static_assert(offsetof(GeneratorGroup, short_records) == 0x28 && offsetof(GeneratorGroup, short_count) == 0x30);
static_assert(offsetof(GeneratorGroup, long_records) == 0x38 && offsetof(GeneratorGroup, long_count) == 0x40);

struct BindingSet {
    uint8_t unknown_0x00[0x48];
    GeneratorNode **nodes;
    uint8_t unknown_0x50[0x8];
    GeneratorGroup **groups;
};
static_assert(offsetof(BindingSet, nodes) == 0x48 && offsetof(BindingSet, groups) == 0x58);

// The objects a binding hands records to: slot 5 takes long records' data, slot 7 short ones'.
struct RecordTarget;
struct RecordTargetVtable {
    void *unknown_slots_0[5];
    void (*take_long)(RecordTarget *, const uint8_t *data);
    void *unknown_slot_6;
    void (*take_short)(RecordTarget *, const uint8_t *data);
};
static_assert(offsetof(RecordTargetVtable, take_long) == 0x28 && offsetof(RecordTargetVtable, take_short) == 0x38);
struct RecordTarget {
    const RecordTargetVtable *vtable;
};

struct GeneratorBinding {
    uint8_t unknown_0x00[0x10];
    BindingSet *set;
    int32_t group_index;
    uint8_t unknown_0x1c[0x14];
    RecordTarget **short_targets;
    uint8_t unknown_0x38[0x8];
    RecordTarget **long_targets;
};
static_assert(offsetof(GeneratorBinding, set) == 0x10 && offsetof(GeneratorBinding, group_index) == 0x18);
static_assert(offsetof(GeneratorBinding, short_targets) == 0x30 && offsetof(GeneratorBinding, long_targets) == 0x40);

struct Generator {
    uint8_t unknown_0x00[0x10];
    GeneratorBinding **bindings;
    int32_t binding_count;
};
static_assert(offsetof(Generator, bindings) == 0x10 && offsetof(Generator, binding_count) == 0x18);

GeneratorGroup *group_of(const GeneratorBinding *binding)
{
    return binding->set->groups[binding->group_index];
}
struct Source;      // opaque: only passed on
struct Target;      // opaque: only passed on

struct Output;
struct OutputVtable {
    void *unknown_slots_0[5];
    void (*use_target)(Output *, Target *, int32_t mode);
    void *unknown_slots_6[3];
    int32_t (*count)(Output *);
};
static_assert(offsetof(OutputVtable, use_target) == 0x28 && offsetof(OutputVtable, count) == 0x48);
struct Output {
    const OutputVtable *vtable;
};
constexpr int32_t output_mode = 0xf;

struct Part {
    uint8_t unknown_0x000[0x10];
    Generator *generator;
    Source *source;
    Target *target;
    Output *output;
    uint8_t output_enabled;
    uint8_t unknown_0x031[0x107];
    void *output_buffer;
    uint8_t generator_idle;
    uint8_t output_used;
    uint8_t unknown_0x142[0x2];
    float seconds;
};
static_assert(offsetof(Part, generator) == 0x10 && offsetof(Part, source) == 0x18 && offsetof(Part, target) == 0x20);
static_assert(offsetof(Part, output) == 0x28 && offsetof(Part, output_enabled) == 0x30);
static_assert(offsetof(Part, output_buffer) == 0x138 && offsetof(Part, generator_idle) == 0x140);
static_assert(offsetof(Part, output_used) == 0x141 && offsetof(Part, seconds) == 0x144);

// The thread's allocator: thread-local, read again on each use as the original does.
struct ThreadAllocator;
struct ThreadAllocatorHolder {
    uint8_t unknown_0x00[0x58];
    ThreadAllocator *allocator;
};
static_assert(offsetof(ThreadAllocatorHolder, allocator) == 0x58);
RT_GLOBAL(0x057e40f8, thread_allocator_offset, const int64_t);

// The thread's slot that holds the allocator holder (the offset is read once per call).
ThreadAllocatorHolder **allocator_slot()
{
    return reinterpret_cast<ThreadAllocatorHolder **>(engine::thread_pointer() + thread_allocator_offset.get());
}

// Allocates a block with a 16-byte header in front of it (the allocator and the size).
RT_ORIGINAL(0x00829a40, allocator_allocate_headed, void *(ThreadAllocator *, uint32_t size));
// Frees such a block.
RT_ORIGINAL(0x00829a90, allocator_free_headed, void(ThreadAllocator *, void *data));

RT_ORIGINAL(0x01576450, source_step, void(Source *, float seconds));
RT_ORIGINAL(0x00b6a020, generator_flag, uint8_t(Generator *));
RT_ORIGINAL(0x00b6a030, generator_refresh, void(Generator *));
RT_ORIGINAL(0x00b68770, generator_buffer_size, uint32_t(Generator *, int32_t entries));
RT_ORIGINAL(0x00b69450, generator_fill, void(Generator *, Target *, int32_t entries, void *buffer, float seconds));
RT_ORIGINAL(0x00b69190, generator_step, void(Generator *, void *buffer, float seconds));
// The generator's buffer layout for a number of entries: a flag (not used here) and the size.
RT_ORIGINAL(0x00b685f0, generator_layout, void(Generator *, int32_t entries, uint8_t *flag, uint32_t *size));
// The fill proper, after the nodes' pending flags are cleared.
RT_ORIGINAL(0x00b694d0, generator_fill_entries,
            void(Generator *, Target *, int32_t entries, void *buffer, float seconds));

constexpr int32_t single_entry = 1;
constexpr uint32_t entries_per_output = 2;

}  // namespace

extern "C" void bb_frame_timing_part_0x10_update_013d5440(Part *part, float seconds)
{
    if (!(seconds > 0.0f)) return;
    part->seconds = seconds;
    if (part->source) source_step(part->source, seconds);
    const bool idle = generator_flag(part->generator) == 0;
    part->generator_idle = idle;
    if (idle) generator_refresh(part->generator);

    if (part->output_enabled) {
        part->output_used = 1;
        // 2n + 1, wrapping as the original's 32-bit arithmetic does.
        const auto count = static_cast<uint32_t>(part->output->vtable->count(part->output));
        const auto entries = static_cast<int32_t>(entries_per_output * count + 1);
        const uint32_t size = generator_buffer_size(part->generator, entries);
        void *buffer = allocator_allocate_headed((*allocator_slot())->allocator, size);
        part->output_buffer = buffer;
        generator_fill(part->generator, part->target, entries, buffer, seconds);
        part->output->vtable->use_target(part->output, part->target, output_mode);
        return;
    }
    const uint32_t size = generator_buffer_size(part->generator, single_entry);
    ThreadAllocatorHolder **slot = allocator_slot();
    void *buffer = allocator_allocate_headed((*slot)->allocator, size);
    generator_step(part->generator, buffer, seconds);
    allocator_free_headed((*slot)->allocator, buffer);
}

extern "C" uint32_t bb_frame_timing_generator_buffer_size_00b68770(Generator *generator, int32_t entries)
{
    uint8_t flag = 0;
    uint32_t size = 0;   // always written by the layout
    generator_layout(generator, entries, &flag, &size);
    return size;
}

extern "C" void bb_frame_timing_generator_fill_00b69450(Generator *generator, Target *target, int32_t entries,
                                                       void *buffer, float seconds)
{
    const int32_t binding_count = generator->binding_count;
    for (int32_t i = 0; i < binding_count; ++i) {
        const GeneratorBinding *binding = generator->bindings[i];
        BindingSet *set = binding->set;
        const GeneratorGroup *group = set->groups[binding->group_index];
        for (int32_t n = 0; n < group->node_count; ++n) {
            GeneratorNode *node = set->nodes[group->node_ids[n]];
            if (node->kind == node_kind_cleared_on_fill) node->pending = 0;
        }
    }
    generator_fill_entries(generator, target, entries, buffer, seconds);
}

extern "C" void bb_frame_timing_generator_refresh_00b6a030(Generator *generator)
{
    for (int32_t i = 0; i < generator->binding_count; ++i) {
        const GeneratorBinding *binding = generator->bindings[i];
        const GeneratorGroup *group = group_of(binding);
        for (int32_t r = 0; r < group->short_count; ++r) {
            const ShortRecord &record = group->short_records[r];
            RecordTarget *target = binding->short_targets[record.index];
            target->vtable->take_short(target, record.data);
        }
        for (int32_t r = 0; r < group->long_count; ++r) {
            const LongRecord &record = group->long_records[r];
            RecordTarget *target = binding->long_targets[record.index];
            target->vtable->take_long(target, record.data);
        }
    }
}
