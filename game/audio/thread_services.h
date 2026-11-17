// SPDX-License-Identifier: GPL-2.0-or-later
// Prefix of the shared context consumed by the FMOD Thread stop helper.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "runtime/original.h"

namespace audio {

struct MemoryPool;
using ThreadNotification = void (*)(void *context, uint32_t selector,
                                    uint64_t native_handle, const char *name);

// The full context class and the public callback typedef remain unrecovered.
struct ThreadServices {
    std::byte unknown_0x0000[8];
    MemoryPool *memory_pool;
    std::byte unknown_0x0010[8];
    ThreadNotification notification;
};
static_assert(offsetof(ThreadServices, memory_pool) == 0x08);
static_assert(offsetof(ThreadServices, notification) == 0x18);

constexpr std::size_t thread_source_file_bytes = 56;
using ThreadSourceFile = std::array<char, thread_source_file_bytes>;
RT_GLOBAL(0x056d6ae8, thread_services, ThreadServices *);
RT_GLOBAL(0x04c7cfae, thread_source_file, const ThreadSourceFile);
RT_ORIGINAL(0x0111adc0, signal_thread_semaphore, uint32_t(void *semaphore));
RT_ORIGINAL(0x0111ada0, wait_thread_semaphore, uint32_t(void *semaphore));
RT_ORIGINAL(0x0111ad80, delete_thread_semaphore, uint32_t(void *semaphore));
RT_ORIGINAL(0x011ab3a0, release_thread_storage,
            void(MemoryPool *pool, void *block, const char *source_file, int32_t source_line));

constexpr uint32_t stop_notification_selector = 6;
constexpr int32_t original_stop_source_line = 0x135;

}  // namespace audio
