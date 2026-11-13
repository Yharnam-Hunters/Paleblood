// SPDX-License-Identifier: GPL-2.0-or-later
// Engine: the engine's thread object's constructors (Dantelion2 DLThread).
//
// A thread object owns a system thread, a start mutex (the new thread waits on it: the
// constructor creates and locks it before the thread starts), a condition signal, the thread's
// name and an empty list. Every constructor builds the same object and differs only in how the
// system thread is made (thread_create_with_affinity, 0x02a3b310, or its wrapper
// thread_create, 0x02a3b4d0, which uses every core and drops its fifth argument):
//
// - engine_thread_construct_0248adc0 (0x0248adc0): the default stack size.
// - engine_thread_construct_sized_0248b200 (0x0248b200): a given stack size (0 for the default).
// - engine_thread_construct_with_affinity_0248afe0 (0x0248afe0): a given stack size and core mask.
//
// Each passes an argument through that the system call ignores, as the shipped game does. A
// default allocator is made when missing (the name string's heap) and checked like any
// container's heap. A null or empty name becomes "unnamed". A thread that was not created and a
// mutex or condition signal that was not created are reported, and construction goes on.
#include <cstddef>
#include <cstdint>

#include "container.h"
#include "narrow_string.h"
#include "pthread_mutex.h"

