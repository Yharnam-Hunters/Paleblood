// SPDX-License-Identifier: GPL-2.0-or-later
// Loading: finding an entry by path name in one of the engine's (FD4's) hashed tables.
//
// A path key holds a wide string and the hash of its text, computed when first needed: the text
// is hashed case-insensitively for ASCII letters and with '\' read as '/', so "A\B" and "a/b"
// hash alike. The table chains its entries in buckets picked by the hash modulo the bucket
// count; within a bucket the engine's path comparison decides which entry matches.
#include <cstddef>
#include <cstdint>

#include "runtime/original.h"

namespace {

struct PathKey {
    const char16_t *text;
    uint32_t hash;
    uint8_t hash_stale;  // nonzero when the text changed: the hash is computed again
};
static_assert(offsetof(PathKey, hash) == 0x8 && offsetof(PathKey, hash_stale) == 0xc);

struct Entry {
    uint8_t unknown_0x00[0x8];
    PathKey key;
    uint8_t unknown_0x18[0x58 - 0x18];
    Entry *next;  // in the same bucket
};
static_assert(offsetof(Entry, key) == 0x8 && offsetof(Entry, next) == 0x58);

struct PathTable {
    uint8_t unknown_0x00[0x1c];
    uint32_t bucket_count;
    Entry **buckets;
};
static_assert(offsetof(PathTable, bucket_count) == 0x1c && offsetof(PathTable, buckets) == 0x20);

// The engine's path comparison: whether two keys name the same path.
RT_ORIGINAL(0x02db46f0, same_path, bool(PathKey *key, PathKey *other));

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

// The entry whose key names the same path as `key`, or null. A stale hash in `key` is
// computed and stored first.
extern "C" Entry *bb_loading_path_table_find_013e68f0(const PathTable *table, PathKey *key)
{
    if (key->hash_stale) {
        key->hash = path_hash(key->text);
        key->hash_stale = 0;
    }
    Entry *entry = table->buckets[key->hash % table->bucket_count];
    while (entry && !same_path(key, &entry->key))
        entry = entry->next;
    return entry;
}
