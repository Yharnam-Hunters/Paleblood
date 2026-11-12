// SPDX-License-Identifier: GPL-2.0-or-later
// Loading: opening a path through the disk file device (structs in disk_file_device.h).
//
// loading_disk_device_open_0246d550 (0x0246d550), called by the device's open (0x024799b0,
// 0x0246bc40), resolves the path's mount prefixes in place and opens the result. It first keeps a
// copy of the path as it was asked for, on the thread's temporary heap. The resolution replaces
// alias prefixes for as long as one matches, then one root prefix, and goes round again while an
// alias matched in that round or the root it replaced was not "title". A path in which no alias
// matched (a bare "title:/..." or "data1:/..." path, or one with no mount name) is not opened:
// the result is null. Otherwise, under the device's lock, the device registered under the
// resolved path's mount name opens it, or with no such device each unnamed device is tried in
// turn. Each open is given the requested text and the two words after it. A device whose opens are
// disabled (+0xf8) returns null at once; if they become disabled while the copy is made, only the
// roots are resolved and nothing is opened.
//
// loading_disk_device_find_named_device_0246e630 (0x0246e630) splits the path's mount name off at
// ':' (as the mount resolution does) and returns the device registered under that name, or null
// when the path has no name, nothing after the separators, or no device of that name.
//
// loading_disk_device_open_on_device_0246e810 (0x0246e810) opens through one device: the device is
// given a working copy of the path (on the path copy heap), and on success the path takes the
// copy's text, which the device may have changed. loading_disk_device_open_on_unnamed_devices_0246e9a0
// (0x0246e9a0) does the same for each device that is not registered under a name, in order, until
// one opens the path.
#include <cstdint>

#include "../engine/container.h"
#include "../engine/engine.h"
#include "../engine/thread.h"
#include "../engine/wide_string_assign.h"
#include "disk_file_device.h"

using namespace loading::disk_file_device;
using frame_timing::job::JobLock;
using frame_timing::job::wait_forever;

