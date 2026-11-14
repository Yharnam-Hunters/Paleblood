// SPDX-License-Identifier: GPL-2.0-or-later
// Loading: what a transfer task's initialization calls: the disk file device singleton, its
// lookup of where a path is found, and the load request's copy assignment (structs in
// disk_file_device.h and resource_queue.h).
//
// loading_disk_file_device_instance_0246be70 (0x0246be70) returns the Ps4DiskFileDevice singleton
// (called from the device's open and from the loaders, 22 call sites). When the instance pointer
// is set it is returned at once. Otherwise, under a light mutex of its own (built once in static
// storage, as the debug print's: a C++ static guard, then its own "constructed" byte), it checks
// again and builds the device: a "destroyed" byte left set by an earlier release at quit is
// cleared, the storage's static guard is taken and at once released (the storage has no
// initializer), the device is constructed (0x0246bff0) in its storage (aligned up to 8), the
// instance pointer is set, and the device's release (0x024712e0) is registered with the quit query
// singleton (made first when missing) through its second vtable slot, with order 1. The instance
// is read again for the result before the mutex is unlocked.
//
// loading_disk_file_device_release_at_quit_024712e0 (0x024712e0) is what the singleton registers
// to run at quit: unless the "destroyed" byte is already set (any value), the instance is destroyed
// (0x0246caa0, even when it is null), the instance pointer cleared and the byte set to 1.
//
// loading_disk_file_device_locate_0246e3d0 (0x0246e3d0) tells which device finds a path, as the
// device's own location code (0 for the first reader, 2 when no device knows the path). Its second
// argument is the path's text (transfer_task.h declares it as a wide string: the callers pass the
// text). Under the device's lock it makes a working copy of the text on the path copy heap and
// resolves its mount prefixes in place: when opens are enabled (+0xf8 clear) and the preferred
// flag is set, in rounds as the open does (aliases for as long as one matches, then one root,
// again while an alias matched or the root resolution returned 1); otherwise only the roots, until
// none matches. Then, with the lock taken once more, the device registered under the resolved
// mount name is asked (vtable slot 0x20: the text and the preferred flag); with no such device,
// after that inner unlock, each device not registered under a name is asked in turn until one
// returns something other than 2. The copy's heap text is freed and the lock released.
//
// loading_load_request_assign_024be630 (0x024be630) is the load request's copy assignment (file
// task initialization, the load event queues). It copies the words +0x08..+0x38, the bytes +0x40
// and +0x41, the words +0x80..+0x98, +0x138 and +0x140 and the strings' owns bytes, assigns the
// three strings (each through the engine's inlined string assignment) only when the request is not
// assigned to itself, and assigns the data array regardless. The third string's owns byte is copied
// only with its text. The vtable and the bytes +0x42..+0x47 and +0x13c..+0x13f are left as they
// were. It returns the request.
#include <cstddef>
#include <cstdint>

#include "../engine/container.h"
#include "../engine/engine.h"
#include "../engine/wide_string_assign.h"
#include "disk_file_device.h"
#include "resource_queue.h"

using namespace loading::disk_file_device;
using frame_timing::job::JobLock;
using frame_timing::job::wait_forever;
using loading::resource_queue::LoadRequest;

