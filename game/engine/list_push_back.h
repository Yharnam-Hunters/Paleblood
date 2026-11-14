// SPDX-License-Identifier: GPL-2.0-or-later
// The engine's list (MSVC's std::list on an engine allocator) and its push_back, which the game
// compiles once per value type (list_push_back.cpp has the copies).
//
// A list is a sentinel node, the size and the allocator; each node holds its next and previous
// node, then the value. push_back takes a node from the list's allocator (its aligned allocation),
// points it at the sentinel and at the last node, and copies the value in. A list already at its
// largest size then throws the C++ library's length error with "list<T> too long" (each copy
// passes its own copy of the message; the throw does not return). Otherwise the size grows by one
// and the node is linked in: the sentinel's previous node becomes it, and so does the next node of
// the node it points back to.
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

template <typename T> void list_push_back(List<T> *list, const T *value, const char *too_long_message)
{
    ListNode<T> *const sentinel = list->sentinel;
    ListNode<T> *const last = sentinel->prev;
    auto *node = static_cast<ListNode<T> *>(list->allocator->allocate(sizeof(ListNode<T>), alignof(ListNode<T>)));
    if (node != nullptr)
        node->next = sentinel;
    else
        report_failed_allocation();
    node->prev = last;
    node->value = *value;
    if (list->size == list_max_size<T>) {
        throw_length_error(too_long_message);
        __builtin_unreachable();
    }
    list->size += 1;
    sentinel->prev = node;
    node->prev->next = node;
}

}  // namespace engine
