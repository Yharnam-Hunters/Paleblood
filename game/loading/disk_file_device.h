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

struct DiskFileDevice {
    uint8_t unknown_0x00[0x58];
    MountTable roots;     // replaced once per round; "title" ends the resolution
    MountTable aliases;   // replaced repeatedly before the roots
};
static_assert(offsetof(DiskFileDevice, roots) == 0x58 && offsetof(DiskFileDevice, aliases) == 0x80);

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
