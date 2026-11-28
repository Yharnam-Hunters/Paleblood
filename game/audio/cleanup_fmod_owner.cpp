// SPDX-License-Identifier: GPL-2.0-or-later
// Tear down the ATRAC9 decoder. An AJM failure stops cleanup before the remaining resources are freed.
#include "fmod_ajm_cleanup.h"
#include "../kernel/kernel_context.h"

namespace {
constexpr uint32_t invalid_batch = UINT32_MAX;
constexpr uint32_t wait_forever = UINT32_MAX;
constexpr uint32_t ajm_error_cancelled = 0x80930017;
constexpr uint8_t wait_without_cancel = 4;
constexpr int32_t first_free_line = 230;
constexpr int32_t second_free_line = 231;

void release_buffer(void *&buffer, int32_t source_line)
{
    if (buffer == nullptr) return;
    kernel::memory_pool_free(kernel::kernel_context_instance.get()->pool, buffer,
                             audio::at9_source_file.address(), source_line);
    buffer = nullptr;
}
}  // namespace

extern "C" uint32_t bb_audio_fmod_cleanup_owner(audio::FmodCleanupOwner *owner)
{
    using namespace audio;
    AjmBatchSlot *batch = owner->batch;
    if (batch && batch->id != invalid_batch && batch->generation == owner->batch_generation) {
        const bool cancel = (owner->flags & wait_without_cancel) == 0;
        if (cancel && ajm_batch_cancel(codec_ajm_context.get(), batch->id) != 0)
            return kernel::kernel_error;
        // Cancellation can invalidate this same slot before the wait.
        if (batch->id != invalid_batch) {
            alignas(16) std::byte error_output[0x20];
            const auto result = ajm_batch_wait(codec_ajm_context.get(), batch->id,
                                               wait_forever, error_output);
            if (result != 0 && !(cancel && result == ajm_error_cancelled))
                return kernel::kernel_error;
            batch->id = invalid_batch;
            ++batch->generation;
        }
        owner->batch = nullptr;
    }
    if (ajm_instance_destroy(codec_ajm_context.get(), owner->instance_id) != 0)
        return kernel::kernel_error;
    release_buffer(owner->buffer_0x0080, first_free_line);
    release_buffer(owner->buffer_0x0228, second_free_line);
    return 0;
}
