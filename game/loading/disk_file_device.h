// SPDX-License-Identifier: GPL-2.0-or-later
// The PS4 disk file device's mount tables (Dantelion2 Ps4DiskFileDevice) and the wide-string
// token scanner its path resolution uses.
//
// A path the game opens starts with a mount name and a colon ("name:/..."). The device keeps two
// tables of (name, target) pairs, each an engine vector of wide strings. Resolving a path
// (0x0246d550) replaces an alias-table prefix for as long as one matches, then a root-table
// prefix, and repeats while the root it replaced was not "title".
#pragma once

#include <cstddef>
#include <cstdint>

#include "../engine/allocator.h"
#include "../engine/wide_string.h"
#include "../frame_timing/job.h"
#include "runtime/original.h"

namespace loading::disk_file_device {

using engine::WideString;

// One mount: the name before the colon and the text that replaces "name:" and the character after
// it.
struct MountEntry {
    WideString name;
    WideString target;
};
static_assert(offsetof(MountEntry, target) == 0x38 && sizeof(MountEntry) == 0x70);

// An engine vector of mounts, with the allocator its buffer comes from.
struct MountTable {
    uint64_t unknown_0x00;
    MountEntry *begin;
    MountEntry *end;
    MountEntry *capacity_end;
    engine::Allocator *allocator;
};
static_assert(offsetof(MountTable, begin) == 0x8 && offsetof(MountTable, capacity_end) == 0x18);
static_assert(offsetof(MountTable, allocator) == 0x20 && sizeof(MountTable) == 0x28);

// A file device the disk device opens through: its vtable's slot 0x10 opens a resolved path and
// returns the open file, or null. It is also given the path as it was asked for, before mount
// resolution, two words passed on as they are (what they mean is not known yet) and a zero.
struct FileDevice;
struct FileDeviceVtable {
    void *unknown_slots[2];
    void *(*open)(FileDevice *, WideString *path, const char16_t *requested, uint64_t word_1,
                  uint64_t word_2, uint64_t zero);
};
static_assert(offsetof(FileDeviceVtable, open) == 0x10);
struct FileDevice {
    const FileDeviceVtable *vtable;
};

// A file device registered under a mount name.
struct NamedDevice {
    WideString name;
    FileDevice *device;
    uint64_t unknown_0x40;
};
static_assert(offsetof(NamedDevice, device) == 0x38 && sizeof(NamedDevice) == 0x48);

struct DiskFileDevice {
    uint8_t unknown_0x00[0x8];
    FileDevice **devices_begin;   // every device, named or not
    FileDevice **devices_end;
    uint8_t unknown_0x18[0x40];
    MountTable roots;     // replaced once per round; "title" ends the resolution
    MountTable aliases;   // replaced repeatedly before the roots
    uint64_t unknown_0xa8;
    NamedDevice *named_begin;
    NamedDevice *named_end;
    uint8_t unknown_0xc0[0x20];
    frame_timing::job::JobLock lock;   // guards the device lists (taken again by what it calls)
    uint8_t unknown_0xe8[0x10];
    uint8_t opens_disabled;   // while set, opening a path through the device does nothing
};
static_assert(offsetof(DiskFileDevice, devices_begin) == 0x8 && offsetof(DiskFileDevice, devices_end) == 0x10);
static_assert(offsetof(DiskFileDevice, roots) == 0x58 && offsetof(DiskFileDevice, aliases) == 0x80);
static_assert(offsetof(DiskFileDevice, named_begin) == 0xb0 && offsetof(DiskFileDevice, named_end) == 0xb8);
static_assert(offsetof(DiskFileDevice, lock) == 0xe0 && offsetof(DiskFileDevice, opens_disabled) == 0xf8);

// A cursor over wide text: where it is, the delimiter characters, and how many characters are
// left to look at.
struct TokenCursor {
    const char16_t *position;
    const char16_t *delimiters;
    uint64_t remaining;
};
static_assert(sizeof(TokenCursor) == 0x18);

// A token the scanner found: its length and first character (0 and null for none). Returned in
// rax:rdx.
struct Token {
    uint64_t length;
    const char16_t *text;
};

// The C library's wide-character functions (2-byte characters on the PS4).
RT_ORIGINAL(0x02fbf228, c_wcslen, uint64_t(const char16_t *));
RT_ORIGINAL(0x02fbf248, c_wcschr, const char16_t *(const char16_t *, uint32_t character));
RT_ORIGINAL(0x02fbf268, c_wmemcmp, int32_t(const char16_t *, const char16_t *, uint64_t count));
RT_ORIGINAL(0x02fbf3e8, c_wcsncmp, int32_t(const char16_t *, const char16_t *, uint64_t count));

// The wide string's assign from part of another (position, count; ~0 for the rest), and its
// replace of `count` characters at `position` with part of another.
RT_ORIGINAL(0x02a279a0, wide_string_assign_part,
            WideString *(WideString *, const WideString *source, uint64_t position, uint64_t count));
RT_ORIGINAL(0x02b25360, wide_string_replace,
            WideString *(WideString *, uint64_t position, uint64_t count, const WideString *source,
                         uint64_t source_position, uint64_t source_count));

RT_ORIGINAL(0x02a2aca0, path_token_next, Token(TokenCursor *));
RT_ORIGINAL(0x0246cf50, resolve_root, bool(DiskFileDevice *, WideString *path));
RT_ORIGINAL(0x0246d180, resolve_alias, bool(DiskFileDevice *, WideString *path));

// The device registered under the path's mount name (or null), and the opens: through one device,
// or through each device with no name in turn.
RT_ORIGINAL(0x0246e630, find_named_device, FileDevice *(DiskFileDevice *, const WideString *path));
RT_ORIGINAL(0x0246e810, open_on_device,
            void *(FileDevice *, WideString *path, const char16_t *requested, uint64_t word_1, uint64_t word_2));
RT_ORIGINAL(0x0246e9a0, open_on_unnamed_devices,
            void *(DiskFileDevice *, WideString *path, const char16_t *requested, uint64_t word_1,
                   uint64_t word_2));

// The heap the opens make their working copy of a path on: an allocator inside the object
// 0x0247bbf0 returns, looked up once and kept.
RT_GLOBAL(0x05940668, path_copy_heap, engine::Allocator *);
RT_ORIGINAL(0x0247bbf0, path_copy_heap_owner, uint8_t *());
constexpr ptrdiff_t path_copy_heap_offset = 0x458;

// The thread's temporary heap (made on first use by 0x0247d250), found at a thread-local offset;
// its allocator is at +0x28.
struct ThreadHeap {
    uint8_t unknown_0x00[0x28];
    engine::Allocator allocator;
};
static_assert(offsetof(ThreadHeap, allocator) == 0x28);
RT_GLOBAL(0x057e4568, thread_heap_offset, const int64_t);
RT_ORIGINAL(0x0247d250, thread_heap_init, void());
RT_ORIGINAL(0x024716d0, mount_table_reallocate, void(MountTable *, uint64_t capacity));

// The mount separator (L":"), and the root whose replacement ends a resolution (L"title").
RT_GLOBAL(0x04dc10aa, mount_separator, const char16_t);
RT_GLOBAL(0x04dc10ba, title_root_name, const char16_t);

constexpr uint64_t npos = ~uint64_t{0};

inline const char16_t *text_of(const WideString &s)
{
    return s.capacity >= engine::wide_string_inline_capacity ? s.heap : s.inline_text;
}

// The empty string a copy starts from before its text is assigned.
inline void begin_copy(WideString *copy, const WideString &source)
{
    copy->allocator = source.allocator;
    copy->capacity = engine::wide_string_inline_capacity - 1;
    copy->size = 0;
    copy->inline_text[0] = u'\0';
}

// Copy-constructs a mount at `slot` (raw memory) from `source`.
inline void construct_copy(MountEntry *slot, const MountEntry &source)
{
    begin_copy(&slot->name, source.name);
    wide_string_assign_part(&slot->name, &source.name, 0, npos);
    slot->name.owns = source.name.owns;
    begin_copy(&slot->target, source.target);
    wide_string_assign_part(&slot->target, &source.target, 0, npos);
    slot->target.owns = source.target.owns;
}

}  // namespace loading::disk_file_device
