// SPDX-License-Identifier: GPL-2.0-or-later
// The engine list's push_back and insert (list_push_back.h), as the game compiles them for each
// list: push_back in 27 copies for 64-bit values and 3 for 32-bit values (ai/debug_display.cpp
// calls 0x02222210 as the id list's push_back), insert in 5 copies for 64-bit values
// (render/update_list_and_level_copy.cpp calls 0x02379f00), and push_back in 5 copies for values
// built with their copy constructor (loading/resource_queue.h calls 0x02a6b1a0 as the finished
// list's push_back). The copies of each are the same code; they differ only in the value (its width,
// or its size and constructor) and in which copy of the "list<T> too long" message they pass to the
// length error.
#include <cstdint>

#include "list_push_back.h"
#include "runtime/original.h"

namespace {

// "list<T> too long", the copy most lists pass, and the three other copies.
RT_GLOBAL(0x04d40378, list_too_long_message, const char);
RT_GLOBAL(0x04cc2966, list_too_long_message_04cc2966, const char);
RT_GLOBAL(0x04cc5dcc, list_too_long_message_04cc5dcc, const char);
RT_GLOBAL(0x04ccacf4, list_too_long_message_04ccacf4, const char);

using List64 = engine::List<uint64_t>;
using List32 = engine::List<uint32_t>;

}  // namespace

extern "C" void bb_engine_list64_push_back_0179b2a0(List64 *list, const uint64_t *value)
{
    engine::list_push_back(list, value, list_too_long_message.address());
}

extern "C" void bb_engine_list64_push_back_017d5790(List64 *list, const uint64_t *value)
{
    engine::list_push_back(list, value, list_too_long_message.address());
}

extern "C" void bb_engine_list64_push_back_0182fd80(List64 *list, const uint64_t *value)
{
    engine::list_push_back(list, value, list_too_long_message.address());
}

extern "C" void bb_engine_list64_push_back_01860ea0(List64 *list, const uint64_t *value)
{
    engine::list_push_back(list, value, list_too_long_message.address());
}

extern "C" void bb_engine_list64_push_back_01860f90(List64 *list, const uint64_t *value)
{
    engine::list_push_back(list, value, list_too_long_message.address());
}

extern "C" void bb_engine_list64_push_back_01868b10(List64 *list, const uint64_t *value)
{
    engine::list_push_back(list, value, list_too_long_message.address());
}

extern "C" void bb_engine_list64_push_back_0187de40(List64 *list, const uint64_t *value)
{
    engine::list_push_back(list, value, list_too_long_message.address());
}

extern "C" void bb_engine_list64_push_back_0189e6a0(List64 *list, const uint64_t *value)
{
    engine::list_push_back(list, value, list_too_long_message.address());
}

extern "C" void bb_engine_list64_push_back_018ad810(List64 *list, const uint64_t *value)
{
    engine::list_push_back(list, value, list_too_long_message.address());
}

extern "C" void bb_engine_list64_push_back_018bf1d0(List64 *list, const uint64_t *value)
{
    engine::list_push_back(list, value, list_too_long_message.address());
}

extern "C" void bb_engine_list64_push_back_018c6cd0(List64 *list, const uint64_t *value)
{
    engine::list_push_back(list, value, list_too_long_message.address());
}

extern "C" void bb_engine_list64_push_back_019851b0(List64 *list, const uint64_t *value)
{
    engine::list_push_back(list, value, list_too_long_message.address());
}

extern "C" void bb_engine_list64_push_back_01b91b30(List64 *list, const uint64_t *value)
{
    engine::list_push_back(list, value, list_too_long_message.address());
}

extern "C" void bb_engine_list64_push_back_01c4c2d0(List64 *list, const uint64_t *value)
{
    engine::list_push_back(list, value, list_too_long_message.address());
}

extern "C" void bb_engine_list64_push_back_01c4c3c0(List64 *list, const uint64_t *value)
{
    engine::list_push_back(list, value, list_too_long_message.address());
}

