// SPDX-License-Identifier: GPL-2.0-or-later
// The family finds_id_pair_in_nested_hash_maps (symbols/families.csv, docs/FAMILIES.md): copies of
// one lookup of a pair of 32-bit ids in a hash map (at +0x8 of the first argument) whose values
// point at hash maps: true when the first id's map holds the second id. Both maps are the engine's
// chained hash table: a list of nodes ordered within each bucket by id, and a vector of buckets,
// each the first and last node of its run as list iterators. An id's bucket comes from its hash
// (the C library's ldiv by 127773 on the id xor 0xdeadbeef, then 16807 times the remainder less
// 2836 times the quotient, made non-negative) masked, folded below the bucket count when it
// lands past it. A bucket's run is walked until an id not below the one looked for.
#include <cstddef>
#include <cstdint>

#include "runtime/original.h"

namespace {

struct HashNode {
    HashNode *next;
    uint8_t unknown_0x08[0x8];
    uint32_t id;
    uint8_t unknown_0x14[0x4];
    void *value;
};
static_assert(offsetof(HashNode, id) == 0x10 && offsetof(HashNode, value) == 0x18);

// A list iterator as a bucket keeps it: only the node is read.
struct NodeIterator {
    uint8_t unknown_0x00[0x10];
    HashNode *node;
};
struct Bucket {
    NodeIterator first;
    NodeIterator last;
};
static_assert(offsetof(Bucket, last) == 0x18 && sizeof(Bucket) == 0x30);

struct HashTable {
    uint8_t unknown_0x00[0x8];
    HashNode *end;
    uint8_t unknown_0x10[0x18];
    Bucket *buckets;
    uint8_t unknown_0x30[0x18];
    uint64_t mask;
    uint64_t bucket_count;
};
static_assert(offsetof(HashTable, end) == 0x8 && offsetof(HashTable, buckets) == 0x28);
static_assert(offsetof(HashTable, mask) == 0x48 && offsetof(HashTable, bucket_count) == 0x50);

// The outer map's place in the object (the copies differ only in what calls them).
struct Owner {
    uint8_t unknown_0x00[0x8];
    HashTable ids;
};
static_assert(offsetof(Owner, ids) == 0x8);

struct Quotient {
    int64_t quotient;
    int64_t remainder;
};
RT_ORIGINAL(0x02fbfd68, ldiv, Quotient(int64_t numerator, int64_t denominator));

constexpr uint32_t hash_seed = 0xdeadbeef;
constexpr int64_t hash_divisor = 127773;
constexpr uint64_t hash_remainder_factor = 16807;
constexpr uint64_t hash_quotient_factor = 2836;
constexpr uint64_t hash_wrap = INT64_MAX;

uint64_t id_hash(uint32_t id)
{
    const Quotient q = ldiv(int64_t{id ^ hash_seed}, hash_divisor);
    const uint64_t hash = static_cast<uint64_t>(q.remainder) * hash_remainder_factor -
                          static_cast<uint64_t>(q.quotient) * hash_quotient_factor;
    return static_cast<int64_t>(hash) < 0 ? hash + hash_wrap : hash;
}

HashNode *find(const HashTable *table, uint32_t id)
{
    uint64_t index = id_hash(id) & table->mask;
    if (table->bucket_count <= index) index -= (table->mask >> 1) + 1;
    HashNode *end = table->end;
    const Bucket &bucket = table->buckets[index];
    HashNode *node = bucket.first.node;
    if (node == end) return end;
    HashNode *stop = bucket.last.node->next;
    for (; node != stop; node = node->next)
        if (node->id >= id) return id < node->id ? end : node;
    return end;
}

}  // namespace

extern "C" bool bb_engine_finds_id_pair_in_nested_hash_maps(Owner *owner, const uint32_t *ids)
{
    HashNode *node = find(&owner->ids, ids[0]);
    if (node == owner->ids.end) return false;
    HashNode *found = find(static_cast<const HashTable *>(node->value), ids[1]);
    return found != static_cast<const HashTable *>(node->value)->end;
}
