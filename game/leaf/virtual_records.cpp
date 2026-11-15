// SPDX-License-Identifier: GPL-2.0-or-later
// Leaf families that call virtual functions of their objects (symbols/families.csv); every member
// of each is the same bytes.
//
// The string writers pass a text and a value to slot 0x90 of a sink (a variadic call: the value is
// the result of the record's own slot 0x60). The toggles switch something on or off through slot
// 0 of their object and keep the state in a byte. The resets free a wide string's heap text through
// slot 0x70 of the allocator stored after it and leave the string empty.
//
// A wide string holds 16-bit characters: up to seven and the terminator inline, else a pointer to
// heap text in the same place; its size and capacity follow.
#include <cstddef>
#include <cstdint>

namespace {

// The function slot_offset bytes into the object's virtual table.
template <typename Function>
Function virtual_slot(const void *object, std::size_t slot_offset)
{
    const uint8_t *virtual_table = *static_cast<const uint8_t *const *>(object);
    return *reinterpret_cast<const Function *>(virtual_table + slot_offset);
}

constexpr uint64_t heap_capacity = 8;     // a capacity this large keeps the text on the heap
constexpr uint64_t inline_capacity = 7;   // an empty string's capacity

struct WideString {
    union {
        char16_t inline_text[heap_capacity];
        char16_t *heap_text;
    };
    uint64_t size;
    uint64_t capacity;

    const char16_t *text() const { return capacity < heap_capacity ? inline_text : heap_text; }
};
static_assert(offsetof(WideString, size) == 0x10);
static_assert(offsetof(WideString, capacity) == 0x18);

// Writers.
using ValueFunction = uint64_t (*)(void *record);
using SinkWrite = uint64_t (*)(void *sink, const char16_t *text, ...);
constexpr std::size_t value_slot = 0x60;
constexpr std::size_t write_slot = 0x90;

struct RecordWithStringAt0x28 {
    uint8_t unknown_0x0[0x28];
    WideString string;
};
static_assert(offsetof(RecordWithStringAt0x28, string) == 0x28);

struct StringAt8 {
    uint64_t unknown_0x0;
    WideString string;
};

struct RecordPointingAtString {
    uint8_t unknown_0x0[0x20];
    const StringAt8 *string;
};
static_assert(offsetof(RecordPointingAtString, string) == 0x20);

struct RecordWithTextAt0x20 {
    uint8_t unknown_0x0[0x20];
    const char16_t *text;
};
static_assert(offsetof(RecordWithTextAt0x20, text) == 0x20);

// The sink's write is read first and the text before the record's value is asked for, as in the
// originals.
uint64_t write_to_sink(void *record, void *sink, SinkWrite write, const char16_t *text)
{
    const uint64_t value = virtual_slot<ValueFunction>(record, value_slot)(record);
    return write(sink, text, value);
}

// Toggles: slot 0 switches on (any second argument but 0) or off (0); a result whose low 32 bits
// are not 0 is a failure and comes back whole, leaving the state byte as it was.
using SwitchFunction = uint64_t (*)(void *object, uint64_t on);
constexpr std::size_t switch_slot = 0;

template <std::size_t StateOffset>
uint64_t toggle(void *object, uint64_t on)
{
    uint8_t *state = static_cast<uint8_t *>(object) + StateOffset;
    if (on != 0 && *state != 0)
        return 0;
    const uint64_t result = virtual_slot<SwitchFunction>(object, switch_slot)(object, on);
    if (static_cast<uint32_t>(result) != 0)
        return result;
    *state = on != 0;
    return 0;
}

// Resets.
using FreeFunction = void (*)(void *allocator, char16_t *text);
constexpr std::size_t free_slot = 0x70;

struct OwnedWideString {
    WideString string;
    void *allocator;
};
static_assert(offsetof(OwnedWideString, allocator) == 0x20);

template <std::size_t Offset>
void reset(void *object)
{
    auto *owned = reinterpret_cast<OwnedWideString *>(static_cast<uint8_t *>(object) + Offset);
    if (owned->string.capacity >= heap_capacity)
        virtual_slot<FreeFunction>(owned->allocator, free_slot)(owned->allocator, owned->string.heap_text);
    owned->string.capacity = inline_capacity;
    owned->string.size = 0;
    owned->string.inline_text[0] = 0;
}

constexpr std::size_t first_state = 8;
constexpr std::size_t second_state = 0x20;
constexpr std::size_t first_string = 8;
constexpr std::size_t second_string = 0x10;

}  // namespace

