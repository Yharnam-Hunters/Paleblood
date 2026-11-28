// SPDX-License-Identifier: GPL-2.0-or-later
// Configure and create the detached native thread used by FMOD Thread startup.
// Publish its handle only after attribute destruction and the final priority update.
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "runtime/original.h"

namespace {

struct KernelThread;
struct KernelThreadAttributes;
using ThreadHandle = KernelThread *;
using AttributeHandle = KernelThreadAttributes *;
using ThreadEntry = void *(*)(void *argument);
static_assert(sizeof(ThreadHandle) == sizeof(uint64_t));
static_assert(sizeof(AttributeHandle) == sizeof(uint64_t));

RT_ORIGINAL(0x011c4970, compare_thread_names, int(const char *left, const char *right));
RT_ORIGINAL(0x0111a8c0, lookup_thread_affinity, int(const char *name, uint64_t *mask));
RT_GLOBAL(0x04c7d061, mixer_name, const char);

RT_ORIGINAL(0x02fbf298, initialize_thread_attributes, int32_t(AttributeHandle *attributes));
RT_ORIGINAL(0x02fbf2a8, set_thread_stack_size,
            int32_t(AttributeHandle *attributes, std::size_t bytes));
RT_ORIGINAL(0x02fbf2b8, set_thread_detach_state,
            int32_t(AttributeHandle *attributes, int32_t state));
RT_ORIGINAL(0x02fbf2c8, set_thread_scheduling_policy,
            int32_t(AttributeHandle *attributes, int32_t policy));
RT_ORIGINAL(0x02fbfe88, set_thread_scheduling_parameters,
            int32_t(AttributeHandle *attributes, const int32_t *priority));
RT_ORIGINAL(0x02fbfe98, set_thread_affinity,
            int32_t(AttributeHandle *attributes, uint64_t mask));
RT_ORIGINAL(0x02fbf2d8, create_thread,
            int32_t(ThreadHandle *thread, AttributeHandle *attributes, ThreadEntry entry,
                    void *argument, const char *name));
RT_ORIGINAL(0x02fbf2e8, destroy_thread_attributes, int32_t(AttributeHandle *attributes));
RT_ORIGINAL(0x02fbf328, set_thread_priority, int32_t(ThreadHandle thread, int32_t priority));

constexpr int32_t lowest_fmod_priority = -2;
constexpr int32_t highest_fmod_priority = 3;
// Native priorities in the FMOD logical-priority order [-2, -1, 0, 1, 2, 3].
constexpr std::array<int32_t, 6> native_priorities{767, 733, 700, 265, 260, 256};
constexpr int32_t mixer_native_priority = 257;
constexpr int32_t minimum_stack_bytes = 0x4000;
constexpr int32_t detached_thread = 1;
constexpr int32_t fifo_scheduling = 1;
constexpr uint32_t thread_error = 0x21;

}  // namespace

// The fifth transported argument is unused; retain its ABI slot.
extern "C" uint32_t bb_audio_fmod_create_native_thread(const char *name, ThreadEntry entry,
    void *argument, int32_t priority, uint64_t, int32_t stack_bytes, uint64_t *native_handle)
{
    int32_t native_priority = 0;
    if (priority >= lowest_fmod_priority && priority <= highest_fmod_priority)
        native_priority = native_priorities[static_cast<std::size_t>(priority - lowest_fmod_priority)];
    if (compare_thread_names(name, mixer_name.address()) == 0)
        native_priority = mixer_native_priority;

    // The lookup leaves unknown names' output untouched; the original mask is unspecified there.
    uint64_t affinity;
    lookup_thread_affinity(name, &affinity);

    AttributeHandle attributes = nullptr;
    if (initialize_thread_attributes(&attributes) != 0)
        return thread_error;
    const auto stack_size = static_cast<std::size_t>(
        stack_bytes < minimum_stack_bytes ? minimum_stack_bytes : stack_bytes);
    if (set_thread_stack_size(&attributes, stack_size) != 0)
        return thread_error;
    if (set_thread_detach_state(&attributes, detached_thread) != 0)
        return thread_error;
    if (set_thread_scheduling_policy(&attributes, fifo_scheduling) != 0)
        return thread_error;
    if (set_thread_scheduling_parameters(&attributes, &native_priority) != 0)
        return thread_error;
    if (set_thread_affinity(&attributes, affinity) != 0)
        return thread_error;

    ThreadHandle thread = nullptr;
    if (create_thread(&thread, &attributes, entry, argument, name) != 0)
        return thread_error;
    if (destroy_thread_attributes(&attributes) != 0)
        return thread_error;
    // The scheduling-parameter call can write its argument before this later read.
    if (set_thread_priority(thread, native_priority) != 0)
        return thread_error;

    std::memcpy(native_handle, &thread, sizeof(thread));
    return 0;
}
