/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Owned SPIR-V grammar fixtures for descriptor-qualified DCE and EXECZ dispatcher
   normalization. Instruction streams and expected values are independent of game data. */
#include "metal_fragment_control.hpp"
#include "metal_fragment_quads.hpp"
#include "spirv_msl.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using Words = std::vector<uint32_t>;
constexpr uint64_t owned_anchor = UINT64_C(0x5c00000000);
constexpr uint32_t descriptor_pair = 4, descriptor_offset = 12, descriptor_bytes = 4;
enum class Layout { Dense, LiteralCollisions, Reversed };
enum class Shape {
    Dispatch, InvertedSkip, Reordered, Simple, TrueAny, FalseAny, EarlyKill, PerLaneQuad, LiveAny, LiveScc,
    EmptyChanges, EmptyExternalStore, UnvisitedStore, RealLoop, UnknownEmptyOpcode,
    VolatileRead, VolatileStore, PointerAlias, PairRedefinition, Noncanonical,
    WrongHighMask, UnalignedOffset, WrongPushOffset, WrongStride,
    VolatileMember, CoherentMember, VolatileInputChain
};

/* Symbolic references are mapped while assembling; every literal is written directly.
   This deliberately does not consult the implementation's operand/use classifier. */
enum class Id : unsigned {
    Void, FunctionType, Bool, UInt, ULong, Float, Float4, UInt4, Array, PushStruct,
    InputPointer, OutputPointer, BoolPointer, UIntPointer, PushPointer, PushWordPointer, PhysicalPointer,
    Zero, One, Two, Three, Four, Five, Count, Shift32, HighMask, InitialValue, Offset64, Anchor64,
    True, False, FragCoord, Output, Push, Main, Entry, PcLocal, ExecLocal, LiveLocal,
    LowLocal, HighLocal, DeadPhysicalLocal, DeadVoteLocal,
    Position, XFloat, XUInt, XParity, Predicate, DeadAny, LiveVote,
    LowAccess, HighAccess, LowWord, HighWord, LowLoad, HighLoad, HighMasked,
    LowWide, HighWide, HighShifted, Pair, Anchored, Address, Physical, PhysicalRead, LowAlias,
    Header, Dispatch, Default, Case0, Case1, Case2, Merge, Continue, Exit, Selector,
    VoteMask, SkipAny, SkipNot, NextPc, BodyMask, OldValue, SwapX, SwapY, SwapDiagonal,
    OrX, OrY, Expanded, NewValue, MaskedValue, UnknownValue,
    FinalValue, FinalMask, FinalVote, FinalVoteWord, FinalScc, FinalSccWord, FinalOutput,
    ExternalOutput, LiteralAny, LiteralAnyWord, QuadBlock, BypassBlock, QuadMerge, EarlyKillBlock, StoreArm, NoStoreArm, StoreMerge, InputScalarPointer, DeadInputAccess, DeadInputValue, End
};

struct Assembler {
    std::array<uint32_t, unsigned(Id::End)> ids{};
    uint32_t bound = 1;
    explicit Assembler(Layout layout)
    {
        for (unsigned symbol = 0; symbol < ids.size(); ++symbol) {
            uint32_t assigned = symbol + 1;
            if (layout == Layout::LiteralCollisions)
                assigned = symbol == unsigned(Id::DeadVoteLocal) ? 64 :
                    symbol == unsigned(Id::DeadAny) ? 4 : 128 + 2 * (unsigned(ids.size()) - symbol);
            else if (layout == Layout::Reversed) assigned = 1024 - symbol;
            ids[symbol] = assigned;
            bound = std::max(bound, assigned + 1);
        }
    }
    uint32_t operator()(Id symbol) const { return ids[unsigned(symbol)]; }
};

void emit(Words &out, spv::Op opcode, std::initializer_list<uint32_t> operands)
{
    out.push_back((uint32_t(operands.size() + 1) << 16) | uint32_t(opcode));
    out.insert(out.end(), operands.begin(), operands.end());
}
void text_instruction(Words &out, spv::Op opcode, std::initializer_list<uint32_t> prefix,
                      const char *text, std::initializer_list<uint32_t> suffix = {})
{
    const size_t start = out.size(), bytes = std::strlen(text) + 1;
    out.push_back(0);
    out.insert(out.end(), prefix.begin(), prefix.end());
    const size_t at = out.size();
    out.resize(at + (bytes + 3) / 4, 0);
    std::memcpy(out.data() + at, text, bytes);
    out.insert(out.end(), suffix.begin(), suffix.end());
    out[start] = (uint32_t(out.size() - start) << 16) | uint32_t(opcode);
}

struct Module {
    Words words;
    Assembler id;
    Shape shape;
    bool physical;
    size_t physical_offset = 0;
};