extern "C" void bb_engine_list64_push_back_020f0380(List64 *list, const uint64_t *value)
{
    engine::list_push_back(list, value, list_too_long_message.address());
}

extern "C" void bb_engine_list64_push_back_020f2ff0(List64 *list, const uint64_t *value)
{
    engine::list_push_back(list, value, list_too_long_message.address());
}

extern "C" void bb_engine_list64_push_back_020fed20(List64 *list, const uint64_t *value)
{
    engine::list_push_back(list, value, list_too_long_message.address());
}

extern "C" void bb_engine_list64_push_back_02105de0(List64 *list, const uint64_t *value)
{
    engine::list_push_back(list, value, list_too_long_message.address());
}

extern "C" void bb_engine_list64_push_back_0219fff0(List64 *list, const uint64_t *value)
{
    engine::list_push_back(list, value, list_too_long_message.address());
}

extern "C" void bb_engine_list64_push_back_0223f000(List64 *list, const uint64_t *value)
{
    engine::list_push_back(list, value, list_too_long_message.address());
}

extern "C" void bb_engine_list64_push_back_02390550(List64 *list, const uint64_t *value)
{
    engine::list_push_back(list, value, list_too_long_message.address());
}

extern "C" void bb_engine_list64_push_back_023a07e0(List64 *list, const uint64_t *value)
{
    engine::list_push_back(list, value, list_too_long_message.address());
}

extern "C" void bb_engine_list64_push_back_02432780(List64 *list, const uint64_t *value)
{
    engine::list_push_back(list, value, list_too_long_message.address());
}

extern "C" void bb_engine_list64_push_back_02db58d0(List64 *list, const uint64_t *value)
{
    engine::list_push_back(list, value, list_too_long_message_04cc2966.address());
}

extern "C" void bb_engine_list64_push_back_02dc9180(List64 *list, const uint64_t *value)
{
    engine::list_push_back(list, value, list_too_long_message_04cc5dcc.address());
}

extern "C" void bb_engine_list64_push_back_02dd3540(List64 *list, const uint64_t *value)
{
    engine::list_push_back(list, value, list_too_long_message_04ccacf4.address());
}

extern "C" void bb_engine_list32_push_back_017dc0f0(List32 *list, const uint32_t *value)
{
    engine::list_push_back(list, value, list_too_long_message.address());
}

extern "C" void bb_engine_list32_push_back_02222210(List32 *list, const uint32_t *value)
{
    engine::list_push_back(list, value, list_too_long_message.address());
}

extern "C" void bb_engine_list32_push_back_023a9520(List32 *list, const uint32_t *value)
{
    engine::list_push_back(list, value, list_too_long_message.address());
}

extern "C" void bb_engine_list64_insert_01727a30(List64 *list, engine::ListNode<uint64_t> *where, const uint64_t *value)
{
    engine::list_insert(list, where, value, list_too_long_message.address());
}

extern "C" void bb_engine_list64_insert_01e77890(List64 *list, engine::ListNode<uint64_t> *where, const uint64_t *value)
{
    engine::list_insert(list, where, value, list_too_long_message.address());
}

extern "C" void bb_engine_list64_insert_01e77980(List64 *list, engine::ListNode<uint64_t> *where, const uint64_t *value)
{
    engine::list_insert(list, where, value, list_too_long_message.address());
}

extern "C" void bb_engine_list64_insert_022cdba0(List64 *list, engine::ListNode<uint64_t> *where, const uint64_t *value)
{
    engine::list_insert(list, where, value, list_too_long_message.address());
}

extern "C" void bb_engine_list64_insert_02379f00(List64 *list, engine::ListNode<uint64_t> *where, const uint64_t *value)
{
    engine::list_insert(list, where, value, list_too_long_message.address());
}

