// SPDX-License-Identifier: GPL-2.0-or-later
// Leaf families over a sixteen-byte key and a singly linked list (symbols/families.csv); every
// member of each is the same bytes.
//
// matches_sixteen_byte_key(object, key, size) is 1 when size (32 bits) is the key's 16 and the
// key equals the 16 bytes 0x18 into the object, else 0. The second halves are compared only when
// the key's first half is not zero.
//
// The list starts at the node pointer 0x30 into the object; each node holds its next pointer at
// +0x10 and a 32-bit value at +0x18. counts_list_nodes returns how many nodes there are and
// reads_nth_list_node_dword(object, index) the value of node index (32 bits, counted from 0), or
// 0 when the list is shorter. Both return their 32-bit result zero-extended.
#include <cstdint>

namespace {

struct Key {
    uint64_t first;
    uint64_t second;
};

struct ObjectWithKey {
    uint8_t unknown_before_key[0x18];
    Key key;
};

struct ListNode {
    uint8_t unknown_before_next[0x10];
    ListNode *next;
    uint32_t value;
};

struct ObjectWithList {
    uint8_t unknown_before_list[0x30];
    ListNode *first;
};

}  // namespace

extern "C" uint64_t bb_leaf_matches_sixteen_byte_key(const ObjectWithKey *object, const Key *key, uint32_t size)
{
    if (size != sizeof(Key))
        return 0;
    const bool second_matches = key->first == 0 || key->second == object->key.second;
    return key->first == object->key.first && second_matches;
}

extern "C" uint64_t bb_leaf_counts_list_nodes(const ObjectWithList *object)
{
    uint32_t count = 0;
    for (const ListNode *node = object->first; node != nullptr; node = node->next)
        ++count;
    return count;
}

extern "C" uint64_t bb_leaf_reads_nth_list_node_dword(const ObjectWithList *object, uint32_t index)
{
    for (const ListNode *node = object->first; node != nullptr; node = node->next, --index) {
        if (index == 0)
            return node->value;
    }
    return 0;
}