Module make_module(Shape shape = Shape::Dispatch, Layout layout = Layout::Dense, bool physical = true)
{
    Assembler i(layout);
    Words capabilities, entry, annotations, types, body;
    const bool simple = shape == Shape::Simple || shape == Shape::TrueAny || shape == Shape::FalseAny ||
        shape == Shape::EarlyKill || shape == Shape::PerLaneQuad;
    size_t case0_word = 0, case1_word = 0, case2_word = 0;
    const uint32_t push_offset = shape == Shape::WrongPushOffset ? 68 : 64;
    emit(capabilities, spv::OpCapability, {spv::CapabilityShader});
    emit(capabilities, spv::OpCapability, {spv::CapabilityInt64});
    emit(capabilities, spv::OpCapability, {spv::CapabilityPhysicalStorageBufferAddresses});
    emit(capabilities, spv::OpCapability, {spv::CapabilityGroupNonUniform});
    emit(capabilities, spv::OpCapability, {spv::CapabilityGroupNonUniformVote});
    emit(capabilities, spv::OpCapability, {spv::CapabilityGroupNonUniformQuad});
    text_instruction(capabilities, spv::OpExtension, {}, "SPV_KHR_physical_storage_buffer");
    emit(capabilities, spv::OpMemoryModel, {spv::AddressingModelPhysicalStorageBuffer64, spv::MemoryModelGLSL450});
    text_instruction(entry, spv::OpEntryPoint, {spv::ExecutionModelFragment, i(Id::Main)}, "owned_fragment_control",
                     {i(Id::FragCoord), i(Id::Output)});
    emit(entry, spv::OpExecutionMode, {i(Id::Main), spv::ExecutionModeOriginUpperLeft});
    emit(annotations, spv::OpDecorate, {i(Id::FragCoord), spv::DecorationBuiltIn, spv::BuiltInFragCoord});
    emit(annotations, spv::OpDecorate, {i(Id::Output), spv::DecorationLocation, 0});
    emit(annotations, spv::OpDecorate, {i(Id::Array), spv::DecorationArrayStride, shape == Shape::WrongStride ? 8u : 4u});
    emit(annotations, spv::OpDecorate, {i(Id::PushStruct), spv::DecorationBlock});
    emit(annotations, spv::OpMemberDecorate, {i(Id::PushStruct), 0, spv::DecorationOffset, push_offset});
    if (shape == Shape::VolatileMember || shape == Shape::CoherentMember)
        emit(annotations, spv::OpMemberDecorate, {i(Id::PushStruct), 0,
            shape == Shape::VolatileMember ? spv::DecorationVolatile : spv::DecorationCoherent});
    if (shape == Shape::VolatileInputChain)
        emit(annotations, spv::OpDecorate, {i(Id::FragCoord), spv::DecorationVolatile});
    emit(types, spv::OpTypeVoid, {i(Id::Void)});
    emit(types, spv::OpTypeFunction, {i(Id::FunctionType), i(Id::Void)});
    emit(types, spv::OpTypeBool, {i(Id::Bool)});
    emit(types, spv::OpTypeInt, {i(Id::UInt), 32, 0});
    emit(types, spv::OpTypeInt, {i(Id::ULong), 64, 0});
    emit(types, spv::OpTypeFloat, {i(Id::Float), 32});
    emit(types, spv::OpTypeVector, {i(Id::Float4), i(Id::Float), 4});
    emit(types, spv::OpTypeVector, {i(Id::UInt4), i(Id::UInt), 4});
    const std::pair<Id, uint32_t> constants[] = {
        {Id::Zero, 0}, {Id::One, 1}, {Id::Two, 2}, {Id::Three, 3}, {Id::Four, 4}, {Id::Five, 5},
        {Id::Count, 16}, {Id::Shift32, 32}, {Id::HighMask, shape == Shape::WrongHighMask ? 0x00ffffffu : 0xffffu},
        {Id::InitialValue, 17}
    };
    for (const auto &[symbol, value] : constants) emit(types, spv::OpConstant, {i(Id::UInt), i(symbol), value});
    emit(types, spv::OpConstant, {i(Id::ULong), i(Id::Offset64), shape == Shape::UnalignedOffset ? 14u : descriptor_offset, 0});
    emit(types, spv::OpConstant, {i(Id::ULong), i(Id::Anchor64), uint32_t(owned_anchor), uint32_t(owned_anchor >> 32)});
    emit(types, spv::OpConstantTrue, {i(Id::Bool), i(Id::True)});
    emit(types, spv::OpConstantFalse, {i(Id::Bool), i(Id::False)});
    emit(types, spv::OpTypeArray, {i(Id::Array), i(Id::UInt), i(Id::Count)});
    emit(types, spv::OpTypeStruct, {i(Id::PushStruct), i(Id::Array)});
    emit(types, spv::OpTypePointer, {i(Id::InputPointer), spv::StorageClassInput, i(Id::Float4)});
    if (shape == Shape::VolatileInputChain)
        emit(types, spv::OpTypePointer, {i(Id::InputScalarPointer), spv::StorageClassInput, i(Id::Float)});
    emit(types, spv::OpTypePointer, {i(Id::OutputPointer), spv::StorageClassOutput, i(Id::UInt4)});
    emit(types, spv::OpTypePointer, {i(Id::BoolPointer), spv::StorageClassFunction, i(Id::Bool)});
    emit(types, spv::OpTypePointer, {i(Id::UIntPointer), spv::StorageClassFunction, i(Id::UInt)});
    emit(types, spv::OpTypePointer, {i(Id::PushPointer), spv::StorageClassPushConstant, i(Id::PushStruct)});
    emit(types, spv::OpTypePointer, {i(Id::PushWordPointer), spv::StorageClassPushConstant, i(Id::UInt)});
    emit(types, spv::OpTypePointer, {i(Id::PhysicalPointer), spv::StorageClassPhysicalStorageBuffer, i(Id::UInt)});
    emit(types, spv::OpVariable, {i(Id::InputPointer), i(Id::FragCoord), spv::StorageClassInput});
    emit(types, spv::OpVariable, {i(Id::OutputPointer), i(Id::Output), spv::StorageClassOutput});
    emit(types, spv::OpVariable, {i(Id::PushPointer), i(Id::Push), spv::StorageClassPushConstant});
    emit(body, spv::OpFunction, {i(Id::Void), i(Id::Main), spv::FunctionControlMaskNone, i(Id::FunctionType)});
    emit(body, spv::OpLabel, {i(Id::Entry)});
    for (Id local : {Id::PcLocal, Id::LiveLocal, Id::LowLocal, Id::HighLocal, Id::DeadPhysicalLocal})
        emit(body, spv::OpVariable, {i(Id::UIntPointer), i(local), spv::StorageClassFunction});
    for (Id local : {Id::ExecLocal, Id::DeadVoteLocal})
        emit(body, spv::OpVariable, {i(Id::BoolPointer), i(local), spv::StorageClassFunction});
    if (shape == Shape::VolatileInputChain) {
        // The qualifier is on the aggregate base, not the derived pointer or load.
        // A dead plain scalar load must not lose that observable memory contract.
        emit(body, spv::OpAccessChain, {i(Id::InputScalarPointer), i(Id::DeadInputAccess), i(Id::FragCoord), i(Id::Zero)});
        emit(body, spv::OpLoad, {i(Id::Float), i(Id::DeadInputValue), i(Id::DeadInputAccess)});
    }
    emit(body, spv::OpLoad, {i(Id::Float4), i(Id::Position), i(Id::FragCoord)});
    emit(body, spv::OpCompositeExtract, {i(Id::Float), i(Id::XFloat), i(Id::Position), 0});
    emit(body, spv::OpConvertFToU, {i(Id::UInt), i(Id::XUInt), i(Id::XFloat)});
    emit(body, spv::OpBitwiseAnd, {i(Id::UInt), i(Id::XParity), i(Id::XUInt), i(Id::One)});
    emit(body, spv::OpIEqual, {i(Id::Bool), i(Id::Predicate), i(Id::XParity), i(Id::Zero)});
    emit(body, spv::OpStore, {i(Id::ExecLocal), i(Id::Predicate)});
    emit(body, spv::OpStore, {i(Id::LiveLocal), i(Id::InitialValue)});
    emit(body, spv::OpGroupNonUniformAny, {i(Id::Bool), i(Id::DeadAny), i(Id::Three), i(Id::Predicate)});
    emit(body, spv::OpStore, {i(Id::DeadVoteLocal), i(Id::DeadAny)});
    if (physical) {
        emit(body, spv::OpAccessChain, {i(Id::PushWordPointer), i(Id::LowAccess), i(Id::Push), i(Id::Zero), i(Id::Four)});
        emit(body, spv::OpAccessChain, {i(Id::PushWordPointer), i(Id::HighAccess), i(Id::Push), i(Id::Zero), i(Id::Five)});
        emit(body, spv::OpLoad, {i(Id::UInt), i(Id::LowWord), i(Id::LowAccess)});
        emit(body, spv::OpLoad, {i(Id::UInt), i(Id::HighWord), i(Id::HighAccess)});
        emit(body, spv::OpStore, {i(Id::LowLocal), i(Id::LowWord)});
        emit(body, spv::OpStore, {i(Id::HighLocal), i(Id::HighWord)});
        uint32_t low_pointer = i(Id::LowLocal);
        if (shape == Shape::PointerAlias) {
            emit(body, spv::OpCopyObject, {i(Id::UIntPointer), i(Id::LowAlias), low_pointer});
            low_pointer = i(Id::LowAlias);
        }
        emit(body, spv::OpLoad, {i(Id::UInt), i(Id::LowLoad), low_pointer});
        emit(body, spv::OpLoad, {i(Id::UInt), i(Id::HighLoad), i(Id::HighLocal)});
        if (shape == Shape::Noncanonical)
            emit(body, spv::OpCopyObject, {i(Id::UInt), i(Id::HighMasked), i(Id::HighLoad)});
        else emit(body, spv::OpBitwiseAnd, {i(Id::UInt), i(Id::HighMasked), i(Id::HighLoad), i(Id::HighMask)});
        emit(body, spv::OpUConvert, {i(Id::ULong), i(Id::LowWide), i(Id::LowLoad)});
        emit(body, spv::OpUConvert, {i(Id::ULong), i(Id::HighWide), i(Id::HighMasked)});
        emit(body, spv::OpShiftLeftLogical, {i(Id::ULong), i(Id::HighShifted), i(Id::HighWide), i(Id::Shift32)});
        emit(body, spv::OpBitwiseOr, {i(Id::ULong), i(Id::Pair), i(Id::LowWide), i(Id::HighShifted)});
        emit(body, spv::OpIAdd, {i(Id::ULong), i(Id::Anchored), i(Id::Pair), i(Id::Anchor64)});
        emit(body, spv::OpIAdd, {i(Id::ULong), i(Id::Address), i(Id::Anchored), i(Id::Offset64)});
        emit(body, spv::OpConvertUToPtr, {i(Id::PhysicalPointer), i(Id::Physical), i(Id::Address)});
        if (shape == Shape::VolatileRead)
            emit(body, spv::OpLoad, {i(Id::UInt), i(Id::PhysicalRead), i(Id::Physical),
                spv::MemoryAccessAlignedMask | spv::MemoryAccessVolatileMask, 4});
        else emit(body, spv::OpLoad, {i(Id::UInt), i(Id::PhysicalRead), i(Id::Physical), spv::MemoryAccessAlignedMask, 4});
        if (shape == Shape::VolatileStore)
            emit(body, spv::OpStore, {i(Id::DeadPhysicalLocal), i(Id::PhysicalRead), spv::MemoryAccessVolatileMask});
        else emit(body, spv::OpStore, {i(Id::DeadPhysicalLocal), i(Id::PhysicalRead)});
    }
    if (!simple) {
        emit(body, spv::OpStore, {i(Id::PcLocal), i(Id::Zero)});
        emit(body, spv::OpBranch, {i(Id::Header)});
        emit(body, spv::OpLabel, {i(Id::Header)});
        emit(body, spv::OpLoopMerge, {i(Id::Exit), i(Id::Continue), spv::LoopControlMaskNone});
        emit(body, spv::OpBranch, {i(Id::Dispatch)});
        emit(body, spv::OpLabel, {i(Id::Dispatch)});
        emit(body, spv::OpLoad, {i(Id::UInt), i(Id::Selector), i(Id::PcLocal)});
        emit(body, spv::OpSelectionMerge, {i(Id::Merge), spv::SelectionControlMaskNone});
        emit(body, spv::OpSwitch, {i(Id::Selector), i(Id::Default), 0, i(Id::Case0), 1, i(Id::Case1), 2, i(Id::Case2)});
        emit(body, spv::OpLabel, {i(Id::Default)});
        emit(body, spv::OpBranch, {i(Id::Merge)});
        case0_word = body.size();
        emit(body, spv::OpLabel, {i(Id::Case0)});
        if (shape == Shape::PairRedefinition) emit(body, spv::OpStore, {i(Id::LowLocal), i(Id::One)});
        emit(body, spv::OpLoad, {i(Id::Bool), i(Id::VoteMask), i(Id::ExecLocal)});
        emit(body, spv::OpGroupNonUniformAny, {i(Id::Bool), i(Id::SkipAny), i(Id::Three), i(Id::VoteMask)});
        uint32_t condition = i(Id::SkipAny), yes = i(Id::One), no = i(Id::Two);
        if (shape == Shape::InvertedSkip) {
            emit(body, spv::OpLogicalNot, {i(Id::Bool), i(Id::SkipNot), condition});
            condition = i(Id::SkipNot); std::swap(yes, no);
        }
        emit(body, spv::OpSelect, {i(Id::UInt), i(Id::NextPc), condition, yes, no});
        emit(body, spv::OpStore, {i(Id::PcLocal), i(Id::NextPc)});
        emit(body, spv::OpBranch, {i(Id::Merge)});
        case1_word = body.size();
        emit(body, spv::OpLabel, {i(Id::Case1)});
    }
    emit(body, spv::OpLoad, {i(Id::Bool), i(Id::BodyMask), i(Id::ExecLocal)});
    emit(body, spv::OpLoad, {i(Id::UInt), i(Id::OldValue), i(Id::LiveLocal)});
    if (shape == Shape::EarlyKill) {
        emit(body, spv::OpSelectionMerge, {i(Id::QuadBlock), spv::SelectionControlMaskNone});
        emit(body, spv::OpBranchConditional, {i(Id::BodyMask), i(Id::EarlyKillBlock), i(Id::QuadBlock)});
        emit(body, spv::OpLabel, {i(Id::QuadBlock)});
    } else if (shape == Shape::PerLaneQuad) {
        emit(body, spv::OpSelectionMerge, {i(Id::QuadMerge), spv::SelectionControlMaskNone});
        emit(body, spv::OpBranchConditional, {i(Id::BodyMask), i(Id::QuadBlock), i(Id::BypassBlock)});
        emit(body, spv::OpLabel, {i(Id::QuadBlock)});
    }
    emit(body, spv::OpGroupNonUniformQuadSwap, {i(Id::Bool), i(Id::SwapX), i(Id::Three), i(Id::BodyMask), i(Id::Zero)});
    emit(body, spv::OpGroupNonUniformQuadSwap, {i(Id::Bool), i(Id::SwapY), i(Id::Three), i(Id::BodyMask), i(Id::One)});
    emit(body, spv::OpGroupNonUniformQuadSwap, {i(Id::Bool), i(Id::SwapDiagonal), i(Id::Three), i(Id::BodyMask), i(Id::Two)});
    emit(body, spv::OpLogicalOr, {i(Id::Bool), i(Id::OrX), i(Id::BodyMask), i(Id::SwapX)});
    emit(body, spv::OpLogicalOr, {i(Id::Bool), i(Id::OrY), i(Id::OrX), i(Id::SwapY)});
    emit(body, spv::OpLogicalOr, {i(Id::Bool), i(Id::Expanded), i(Id::OrY), i(Id::SwapDiagonal)});
    emit(body, spv::OpSelect, {i(Id::UInt), i(Id::NewValue), i(Id::Expanded), i(Id::One), i(Id::Zero)});
    if (shape == Shape::UnknownEmptyOpcode)
        emit(body, spv::OpUDiv, {i(Id::UInt), i(Id::UnknownValue), i(Id::OldValue), i(Id::One)});
    emit(body, spv::OpSelect, {i(Id::UInt), i(Id::MaskedValue), i(Id::BodyMask), i(Id::NewValue),
        shape == Shape::EmptyChanges ? i(Id::One) : i(Id::OldValue)});
    emit(body, spv::OpStore, {i(Id::LiveLocal), i(Id::MaskedValue)});
    if (shape == Shape::UnvisitedStore) {
        emit(body, spv::OpSelectionMerge, {i(Id::StoreMerge), spv::SelectionControlMaskNone});
        emit(body, spv::OpBranchConditional, {i(Id::BodyMask), i(Id::StoreArm), i(Id::NoStoreArm)});
        emit(body, spv::OpLabel, {i(Id::StoreArm)});
        emit(body, spv::OpCompositeConstruct, {i(Id::UInt4), i(Id::ExternalOutput), i(Id::NewValue), i(Id::One), i(Id::Two), i(Id::Three)});
        emit(body, spv::OpStore, {i(Id::Output), i(Id::ExternalOutput)});
        emit(body, spv::OpBranch, {i(Id::StoreMerge)});
        emit(body, spv::OpLabel, {i(Id::NoStoreArm)});
        emit(body, spv::OpBranch, {i(Id::StoreMerge)});
        emit(body, spv::OpLabel, {i(Id::StoreMerge)});
    }
    if (shape == Shape::EmptyExternalStore) {
        emit(body, spv::OpCompositeConstruct, {i(Id::UInt4), i(Id::ExternalOutput), i(Id::Zero), i(Id::Zero), i(Id::Zero), i(Id::Zero)});
        emit(body, spv::OpStore, {i(Id::Output), i(Id::ExternalOutput)});
    }
    if (shape == Shape::PerLaneQuad) {
        emit(body, spv::OpBranch, {i(Id::QuadMerge)});
        emit(body, spv::OpLabel, {i(Id::BypassBlock)});
        emit(body, spv::OpBranch, {i(Id::QuadMerge)});
        emit(body, spv::OpLabel, {i(Id::QuadMerge)});
    }
    if (!simple) {
        emit(body, spv::OpStore, {i(Id::PcLocal), shape == Shape::RealLoop ? i(Id::One) : i(Id::Two)});
        emit(body, spv::OpBranch, {i(Id::Merge)});
        case2_word = body.size();
        emit(body, spv::OpLabel, {i(Id::Case2)});
    }
    emit(body, spv::OpLoad, {i(Id::UInt), i(Id::FinalValue), i(Id::LiveLocal)});
    uint32_t extra = i(Id::One);
    if (shape == Shape::LiveAny) {
        emit(body, spv::OpLoad, {i(Id::Bool), i(Id::FinalMask), i(Id::ExecLocal)});
        emit(body, spv::OpGroupNonUniformAny, {i(Id::Bool), i(Id::FinalVote), i(Id::Three), i(Id::FinalMask)});
        emit(body, spv::OpSelect, {i(Id::UInt), i(Id::FinalVoteWord), i(Id::FinalVote), i(Id::One), i(Id::Zero)});
        extra = i(Id::FinalVoteWord);
    } else if (shape == Shape::LiveScc) {
        emit(body, spv::OpLoad, {i(Id::Bool), i(Id::FinalScc), i(Id::DeadVoteLocal)});
        emit(body, spv::OpSelect, {i(Id::UInt), i(Id::FinalSccWord), i(Id::FinalScc), i(Id::One), i(Id::Zero)});
        extra = i(Id::FinalSccWord);
    } else if (shape == Shape::TrueAny || shape == Shape::FalseAny) {
        emit(body, spv::OpGroupNonUniformAny, {i(Id::Bool), i(Id::LiteralAny), i(Id::Three),
            shape == Shape::TrueAny ? i(Id::True) : i(Id::False)});
        emit(body, spv::OpSelect, {i(Id::UInt), i(Id::LiteralAnyWord), i(Id::LiteralAny), i(Id::One), i(Id::Zero)});
        extra = i(Id::LiteralAnyWord);
    }
    emit(body, spv::OpCompositeConstruct, {i(Id::UInt4), i(Id::FinalOutput), i(Id::FinalValue), extra, i(Id::Two), i(Id::Three)});
    emit(body, spv::OpStore, {i(Id::Output), i(Id::FinalOutput)});
    emit(body, spv::OpReturn, {});
    const size_t final_case_end = body.size();
    if (shape == Shape::EarlyKill) {
        // Physical layout places the kill after the quad block; its CFG edge is earlier.
        emit(body, spv::OpLabel, {i(Id::EarlyKillBlock)});
        emit(body, spv::OpKill, {});
    }
    if (!simple) {
        emit(body, spv::OpLabel, {i(Id::Merge)});
        emit(body, spv::OpBranch, {i(Id::Continue)});
        emit(body, spv::OpLabel, {i(Id::Continue)});
        emit(body, spv::OpBranch, {i(Id::Header)});
        emit(body, spv::OpLabel, {i(Id::Exit)});
        emit(body, spv::OpReturn, {});
    }
    emit(body, spv::OpFunctionEnd, {});
    if (shape == Shape::Reordered) {
        // Structured Switch case order is independent of literal PC order. Construct
        // case2, case1, case0 physically; preparation must schedule their forward CFG.
        Words reordered;
        const std::pair<size_t, size_t> ranges[] = {
            {0, case0_word}, {case2_word, final_case_end}, {case1_word, case2_word},
            {case0_word, case1_word}, {final_case_end, body.size()}
        };
        for (const auto &[first, last] : ranges)
            reordered.insert(reordered.end(), body.begin() + first, body.begin() + last);
        body = std::move(reordered);
    }
    Module module{{spv::MagicNumber, 0x00010300, 0, i.bound, 0}, i, shape, physical};
    for (const Words *section : {&capabilities, &entry, &annotations, &types, &body})
        module.words.insert(module.words.end(), section->begin(), section->end());
    for (size_t at = 5; at < module.words.size(); at += module.words[at] >> 16)
        if ((module.words[at] & 0xffffu) == spv::OpLoad && module.words[at + 2] == i(Id::PhysicalRead)) module.physical_offset = at;
    return module;
}

