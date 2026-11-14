// SPDX-License-Identifier: GPL-2.0-or-later
// The engine's list (MSVC's std::list on an engine allocator) and its insert and push_back, which
// the game compiles once per value type (list_push_back.cpp has the copies).
//
// A list is a sentinel node, the size and the allocator; each node holds its next and previous
// node, then the value. An insert in front of `where` takes a node from the list's allocator (its
// aligned allocation), points it at `where` and at the node before it, and copies the value in. A
// list already at its largest size then throws the C++ library's length error with "list<T> too
// long" (each copy passes its own copy of the message; the throw does not return). Otherwise the
// size grows by one and the node is linked in: `where`'s previous node becomes it, and so does the
// next node of the node it points back to. push_back is the insert in front of the sentinel. Lists
// of values with a copy constructor build the value in the node through it (list_insert_with).
//
// A failed allocation goes to the engine's transaction handling (transaction.h). Outside a
// transaction that returns, and the game goes on to store through the null node, which faults;
// here as there.
#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

#include "allocator.h"
#include "runtime/original.h"
#include "transaction.h"

namespace engine {

template <typename T> struct ListNode {
    ListNode *next;
    ListNode *prev;
    T value;
};

template <typename T> struct List {
    void *proxy;
    ListNode<T> *sentinel;
    uint64_t size;
    Allocator *allocator;
};
static_assert(offsetof(List<uint64_t>, sentinel) == 0x8 && offsetof(List<uint64_t>, size) == 0x10 &&
              offsetof(List<uint64_t>, allocator) == 0x18);
static_assert(offsetof(ListNode<uint32_t>, value) == 0x10 && sizeof(ListNode<uint32_t>) == 0x18 &&
              sizeof(ListNode<uint64_t>) == 0x18);

// The largest size: one less than the most nodes the address space could hold.
template <typename T>
constexpr uint64_t list_max_size = std::numeric_limits<uint64_t>::max() / sizeof(ListNode<T>) - 1;

RT_ORIGINAL(0x02fbf1b8, throw_length_error, void(const char *message));

// Inserts a node in front of `where` (a node of the list, or its sentinel); `store_value` fills the
// new node's value (a plain copy, or the value type's copy constructor).
template <typename T, typename StoreValue>
void list_insert_with(List<T> *list, ListNode<T> *where, StoreValue store_value, const char *too_long_message)
{
    ListNode<T> *const before = where->prev;
    auto *node = static_cast<ListNode<T> *>(list->allocator->allocate(sizeof(ListNode<T>), alignof(ListNode<T>)));
    if (node != nullptr)
        node->next = where;
    else
        report_failed_allocation();
    node->prev = before;
    store_value(&node->value);
    if (list->size == list_max_size<T>) {
        throw_length_error(too_long_message);
        __builtin_unreachable();
    }
    list->size += 1;
    where->prev = node;
    node->prev->next = node;
}

// Inserts a copy of the value in front of `where`.
template <typename T>
void list_insert(List<T> *list, ListNode<T> *where, const T *value, const char *too_long_message)
{
    list_insert_with(list, where, [value](T *slot) { *slot = *value; }, too_long_message);
}

// Appends a copy of the value: an insert in front of the sentinel.
template <typename T> void list_push_back(List<T> *list, const T *value, const char *too_long_message)
{
    list_insert(list, list->sentinel, value, too_long_message);
}

}  // namespace engine
