// SPDX-License-Identifier: GPL-2.0-or-later
// Loading: finding an entry by path name in one of the engine's (FD4's) hashed tables.
//
// A path key holds a pointer to wide text and the hash of that text, computed when first needed:
// the text is hashed case-insensitively for ASCII letters and with '\' read as '/', so "A\B" and
// "a/b" hash alike. Each entry is filed under a path name of its own (the engine's wide string
// object and the same kind of cached hash). The table chains its entries in buckets picked by
// the hash modulo the bucket count; within a bucket the engine's path comparison decides which
// entry matches.
#include <cstddef>
#include <cstdint>

#include "../engine/wide_string.h"
#include "runtime/original.h"

namespace {

struct PathKey {
    const char16_t *text;
    uint32_t hash;
    uint8_t hash_stale;  // nonzero when the text changed: the hash is computed again
};
static_assert(offsetof(PathKey, hash) == 0x8 && offsetof(PathKey, hash_stale) == 0xc);

// The name an entry is filed under, as the path comparison reads it: the path as the engine's
// wide string object and the hash of its text, cached and marked stale like a key's.
struct EntryPathName {
    uint8_t unknown_0x00[0x8];
    engine::WideString text;
    uint32_t hash;
    uint8_t hash_stale;
};
static_assert(offsetof(EntryPathName, text) == 0x8 && offsetof(EntryPathName, hash) == 0x40);
static_assert(offsetof(EntryPathName, hash_stale) == 0x44 && sizeof(EntryPathName) == 0x48);

struct Entry {
    uint8_t unknown_0x00[0x8];
    EntryPathName name;  // wide string at +0x10, hash +0x48, stale byte +0x4c
    uint8_t unknown_0x50[0x58 - 0x50];
    Entry *next;  // in the same bucket
};
static_assert(offsetof(Entry, name) == 0x8 && offsetof(Entry, next) == 0x58);

struct PathTable {
    uint8_t unknown_0x00[0x1c];
    uint32_t bucket_count;
    Entry **buckets;
};
static_assert(offsetof(PathTable, bucket_count) == 0x1c && offsetof(PathTable, buckets) == 0x20);

// The engine's path comparison: whether a key names the same path as an entry's name (it also
// brings a stale hash in the name up to date).
RT_ORIGINAL(0x02db46f0, same_path, bool(PathKey *key, EntryPathName *name));

constexpr uint32_t hash_multiplier = 0x89;

char16_t fold(char16_t c)
{
    if (c >= u'A' && c <= u'Z')
        return static_cast<char16_t>(c + (u'a' - u'A'));
    if (c == u'\\')
        return u'/';
    return c;
}

uint32_t path_hash(const char16_t *text)
{
    uint32_t hash = 0;
    if (!text)
        return hash;
    for (; fold(*text) != 0; ++text)
        hash = hash * hash_multiplier + fold(*text);
    return hash;
}

}  // namespace

// The entry whose name is the same path as `key`, or null. A stale hash in `key` is
// computed and stored first.
extern "C" Entry *bb_loading_path_table_find_013e68f0(const PathTable *table, PathKey *key)
{
    if (key->hash_stale) {
        key->hash = path_hash(key->text);
        key->hash_stale = 0;
    }
    Entry *entry = table->buckets[key->hash % table->bucket_count];
    while (entry && !same_path(key, &entry->name))
        entry = entry->next;
    return entry;
}