namespace {

// What the resolution loop reports: bit 1 is set when an alias was replaced in a round that
// ended; the root resolution's own result (0 or 1) is or-ed in too and is not looked at.
constexpr uint8_t resolved_alias = 0x2;

engine::Allocator *thread_temporary_allocator()
{
    const int64_t offset = thread_heap_offset.get();
    auto *heap = *reinterpret_cast<ThreadHeap **>(engine::thread_pointer() + offset);
    if (!heap) {
        thread_heap_init();
        heap = *reinterpret_cast<ThreadHeap **>(engine::thread_pointer() + offset);
    }
    return &heap->allocator;
}

// The path copy heap, looked up on first use.
engine::Allocator *path_copy_allocator()
{
    engine::Allocator *&heap = path_copy_heap.get();
    if (!heap)
        heap = reinterpret_cast<engine::Allocator *>(path_copy_heap_owner() + path_copy_heap_offset);
    return heap;
}

void lock(JobLock *lock) { lock->vtable->lock(lock, wait_forever); }
void unlock(JobLock *lock) { lock->vtable->unlock(lock); }

// Resolves the path's mount prefixes in place; see the file comment for the rounds.
uint8_t resolve_mounts(DiskFileDevice *device, WideString *path)
{
    uint8_t result = 0;
    if (device->opens_disabled) {
        uint8_t replaced;
        do {
            replaced = resolve_root(device, path);
            result |= replaced;
        } while (replaced);
        return result;
    }
    for (;;) {
        bool aliased = false;
        while (resolve_alias(device, path)) {
            aliased = true;
            result |= resolved_alias;
        }
        const uint8_t replaced = resolve_root(device, path);
        result |= replaced;
        if (!aliased && replaced != 1)
            return result;
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

// Tries one device with a working copy of the path; on success the path takes the copy's text
// (and its owns byte, which the copy took from the path).
void *try_open(FileDevice *device, WideString *path, WideString *copy, const char16_t *requested,
               uint64_t word_1, uint64_t word_2)
{
    copy->owns = path->owns;
    engine::wide_string_copy_assign(copy, *path);
    void *file = device->vtable->open(device, copy, requested, word_1, word_2, 0);
    if (file) {
        path->owns = copy->owns;
        engine::wide_string_copy_assign(path, *copy);
    }
    return file;
}

}  // namespace

extern "C" void *bb_loading_disk_device_open_0246d550(DiskFileDevice *device, WideString *path,
                                                      uint64_t word_1, uint64_t word_2)
{
    if (device->opens_disabled)
        return nullptr;

    const char16_t *text = text_of(*path);
    engine::Allocator *allocator = thread_temporary_allocator();
    engine::check_container_heap(allocator);
    WideString requested;
    engine::wide_string_begin(&requested, allocator);
    const uint64_t length = text[0] != u'\0' ? c_wcslen(text) : 0;
    engine::wide_string_assign(&requested, text, length);
    requested.owns = 1;

    void *file = nullptr;
    if (resolve_mounts(device, path) & resolved_alias) {
        const char16_t *requested_text = text_of(requested);
        lock(&device->lock);
        if (FileDevice *named = find_named_device(device, path))
            file = open_on_device(named, path, requested_text, word_1, word_2);
        else
            file = open_on_unnamed_devices(device, path, requested_text, word_1, word_2);
        unlock(&device->lock);
    }
    engine::wide_string_release(&requested);
    return file;
}

extern "C" FileDevice *bb_loading_disk_device_find_named_device_0246e630(DiskFileDevice *device,
                                                                         const WideString *path)
{
    lock(&device->lock);
    FileDevice *found = nullptr;
    const char16_t *text = text_of(*path);
    TokenCursor cursor{text, mount_separator.address(), text ? c_wcslen(text) : 0};
    const Token name = path_token_next(&cursor);
    if (cursor.delimiters && cursor.position && cursor.remaining != 0) {
        // Skip the separators after the name; running out of characters there means no name.
        bool ran_out = false;
        while (c_wcschr(cursor.delimiters, *cursor.position)) {
            cursor.position += 1;
            if (cursor.remaining == 1) {
                ran_out = true;
                break;
            }
            cursor.remaining -= 1;
        }
        if (!ran_out && *cursor.position != u'\0') {
            for (const NamedDevice *entry = device->named_begin; entry != device->named_end; ++entry) {
                const char16_t *entry_name = text_of(entry->name);
                if (c_wcslen(entry_name) == name.length && c_wcsncmp(name.text, entry_name, name.length) == 0) {
                    found = entry->device;
                    break;
                }
            }
        }
    }
    unlock(&device->lock);
    return found;
}

extern "C" void *bb_loading_disk_device_open_on_device_0246e810(FileDevice *device, WideString *path,
                                                               const char16_t *requested, uint64_t word_1,
                                                               uint64_t word_2)
{
    if (!device)
        return nullptr;
    engine::Allocator *allocator = path_copy_allocator();
    engine::check_container_heap(allocator);
    WideString copy;
    engine::wide_string_begin(&copy, allocator);
    void *file = try_open(device, path, &copy, requested, word_1, word_2);
    engine::wide_string_release(&copy);
    return file;
}

extern "C" void *bb_loading_disk_device_open_on_unnamed_devices_0246e9a0(DiskFileDevice *device, WideString *path,
                                                                        const char16_t *requested,
                                                                        uint64_t word_1, uint64_t word_2)
{
    engine::Allocator *allocator = path_copy_allocator();
    engine::check_container_heap(allocator);
    WideString copy;
    engine::wide_string_begin(&copy, allocator);
    copy.owns = 1;

    lock(&device->lock);
    void *file = nullptr;
    for (FileDevice **candidate = device->devices_begin; candidate != device->devices_end; ++candidate) {
        if (is_named(device, *candidate))
            continue;
        file = try_open(*candidate, path, &copy, requested, word_1, word_2);
        if (file)
            break;
    }
    unlock(&device->lock);
    engine::wide_string_release(&copy);
    return file;
}
