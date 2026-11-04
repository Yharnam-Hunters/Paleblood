// SPDX-License-Identifier: GPL-2.0-or-later
// Present on the GPU context's owner thread: wait for a reusable flip slot, prepare the
// selected buffer and submit it. Successful submission advances the frame and notifies the
// context's queue. A second owner check runs even when the first check skipped submission.
#include <bit>
#include <cstdlib>
#include <cstring>

#include "swap_chain_state.h"

namespace {
using namespace render;

// Only flip_arg is read. Reserve the complete SDK structure; native write extents are unknown.
struct FlipStatus {
    uint8_t unknown_0x00[0x18];
    int64_t flip_arg;
    uint8_t unknown_0x20[0x20];
};
static_assert(offsetof(FlipStatus, flip_arg) == 0x18 && sizeof(FlipStatus) == 0x40);
struct alignas(uint64_t) FlipEvent {
    uint8_t unknown_0x00[0x20];
};
static_assert(sizeof(FlipEvent) == 0x20);

RT_GLOBAL(0x057e3be0, submit_guard_pointer, const uint64_t *);
RT_ORIGINAL(0x02fbe4f8, submit_guard_failure, void());

uint64_t read_submit_guard()
{
    uint64_t value;
    std::memcpy(&value, submit_guard_pointer.get(), sizeof(value));
    return value;
}

int32_t guarded_submit_result(uint64_t expected, int32_t result)
{
    // Reload the global pointer on every exit after the opaque callbacks.
    if (read_submit_guard() != expected) {
        submit_guard_failure();
        // A return from the original failure routine remains unsupported.
        std::abort();
    }
    return result;
}

RT_ORIGINAL(0x0248ba90, current_thread, uint64_t());
RT_ORIGINAL(0x02fbf5e8, video_out_get_flip_status, int32_t(int32_t, FlipStatus *));
RT_ORIGINAL(0x02fbf5f8, wait_flip_event,
            int32_t(uint64_t queue, FlipEvent *, int32_t maximum, int32_t *count, void *timeout));
RT_ORIGINAL(0x01486ca0, present_buffer_value, uint32_t(const PresentBufferRecord *));
RT_ORIGINAL(0x02ad1eb0, prepare_present_source,
            void(GpuPresentPreparation *, PresentGpuContext *, uintptr_t source));
RT_ORIGINAL(0x02ab6cb0, bind_present_descriptor,
            void(PresentGpuContext *, PresentDescriptor *, uintptr_t source_payload, PresentAuxiliary *));
RT_ORIGINAL(0x02aafb30, submit_present,
            int32_t(GpuCommandState *, int32_t video_out, int64_t buffer_index, uint32_t flip_mode, int64_t frame));
RT_ORIGINAL(0x02aaa860, notify_present_queue, void(void *queue, GpuCommandState **));
RT_ORIGINAL(0x02568530, finish_present_context, void(PresentGpuContext *));
RT_ORIGINAL(0x02aaf700, finish_present_commands, void(GpuCommandState *));

constexpr int32_t flip_wait_failed = 5;
constexpr uint32_t hsync_flip = 2, vsync_flip = 1;
constexpr unsigned buffer_value_shift = 8;

PresentBufferRecord *buffer_record(SwapChainState *self, int64_t index)
{
    // The original uses signed remainders and does not enforce an array bound. Describe the
    // record stride by its type, and preserve the machine's wrapping address arithmetic even
    // for negative indices into externally supplied backing storage.
    const uintptr_t base = reinterpret_cast<uintptr_t>(&self->first_buffer);
    const uintptr_t displacement = static_cast<uintptr_t>(index) * sizeof(PresentBufferRecord);
    return reinterpret_cast<PresentBufferRecord *>(base + displacement);
}

}  // namespace

extern "C" int32_t bb_render_swap_chain_submit(render::SwapChainState *self,
                                                uintptr_t source, uint32_t mode)
{
    // Both the initial slot and count are captured before the first owner check. The slot
    // used to read the buffer record is recomputed after the wait from the then-current frame.
    const uint64_t guard = read_submit_guard();
    const int64_t buffer_count = self->buffer_count;
    PresentGpuContext *context = self->context;
    const int64_t submitted_index = self->frame % buffer_count;
    const uint64_t submitting_thread = current_thread();
    if (submitting_thread == context->owner_thread) {
        for (;;) {
            FlipStatus status;
            if (video_out_get_flip_status(self->video_out, &status) < 0) return guarded_submit_result(guard, flip_wait_failed);
            const int64_t frame = self->frame;
            if (frame < buffer_count || status.flip_arg >= frame - (buffer_count - 1)) break;
            FlipEvent event;
            int32_t event_count;
            if (wait_flip_event(self->flip_queue, &event, 1, &event_count, nullptr) < 0)
                return guarded_submit_result(guard, flip_wait_failed);
        }

        const int64_t record_index = self->frame % buffer_count;
        const uint32_t buffer_value = present_buffer_value(buffer_record(self, record_index));
        self->descriptor.shifted_buffer_value = static_cast<uint64_t>(buffer_value) << buffer_value_shift;
        prepare_present_source(&context->preparation, context, source);
        const uintptr_t source_payload = source + offsetof(PresentSource, payload);
        bind_present_descriptor(context, &self->descriptor, source_payload, &self->auxiliary);
        const uint32_t flip_mode = mode == 0 ? hsync_flip : vsync_flip;
        const int32_t result = submit_present(&context->commands, self->video_out,
                                              submitted_index, flip_mode, self->frame);
        if (result != 0) return guarded_submit_result(guard, result);

        // The original increment wraps at the signed counter's limit.
        self->frame = std::bit_cast<int64_t>(static_cast<uint64_t>(self->frame) + 1);
        GpuCommandState *commands = &context->commands;
        notify_present_queue(context->owner->queue, &commands);
        finish_present_context(context);
    }

    // Reload the context: the notification/helpers may have replaced it or changed ownership.
    context = self->context;
    const uint64_t finishing_thread = current_thread();
    if (finishing_thread == context->owner_thread) finish_present_commands(&context->commands);
    return guarded_submit_result(guard, 0);
}
