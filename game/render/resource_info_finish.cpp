// SPDX-License-Identifier: GPL-2.0-or-later
// Render: the two steps that end swap-chain resource information processing, called as
// originals by prepare_swap_chain_resource_info (swap_chain_resource_info.h).
// - render_resource_info_write_completion_label_02ab1930: takes the next label value from the
//   services' shared counter (taking one more when it equals the reserved value) and writes a
//   release-memory packet that stores it, as 32-bit data, at the label address once compute work
//   is done. Returns what the packet writer returns.
// - render_resource_info_submit_pending_commands_02ab18c0: submits the packets written since the
//   buffer's start (their byte count from the low 32 bits of the two addresses, whole words only)
//   to the processor's submission queue as one range, whatever the queue answers, and then
//   starts the buffer again at the cursor.
#include <cstddef>
#include <cstdint>

#include "swap_chain_resource_info.h"

namespace {

using render::PacketBuffer;
using render::ResourceInfoProcessor;
using render::ResourceInfoProcessorServices;

// The queue sits after the fields this header models; only its address is passed on.
struct SubmissionQueue;
struct ProcessorWithQueue {
    ResourceInfoProcessor processor;
    uint8_t unknown_48[0x10];
    uint8_t queue;
};
static_assert(offsetof(ProcessorWithQueue, queue) == 0x58);

RT_ORIGINAL(0x0247ebd0, atomic_fetch_increment, uint32_t(uint32_t *));
// Writes one release-memory packet: event, destination flags, address, data selection, data,
// cache action and cache policy (the last two on the stack).
RT_ORIGINAL(0x0148b0f0, write_release_memory,
            uintptr_t(PacketBuffer *, uint32_t event, uint32_t destination_flags, uint64_t address,
                      uint32_t data_selection, uint64_t data, uint32_t cache_action, uint32_t cache_policy));
// Queues `count` command ranges (addresses and byte sizes) on a compute ring and rings its doorbell.
RT_ORIGINAL(0x0148c830, submit_command_ranges,
            uint32_t(SubmissionQueue *, uint32_t count, const uintptr_t *addresses, const uint32_t *bytes));

constexpr uint32_t event_compute_done = 0x2f;
constexpr uint32_t destination_memory = 0;
constexpr uint32_t data_low_32_bits = 1;
constexpr uint32_t cache_action_write_back = 0x38;
constexpr uint32_t cache_policy_default = 0;
constexpr uint32_t one_range = 1;
constexpr uint32_t whole_words = ~(render::packet_word_bytes - 1);

SubmissionQueue *submission_queue(ResourceInfoProcessor *processor)
{
    auto *view = reinterpret_cast<ProcessorWithQueue *>(processor);
    return reinterpret_cast<SubmissionQueue *>(&view->queue);
}

}  // namespace

extern "C" uintptr_t bb_render_resource_info_write_completion_label_02ab1930(ResourceInfoProcessor *processor)
{
    ResourceInfoProcessorServices *services = processor->services.get();
    uint32_t label = atomic_fetch_increment(services->label_counter.get());
    // The reserved value is a full 64-bit word: one with high bits set never matches.
    if (label == services->reserved_label.get())
        label = atomic_fetch_increment(services->label_counter.get());
    return write_release_memory(&processor->commands, event_compute_done, destination_memory,
                                services->label_address.get(), data_low_32_bits, label,
                                cache_action_write_back, cache_policy_default);
}

extern "C" void bb_render_resource_info_submit_pending_commands_02ab18c0(ResourceInfoProcessor *processor)
{
    PacketBuffer &commands = processor->commands;
    const uintptr_t start = commands.begin;
    const uint32_t bytes = (static_cast<uint32_t>(commands.cursor) - static_cast<uint32_t>(start)) & whole_words;
    submit_command_ranges(submission_queue(processor), one_range, &start, &bytes);
    commands.begin = commands.cursor;
}