struct Instruction { spv::Op opcode; size_t at; unsigned extent; };
std::vector<Instruction> instructions(const Words &words)
{
    std::vector<Instruction> result;
    for (size_t at = 5; at < words.size();) {
        const unsigned extent = words[at] >> 16;
        if (!extent || extent > words.size() - at) throw std::runtime_error("invalid owned module extent");
        result.push_back({static_cast<spv::Op>(words[at] & 0xffffu), at, extent});
        at += extent;
    }
    return result;
}
unsigned count(const Words &words, spv::Op opcode)
{
    unsigned total = 0;
    for (const auto &op : instructions(words)) total += op.opcode == opcode;
    return total;
}
bool typed_result(const Words &words, spv::Op opcode, uint32_t result)
{
    for (const auto &op : instructions(words))
        if (op.opcode == opcode && op.extent >= 3 && words[op.at + 2] == result) return true;
    return false;
}
bool store_to(const Words &words, uint32_t pointer)
{
    for (const auto &op : instructions(words))
        if (op.opcode == spv::OpStore && op.extent >= 3 && words[op.at + 1] == pointer) return true;
    return false;
}
std::vector<std::pair<spv::Op, unsigned>> signature(const Words &words)
{
    std::vector<std::pair<spv::Op, unsigned>> result;
    for (const auto &op : instructions(words)) result.emplace_back(op.opcode, op.extent);
    return result;
}