namespace {

// The light mutex the singleton is built under: a vtable (lock with a timeout at slot 3, unlock
// at slot 5), the system mutex and a "created" flag.
struct LightMutex;
struct LightMutexVtable {
    void *unknown_slots[3];
    int32_t (*lock)(LightMutex *, int32_t timeout);
    void *try_lock;
    void (*unlock)(LightMutex *);
};
static_assert(offsetof(LightMutexVtable, lock) == 0x18 && offsetof(LightMutexVtable, unlock) == 0x28);
struct LightMutex {
    const LightMutexVtable *vtable;
    uint64_t handle;
    bool created;
};
static_assert(offsetof(LightMutex, handle) == 0x8 && offsetof(LightMutex, created) == 0x10);

// A C++ static guard: its first byte is set once the object is constructed.
using StaticGuard = uint64_t;

// The quit query singleton: its second vtable slot registers a function to run at quit, in the
// given order.
struct QuitQuery;
struct QuitQueryVtable {
    void *unknown_slot_0;
    void (*add_quit_function)(QuitQuery *, void *function, int32_t order);
};
static_assert(offsetof(QuitQueryVtable, add_quit_function) == 0x8);
struct QuitQuery {
    const QuitQueryVtable *vtable;
};
constexpr int32_t device_quit_order = 1;

// A file device's lookup (vtable slot 0x20): where it finds a path.
struct LocatingDeviceVtable {
    void *unknown_slots[4];
    int32_t (*locate)(FileDevice *, const char16_t *path, uint8_t preferred);
};
static_assert(offsetof(LocatingDeviceVtable, locate) == 0x20);
constexpr int32_t location_not_found = 2;   // this device does not know the path: ask the next

constexpr uintptr_t storage_alignment = 8;
constexpr uint8_t device_destroyed = 1;

RT_ORIGINAL(0x02fbe538, static_guard_acquire, int32_t(StaticGuard *));
RT_ORIGINAL(0x02fbe558, static_guard_release, void(StaticGuard *));
RT_ORIGINAL(0x0247ee80, light_mutex_is_created, bool(LightMutex *));
RT_ORIGINAL(0x02483fe0, system_mutex_create, int32_t(uint64_t *handle));
RT_GLOBAL(0x05aa5720, light_mutex_storage, uint8_t);
RT_GLOBAL(0x05aa573f, light_mutex_constructed, bool);
RT_GLOBAL(0x05aa5740, light_mutex_guard, StaticGuard);
RT_GLOBAL(0x056b6220, light_mutex_vtable, const uint8_t);

RT_GLOBAL(0x05aa1ab0, device_instance, DiskFileDevice *);
RT_GLOBAL(0x05aa1ab8, device_state, uint8_t);        // set by the release at quit
RT_GLOBAL(0x05aa1ab9, device_storage, uint8_t);      // aligned up to 8 before use
RT_GLOBAL(0x05aa1bc0, device_storage_guard, StaticGuard);
RT_ORIGINAL(0x0246bff0, disk_file_device_construct, void(DiskFileDevice *));
RT_ORIGINAL(0x024712e0, disk_file_device_release_at_quit, void());
RT_ORIGINAL(0x0246caa0, disk_file_device_destroy, void(DiskFileDevice *));
RT_GLOBAL(0x05aa54f8, quit_query_instance, QuitQuery *);
RT_ORIGINAL(0x024e2550, quit_query_create, QuitQuery *());

// The wide string's assign of `length` characters of a text.
RT_ORIGINAL(0x02a2a720, wide_string_assign_counted, WideString *(WideString *, const char16_t *text, uint64_t length));

template <typename T>
T *align_up(uint8_t *storage)
{
    const uintptr_t address = reinterpret_cast<uintptr_t>(storage);
    return reinterpret_cast<T *>(address + (-address & (storage_alignment - 1)));
}

// The first time through, takes the guard (the caller runs the initializer, then releases it).
bool guard_first_time(StaticGuard *guard)
{
    return *reinterpret_cast<const uint8_t *>(guard) == 0 && static_guard_acquire(guard) != 0;
}

LightMutex *singleton_mutex()
{
    StaticGuard *guard = light_mutex_guard.address();
    if (guard_first_time(guard)) {
        light_mutex_constructed.get() = false;
        static_guard_release(guard);
    }
    LightMutex *mutex = align_up<LightMutex>(light_mutex_storage.address());
    if (!light_mutex_constructed.get()) {
        mutex->created = false;
        mutex->vtable = reinterpret_cast<const LightMutexVtable *>(light_mutex_vtable.address() +
                                                                   engine::vtable_address_point);
        if (!light_mutex_is_created(mutex) && system_mutex_create(&mutex->handle) == 0)
            mutex->created = true;
        light_mutex_constructed.get() = true;
    }
    return mutex;
}

void build_device()
{
    if (device_state.get() == device_destroyed)
        device_state.get() = 0;
    StaticGuard *guard = device_storage_guard.address();
    if (guard_first_time(guard))
        static_guard_release(guard);
    auto *device = align_up<DiskFileDevice>(device_storage.address());
    disk_file_device_construct(device);
    device_instance.get() = device;

    QuitQuery *query = quit_query_instance.get();
    if (!query)
        query = quit_query_create();
    query->vtable->add_quit_function(query, disk_file_device_release_at_quit.address(), device_quit_order);
}

void lock(JobLock *lock) { lock->vtable->lock(lock, wait_forever); }
void unlock(JobLock *lock) { lock->vtable->unlock(lock); }

// The path copy heap, looked up on first use.
engine::Allocator *path_copy_allocator()
{
    engine::Allocator *&heap = path_copy_heap.get();
    if (!heap)
        heap = reinterpret_cast<engine::Allocator *>(path_copy_heap_owner() + path_copy_heap_offset);
    return heap;
}

// Resolves the path's mount prefixes in place: in rounds of aliases then a root, or roots only.
void resolve_mounts(DiskFileDevice *device, WideString *path, bool rounds)
{
    if (!rounds) {
        while (resolve_root(device, path)) {
        }
        return;
    }
    for (;;) {
        bool aliased = false;
        while (resolve_alias(device, path))
            aliased = true;
        if (!resolve_root(device, path) && !aliased)
            return;
    }
}

bool is_named(const DiskFileDevice *device, const FileDevice *candidate)
{
    for (const NamedDevice *entry = device->named_begin; entry != device->named_end; ++entry) {
        if (entry->device == candidate)
            return true;
    }
    return false;
}

int32_t ask_device(FileDevice *device, const WideString &path, uint8_t preferred)
{
    const auto *vtable = reinterpret_cast<const LocatingDeviceVtable *>(device->vtable);
    return vtable->locate(device, text_of(path), preferred);
}

}  // namespace

