// SPDX-License-Identifier: GPL-2.0-or-later
// visits_children_of_list_at_0x40 and secondary_base_visits_children_of_list_at_0x40
// (symbols/families.csv): the copies of a method that walks the children of the list its object
// points at (+0x40): each child goes to 0x00b69e20 with the object's part at +0x10, then to the
// object's own slot 0x28. The count is read once, before the walk; the list and its array are read
// again for every child after the first, so a slot call may change them. The second family is the
// same method entered through a secondary base 0x48 bytes into the object.
#include <cstddef>
#include <cstdint>

#include "runtime/original.h"

namespace {

struct Child;
struct ChildList {
    uint8_t unknown_0x00[0xa0];
    Child **children;
    int32_t count;
};
static_assert(offsetof(ChildList, children) == 0xa0 && offsetof(ChildList, count) == 0xa8);

struct Visitor;
struct VisitorPart;
struct VisitorVtable {
    uint8_t unknown_0x00[0x28];
    void (*visit_child)(Visitor *, Child *);
};
static_assert(offsetof(VisitorVtable, visit_child) == 0x28);
struct Visitor {
    const VisitorVtable *vtable;
    uint8_t unknown_0x08[0x38];
    ChildList *list;
};
static_assert(offsetof(Visitor, list) == 0x40);
// The part of the object every child is added to (only its address is used here).
constexpr std::ptrdiff_t part_offset = 0x10;

RT_ORIGINAL(0x00b69e20, pass_child, void(Child *, VisitorPart *));

// Where the secondary base the second family is entered through sits in the object.
constexpr std::ptrdiff_t secondary_base_offset = 0x48;

void visit_children(Visitor *visitor)
{
    const ChildList *list = visitor->list;
    if (!list)
        return;
    const int32_t count = list->count;
    auto *part = reinterpret_cast<VisitorPart *>(reinterpret_cast<uint8_t *>(visitor) + part_offset);
    for (int32_t index = 0; index < count; ++index) {
        Child *child = visitor->list->children[index];
        pass_child(child, part);
        visitor->vtable->visit_child(visitor, child);
    }
}

}  // namespace

extern "C" void bb_engine_visits_children_of_list_at_0x40(Visitor *visitor)
{
    visit_children(visitor);
}

extern "C" void bb_engine_secondary_base_visits_children_of_list_at_0x40(uint8_t *secondary_base)
{
    visit_children(reinterpret_cast<Visitor *>(secondary_base - secondary_base_offset));
}