/* A deliberately small constant observer checks the live literal-vote output, not
   the normalizer's intermediate rewrite spelling. It has no subgroup evaluator. */
uint32_t constant_value(const Words &words, uint32_t id, unsigned depth = 0)
{
    if (depth > 16) throw std::runtime_error("owned constant observer recursion");
    for (const auto &op : instructions(words)) {
        const size_t at = op.at;
        if (op.extent < 3 || words[at + 2] != id) continue;
        switch (op.opcode) {
        case spv::OpConstantTrue: return 1;
        case spv::OpConstantFalse: case spv::OpConstantNull: return 0;
        case spv::OpConstant:
            if (op.extent == 4) return words[at + 3];
            break;
        case spv::OpCopyObject:
            if (op.extent == 4) return constant_value(words, words[at + 3], depth + 1);
            break;
        case spv::OpSelect:
            if (op.extent == 6)
                return constant_value(words, words[at + (constant_value(words, words[at + 3], depth + 1) ? 4 : 5)], depth + 1);
            break;
        default: break;
        }
    }
    throw std::runtime_error("live literal-vote output is not a proven constant");
}

struct Results {
    unsigned cases = 0, checks = 0, failures = 0;
    void check(bool condition, const std::string &message)
    {
        ++checks;
        if (condition) return;
        ++failures;
        if (failures <= 30) std::fprintf(stderr, "%s\n", message.c_str());
    }
};
const std::vector<pb::MetalDescriptorSpan> covered = {{descriptor_pair, descriptor_offset, descriptor_bytes, true}};

