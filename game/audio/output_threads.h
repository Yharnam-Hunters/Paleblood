// SPDX-License-Identifier: GPL-2.0-or-later
// Accessed storage for the FMOD output owner's two Thread objects.
#pragma once

#include <cstddef>
#include <cstdint>

namespace audio {

constexpr std::size_t fmod_thread_name_bytes = 256;

// RTTI identifies FMOD::Thread. The constructor and consumers establish this
// storage stride; these names do not claim recovered original declarations.
struct FmodThreadStorage {
    const void *vtable;
    std::byte unknown_0x0008;
    char name[fmod_thread_name_bytes];
    std::byte unknown_0x0109[7];
    uint64_t native_handle;
    uint8_t running;
    std::byte unknown_0x0119[7];
    void *argument;
    uint64_t unknown_0x0128;
    void *wakeup_semaphore;
    void *startup_semaphore;
    void *entry_address;
    void *service_context;
    uint32_t callback_delay;
};
static_assert(offsetof(FmodThreadStorage, name) == 0x09);
static_assert(offsetof(FmodThreadStorage, native_handle) == 0x110);
static_assert(offsetof(FmodThreadStorage, running) == 0x118);
static_assert(offsetof(FmodThreadStorage, argument) == 0x120);
static_assert(offsetof(FmodThreadStorage, wakeup_semaphore) == 0x130);
static_assert(offsetof(FmodThreadStorage, startup_semaphore) == 0x138);
static_assert(offsetof(FmodThreadStorage, entry_address) == 0x140);
static_assert(offsetof(FmodThreadStorage, service_context) == 0x148);
static_assert(offsetof(FmodThreadStorage, callback_delay) == 0x150);
static_assert(sizeof(FmodThreadStorage) == 0x158);

// A prefix view: the outer class and its callback subobject type are unknown.
struct OutputThreadContext {
    std::byte unknown_0x0000[0x60];
    void *service_context;
    std::byte unknown_0x0068[0x668];
    FmodThreadStorage audio_out_thread;
    FmodThreadStorage mixer_thread;
};
static_assert(offsetof(OutputThreadContext, service_context) == 0x60);
static_assert(offsetof(OutputThreadContext, audio_out_thread) == 0x6d0);
static_assert(offsetof(OutputThreadContext, mixer_thread) == 0x828);

}  // namespace audio
