// SPDX-License-Identifier: GPL-2.0-or-later
// Leaf families of delegate calls (symbols/families.csv); every member of each is the same bytes.
// A record holds an object pointer and a pointer to member function (game/leaf/member_function.h),
// and the function calls that method on that object with values from the record or the caller,
// passing on the arguments it does not set as they came (game/leaf/forwarding.h). The guarded ones
// return without a call when the method or the object is not set or a flag is clear: the original
// then leaves rax as it was (their methods return nothing), and the replacement returns 0.
//
// The structs below are only what each family reads; what the records are is not traced here.
#include "member_function.h"

namespace {

struct CallsDelegateWithArgumentField0x10Record {
    uint8_t unknown_0x0[0x8];
    void *object;
    MemberFunctionPointer method;
    uint8_t unknown_0x20[0x8];
    uint32_t value;
};
static_assert(offsetof(CallsDelegateWithArgumentField0x10Record, object) == 0x8);
static_assert(offsetof(CallsDelegateWithArgumentField0x10Record, method) == 0x10);
static_assert(offsetof(CallsDelegateWithArgumentField0x10Record, value) == 0x28);

struct CallsDelegateWithArgumentField0x10Argument {
    uint8_t unknown_0x0[0x10];
    uint64_t value;
};
static_assert(offsetof(CallsDelegateWithArgumentField0x10Argument, value) == 0x10);

struct CallsDelegateWithArgumentField8Record {
    uint8_t unknown_0x0[0x8];
    void *object;
    MemberFunctionPointer method;
    uint8_t unknown_0x20[0x8];
    uint32_t value;
};
static_assert(offsetof(CallsDelegateWithArgumentField8Record, object) == 0x8);
static_assert(offsetof(CallsDelegateWithArgumentField8Record, method) == 0x10);
static_assert(offsetof(CallsDelegateWithArgumentField8Record, value) == 0x28);

struct CallsDelegateWithArgumentField8Argument {
    uint8_t unknown_0x0[0x8];
    uint64_t value;
};
static_assert(offsetof(CallsDelegateWithArgumentField8Argument, value) == 0x8);

struct CallsDelegateWithDwordAt0x20Record {
    uint8_t unknown_0x0[0x8];
    void *object;
    MemberFunctionPointer method;
    uint32_t value;
};
static_assert(offsetof(CallsDelegateWithDwordAt0x20Record, object) == 0x8);
static_assert(offsetof(CallsDelegateWithDwordAt0x20Record, method) == 0x10);
static_assert(offsetof(CallsDelegateWithDwordAt0x20Record, value) == 0x20);

struct CallsDelegateAt0x18WithDwordAt0x40Record {
    uint8_t unknown_0x0[0x18];
    void *object;
    MemberFunctionPointer method;
    uint8_t unknown_0x30[0x10];
    uint32_t value;
};
static_assert(offsetof(CallsDelegateAt0x18WithDwordAt0x40Record, object) == 0x18);
static_assert(offsetof(CallsDelegateAt0x18WithDwordAt0x40Record, method) == 0x20);
static_assert(offsetof(CallsDelegateAt0x18WithDwordAt0x40Record, value) == 0x40);

constexpr uint8_t record_call_flag = 0x40;

struct CallsFlaggedDelegateWithDwordAt0x18Record {
    uint8_t unknown_0x0[0xb];
    uint8_t flags;
    uint8_t unknown_0xc[0x4];
    void *object;
    uint32_t value;
    uint8_t unknown_0x1c[0x4];
    MemberFunctionPointer method;
};
static_assert(offsetof(CallsFlaggedDelegateWithDwordAt0x18Record, flags) == 0xb);
static_assert(offsetof(CallsFlaggedDelegateWithDwordAt0x18Record, object) == 0x10);
static_assert(offsetof(CallsFlaggedDelegateWithDwordAt0x18Record, value) == 0x18);
static_assert(offsetof(CallsFlaggedDelegateWithDwordAt0x18Record, method) == 0x20);

constexpr uint64_t argument_part_offset = 0x28;

struct CallsDelegateWithPartOfArgumentTargetRecord {
    uint8_t unknown_0x0[0x8];
    void *object;
    MemberFunctionPointer method;
    uint32_t value;
};
static_assert(offsetof(CallsDelegateWithPartOfArgumentTargetRecord, object) == 0x8);
static_assert(offsetof(CallsDelegateWithPartOfArgumentTargetRecord, method) == 0x10);
static_assert(offsetof(CallsDelegateWithPartOfArgumentTargetRecord, value) == 0x20);

struct CallsDelegateWithPartOfArgumentTargetArgument {
    uint8_t unknown_0x0[0x8];
    uint8_t *target;
};
static_assert(offsetof(CallsDelegateWithPartOfArgumentTargetArgument, target) == 0x8);

struct ForwardsToDelegateAt0x10Record {
    uint8_t unknown_0x0[0x10];
    void *object;
    MemberFunctionPointer method;
};
static_assert(offsetof(ForwardsToDelegateAt0x10Record, object) == 0x10);
static_assert(offsetof(ForwardsToDelegateAt0x10Record, method) == 0x18);

struct ForwardsToDelegateAt0x18Record {
    uint8_t unknown_0x0[0x18];
    void *object;
    MemberFunctionPointer method;
};
static_assert(offsetof(ForwardsToDelegateAt0x18Record, object) == 0x18);
static_assert(offsetof(ForwardsToDelegateAt0x18Record, method) == 0x20);

struct ForwardsToDelegateAt0x20Record {
    uint8_t unknown_0x0[0x20];
    void *object;
    MemberFunctionPointer method;
};
static_assert(offsetof(ForwardsToDelegateAt0x20Record, object) == 0x20);
static_assert(offsetof(ForwardsToDelegateAt0x20Record, method) == 0x28);

constexpr uint8_t argument_call_flag = 0x10;

struct CallsDelegateAt0x50WhenArgumentFlaggedRecord {
    uint8_t unknown_0x0[0x50];
    void *object;
    MemberFunctionPointer method;
    uint32_t value;
};
static_assert(offsetof(CallsDelegateAt0x50WhenArgumentFlaggedRecord, object) == 0x50);
static_assert(offsetof(CallsDelegateAt0x50WhenArgumentFlaggedRecord, method) == 0x58);
static_assert(offsetof(CallsDelegateAt0x50WhenArgumentFlaggedRecord, value) == 0x68);

struct CallsDelegateAt0x50WhenArgumentFlaggedArgument {
    uint8_t unknown_0x0[0x3d];
    uint8_t flags;
};
static_assert(offsetof(CallsDelegateAt0x50WhenArgumentFlaggedArgument, flags) == 0x3d);

}  // namespace