bool stock_parse(Results &results, const Module &module, const char *name)
{
    try {
        spirv_cross::CompilerMSL parsed(module.words.data(), module.words.size());
        return true;
    } catch (const std::exception &error) {
        results.check(false, std::string(name) + ": stock SPIR-V parser rejected fixture: " + error.what());
        return false;
    }
}

pb::MetalPreparedFragment positive(Results &results, const char *name, const Module &module,
                                  const std::vector<pb::MetalDescriptorSpan> &spans = covered,
                                  uint32_t push_offset = 64)
{
    ++results.cases;
    if (!stock_parse(results, module, name)) throw std::runtime_error("invalid positive fixture");
    const auto original = module.words;
    auto prepared = pb::prepare_metal_fragment(module.words.data(), module.words.size(), spans, owned_anchor, push_offset);
    const auto repeated = pb::prepare_metal_fragment(module.words.data(), module.words.size(), spans, owned_anchor, push_offset);
    results.check(prepared.words == repeated.words && prepared.elided_skips == repeated.elided_skips &&
        prepared.linearized == repeated.linearized && prepared.dce.removed_values == repeated.dce.removed_values &&
        prepared.dce.removed_stores == repeated.dce.removed_stores &&
        prepared.dce.removed_reads.size() == repeated.dce.removed_reads.size(), std::string(name) + ": preparation is not deterministic");
    results.check(original == module.words, std::string(name) + ": preparation changed the input buffer");
    results.check(count(prepared.words, spv::OpGroupNonUniformAny) == 0, std::string(name) + ": an observable subgroup Any remains");
    results.check(count(prepared.words, spv::OpGroupNonUniformQuadSwap) == 3, std::string(name) + ": Boolean quad data was lost");
    results.check(count(prepared.words, spv::OpSwitch) == 0 && count(prepared.words, spv::OpLoopMerge) == 0,
                   std::string(name) + ": dispatcher scaffolding remains");
    results.check(typed_result(prepared.words, spv::OpCompositeConstruct, module.id(Id::FinalOutput)) &&
        store_to(prepared.words, module.id(Id::Output)), std::string(name) + ": live uint4 export was lost");
    const bool simple = module.shape == Shape::Simple || module.shape == Shape::TrueAny || module.shape == Shape::FalseAny;
    results.check(prepared.elided_skips == (simple ? 0u : 1u) && prepared.linearized == !simple,
                   std::string(name) + ": wrong skip/linearization report");
    spirv_cross::CompilerMSL parsed_output(prepared.words.data(), prepared.words.size());
    pb::MetalFragmentQuadCompiler compiler(prepared, 0x100004);
    auto options = compiler.get_msl_options();
    options.set_msl_version(2, 3);
    compiler.set_msl_options(options);
    const std::string msl = compiler.compile();
    size_t position = 0;
    unsigned calls = 0;
    while ((position = msl.find("::swap(", position)) != std::string::npos) { ++calls; ++position; }
    results.check(calls == 3 && msl.find("[[thread_index_in_quadgroup]]") != std::string::npos,
                   std::string(name) + ": prepared MSL lost guarded Boolean quad calls");
    const bool volatile_memory = module.shape == Shape::VolatileRead || module.shape == Shape::VolatileStore;
    if (module.physical && !volatile_memory) {
        results.check(prepared.dce.removed_reads.size() == 1 && !typed_result(prepared.words, spv::OpLoad, module.id(Id::PhysicalRead)),
                       std::string(name) + ": covered dead physical read was not removed");
        if (prepared.dce.removed_reads.size() == 1) {
            const auto &read = prepared.dce.removed_reads[0];
            results.check(read.pointer_sgpr == descriptor_pair && read.offset == descriptor_offset &&
                read.bytes == descriptor_bytes && read.instruction_offset == module.physical_offset,
                std::string(name) + ": removed read has the wrong independent origin");
        }
    } else {
        results.check(prepared.dce.removed_reads.empty(), std::string(name) + ": an unqualified read was removed");
        if (volatile_memory)
            results.check(typed_result(prepared.words, spv::OpLoad, module.id(Id::PhysicalRead)),
                           std::string(name) + ": volatile or volatile-consumed physical read was lost");
    }
    if (module.shape == Shape::VolatileStore)
        results.check(store_to(prepared.words, module.id(Id::DeadPhysicalLocal)), "volatile local store was lost");
    return prepared;
}

