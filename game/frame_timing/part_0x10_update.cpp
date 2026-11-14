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
#include <cstddef>
#include <cstdint>

#include "../engine/thread.h"
#include "runtime/original.h"

namespace {

struct Generator;   // opaque: only passed to its calls
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
