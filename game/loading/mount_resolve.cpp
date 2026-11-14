// SPDX-License-Identifier: GPL-2.0-or-later
// Loading: replace a path's mount prefix from one of the disk file device's mount tables
// (structs in disk_file_device.h).
//
// loading_disk_device_resolve_root_0246cf50 (0x0246cf50, the root table) and
// loading_disk_device_resolve_alias_0246d180 (0x0246d180, the alias table) are the same code on
// different tables; the device's path resolution (0x0246d550) calls them in turn. Each splits the
// path's first token off at the mount separator ':' and skips the separators after it; a path
// with no table entries, no token, or nothing after the separators is left alone (false). So is a
// path shorter than the name plus two characters. Otherwise the first entry whose name equals the
// token (same length, wcsncmp) replaces the path's first name-length + 2 characters ("name:" and
// the character after it, normally '/') with its target. The result says whether to go on
// resolving: true unless the entry's name is "title", the root everything else ends in. With no
// matching entry the path is unchanged and the result is false.
#include <cstdint>

#include "disk_file_device.h"

using namespace loading::disk_file_device;

namespace {

// "title:/" style prefixes: the name, the separator and one more character.
constexpr uint64_t prefix_extra_characters = 2;

bool is_separator(char16_t character)
{
    return c_wcschr(mount_separator.address(), character) != nullptr;
}

// The table's first entry named `name`, or null.
const MountEntry *find_mount(const MountTable &table, const Token &name)
{
    for (const MountEntry *entry = table.begin; entry != table.end; ++entry) {
        if (entry->name.size == name.length &&
            c_wcsncmp(name.text, text_of(entry->name), name.length) == 0)
            return entry;
    }
    return nullptr;
}

// The wide string comparison with "title", as the string class does it: the common length with
// wmemcmp, then the lengths.
bool is_title_root(const WideString &name)
{
    const uint64_t title_length = c_wcslen(title_root_name.address());
    const uint64_t common = name.size < title_length ? name.size : title_length;
    const int32_t order = common ? c_wmemcmp(text_of(name), title_root_name.address(), common) : 0;
    return order == 0 && name.size == title_length;
}

bool resolve_mount(const MountTable &table, WideString *path)
{
    if (table.end == table.begin)
        return false;

    const char16_t *text = text_of(*path);
    TokenCursor cursor{text, mount_separator.address(), text ? c_wcslen(text) : 0};
    const Token name = path_token_next(&cursor);
    if (!cursor.delimiters || !cursor.position || cursor.remaining == 0)
        return false;

    // Skip the separators after the name; running out of characters there means no mount.
    while (is_separator(*cursor.position)) {
        cursor.position += 1;
        if (cursor.remaining == 1)
            return false;
        cursor.remaining -= 1;
    }
    if (*cursor.position == u'\0')
        return false;

    const uint64_t prefix_length = name.length + prefix_extra_characters;
    if (path->size < prefix_length)
        return false;
    const MountEntry *entry = find_mount(table, name);
    if (!entry)
        return false;
    wide_string_replace(path, 0, prefix_length, &entry->target, 0, npos);
    return !is_title_root(entry->name);
}

}  // namespace

extern "C" bool bb_loading_disk_device_resolve_root_0246cf50(DiskFileDevice *device, WideString *path)
{
    return resolve_mount(device->roots, path);
}

extern "C" bool bb_loading_disk_device_resolve_alias_0246d180(DiskFileDevice *device, WideString *path)
{
    return resolve_mount(device->aliases, path);
}