void refuse(Results &results, const char *name, const Module &module, const char *diagnostic,
            const std::vector<pb::MetalDescriptorSpan> &spans = covered, uint64_t anchor = owned_anchor)
{
    ++results.cases;
    if (!stock_parse(results, module, name)) return;
    const Words original = module.words;
    try {
        (void)pb::prepare_metal_fragment(module.words.data(), module.words.size(), spans, anchor);
        results.check(false, std::string(name) + ": preparation unexpectedly succeeded");
    } catch (const std::exception &error) {
        results.check(std::string(error.what()).find(diagnostic) != std::string::npos,
                       std::string(name) + ": expected '" + diagnostic + "', got '" + error.what() + "'");
    }
    results.check(module.words == original, std::string(name) + ": refusal changed the input buffer");
}

void compiler_refusal(Results &results, const char *name, Shape shape, const char *diagnostic)
{
    ++results.cases;
    const auto module = make_module(shape, Layout::LiteralCollisions, false);
    if (!stock_parse(results, module, name)) return;
    const auto prepared = pb::prepare_metal_fragment(module.words.data(), module.words.size(), {}, owned_anchor);
    results.check(!prepared.linearized && count(prepared.words, spv::OpGroupNonUniformQuadSwap) == 3,
                   std::string(name) + ": factory did not preserve the shape for compiler validation");
    try {
        pb::MetalFragmentQuadCompiler compiler(prepared, 0x100004);
        (void)compiler.compile();
        results.check(false, std::string(name) + ": compiler unexpectedly admitted unsafe quad control");
    } catch (const std::exception &error) {
        results.check(std::string(error.what()).find(diagnostic) != std::string::npos,
                       std::string(name) + ": expected '" + diagnostic + "', got '" + error.what() + "'");
    }
}