extern "C" DiskFileDevice *bb_loading_disk_file_device_instance_0246be70()
{
    if (DiskFileDevice *device = device_instance.get())
        return device;
    LightMutex *mutex = singleton_mutex();
    mutex->vtable->lock(mutex, wait_forever);
    if (!device_instance.get())
        build_device();
    DiskFileDevice *device = device_instance.get();
    mutex->vtable->unlock(mutex);
    return device;
}

extern "C" int32_t bb_loading_disk_file_device_locate_0246e3d0(DiskFileDevice *device, const char16_t *path,
                                                               uint8_t preferred)
{
    lock(&device->lock);
    engine::Allocator *allocator = path_copy_allocator();
    engine::check_container_heap(allocator);
    WideString copy;
    engine::wide_string_begin(&copy, allocator);
    copy.owns = 1;
    const uint64_t length = path[0] != u'\0' ? c_wcslen(path) : 0;
    wide_string_assign_counted(&copy, path, length);
    resolve_mounts(device, &copy, !device->opens_disabled && preferred);

    int32_t location = location_not_found;
    lock(&device->lock);
    FileDevice *named = find_named_device(device, &copy);
    if (named)
        location = ask_device(named, copy, preferred);
    unlock(&device->lock);
    if (!named) {
        for (FileDevice **candidate = device->devices_begin; candidate != device->devices_end; ++candidate) {
            if (is_named(device, *candidate))
                continue;
            location = ask_device(*candidate, copy, preferred);
            if (location != location_not_found)
                break;
        }
    }
    engine::wide_string_release(&copy);
    unlock(&device->lock);
    return location;
}

extern "C" LoadRequest *bb_loading_load_request_assign_024be630(LoadRequest *request, const LoadRequest *source)
{
    const bool same_request = request == source;
    request->word_0x08 = source->word_0x08;
    request->word_0x10 = source->word_0x10;
    request->word_0x18 = source->word_0x18;
    request->word_0x20 = source->word_0x20;
    request->word_0x28 = source->word_0x28;
    request->word_0x30 = source->word_0x30;
    request->word_0x38 = source->word_0x38;
    request->byte_0x40 = source->byte_0x40;
    request->byte_0x41 = source->byte_0x41;
    request->name.owns = source->name.owns;
    if (!same_request)
        engine::wide_string_copy_assign(&request->name, source->name);

    request->word_0x80 = source->word_0x80;
    request->word_0x88 = source->word_0x88;
    request->word_0x90 = source->word_0x90;
    request->word_0x98 = source->word_0x98;
    request->text_0xa0.owns = source->text_0xa0.owns;
    if (!same_request) {
        engine::wide_string_copy_assign(&request->text_0xa0, source->text_0xa0);
        request->text_0xd8.owns = source->text_0xd8.owns;
        engine::wide_string_copy_assign(&request->text_0xd8, source->text_0xd8);
    }
    loading::resource_queue::byte_array_assign(&request->data, &source->data);
    request->value_0x138 = source->value_0x138;
    request->word_0x140 = source->word_0x140;
    return request;
}

extern "C" void bb_loading_disk_file_device_release_at_quit_024712e0()
{
    if (device_state.get() != 0)
        return;
    disk_file_device_destroy(device_instance.get());
    device_instance.get() = nullptr;
    device_state.get() = device_destroyed;
}
