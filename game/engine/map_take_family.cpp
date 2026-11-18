// SPDX-License-Identifier: GPL-2.0-or-later
// The family takes_and_erases_map_entry_at_0x3b0 (symbols/families.csv, docs/FAMILIES.md): nine
// copies of one function that takes an entry out of the map at +0x3b0 of an object it does not
// otherwise touch. The map (engine/tree_container.h, msvc_tree.h) is keyed by 32-bit ids and holds
// references to counted objects (engine/ref_object.h). The result, a reference, is returned through
// the caller's slot (the first argument, which is also returned): cleared first; when the key is
// found, the entry's object (when there is one) is taken with a new reference, and the entry is
// erased through 0x022a8a80, the map's erase by iterator.
#include <cstddef>
#include <cstdint>

#include "msvc_tree.h"
#include "ref_object.h"
#include "runtime/original.h"
#include "tree_container.h"

namespace {

using Node = engine::TreeNode<uint32_t, engine::RefObject *>;
using Map = engine::Tree<Node>;

// The map's position in the object (the copies differ only in what calls them).
constexpr std::ptrdiff_t map_offset = 0x3b0;

// The map's iterator as erase takes it (by value, in memory) and returns it: only the node is
// set; the copies pass whatever their stack holds in the first 16 bytes.
struct MapIterator {
    uint8_t unknown_0x00[0x10];
    Node *node;
};
static_assert(sizeof(MapIterator) == 0x18 && offsetof(MapIterator, node) == 0x10);

RT_ORIGINAL(0x022a8a80, map_erase, MapIterator(Map *, MapIterator where));

// The result: a reference to a counted object, or null.
using Reference = engine::RefObject *;

}  // namespace

extern "C" Reference *bb_engine_takes_and_erases_map_entry_at_0x3b0(Reference *out, uint8_t *owner, const uint32_t *key)
{
    *out = nullptr;
    Map *map = reinterpret_cast<Map *>(owner + map_offset);
    Node *node = engine::tree_find(map->head, *key);
    if (node != map->head) {
        if (engine::RefObject *object = node->value) {
            *out = object;
            engine::retain(object);
        }
        MapIterator where{};
        where.node = node;
        map_erase(map, where);
    }
    return out;
}