extern "C" void bb_leaf_calls_delegate_with_argument_field_0x10(void *object, uint64_t second, uint64_t third, uint64_t fourth,
                                                                uint64_t fifth, uint64_t sixth)
{
    const auto *record = static_cast<const CallsDelegateWithArgumentField0x10Record *>(object);
    const auto *argument = reinterpret_cast<const CallsDelegateWithArgumentField0x10Argument *>(second);
    (void)third;
    if (record->method.function == 0 || record->object == nullptr)
        return;  // the original returns with rax as it was: no result
    const Bound<ForwardedProcedure> method = bind<ForwardedProcedure>(record->object, record->method);
    BB_TAIL_CALL return method.function(method.target, argument->value, record->value, fourth, fifth, sixth);
}

extern "C" void bb_leaf_calls_delegate_with_argument_field_8(void *object, uint64_t second, uint64_t third, uint64_t fourth,
                                                             uint64_t fifth, uint64_t sixth)
{
    const auto *record = static_cast<const CallsDelegateWithArgumentField8Record *>(object);
    const auto *argument = reinterpret_cast<const CallsDelegateWithArgumentField8Argument *>(second);
    (void)third;
    if (record->method.function == 0 || record->object == nullptr)
        return;  // the original returns with rax as it was: no result
    const Bound<ForwardedProcedure> method = bind<ForwardedProcedure>(record->object, record->method);
    BB_TAIL_CALL return method.function(method.target, argument->value, record->value, fourth, fifth, sixth);
}

extern "C" void bb_leaf_calls_delegate_with_dword_at_0x20(void *object, uint64_t second, uint64_t third, uint64_t fourth,
                                                          uint64_t fifth, uint64_t sixth)
{
    const auto *record = static_cast<const CallsDelegateWithDwordAt0x20Record *>(object);
    (void)second;
    if (record->method.function == 0 || record->object == nullptr)
        return;  // the original returns with rax as it was: no result
    const Bound<ForwardedProcedure> method = bind<ForwardedProcedure>(record->object, record->method);
    BB_TAIL_CALL return method.function(method.target, record->value, third, fourth, fifth, sixth);
}