namespace {

using engine::Allocator;
using engine::NarrowString;
using engine::pthread_mutex::Mutex;

// The condition signal (condition_signal.cpp), followed here by the object it belongs to.
struct ConditionSignal {
    const void *vtable;
    void *mutex_handle;
    void *condition;
    uint32_t flags;
    void *owner;
};
static_assert(offsetof(ConditionSignal, flags) == 0x18 && offsetof(ConditionSignal, owner) == 0x20);
constexpr uint32_t signal_created = 1u << 0;

struct ListLinks {
    ListLinks *next;
    ListLinks *prev;
};

// The system thread's slot: its handle and a started flag.
struct SystemThread {
    uint64_t handle;
    bool started;
};

struct Thread {
    const void *vtable;
    SystemThread system_thread;
    uint64_t handle;                 // the handle thread creation returned, 0 on failure
    void *procedure;                 // stored for the thread's entry
    uint64_t unknown_0x28;
    void *argument;
    Mutex start_mutex;
    ConditionSignal signal;
    NarrowString name;
    ListLinks list;
    bool unknown_0xc0;
};
static_assert(offsetof(Thread, handle) == 0x18 && offsetof(Thread, procedure) == 0x20);
static_assert(offsetof(Thread, argument) == 0x30 && offsetof(Thread, start_mutex) == 0x38);
static_assert(offsetof(Thread, signal) == 0x50 && offsetof(Thread, name) == 0x78);
static_assert(offsetof(Thread, list) == 0xb0 && offsetof(Thread, unknown_0xc0) == 0xc0);

RT_GLOBAL(0x057b3260, thread_vtable, const uint8_t);
RT_GLOBAL(0x057b2f60, condition_signal_vtable, const uint8_t);
RT_GLOBAL(0x05940670, default_allocator, Allocator *);
RT_ORIGINAL(0x02fa2b10, default_allocator_create, Allocator *());

RT_ORIGINAL(0x02483fe0, mutex_create, int32_t(void **handle));
RT_ORIGINAL(0x0247ee90, mutex_lock, int32_t(Mutex *));
RT_ORIGINAL(0x0247e690, condition_signal_create, int32_t(ConditionSignal *, uint8_t signalled));
RT_ORIGINAL(0x02a3b310, thread_create_with_affinity,
            uint64_t(SystemThread *, Thread *entry_argument, uint64_t unused_1, uint64_t stack_size,
                     uint64_t core_mask, int32_t unused_2, const char *name));
RT_ORIGINAL(0x02a3b4d0, thread_create,
            uint64_t(SystemThread *, Thread *entry_argument, uint64_t unused_1, uint64_t stack_size,
                     uint32_t dropped, const char *name));
// The executable's call stub for strlen.
RT_ORIGINAL(0x02fbe608, string_length, uint64_t(const char *));

// "N:\SPRJ\Source\Library\Dantelion2\dist_ps4\source\ClassLibrary\Core\Kernel\Source\DLThread.cpp"
RT_GLOBAL(0x04d3cd5e, source_file, const char);
// "Thread creation fault"
RT_GLOBAL(0x04d3cdbd, message_thread_fault, const char);
// "Sync object creation fault"
RT_GLOBAL(0x04d3cdd3, message_sync_fault, const char);
// "unnamed"
RT_GLOBAL(0x04d3cdee, unnamed, const char);

// The source lines of a constructor's three checks.
struct CheckLines {
    int32_t thread, mutex, signal;
};

// Everything before the system thread is made: the members, the default allocator, the start
// mutex (created when its vtable says it is not, and locked) and the condition signal.
void construct_members(Thread *thread, void *procedure, void *argument)
{
    thread->vtable = thread_vtable.address();
    thread->system_thread.handle = 0;
    thread->system_thread.started = false;
    thread->procedure = procedure;
    thread->argument = argument;
    thread->start_mutex.vtable = engine::pthread_mutex::mutex_vtable.address();
    thread->start_mutex.initialized = false;
    thread->signal.vtable = condition_signal_vtable.address();
    thread->signal.flags = 0;
    thread->signal.owner = nullptr;

    Allocator *allocator = default_allocator.get();
    if (!allocator) {
        allocator = default_allocator_create();
        default_allocator.get() = allocator;
    }
    engine::check_container_heap(allocator);
    engine::narrow_string_begin(&thread->name, allocator);
    thread->name.owns = 0;
    thread->list.next = thread->list.prev = &thread->list;
    thread->unknown_0xc0 = false;

    Mutex *mutex = &thread->start_mutex;
    if (!mutex->vtable->is_initialized(mutex) && mutex_create(&mutex->handle) == 0)
        mutex->initialized = true;
    condition_signal_create(&thread->signal, 0);
    thread->signal.owner = thread;
    mutex_lock(mutex);
}

// Everything after: the handle, the name, and the checks.
void finish(Thread *thread, uint64_t handle, const char *name, const CheckLines &lines)
{
    thread->handle = handle;
    const char *text = name ? name : unnamed.address();
    const uint64_t length = *text ? string_length(text) : 0;
    engine::narrow_string_assign(&thread->name, text, length);
    if (!thread->handle)
        engine::fatal_error(source_file.address(), lines.thread, message_thread_fault.address());
    if (!thread->start_mutex.initialized)
        engine::fatal_error(source_file.address(), lines.mutex, message_sync_fault.address());
    if (!(thread->signal.flags & signal_created))
        engine::fatal_error(source_file.address(), lines.signal, message_sync_fault.address());
}

constexpr uint64_t default_stack_size = 0;

// Each constructor's checks, at its own lines of DLThread.cpp.
constexpr CheckLines default_lines = {0x60, 0x61, 0x62};
constexpr CheckLines with_affinity_lines = {0x8d, 0x8e, 0x8f};
constexpr CheckLines sized_lines = {0xb2, 0xb3, 0xb4};

}  // namespace

extern "C" void bb_engine_thread_construct_0248adc0(Thread *thread, void *procedure, uint32_t dropped,
                                                    const char *name, void *argument)
{
    construct_members(thread, procedure, argument);
    const uint64_t handle = thread_create(&thread->system_thread, thread, 0, default_stack_size, dropped, name);
    finish(thread, handle, name, default_lines);
}

extern "C" void bb_engine_thread_construct_with_affinity_0248afe0(Thread *thread, void *procedure, uint64_t stack_size,
                                                                  uint64_t core_mask, const char *name, void *argument)
{
    constexpr int32_t unused_flag = 1;
    construct_members(thread, procedure, argument);
    const uint64_t handle =
        thread_create_with_affinity(&thread->system_thread, thread, 0, stack_size, core_mask, unused_flag, name);
    finish(thread, handle, name, with_affinity_lines);
}

extern "C" void bb_engine_thread_construct_sized_0248b200(Thread *thread, void *procedure, uint64_t stack_size,
                                                          uint32_t dropped, const char *name, void *argument)
{
    construct_members(thread, procedure, argument);
    const uint64_t handle = thread_create(&thread->system_thread, thread, 0, stack_size, dropped, name);
    finish(thread, handle, name, sized_lines);
}