extern "C" uint64_t bb_leaf_writes_wide_string_at_0x28_to_sink(void *record, void *sink)
{
    const SinkWrite write = virtual_slot<SinkWrite>(sink, write_slot);
    return write_to_sink(record, sink, write, static_cast<const RecordWithStringAt0x28 *>(record)->string.text());
}

extern "C" uint64_t bb_leaf_writes_wide_string_through_0x20_to_sink(void *record, void *sink)
{
    const SinkWrite write = virtual_slot<SinkWrite>(sink, write_slot);
    const char16_t *text = static_cast<const RecordPointingAtString *>(record)->string->string.text();
    return write_to_sink(record, sink, write, text);
}

extern "C" uint64_t bb_leaf_writes_text_at_0x20_to_sink(void *record, void *sink)
{
    const SinkWrite write = virtual_slot<SinkWrite>(sink, write_slot);
    return write_to_sink(record, sink, write, static_cast<const RecordWithTextAt0x20 *>(record)->text);
}

extern "C" uint64_t bb_leaf_toggles_flag_at_8_through_first_virtual(void *object, uint64_t on)
{
    return toggle<first_state>(object, on);
}

extern "C" uint64_t bb_leaf_toggles_flag_at_0x20_through_first_virtual(void *object, uint64_t on)
{
    return toggle<second_state>(object, on);
}

extern "C" void bb_leaf_resets_wide_string_at_8(void *object)
{
    reset<first_string>(object);
}

extern "C" void bb_leaf_resets_wide_string_at_0x10(void *object)
{
    reset<second_string>(object);
}

// Five more families: a call through the nth node of a list, a slot that gets the arguments
// shifted down, a stop-store-start sequence, a guarded flush, and a call through the object
// another slot returns.
namespace {

using SixArgumentFunction = uint64_t (*)(void *object, uint64_t second, uint64_t third, uint64_t fourth,
                                         uint64_t fifth, uint64_t sixth);

struct ListNode {
    uint8_t unknown_0x0[0x10];
    ListNode *next;
};
static_assert(offsetof(ListNode, next) == 0x10);

struct ObjectWithListAt0x30 {
    uint8_t unknown_0x0[0x30];
    ListNode *first;
};
static_assert(offsetof(ObjectWithListAt0x30, first) == 0x30);

constexpr std::size_t node_slot = 0;
constexpr std::size_t shifted_slot = 0x88;

using StopFunction = uint64_t (*)(void *object, uint64_t zero);
using StartFunction = uint64_t (*)(void *object);
constexpr std::size_t stop_slot = 0x78;
constexpr std::size_t start_slot = 0xa8;

struct Restartable {
    uint64_t virtual_table;
    uint8_t started;
    uint8_t unknown_0x9[0x17];
    uint32_t setting;
};
static_assert(offsetof(Restartable, started) == 8);
static_assert(offsetof(Restartable, setting) == 0x20);

// The flushed object: bit 0 of its flags says it has work, bit 1 that a flush is running.
using FlushFunction = void (*)(void *pending, uint64_t argument);
constexpr std::size_t prepare_slot = 0x18;
constexpr std::size_t flush_slot = 0x20;
constexpr uint8_t has_work = 1;
constexpr uint8_t flushing = 2;

struct Pending {
    uint64_t virtual_table;
    uint32_t unknown_0x8;
    uint8_t flags;
    uint8_t unknown_0xd[3];
    uint32_t count;
};
static_assert(offsetof(Pending, flags) == 0xc);
static_assert(offsetof(Pending, count) == 0x10);

struct HolderOfPending {
    uint8_t unknown_0x0[0x40];
    Pending *const *pending;
};
static_assert(offsetof(HolderOfPending, pending) == 0x40);

using InnerFunction = void *(*)(void *object, uint64_t second);
using OuterFunction = uint64_t (*)(void *inner, uint64_t second);
constexpr std::size_t inner_slot = 0x38;
constexpr std::size_t outer_slot = 0x68;

}  // namespace