void memory_decoration_refusal(Results &results, const char *name, Shape shape, bool physical)
{
    const auto module = make_module(shape, Layout::LiteralCollisions, physical);
    constexpr const char *diagnostic = "fragment control does not admit volatile or coherent memory decorations";
    refuse(results, name, module, diagnostic);
    // The shared lower API is callable independently of factory validation. Refusal
    // must be transactional there too, before any dead state or read is removed.
    ++results.cases;
    pb::MetalFragmentIR ir(module.words.data(), module.words.size());
    const auto original = ir.serialize();
    try {
        (void)pb::metal_fragment_dce(ir, covered, owned_anchor);
        results.check(false, std::string(name) + ": lower DCE unexpectedly admitted memory decoration");
    } catch (const std::exception &error) {
        results.check(std::string(error.what()) == diagnostic, std::string(name) + ": lower DCE: " + error.what());
    }
    results.check(ir.serialize() == original, std::string(name) + ": lower DCE refusal changed the module");
}

void tests(Results &results)
{
    std::vector<std::pair<spv::Op, unsigned>> dispatch_signature, simple_signature;
    size_t removed_values = 0, removed_stores = 0;
    for (Layout layout : {Layout::Dense, Layout::LiteralCollisions, Layout::Reversed}) {
        auto module = make_module(Shape::Dispatch, layout);
        const auto prepared = positive(results, "owned forward EXECZ dispatcher", module);
        const auto operations = signature(prepared.words);
        if (layout == Layout::Dense) {
            dispatch_signature = operations;
            removed_values = prepared.dce.removed_values;
            removed_stores = prepared.dce.removed_stores;
        } else results.check(operations == dispatch_signature && prepared.dce.removed_values == removed_values &&
            prepared.dce.removed_stores == removed_stores, "ID renumbering changed normalization or DCE");
        if (layout == Layout::LiteralCollisions) {
            results.check(module.id(Id::DeadVoteLocal) == 64 && module.id(Id::DeadAny) == 4,
                           "literal-collision fixture did not force the advertised independent IDs");
            results.check(!store_to(prepared.words, 64) && !typed_result(prepared.words, spv::OpGroupNonUniformAny, 4),
                           "literal operands retained the dead variable or dead Any");
        }
        (void)positive(results, "inverted EXECZ condition", make_module(Shape::InvertedSkip, layout));
        const auto simple = positive(results, "simple no-dispatch quad shader", make_module(Shape::Simple, layout, false));
        if (layout == Layout::Dense) simple_signature = signature(simple.words);
        else results.check(signature(simple.words) == simple_signature, "ID renumbering changed a simple shader");
    }
    const auto reordered = positive(results, "reverse physical case layout", make_module(Shape::Reordered, Layout::LiteralCollisions));
    results.check(signature(reordered.words) == dispatch_signature, "physical case order changed the normalized forward schedule");
    for (Shape shape : {Shape::TrueAny, Shape::FalseAny}) {
        const auto module = make_module(shape, Layout::LiteralCollisions, false);
        const auto prepared = positive(results, "live literal subgroup Any", module);
        bool found = false;
        for (const auto &op : instructions(prepared.words))
            if (op.opcode == spv::OpCompositeConstruct && op.extent == 7 && prepared.words[op.at + 2] == module.id(Id::FinalOutput)) {
                found = true;
                results.check(constant_value(prepared.words, prepared.words[op.at + 4]) == uint32_t(shape == Shape::TrueAny),
                               "literal Any folded to the wrong exported word");
            }
        results.check(found, "live literal Any export not found");
    }
    // The read captures both entry words before the later case overwrites the local.
    // Actual CFG edges cannot revisit the entry, so this is now a proved positive.
    (void)positive(results, "entry pair captured before later overwrite", make_module(Shape::PairRedefinition));
    (void)positive(results, "explicit nondefault push offset", make_module(Shape::WrongPushOffset), covered, 68);
    (void)positive(results, "volatile physical read preserved", make_module(Shape::VolatileRead));
    const auto guarded_store_module = make_module(Shape::UnvisitedStore, Layout::LiteralCollisions, false);
    const auto guarded_store = positive(results, "external store bypassed by empty mask", guarded_store_module, {});
    results.check(typed_result(guarded_store.words, spv::OpCompositeConstruct, guarded_store_module.id(Id::ExternalOutput)),
                   "the guarded external-store path was incorrectly deleted");

    const auto descriptor = make_module();
    constexpr const char *span_diagnostic = "outside validated descriptor spans";
    constexpr const char *address_diagnostic = "lacks canonical descriptor coverage";
    refuse(results, "missing descriptor span", descriptor, span_diagnostic, {});
    refuse(results, "partial descriptor span", descriptor, span_diagnostic, {{descriptor_pair, descriptor_offset, 2, true}});
    refuse(results, "separate partial spans", descriptor, span_diagnostic,
           {{descriptor_pair, descriptor_offset, 2, true}, {descriptor_pair, descriptor_offset + 2, 2, true}});
    refuse(results, "wrong descriptor origin", descriptor, span_diagnostic, {{descriptor_pair + 2, descriptor_offset, descriptor_bytes, true}});
    refuse(results, "wrong descriptor offset", descriptor, span_diagnostic, {{descriptor_pair, descriptor_offset + 4, descriptor_bytes, true}});
    refuse(results, "direct user words are not memory coverage", descriptor, span_diagnostic,
           {{descriptor_pair, descriptor_offset, descriptor_bytes, false}});
    refuse(results, "wrong address anchor", descriptor, address_diagnostic, covered, owned_anchor + 4);
    struct AddressCase { const char *name; Shape shape; };
    const AddressCase addresses[] = {
        {"unmasked high address word", Shape::Noncanonical}, {"wrong high-word mask", Shape::WrongHighMask},
        {"unaligned physical offset", Shape::UnalignedOffset}, {"wrong push member offset", Shape::WrongPushOffset},
        {"wrong push array stride", Shape::WrongStride}
    };
    for (const auto &item : addresses) refuse(results, item.name, make_module(item.shape), address_diagnostic);

    refuse(results, "copied Function pointer alias", make_module(Shape::PointerAlias), "alias-free nonvolatile function variables");
    refuse(results, "volatile Function local store", make_module(Shape::VolatileStore), "alias-free nonvolatile function variables");
    refuse(results, "live Any directly exported", make_module(Shape::LiveAny, Layout::LiteralCollisions, false), "fragment live wave vote");
    refuse(results, "live SCC vote through Function storage", make_module(Shape::LiveScc, Layout::LiteralCollisions, false), "fragment live wave vote");
    refuse(results, "empty mask changes a live local", make_module(Shape::EmptyChanges, Layout::LiteralCollisions, false), "fragment empty path changes");
    refuse(results, "empty mask performs an output store", make_module(Shape::EmptyExternalStore, Layout::LiteralCollisions, false), "fragment empty path performs");
    refuse(results, "backward guest edge is a real loop", make_module(Shape::RealLoop, Layout::LiteralCollisions, false), "fragment PC assignment is not a forward guest edge");
    refuse(results, "unproved division on empty path", make_module(Shape::UnknownEmptyOpcode, Layout::LiteralCollisions, false), "fragment empty path contains an unproved operation");

    compiler_refusal(results, "late-layout early kill", Shape::EarlyKill, "termination before a later quad read");
    compiler_refusal(results, "quad reads in a per-lane conditional", Shape::PerLaneQuad, "inside per-lane control");

    memory_decoration_refusal(results, "volatile push aggregate member", Shape::VolatileMember, true);
    memory_decoration_refusal(results, "coherent push aggregate member", Shape::CoherentMember, true);
    memory_decoration_refusal(results, "volatile Input base through a plain scalar chain", Shape::VolatileInputChain, false);

    // Redefining a result ID is malformed input, separate from the valid CFG negatives.
    // No stock-parser or semantic-equivalence credit is claimed for this case.
    ++results.cases;
    auto duplicate = make_module(Shape::Simple, Layout::LiteralCollisions, false);
    const auto constants = instructions(duplicate.words);
    for (const auto &op : constants) if (op.opcode == spv::OpConstant && duplicate.words[op.at + 2] == duplicate.id(Id::Zero)) {
        const Words definition(duplicate.words.begin() + op.at, duplicate.words.begin() + op.at + op.extent);
        duplicate.words.insert(duplicate.words.begin() + op.at, definition.begin(), definition.end());
        break;
    }
    try {
        (void)pb::prepare_metal_fragment(duplicate.words.data(), duplicate.words.size(), {}, owned_anchor);
        results.check(false, "redefined result ID unexpectedly accepted");
    } catch (const std::exception &error) {
        results.check(std::string(error.what()).find("invalid or redefined result ID") != std::string::npos,
                       std::string("redefined result ID: ") + error.what());
    }
}
}

int main()
{
    Results results;
    try { tests(results); }
    catch (const std::exception &error) { results.check(false, std::string("fixture exception: ") + error.what()); }
    std::printf("SUMMARY cases=%u checks=%u failures=%u\n", results.cases, results.checks, results.failures);
    return results.failures ? 1 : 0;
}
