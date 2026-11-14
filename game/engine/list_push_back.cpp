// SPDX-License-Identifier: GPL-2.0-or-later
// The engine list's push_back (list_push_back.h), as the game compiles it for each list: 27 copies
// for 64-bit values and 3 for 32-bit values (ai/debug_display.cpp calls 0x02222210 as the id
// list's push_back). Every copy is the same code; they differ only in the value's width and in
// which copy of the "list<T> too long" message they pass to the length error.
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