extern "C" uint64_t bb_leaf_tail_calls_slot_0_of_nth_node_at_0x30(void *object, uint64_t index, uint64_t third,
                                                                  uint64_t fourth, uint64_t fifth, uint64_t sixth)
{
    ListNode *node = static_cast<ObjectWithListAt0x30 *>(object)->first;
    if (node == nullptr)
        return 0;
    for (uint32_t steps = static_cast<uint32_t>(index); steps != 0; --steps) {
        node = node->next;
        if (node == nullptr)
            return 0;
    }
    return virtual_slot<SixArgumentFunction>(node, node_slot)(node, 0, third, fourth, fifth, sixth);
}

extern "C" uint64_t bb_leaf_shifts_arguments_into_virtual_0x88(void *object, uint64_t second, uint64_t third,
                                                              uint64_t fourth, uint64_t fifth, uint64_t sixth)
{
    (void)second;
    const uint64_t low_byte = static_cast<uint8_t>(sixth);
    return virtual_slot<SixArgumentFunction>(object, shifted_slot)(object, third, fourth, fifth, low_byte, 0);
}

extern "C" uint64_t bb_leaf_calls_0x78_stores_dword_at_0x20_calls_0xa8(void *object, uint64_t setting)
{
    auto *restartable = static_cast<Restartable *>(object);
    virtual_slot<StopFunction>(object, stop_slot)(object, 0);
    restartable->setting = static_cast<uint32_t>(setting);
    const uint64_t result = virtual_slot<StartFunction>(object, start_slot)(object);
    restartable->started = 1;
    return result;
}

extern "C" void bb_leaf_flushes_pending_through_member_at_0x40(void *object, uint64_t argument)
{
    Pending *const *holder = static_cast<HolderOfPending *>(object)->pending;
    if (holder == nullptr)
        return;
    Pending *pending = *holder;
    if (pending == nullptr || !(pending->flags & has_work) || pending->count == 0 || (pending->flags & flushing))
        return;
    pending->flags |= flushing;
    virtual_slot<FlushFunction>(pending, prepare_slot)(pending, argument);
    virtual_slot<FlushFunction>(pending, flush_slot)(pending, argument);
    pending->flags &= static_cast<uint8_t>(~flushing);
    pending->count = 0;
}

extern "C" uint64_t bb_leaf_tail_calls_0x68_of_what_0x38_returns(void *object, uint64_t second)
{
    void *inner = virtual_slot<InnerFunction>(object, inner_slot)(object, second);
    return virtual_slot<OuterFunction>(inner, outer_slot)(inner, second);
}

// Tree erases: the nodes of a subtree are freed through slot 0x70 of the tree's allocator, the
// right subtree first (recursively), then the node, then on along its left child, until the nil
// node. The second family resets the wide string each node holds first.
namespace {

struct TreeNode {
    TreeNode *left;
    TreeNode *parent;
    TreeNode *right;
    uint8_t color;
    uint8_t is_nil;
};
static_assert(offsetof(TreeNode, right) == 0x10);
static_assert(offsetof(TreeNode, is_nil) == 0x19);

struct TreeWithAllocator {
    uint8_t unknown_0x0[0x18];
    void *allocator;
};
static_assert(offsetof(TreeWithAllocator, allocator) == 0x18);

using NodeFreeFunction = void (*)(void *allocator, TreeNode *node);
constexpr std::size_t node_free_slot = 0x70;
constexpr std::size_t node_string = 0x28;

template <bool ResetString>
void erase_subtree(TreeWithAllocator *tree, TreeNode *node)
{
    while (!node->is_nil) {
        erase_subtree<ResetString>(tree, node->right);
        TreeNode *left = node->left;
        if constexpr (ResetString)
            reset<node_string>(node);
        virtual_slot<NodeFreeFunction>(tree->allocator, node_free_slot)(tree->allocator, node);
        node = left;
    }
}

}  // namespace

extern "C" void bb_leaf_erases_subtree_through_allocator_at_0x18(void *tree, void *node)
{
    erase_subtree<false>(static_cast<TreeWithAllocator *>(tree), static_cast<TreeNode *>(node));
}

extern "C" void bb_leaf_erases_subtree_with_wide_string_at_0x28(void *tree, void *node)
{
    erase_subtree<true>(static_cast<TreeWithAllocator *>(tree), static_cast<TreeNode *>(node));
}