namespace {

// Values the game builds with their own copy constructors; only their sizes are known here.
constexpr uint64_t node_links_size = 2 * sizeof(void *);
template <uint64_t NodeSize> struct alignas(sizeof(void *)) OpaqueValue {
    uint8_t bytes[NodeSize - node_links_size];
};

constexpr uint64_t node_size_02a6b1a0 = 0x58;
using Value_02a6b1a0 = OpaqueValue<node_size_02a6b1a0>;
RT_ORIGINAL(0x02aa4ab0, copy_construct_02aa4ab0, void(Value_02a6b1a0 *, const Value_02a6b1a0 *));
RT_GLOBAL(0x04c6bde8, list_too_long_message_04c6bde8, const char);

constexpr uint64_t node_size_02b2dae0 = 0x1e8;
using Value_02b2dae0 = OpaqueValue<node_size_02b2dae0>;
RT_ORIGINAL(0x010d1d30, copy_construct_010d1d30, void(Value_02b2dae0 *, const Value_02b2dae0 *));
RT_GLOBAL(0x04c747c5, list_too_long_message_04c747c5, const char);

constexpr uint64_t node_size_02b2f840 = 0x1a8;
using Value_02b2f840 = OpaqueValue<node_size_02b2f840>;
RT_ORIGINAL(0x0108a340, copy_construct_0108a340, void(Value_02b2f840 *, const Value_02b2f840 *));
RT_GLOBAL(0x04c74ee3, list_too_long_message_04c74ee3, const char);

constexpr uint64_t node_size_02b306f0 = 0x18;
using Value_02b306f0 = OpaqueValue<node_size_02b306f0>;
RT_ORIGINAL(0x010d5310, copy_construct_010d5310, void(Value_02b306f0 *, const Value_02b306f0 *));
RT_GLOBAL(0x04c750c8, list_too_long_message_04c750c8, const char);

constexpr uint64_t node_size_02da1630 = 0x80;
using Value_02da1630 = OpaqueValue<node_size_02da1630>;
RT_ORIGINAL(0x02d9fcf0, copy_construct_02d9fcf0, void(Value_02da1630 *, const Value_02da1630 *));
RT_GLOBAL(0x04cbcee6, list_too_long_message_04cbcee6, const char);

}  // namespace

extern "C" void bb_engine_list_push_back_constructed_02a6b1a0(engine::List<Value_02a6b1a0> *list,
                                                             const Value_02a6b1a0 *value)
{
    engine::list_insert_with(
        list, list->sentinel, [value](Value_02a6b1a0 *slot) { copy_construct_02aa4ab0(slot, value); },
        list_too_long_message_04c6bde8.address());
}

extern "C" void bb_engine_list_push_back_constructed_02b2dae0(engine::List<Value_02b2dae0> *list,
                                                             const Value_02b2dae0 *value)
{
    engine::list_insert_with(
        list, list->sentinel, [value](Value_02b2dae0 *slot) { copy_construct_010d1d30(slot, value); },
        list_too_long_message_04c747c5.address());
}

extern "C" void bb_engine_list_push_back_constructed_02b2f840(engine::List<Value_02b2f840> *list,
                                                             const Value_02b2f840 *value)
{
    engine::list_insert_with(
        list, list->sentinel, [value](Value_02b2f840 *slot) { copy_construct_0108a340(slot, value); },
        list_too_long_message_04c74ee3.address());
}

extern "C" void bb_engine_list_push_back_constructed_02b306f0(engine::List<Value_02b306f0> *list,
                                                             const Value_02b306f0 *value)
{
    engine::list_insert_with(
        list, list->sentinel, [value](Value_02b306f0 *slot) { copy_construct_010d5310(slot, value); },
        list_too_long_message_04c750c8.address());
}

extern "C" void bb_engine_list_push_back_constructed_02da1630(engine::List<Value_02da1630> *list,
                                                             const Value_02da1630 *value)
{
    engine::list_insert_with(
        list, list->sentinel, [value](Value_02da1630 *slot) { copy_construct_02d9fcf0(slot, value); },
        list_too_long_message_04cbcee6.address());
}