extern "C" void bb_leaf_calls_delegate_at_0x18_with_dword_at_0x40(void *object, uint64_t second, uint64_t third, uint64_t fourth,
                                                                  uint64_t fifth, uint64_t sixth)
{
    const auto *record = static_cast<const CallsDelegateAt0x18WithDwordAt0x40Record *>(object);
    (void)third;
    if (record->method.function == 0)
        return;  // the original returns with rax as it was: no result
    const Bound<ForwardedProcedure> method = bind<ForwardedProcedure>(record->object, record->method);
    BB_TAIL_CALL return method.function(method.target, second, record->value, fourth, fifth, sixth);
}

extern "C" void bb_leaf_calls_flagged_delegate_with_dword_at_0x18(void *object, uint64_t second, uint64_t third, uint64_t fourth,
                                                                  uint64_t fifth, uint64_t sixth)
{
    const auto *record = static_cast<const CallsFlaggedDelegateWithDwordAt0x18Record *>(object);
    (void)second;
    if ((record->flags & record_call_flag) == 0)
        return;  // the original returns with rax as it was: no result
    const Bound<ForwardedProcedure> method = bind<ForwardedProcedure>(record->object, record->method);
    BB_TAIL_CALL return method.function(method.target, record->value, third, fourth, fifth, sixth);
}

extern "C" void bb_leaf_calls_delegate_with_part_of_argument_target(void *object, uint64_t second, uint64_t third, uint64_t fourth,
                                                                    uint64_t fifth, uint64_t sixth)
{
    const auto *record = static_cast<const CallsDelegateWithPartOfArgumentTargetRecord *>(object);
    const auto *argument = reinterpret_cast<const CallsDelegateWithPartOfArgumentTargetArgument *>(second);
    (void)third;
    if (argument == nullptr || argument->target == nullptr || record->object == nullptr)
        return;  // the original returns with rax as it was: no result
    const Bound<ForwardedProcedure> method = bind<ForwardedProcedure>(record->object, record->method);
    BB_TAIL_CALL return method.function(method.target, reinterpret_cast<uint64_t>(argument->target + argument_part_offset), record->value, fourth, fifth, sixth);
}

extern "C" uint64_t bb_leaf_forwards_to_delegate_at_0x10(void *object, uint64_t second, uint64_t third, uint64_t fourth,
                                                         uint64_t fifth, uint64_t sixth)
{
    const auto *record = static_cast<const ForwardsToDelegateAt0x10Record *>(object);
    const BoundMethod method = bind(record->object, record->method);
    BB_TAIL_CALL return method.function(method.target, second, third, fourth, fifth, sixth);
}

extern "C" uint64_t bb_leaf_forwards_to_delegate_at_0x18(void *object, uint64_t second, uint64_t third, uint64_t fourth,
                                                         uint64_t fifth, uint64_t sixth)
{
    const auto *record = static_cast<const ForwardsToDelegateAt0x18Record *>(object);
    const BoundMethod method = bind(record->object, record->method);
    BB_TAIL_CALL return method.function(method.target, second, third, fourth, fifth, sixth);
}

extern "C" uint64_t bb_leaf_forwards_to_delegate_at_0x20(void *object, uint64_t second, uint64_t third, uint64_t fourth,
                                                         uint64_t fifth, uint64_t sixth)
{
    const auto *record = static_cast<const ForwardsToDelegateAt0x20Record *>(object);
    const BoundMethod method = bind(record->object, record->method);
    BB_TAIL_CALL return method.function(method.target, second, third, fourth, fifth, sixth);
}

extern "C" void bb_leaf_calls_delegate_at_0x50_when_argument_flagged(void *object, uint64_t second, uint64_t third, uint64_t fourth,
                                                                     uint64_t fifth, uint64_t sixth)
{
    const auto *record = static_cast<const CallsDelegateAt0x50WhenArgumentFlaggedRecord *>(object);
    const auto *argument = reinterpret_cast<const CallsDelegateAt0x50WhenArgumentFlaggedArgument *>(second);
    if ((argument->flags & argument_call_flag) == 0)
        return;  // the original returns with rax as it was: no result
    const Bound<ForwardedProcedure> method = bind<ForwardedProcedure>(record->object, record->method);
    BB_TAIL_CALL return method.function(method.target, record->value, third, fourth, fifth, sixth);
}
