/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Owned SPIR-V grammar and native Metal witnesses for an optional local-math case
   and an exact typed UnpackHalf2x16 in its common tail.
   No instruction stream, address or expected value comes from a game artifact.
   This primitive fixture does not exercise the executor's shader-cache preflight. */
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include "metal_fragment_control.hpp"
#include "metal_fragment_quads.hpp"
#include "metal_fragment_read_range.hpp"
#include "GLSL.std.450.h"
#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <initializer_list>
#include <map>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {
using Words = std::vector<uint32_t>;
using Pixel = std::array<uint32_t, 4>;
constexpr unsigned side = 8;
constexpr uint32_t baseline_red = 0x2468ace0u, baseline_blue = 0x7d23c951u;
constexpr uint32_t true_tag = 0x6a850101u, false_tag = 0x91760200u;
constexpr uint64_t owned_anchor = UINT64_C(0x6d10000000);
constexpr uint32_t pair_slot = 8, read_offset = 12;
constexpr std::array<uint32_t, 4> fault_initial = {0x52e160a7u, 0, 0x73c05bd1u, 0xe194268fu};
constexpr Pixel clear_pixel = {UINT32_MAX, UINT32_MAX, 0, UINT32_MAX};
constexpr std::array<Pixel, 2> patterns = {{
    {0, UINT32_MAX, 0x7fc05432u, 0x80000000u},
    {0x3fc00000u, 0xffc07654u, UINT32_MAX, 0}
}};

enum class Shape {
    Ordinary, Math, Pointer, PointerLater, PointerPrior, PointerConditional,
    QuadBody, QuadTail, BodyInput, BodyPhysical, BodyStore, BodyKill, BodyNested, Backedge, UnsupportedExt, DerivativeTail,
    SccOverwrite, SccReadArm, SccReadModifyWrite, SccOptionalRead, SccLoop
};
enum class Layout { Dense, LiteralCollisions, Reversed };
enum class Tail { None, HalfPair, UnormPair, PointerOutput, DoubleMath, SignedHalf };

struct Results {
    unsigned checks = 0, failures = 0, cpu_modules = 0, native_draws = 0;
    unsigned raster_covered = 0, expected_written = 0, observed_written = 0;
    unsigned tail_draws = 0, tail_covered = 0, tail_written = 0;
    int tail_subnormal_policy = -1;
    std::vector<std::pair<std::string, Words>> artifacts;
    void check(bool good, const std::string &message)
    {
        ++checks;
        if (!good) {
            ++failures;
            if (failures <= 40) std::fprintf(stderr, "%s\n", message.c_str());
        }
    }
};

/* Symbols are remapped during assembly, independently of production's ID classifier.
   Literal64/4/type widths remain literal even when a local/result has that ID. */
struct Symbols {
    std::map<std::string, uint32_t> assigned;
    uint32_t next = 128, bound = 1;
    Layout layout;
    explicit Symbols(Layout mode) : layout(mode) {}
    uint32_t operator()(const std::string &name)
    {
        const auto found = assigned.find(name);
        if (found != assigned.end()) return found->second;
        uint32_t value;
        if (layout == Layout::LiteralCollisions && name == "low_local") value = 64;
        else if (layout == Layout::LiteralCollisions && name == "quad_numeric") value = 4;
        else {value = layout == Layout::Reversed ? 4096 - next : next; next += 3;}
        bound = std::max(bound, value + 1);
        assigned.emplace(name, value);
        return value;
    }
};
void emit(Words &words, spv::Op opcode, std::initializer_list<uint32_t> operands)
{
    words.push_back(uint32_t(operands.size() + 1) << 16 | uint32_t(opcode));
    words.insert(words.end(), operands.begin(), operands.end());
}
void text(Words &words, spv::Op opcode, std::initializer_list<uint32_t> prefix,
          const char *value, std::initializer_list<uint32_t> suffix = {})
{
    const size_t begin = words.size(), bytes = std::strlen(value) + 1;
    words.push_back(0);
    words.insert(words.end(), prefix.begin(), prefix.end());
    const size_t string_at = words.size();
    words.resize(string_at + (bytes + 3) / 4, 0);
    std::memcpy(words.data() + string_at, value, bytes);
    words.insert(words.end(), suffix.begin(), suffix.end());
    words[begin] = uint32_t(words.size() - begin) << 16 | uint32_t(opcode);
}
struct Module {
    Words words;
    uint32_t join, decision, physical_read, scc_vote;
    bool reversed, math, physical;
    unsigned source;
};

Module make_module(bool reversed = false, unsigned source = 3, Shape shape = Shape::Ordinary,
                   Layout layout = Layout::Dense, Tail tail = Tail::None)
{
    Symbols i(layout);
    const auto k = [&](unsigned value) {return i("constant_" + std::to_string(value));};
    const bool physical = shape == Shape::Pointer || shape == Shape::PointerLater ||
        shape == Shape::PointerPrior || shape == Shape::PointerConditional || shape == Shape::BodyPhysical;
    const bool math = shape == Shape::Math || shape == Shape::UnsupportedExt;
    const bool scc = shape >= Shape::SccOverwrite && shape <= Shape::SccLoop;
    const bool numeric = !math && shape != Shape::BodyInput && shape != Shape::BodyPhysical &&
        shape != Shape::BodyKill && shape != Shape::DerivativeTail;
    Words pre, entries, annotations, types, entry, case0, case1, case2, footer;
    emit(pre, spv::OpCapability, {spv::CapabilityShader});
    emit(pre, spv::OpCapability, {spv::CapabilityGroupNonUniform});
    emit(pre, spv::OpCapability, {spv::CapabilityGroupNonUniformQuad});
    if (scc) emit(pre, spv::OpCapability, {spv::CapabilityGroupNonUniformVote});
    if (tail == Tail::DoubleMath) emit(pre, spv::OpCapability, {spv::CapabilityFloat64});
    if (physical) {
        emit(pre, spv::OpCapability, {spv::CapabilityInt64});
        emit(pre, spv::OpCapability, {spv::CapabilityPhysicalStorageBufferAddresses});
        text(pre, spv::OpExtension, {}, "SPV_KHR_physical_storage_buffer");
    }
    text(pre, spv::OpExtInstImport, {i("glsl")}, "GLSL.std.450");
    emit(pre, spv::OpMemoryModel, {physical ? spv::AddressingModelPhysicalStorageBuffer64 : spv::AddressingModelLogical,
                                  spv::MemoryModelGLSL450});
    if (shape == Shape::BodyStore)
        text(entries, spv::OpEntryPoint, {spv::ExecutionModelFragment, i("main")}, "owned_diamond", {i("position"), i("output"), i("scalar_output")});
    else text(entries, spv::OpEntryPoint, {spv::ExecutionModelFragment, i("main")}, "owned_diamond", {i("position"), i("output")});
    emit(entries, spv::OpExecutionMode, {i("main"), spv::ExecutionModeOriginUpperLeft});
    emit(annotations, spv::OpDecorate, {i("position"), spv::DecorationBuiltIn, spv::BuiltInFragCoord});
    emit(annotations, spv::OpDecorate, {i("output"), spv::DecorationLocation, 0});
    if (shape == Shape::BodyStore) emit(annotations, spv::OpDecorate, {i("scalar_output"), spv::DecorationLocation, 1});
    emit(annotations, spv::OpDecorate, {i("array"), spv::DecorationArrayStride, 4});
    emit(annotations, spv::OpDecorate, {i("push_struct"), spv::DecorationBlock});
    emit(annotations, spv::OpMemberDecorate, {i("push_struct"), 0, spv::DecorationOffset, 64});
    emit(types, spv::OpTypeVoid, {i("void")});
    emit(types, spv::OpTypeFunction, {i("function"), i("void")});
    emit(types, spv::OpTypeBool, {i("bool")});
    emit(types, spv::OpTypeInt, {i("uint"), 32, 0});
    if (tail == Tail::SignedHalf) emit(types, spv::OpTypeInt, {i("int"), 32, 1});
    if (physical) emit(types, spv::OpTypeInt, {i("ulong"), 64, 0});
    emit(types, spv::OpTypeFloat, {i("float"), 32});
    if (tail != Tail::None) emit(types, spv::OpTypeVector, {i("float2"), i("float"), 2});
    if (tail == Tail::DoubleMath) emit(types, spv::OpTypeFloat, {i("double"), 64});
    emit(types, spv::OpTypeVector, {i("float4"), i("float"), 4});
    emit(types, spv::OpTypeVector, {i("uint4"), i("uint"), 4});
    for (unsigned value = 0; value <= 16; ++value) emit(types, spv::OpConstant, {i("uint"), k(value), value});
    emit(types, spv::OpConstant, {i("uint"), i("shift32"), 32});
    emit(types, spv::OpConstant, {i("uint"), i("high_mask"), 0xffff});
    for (const auto &constant : {std::pair<const char *, uint32_t>{"baseline_red", baseline_red},
            {"baseline_blue", baseline_blue}, {"true_tag", true_tag}, {"false_tag", false_tag}})
        emit(types, spv::OpConstant, {i("uint"), i(constant.first), constant.second});
    emit(types, spv::OpConstant, {i("float"), i("half"), 0x3f000000u});
    emit(types, spv::OpConstantTrue, {i("bool"), i("true")});
    emit(types, spv::OpConstantFalse, {i("bool"), i("false")});
    if (physical) {
        emit(types, spv::OpConstant, {i("ulong"), i("anchor"), uint32_t(owned_anchor), uint32_t(owned_anchor >> 32)});
        emit(types, spv::OpConstant, {i("ulong"), i("offset"), read_offset, 0});
        emit(types, spv::OpConstant, {i("ulong"), i("body_address"), 0x20000, 0});
    }
    emit(types, spv::OpTypeArray, {i("array"), i("uint"), k(16)});
    emit(types, spv::OpTypeStruct, {i("push_struct"), i("array")});
    for (const auto &pointer : {std::pair<const char *, const char *>{"local_uint", "uint"},
            {"local_bool", "bool"}, {"local_float", "float"}})
        emit(types, spv::OpTypePointer, {i(pointer.first), spv::StorageClassFunction, i(pointer.second)});
    emit(types, spv::OpTypePointer, {i("input_pointer"), spv::StorageClassInput, i("float4")});
    emit(types, spv::OpTypePointer, {i("output_pointer"), spv::StorageClassOutput, i("uint4")});
    if (shape == Shape::BodyStore) emit(types, spv::OpTypePointer, {i("scalar_output_pointer"), spv::StorageClassOutput, i("uint")});
    emit(types, spv::OpTypePointer, {i("push_pointer"), spv::StorageClassPushConstant, i("push_struct")});
    emit(types, spv::OpTypePointer, {i("push_word_pointer"), spv::StorageClassPushConstant, i("uint")});
    if (physical) emit(types, spv::OpTypePointer, {i("physical_pointer"), spv::StorageClassPhysicalStorageBuffer, i("uint")});
    emit(types, spv::OpVariable, {i("input_pointer"), i("position"), spv::StorageClassInput});
    emit(types, spv::OpVariable, {i("output_pointer"), i("output"), spv::StorageClassOutput});
    if (shape == Shape::BodyStore) emit(types, spv::OpVariable, {i("scalar_output_pointer"), i("scalar_output"), spv::StorageClassOutput});
    emit(types, spv::OpVariable, {i("push_pointer"), i("push"), spv::StorageClassPushConstant});
    const auto push_load = [&](Words &section, const std::string &name, uint32_t index) {
        emit(section, spv::OpAccessChain, {i("push_word_pointer"), i(name + "_pointer"), i("push"), k(0), index});
        emit(section, spv::OpLoad, {i("uint"), i(name), i(name + "_pointer")});
    };
    emit(entry, spv::OpFunction, {i("void"), i("main"), spv::FunctionControlMaskNone, i("function")});
    emit(entry, spv::OpLabel, {i("entry")});
    for (const char *name : {"pc", "body_value", "body_marker", "body_blue", "quad_local", "packed_local", "pattern_local", "low_local", "high_local", "dead_local", "scratch_local"})
        emit(entry, spv::OpVariable, {i("local_uint"), i(name), spv::StorageClassFunction});
    emit(entry, spv::OpVariable, {i("local_bool"), i("predicate_local"), spv::StorageClassFunction});
    emit(entry, spv::OpVariable, {i("local_bool"), i("scc_local"), spv::StorageClassFunction});
    emit(entry, spv::OpVariable, {i("local_float"), i("math_local"), spv::StorageClassFunction});
    emit(entry, spv::OpStore, {i("pc"), k(0)});
    emit(entry, spv::OpStore, {i("body_value"), i("baseline_red")});
    emit(entry, spv::OpStore, {i("body_marker"), k(0)});
    emit(entry, spv::OpStore, {i("body_blue"), i("baseline_blue")});
    emit(entry, spv::OpStore, {i("quad_local"), k(0)});
    emit(entry, spv::OpStore, {i("scratch_local"), k(0)});
    if (physical) {
        push_load(entry, "low_word", k(pair_slot)); push_load(entry, "high_word", k(pair_slot + 1));
        emit(entry, spv::OpStore, {i("low_local"), i("low_word")});
        emit(entry, spv::OpStore, {i("high_local"), i("high_word")});
    }
    if (math) {
        push_load(entry, "math_input", k(6));
        emit(entry, spv::OpBitcast, {i("float"), i("math_float"), i("math_input")});
        emit(entry, spv::OpStore, {i("math_local"), i("math_float")});
    }
    if (shape == Shape::BodyPhysical)
        emit(entry, spv::OpConvertUToPtr, {i("physical_pointer"), i("body_physical"), i("body_address")});
    emit(entry, spv::OpBranch, {i("header")});
    emit(entry, spv::OpLabel, {i("header")});
    emit(entry, spv::OpLoopMerge, {i("exit"), i("continue"), spv::LoopControlMaskNone});
    emit(entry, spv::OpBranch, {i("dispatch")});
    emit(entry, spv::OpLabel, {i("dispatch")});
    emit(entry, spv::OpLoad, {i("uint"), i("selector"), i("pc")});
    emit(entry, spv::OpSelectionMerge, {i("shared"), spv::SelectionControlMaskNone});
    emit(entry, spv::OpSwitch, {i("selector"), i("default"), 0, i("case0"), 1, i("case1"), 2, i("case2")});
    emit(entry, spv::OpLabel, {i("default")}); emit(entry, spv::OpBranch, {i("shared")});

    emit(case0, spv::OpLabel, {i("case0")});
    emit(case0, spv::OpLoad, {i("float4"), i("coordinate"), i("position")});
    for (unsigned component = 0; component < 2; ++component) {
        const std::string axis = component ? "y" : "x";
        emit(case0, spv::OpCompositeExtract, {i("float"), i(axis + "_float"), i("coordinate"), component});
        emit(case0, spv::OpConvertFToU, {i("uint"), i(axis), i(axis + "_float")});
        emit(case0, spv::OpBitwiseAnd, {i("uint"), i(axis + "_parity"), i(axis), k(1)});
        emit(case0, spv::OpShiftRightLogical, {i("uint"), i(axis + "_quad"), i(axis), k(1)});
    }
    emit(case0, spv::OpShiftLeftLogical, {i("uint"), i("lane_y"), i("y_parity"), k(1)});
    emit(case0, spv::OpBitwiseOr, {i("uint"), i("lane"), i("x_parity"), i("lane_y")});
    emit(case0, spv::OpShiftLeftLogical, {i("uint"), i("quad_y"), i("y_quad"), k(2)});
    emit(case0, spv::OpBitwiseOr, {i("uint"), i("quad"), i("x_quad"), i("quad_y")});
    push_load(case0, "seed", k(4)); push_load(case0, "varying", k(5));
    emit(case0, spv::OpINotEqual, {i("bool"), i("varying_bool"), i("varying"), k(0)});
    emit(case0, spv::OpBitwiseXor, {i("uint"), i("varied_mask"), i("seed"), i("quad")});
    emit(case0, spv::OpSelect, {i("uint"), i("mask"), i("varying_bool"), i("varied_mask"), i("seed")});
    emit(case0, spv::OpShiftRightLogical, {i("uint"), i("mask_shifted"), i("mask"), i("lane")});
    emit(case0, spv::OpBitwiseAnd, {i("uint"), i("mask_bit"), i("mask_shifted"), k(1)});
    emit(case0, spv::OpINotEqual, {i("bool"), i("predicate"), i("mask_bit"), k(0)});
    emit(case0, spv::OpStore, {i("predicate_local"), i("predicate")});
    if (numeric) {
        push_load(case0, "pattern", i("lane"));
        emit(case0, spv::OpStore, {i("pattern_local"), i("pattern")});
    }
    if (scc) {
        emit(case0, spv::OpGroupNonUniformAny, {i("bool"), i("scc_vote"), k(3), i("predicate")});
        emit(case0, spv::OpStore, {i("scc_local"), i("scc_vote")});
        for (unsigned selection = 0; selection < 2; ++selection) {
            const std::string prefix = "scc_selection_" + std::to_string(selection);
            emit(case0, spv::OpSelectionMerge, {i(prefix + "_merge"), spv::SelectionControlMaskNone});
            emit(case0, spv::OpBranchConditional, {i("predicate"), i(prefix + "_yes"), i(prefix + "_no")});
            emit(case0, spv::OpLabel, {i(prefix + "_yes")});
            if (selection == 0 && shape == Shape::SccReadArm) {
                emit(case0, spv::OpLoad, {i("bool"), i("arm_scc"), i("scc_local")});
                emit(case0, spv::OpSelect, {i("uint"), i("arm_observer"), i("arm_scc"), k(1), k(0)});
                emit(case0, spv::OpStore, {i("scratch_local"), i("arm_observer")});
            } else if (selection == 0 && shape == Shape::SccOptionalRead) {
                emit(case0, spv::OpIEqual, {i("bool"), i("optional_overwrite"), i("mask_bit"), k(0)});
                emit(case0, spv::OpStore, {i("scc_local"), i("optional_overwrite")});
            } else if (selection == 0)
                emit(case0, spv::OpStore, {i("scratch_local"), k(1)});
            emit(case0, spv::OpBranch, {i(prefix + "_merge")});
            emit(case0, spv::OpLabel, {i(prefix + "_no")});
            if (selection == 0) emit(case0, spv::OpStore, {i("scratch_local"), k(2)});
            emit(case0, spv::OpBranch, {i(prefix + "_merge")});
            emit(case0, spv::OpLabel, {i(prefix + "_merge")});
        }
        if (shape == Shape::SccLoop) {
            emit(case0, spv::OpBranch, {i("scc_loop_header")});
            emit(case0, spv::OpLabel, {i("scc_loop_header")});
            emit(case0, spv::OpLoopMerge, {i("scc_loop_merge"), i("scc_loop_continue"), spv::LoopControlMaskNone});
            emit(case0, spv::OpBranchConditional, {i("predicate"), i("scc_loop_merge"), i("scc_loop_body")});
            emit(case0, spv::OpLabel, {i("scc_loop_body")}); emit(case0, spv::OpBranch, {i("scc_loop_continue")});
            emit(case0, spv::OpLabel, {i("scc_loop_continue")}); emit(case0, spv::OpBranch, {i("scc_loop_header")});
            emit(case0, spv::OpLabel, {i("scc_loop_merge")});
        }
        if (shape == Shape::SccReadModifyWrite) {
            emit(case0, spv::OpLoad, {i("bool"), i("old_scc"), i("scc_local")});
            emit(case0, spv::OpLogicalNot, {i("bool"), i("scc_overwrite"), i("old_scc")});
        } else if (shape != Shape::SccOptionalRead)
            emit(case0, spv::OpIEqual, {i("bool"), i("scc_overwrite"), i("mask_bit"), k(0)});
        if (shape != Shape::SccOptionalRead) emit(case0, spv::OpStore, {i("scc_local"), i("scc_overwrite")});
    }
    if (shape == Shape::PointerConditional) {
        emit(case0, spv::OpSelectionMerge, {i("pointer_merge"), spv::SelectionControlMaskNone});
        emit(case0, spv::OpBranchConditional, {i("predicate"), i("pointer_write"), i("pointer_keep")});
        emit(case0, spv::OpLabel, {i("pointer_write")});
        emit(case0, spv::OpStore, {i("low_local"), k(0)}); emit(case0, spv::OpBranch, {i("pointer_merge")});
        emit(case0, spv::OpLabel, {i("pointer_keep")}); emit(case0, spv::OpBranch, {i("pointer_merge")});
        emit(case0, spv::OpLabel, {i("pointer_merge")});
    }
    if (shape == Shape::PointerPrior) emit(case0, spv::OpStore, {i("low_local"), k(0)});
    if (physical && shape != Shape::BodyPhysical) {
        emit(case0, spv::OpLoad, {i("uint"), i("low"), i("low_local")});
        emit(case0, spv::OpLoad, {i("uint"), i("high"), i("high_local")});
        emit(case0, spv::OpBitwiseAnd, {i("uint"), i("masked_high"), i("high"), i("high_mask")});
        emit(case0, spv::OpUConvert, {i("ulong"), i("wide_low"), i("low")});
        emit(case0, spv::OpUConvert, {i("ulong"), i("wide_high"), i("masked_high")});
        emit(case0, spv::OpShiftLeftLogical, {i("ulong"), i("shifted_high"), i("wide_high"), i("shift32")});
        emit(case0, spv::OpBitwiseOr, {i("ulong"), i("pair"), i("wide_low"), i("shifted_high")});
        emit(case0, spv::OpIAdd, {i("ulong"), i("anchored"), i("pair"), i("anchor")});
        emit(case0, spv::OpIAdd, {i("ulong"), i("address"), i("anchored"), i("offset")});
        emit(case0, spv::OpConvertUToPtr, {i("physical_pointer"), i("physical"), i("address")});
        emit(case0, spv::OpLoad, {i("uint"), i("physical_read"), i("physical"), spv::MemoryAccessAlignedMask, 4});
        emit(case0, spv::OpStore, {i("dead_local"), i("physical_read")});
    }
    if (numeric && shape != Shape::QuadBody && shape != Shape::QuadTail) {
        emit(case0, spv::OpGroupNonUniformQuadBroadcast, {i("uint"), i("quad_numeric"), k(3), i("pattern"), k(source)});
        emit(case0, spv::OpStore, {i("quad_local"), i("quad_numeric")});
    }
    emit(case0, spv::OpSelect, {i("uint"), i("own_bit"), i("predicate"), k(1), k(0)});
    uint32_t packed = i("own_bit");
    for (unsigned direction = 0; direction < 3; ++direction) {
        const std::string name = "swap_" + std::to_string(direction);
        emit(case0, spv::OpGroupNonUniformQuadSwap, {i("bool"), i(name), k(3), i("predicate"), k(direction)});
        emit(case0, spv::OpSelect, {i("uint"), i(name + "_word"), i(name), k(1), k(0)});
        emit(case0, spv::OpShiftLeftLogical, {i("uint"), i(name + "_shifted"), i(name + "_word"), k(direction + 1)});
        emit(case0, spv::OpBitwiseOr, {i("uint"), i(name + "_packed"), packed, i(name + "_shifted")});
        packed = i(name + "_packed");
    }
    emit(case0, spv::OpStore, {i("packed_local"), packed});
    emit(case0, spv::OpLoad, {i("bool"), i("decision"), i("predicate_local")});
    emit(case0, spv::OpSelect, {i("uint"), i("next_pc"), i("decision"), k(reversed ? 2 : 1), k(reversed ? 1 : 2)});
    emit(case0, spv::OpStore, {i("pc"), i("next_pc")});
    // This store stays live through the tail observer. Re-loading the local to branch
    // would reverse the original decision, even though its address has not changed.
    emit(case0, spv::OpLogicalNot, {i("bool"), i("later_predicate"), i("decision")});
    emit(case0, spv::OpStore, {i("predicate_local"), i("later_predicate")});
    emit(case0, spv::OpBranch, {i("shared")});

    emit(case1, spv::OpLabel, {i("case1")});
    if (shape == Shape::BodyNested) {
        emit(case1, spv::OpLoad, {i("bool"), i("body_condition"), i("predicate_local")});
        emit(case1, spv::OpSelectionMerge, {i("body_merge"), spv::SelectionControlMaskNone});
        emit(case1, spv::OpBranchConditional, {i("body_condition"), i("body_yes"), i("body_no")});
        emit(case1, spv::OpLabel, {i("body_yes")}); emit(case1, spv::OpBranch, {i("body_merge")});
        emit(case1, spv::OpLabel, {i("body_no")}); emit(case1, spv::OpBranch, {i("body_merge")});
        emit(case1, spv::OpLabel, {i("body_merge")});
    }
    if (shape == Shape::QuadBody) {
        emit(case1, spv::OpLoad, {i("uint"), i("body_pattern"), i("pattern_local")});
        emit(case1, spv::OpGroupNonUniformQuadBroadcast, {i("uint"), i("quad_numeric"), k(3), i("body_pattern"), k(source)});
        emit(case1, spv::OpStore, {i("quad_local"), i("quad_numeric")});
    }
    uint32_t body_value = i("body_loaded");
    if (math) {
        emit(case1, spv::OpLoad, {i("float"), i("body_float"), i("math_local")});
        emit(case1, spv::OpExtInst, {i("float"), i("log"), i("glsl"),
            shape == Shape::UnsupportedExt ? GLSLstd450Sin : GLSLstd450Log2, i("body_float")});
        emit(case1, spv::OpExtInst, {i("float"), i("exp"), i("glsl"), GLSLstd450Exp2, i("log")});
        emit(case1, spv::OpFMul, {i("float"), i("scaled"), i("exp"), i("half")});
        emit(case1, spv::OpBitcast, {i("uint"), body_value, i("scaled")});
    } else if (shape == Shape::BodyInput) {
        emit(case1, spv::OpLoad, {i("float4"), i("body_position"), i("position")});
        emit(case1, spv::OpCompositeExtract, {i("float"), i("body_x"), i("body_position"), 0});
        emit(case1, spv::OpConvertFToU, {i("uint"), body_value, i("body_x")});
    } else if (shape == Shape::BodyPhysical) {
        emit(case1, spv::OpLoad, {i("uint"), body_value, i("body_physical"), spv::MemoryAccessAlignedMask, 4});
    } else emit(case1, spv::OpLoad, {i("uint"), body_value, i("quad_local")});
    emit(case1, spv::OpStore, {i("body_value"), body_value});
    emit(case1, spv::OpLoad, {i("uint"), i("body_packed"), i("packed_local")});
    emit(case1, spv::OpStore, {i("body_blue"), i("body_packed")});
    emit(case1, spv::OpStore, {i("body_marker"), k(1)});
    if (shape == Shape::BodyStore) emit(case1, spv::OpStore, {i("scalar_output"), body_value});
    if (shape == Shape::BodyKill) emit(case1, spv::OpKill, {});
    else {
        emit(case1, spv::OpStore, {i("pc"), k(shape == Shape::Backedge ? 1 : 2)});
        emit(case1, spv::OpBranch, {i("shared")});
    }

    emit(case2, spv::OpLabel, {i("case2")});
    emit(case2, spv::OpLoad, {i("uint"), i("red"), i("body_value")});
    emit(case2, spv::OpLoad, {i("uint"), i("green"), i("body_marker")});
    emit(case2, spv::OpLoad, {i("uint"), i("blue"), i("body_blue")});
    emit(case2, spv::OpLoad, {i("bool"), i("final_predicate"), i("predicate_local")});
    emit(case2, spv::OpSelect, {i("uint"), i("tag"), i("final_predicate"), i("true_tag"), i("false_tag")});
    uint32_t red = i("red"), tag = i("tag");
    if (scc) {
        emit(case2, spv::OpLoad, {i("bool"), i("final_scc"), i("scc_local")});
        emit(case2, spv::OpSelect, {i("uint"), i("scc_observer"), i("final_scc"), k(1), k(0)});
        emit(case2, spv::OpLoad, {i("uint"), i("scratch_observer"), i("scratch_local")});
        emit(case2, spv::OpBitwiseXor, {i("uint"), i("scc_tag"), tag, i("scc_observer")});
        emit(case2, spv::OpBitwiseXor, {i("uint"), i("scratch_tag"), i("scc_tag"), i("scratch_observer")});
        tag = i("scratch_tag");
    }
    if (shape == Shape::QuadTail) {
        emit(case2, spv::OpLoad, {i("uint"), i("tail_pattern"), i("pattern_local")});
        emit(case2, spv::OpGroupNonUniformQuadBroadcast, {i("uint"), i("quad_numeric"), k(3), i("tail_pattern"), k(source)});
        red = i("quad_numeric");
    }
    if (shape == Shape::DerivativeTail) {
        emit(case2, spv::OpLoad, {i("float4"), i("tail_position"), i("position")});
        emit(case2, spv::OpCompositeExtract, {i("float"), i("tail_x"), i("tail_position"), 0});
        emit(case2, spv::OpDPdx, {i("float"), i("tail_derivative"), i("tail_x")});
        emit(case2, spv::OpBitcast, {i("uint"), i("tail_derivative_bits"), i("tail_derivative")});
        red = i("tail_derivative_bits");
    }
    if (shape == Shape::PointerLater) {
        emit(case2, spv::OpStore, {i("low_local"), k(0)});
        emit(case2, spv::OpLoad, {i("uint"), i("later_pointer_observer"), i("low_local")});
        emit(case2, spv::OpBitwiseXor, {i("uint"), i("observed_tag"), tag, i("later_pointer_observer")});
        tag = i("observed_tag");
    }
    uint32_t green = i("green"), blue = i("blue");
    if (tail != Tail::None) {
        // This per-lane input is dynamic and was captured before the optional case.
        // Both unpacked components stay live through independent integer channels.
        emit(case2, spv::OpLoad, {i("uint"), i("tail_word"), i("pattern_local")});
        if (tail == Tail::PointerOutput) {
            emit(case2, spv::OpExtInst, {i("float"), i("tail_scalar"), i("glsl"), GLSLstd450Modf, i("half"), i("math_local")});
            emit(case2, spv::OpLoad, {i("float"), i("tail_other"), i("math_local")});
            emit(case2, spv::OpBitcast, {i("uint"), i("tail_low_bits"), i("tail_scalar")});
            emit(case2, spv::OpBitcast, {i("uint"), i("tail_high_bits"), i("tail_other")});
        } else if (tail == Tail::DoubleMath) {
            emit(case2, spv::OpConvertUToF, {i("double"), i("tail_double"), i("tail_word")});
            emit(case2, spv::OpExtInst, {i("double"), i("tail_root"), i("glsl"), GLSLstd450Sqrt, i("tail_double")});
            emit(case2, spv::OpFConvert, {i("float"), i("tail_scalar"), i("tail_root")});
            emit(case2, spv::OpBitcast, {i("uint"), i("tail_low_bits"), i("tail_scalar")});
            emit(case2, spv::OpCopyObject, {i("uint"), i("tail_high_bits"), k(0)});
        } else {
            uint32_t operand = i("tail_word");
            if (tail == Tail::SignedHalf) {
                emit(case2, spv::OpBitcast, {i("int"), i("tail_signed"), operand});
                operand = i("tail_signed");
            }
            emit(case2, spv::OpExtInst, {i("float2"), i("tail_pair"), i("glsl"),
                tail == Tail::UnormPair ? GLSLstd450UnpackUnorm2x16 : GLSLstd450UnpackHalf2x16, operand});
            for (unsigned component = 0; component < 2; ++component) {
                const std::string name = component ? "tail_high" : "tail_low";
                emit(case2, spv::OpCompositeExtract, {i("float"), i(name), i("tail_pair"), component});
                emit(case2, spv::OpBitcast, {i("uint"), i(name + "_bits"), i(name)});
            }
        }
        // Keep the original quad results and optional-case outputs live as well.
        // The marker occupies bit0; packed quad data uses the remaining bits.
        emit(case2, spv::OpShiftLeftLogical, {i("uint"), i("tail_blue_shifted"), i("blue"), k(1)});
        emit(case2, spv::OpBitwiseOr, {i("uint"), i("tail_quad_observer"), i("tail_blue_shifted"), i("green")});
        emit(case2, spv::OpBitwiseXor, {i("uint"), i("tail_tag_observer"), tag, red});
        red = i("tail_low_bits"); green = i("tail_high_bits"); blue = i("tail_quad_observer"); tag = i("tail_tag_observer");
    }
    emit(case2, spv::OpCompositeConstruct, {i("uint4"), i("color"), red, green, blue, tag});
    emit(case2, spv::OpStore, {i("output"), i("color")}); emit(case2, spv::OpReturn, {});
    emit(footer, spv::OpLabel, {i("shared")}); emit(footer, spv::OpBranch, {i("continue")});
    emit(footer, spv::OpLabel, {i("continue")}); emit(footer, spv::OpBranch, {i("header")});
    emit(footer, spv::OpLabel, {i("exit")}); emit(footer, spv::OpReturn, {});
    emit(footer, spv::OpFunctionEnd, {});
    Module module{{spv::MagicNumber, 0x00010300, 0, i.bound, 0}, i("case2"), i("decision"),
                  physical && shape != Shape::BodyPhysical ? i("physical_read") : 0, scc ? i("scc_vote") : 0, reversed, math, physical, source};
    const std::array<const Words *, 10> normal = {&pre, &entries, &annotations, &types, &entry, &case0, &case1, &case2, &footer, nullptr};
    const std::array<const Words *, 10> reordered = {&pre, &entries, &annotations, &types, &entry, &case2, &case1, &case0, &footer, nullptr};
    for (const auto *section : layout == Layout::Reversed ? reordered : normal)
        if (section) module.words.insert(module.words.end(), section->begin(), section->end());
    // References in the descriptor fields above already exist; they allocate no extra IDs.
    module.words[3] = i.bound;
    return module;
}

/* A UInt64 Switch has two literal words per case. Deliberate literal/label-ID
   collisions expose a parser that incorrectly treats its operands as UInt32 cases.
   The alternate entry arm keeps the capture load reachable even when that wrong
   parser loses the overwrite-to-capture edge. No execution or branch pruning is used. */
Words make_switch64_module(Layout layout)
{
    Symbols i(layout);
    Words pre, declarations, body;
    const uint32_t terminal = i("wide_terminal");
    emit(pre, spv::OpCapability, {spv::CapabilityShader});
    emit(pre, spv::OpCapability, {spv::CapabilityInt64});
    emit(pre, spv::OpCapability, {spv::CapabilityPhysicalStorageBufferAddresses});
    text(pre, spv::OpExtension, {}, "SPV_KHR_physical_storage_buffer");
    emit(pre, spv::OpMemoryModel, {spv::AddressingModelPhysicalStorageBuffer64, spv::MemoryModelGLSL450});
    text(pre, spv::OpEntryPoint, {spv::ExecutionModelFragment, i("wide_main")}, "owned_switch64");
    emit(pre, spv::OpExecutionMode, {i("wide_main"), spv::ExecutionModeOriginUpperLeft});
    emit(pre, spv::OpDecorate, {i("wide_array"), spv::DecorationArrayStride, 4});
    emit(pre, spv::OpDecorate, {i("wide_push_struct"), spv::DecorationBlock});
    emit(pre, spv::OpMemberDecorate, {i("wide_push_struct"), 0, spv::DecorationOffset, 64});
    emit(declarations, spv::OpTypeVoid, {i("wide_void")});
    emit(declarations, spv::OpTypeFunction, {i("wide_function"), i("wide_void")});
    emit(declarations, spv::OpTypeBool, {i("wide_bool")});
    emit(declarations, spv::OpTypeInt, {i("wide_uint"), 32, 0});
    emit(declarations, spv::OpTypeInt, {i("wide_ulong"), 64, 0});
    for (const auto &constant : {std::pair<const char *, uint32_t>{"wide_zero", 0},
            {"wide_count", 16}, {"wide_pair_low", pair_slot}, {"wide_pair_high", pair_slot + 1},
            {"wide_condition_slot", 6}, {"wide_mask", 0xffff}, {"wide_shift", 32}, {"wide_changed_low", 0xfffffffcu}})
        emit(declarations, spv::OpConstant, {i("wide_uint"), i(constant.first), constant.second});
    emit(declarations, spv::OpConstant, {i("wide_ulong"), i("wide_selector"), 0, terminal});
    emit(declarations, spv::OpConstant, {i("wide_ulong"), i("wide_anchor"), uint32_t(owned_anchor), uint32_t(owned_anchor >> 32)});
    emit(declarations, spv::OpConstant, {i("wide_ulong"), i("wide_offset"), read_offset, 0});
    emit(declarations, spv::OpTypeArray, {i("wide_array"), i("wide_uint"), i("wide_count")});
    emit(declarations, spv::OpTypeStruct, {i("wide_push_struct"), i("wide_array")});
    emit(declarations, spv::OpTypePointer, {i("wide_local_pointer"), spv::StorageClassFunction, i("wide_uint")});
    emit(declarations, spv::OpTypePointer, {i("wide_push_pointer"), spv::StorageClassPushConstant, i("wide_push_struct")});
    emit(declarations, spv::OpTypePointer, {i("wide_word_pointer"), spv::StorageClassPushConstant, i("wide_uint")});
    emit(declarations, spv::OpTypePointer, {i("wide_physical_pointer"), spv::StorageClassPhysicalStorageBuffer, i("wide_uint")});
    emit(declarations, spv::OpVariable, {i("wide_push_pointer"), i("wide_push"), spv::StorageClassPushConstant});
    emit(body, spv::OpFunction, {i("wide_void"), i("wide_main"), spv::FunctionControlMaskNone, i("wide_function")});
    emit(body, spv::OpLabel, {i("wide_entry")});
    emit(body, spv::OpVariable, {i("wide_local_pointer"), i("wide_low_local"), spv::StorageClassFunction});
    emit(body, spv::OpVariable, {i("wide_local_pointer"), i("wide_high_local"), spv::StorageClassFunction});
    for (const auto &word : {std::pair<const char *, const char *>{"wide_low_word", "wide_pair_low"},
            {"wide_high_word", "wide_pair_high"}, {"wide_condition_word", "wide_condition_slot"}}) {
        emit(body, spv::OpAccessChain, {i("wide_word_pointer"), i(std::string(word.first) + "_pointer"),
            i("wide_push"), i("wide_zero"), i(word.second)});
        emit(body, spv::OpLoad, {i("wide_uint"), i(word.first), i(std::string(word.first) + "_pointer")});
    }
    emit(body, spv::OpStore, {i("wide_low_local"), i("wide_low_word")});
    emit(body, spv::OpStore, {i("wide_high_local"), i("wide_high_word")});
    emit(body, spv::OpINotEqual, {i("wide_bool"), i("wide_condition"), i("wide_condition_word"), i("wide_zero")});
    emit(body, spv::OpSelectionMerge, {i("wide_capture"), spv::SelectionControlMaskNone});
    emit(body, spv::OpBranchConditional, {i("wide_condition"), i("wide_overwrite"), i("wide_alternate")});
    emit(body, spv::OpLabel, {i("wide_overwrite")});
    emit(body, spv::OpStore, {i("wide_low_local"), i("wide_changed_low")});
    emit(body, spv::OpSelectionMerge, {i("wide_switch_merge"), spv::SelectionControlMaskNone});
    // Literals are low-word/high-word pairs. Under a wrong UInt32 decode, every
    // apparent target is terminal, hiding the real route to the capture block.
    emit(body, spv::OpSwitch, {i("wide_selector"), terminal,
        0, terminal, i("wide_route"), terminal, 0, terminal});
    emit(body, spv::OpLabel, {i("wide_route")}); emit(body, spv::OpBranch, {i("wide_switch_merge")});
    emit(body, spv::OpLabel, {i("wide_switch_merge")}); emit(body, spv::OpBranch, {i("wide_capture")});
    emit(body, spv::OpLabel, {i("wide_alternate")}); emit(body, spv::OpBranch, {i("wide_capture")});
    emit(body, spv::OpLabel, {i("wide_capture")});
    emit(body, spv::OpLoad, {i("wide_uint"), i("wide_low_capture"), i("wide_low_local")});
    emit(body, spv::OpLoad, {i("wide_uint"), i("wide_high_capture"), i("wide_high_local")});
    emit(body, spv::OpBitwiseAnd, {i("wide_uint"), i("wide_high_masked"), i("wide_high_capture"), i("wide_mask")});
    emit(body, spv::OpUConvert, {i("wide_ulong"), i("wide_low_wide"), i("wide_low_capture")});
    emit(body, spv::OpUConvert, {i("wide_ulong"), i("wide_high_wide"), i("wide_high_masked")});
    emit(body, spv::OpShiftLeftLogical, {i("wide_ulong"), i("wide_high_shifted"), i("wide_high_wide"), i("wide_shift")});
    emit(body, spv::OpBitwiseOr, {i("wide_ulong"), i("wide_pair"), i("wide_low_wide"), i("wide_high_shifted")});
    emit(body, spv::OpIAdd, {i("wide_ulong"), i("wide_anchored"), i("wide_pair"), i("wide_anchor")});
    emit(body, spv::OpIAdd, {i("wide_ulong"), i("wide_address"), i("wide_anchored"), i("wide_offset")});
    emit(body, spv::OpConvertUToPtr, {i("wide_physical_pointer"), i("wide_physical"), i("wide_address")});
    emit(body, spv::OpLoad, {i("wide_uint"), i("wide_dead_read"), i("wide_physical"), spv::MemoryAccessAlignedMask, 4});
    emit(body, spv::OpReturn, {});
    emit(body, spv::OpLabel, {terminal}); emit(body, spv::OpReturn, {});
    emit(body, spv::OpFunctionEnd, {});
    Words words = {spv::MagicNumber, 0x00010300, 0, i.bound, 0};
    for (const auto *section : {&pre, &declarations, &body}) words.insert(words.end(), section->begin(), section->end());
    return words;
}

unsigned count(const Words &words, spv::Op opcode)
{
    unsigned result = 0;
    for (size_t at = 5; at < words.size();) {
        const size_t length = words[at] >> 16;
        if (!length || length > words.size() - at) throw std::runtime_error("owned SPIR-V has invalid extent");
        result += (words[at] & 0xffffu) == uint32_t(opcode); at += length;
    }
    return result;
}
void configure(spirv_cross::CompilerMSL &compiler)
{
    auto options = compiler.get_msl_options(); options.set_msl_version(2, 3); compiler.set_msl_options(options);
    spirv_cross::MSLResourceBinding binding{};
    binding.stage = spv::ExecutionModelFragment;
    binding.desc_set = spirv_cross::ResourceBindingPushConstantDescriptorSet;
    binding.binding = spirv_cross::ResourceBindingPushConstantBinding; binding.msl_buffer = 0;
    compiler.add_msl_resource_binding(binding);
}
struct Generated { std::string source, entry; };
Generated generate(const pb::MetalPreparedFragment &prepared, uint64_t fault_address)
{
    pb::MetalFragmentQuadCompiler compiler(prepared, fault_address); configure(compiler);
    const std::string source = compiler.compile();
    return {source, compiler.get_cleansed_entry_point_name("owned_diamond", spv::ExecutionModelFragment)};
}

pb::MetalPreparedFragment positive(Results &r, const std::string &label, const Module &module,
    const std::vector<pb::MetalDescriptorSpan> &spans = {}, const pb::MetalFragmentReadCoverage &coverage = {})
{
    ++r.cpu_modules; r.artifacts.emplace_back(label + "-original", module.words);
    spirv_cross::CompilerMSL stock(module.words.data(), module.words.size());
    const auto prepared = pb::prepare_metal_fragment(module.words.data(), module.words.size(), spans, owned_anchor, 64, coverage);
    r.artifacts.emplace_back(label + "-prepared", prepared.words);
    r.check(prepared.linearized, label + ": dispatcher was not removed");
    r.check(!count(prepared.words, spv::OpSwitch) && !count(prepared.words, spv::OpLoopMerge), label + ": dispatcher scaffold remains");
    bool branch = false, physical_removed = module.physical_read == 0;
    for (size_t at = 5; at < prepared.words.size(); at += prepared.words[at] >> 16) {
        const auto opcode = spv::Op(prepared.words[at] & 0xffffu);
        if (opcode == spv::OpSelectionMerge && prepared.words[at + 1] == module.join) {
            const size_t next = at + (prepared.words[at] >> 16);
            branch = next < prepared.words.size() && (prepared.words[next] & 0xffffu) == spv::OpBranchConditional &&
                prepared.words[next + 1] == module.decision;
        }
    }
    if (module.physical_read) {
        physical_removed = true;
        for (size_t at = 5; at < prepared.words.size(); at += prepared.words[at] >> 16)
            if ((prepared.words[at] & 0xffffu) == spv::OpLoad && prepared.words[at + 2] == module.physical_read) physical_removed = false;
        r.check(prepared.dce.removed_reads.size() == 1 && prepared.dce.removed_reads[0].pointer_sgpr == pair_slot &&
            prepared.dce.removed_reads[0].offset == read_offset && prepared.dce.removed_reads[0].bytes == 4,
            label + ": removed-read requirement changed");
    }
    if (module.scc_vote) r.check(!count(prepared.words, spv::OpGroupNonUniformAny), label + ": overwritten SCC wave vote remains");
    r.check(branch, label + ": original materialized Boolean decision or merge was lost");
    r.check(physical_removed, label + ": certified dead load remains");
    const auto first = generate(prepared, 0x100004);
    const auto repeated = pb::prepare_metal_fragment(module.words.data(), module.words.size(), spans, owned_anchor, 64, coverage);
    r.check(prepared.words == repeated.words && first.source == generate(repeated, 0x100004).source,
        label + ": fresh preparation/compilation is not deterministic");
    return prepared;
}
void refusal(Results &r, const std::string &label, const Module &module, const char *diagnostic,
    const std::vector<pb::MetalDescriptorSpan> &spans = {}, const pb::MetalFragmentReadCoverage &coverage = {},
    uint64_t anchor = owned_anchor)
{
    ++r.cpu_modules; r.artifacts.emplace_back(label + "-original", module.words);
    try {spirv_cross::CompilerMSL stock(module.words.data(), module.words.size());}
    catch (const std::exception &error) {r.check(false, label + ": stock parse failed: " + error.what()); return;}
    try {
        const auto prepared = pb::prepare_metal_fragment(module.words.data(), module.words.size(), spans, anchor, 64, coverage);
        (void)generate(prepared, 0x100004);
        r.check(false, label + ": unexpectedly admitted");
    } catch (const std::exception &error) {
        r.check(std::string(error.what()).find(diagnostic) != std::string::npos,
            label + ": expected '" + diagnostic + "', got '" + error.what() + "'");
    }
}

void range_tests(Results &r)
{
    std::array<uint32_t, 16> user{};
    constexpr uint64_t guest = 0x23450000, gpu = 0x73450000, bytes = 64;
    const auto pointer = [&](uint64_t address) {user[pair_slot] = uint32_t(address); user[pair_slot + 1] = uint32_t(address >> 32);};
    const auto allowed = [&](pb::MetalFragmentRead read = {pair_slot, read_offset, 4, 0}, unsigned declared = 16,
                             uint32_t written = 0xffffu, uint64_t gbase = 0x23450000, uint64_t nbase = 0x73450000, uint64_t span = 64) {
        return pb::metal_fragment_read_in_pool(read, user.data(), declared, written, gbase, nbase, span);
    };
    pointer(guest); r.check(allowed(), "range: valid canonical pair");
    user[pair_slot + 1] |= 0xa5310000u; r.check(allowed(), "range: ignored high16 bits changed pointer");
    pointer(guest + bytes - 4 - read_offset); r.check(allowed(), "range: exact last four bytes refused");
    pointer(guest + bytes - 3 - read_offset); r.check(!allowed(), "range: unaligned partial tail accepted");
    pointer(guest + bytes - 4 - read_offset);
    r.check(!allowed({pair_slot, read_offset, 4, 0}, 16, 0xffffu, guest, gpu, bytes - 1), "range: aligned read crosses partial pool tail");
    pointer(guest + bytes - read_offset); r.check(!allowed(), "range: pool end accepted");
    pointer(guest - read_offset - 4); r.check(!allowed(), "range: below pool accepted");
    pointer(guest); r.check(!allowed({pair_slot, read_offset, 4, 0}, pair_slot + 1), "range: second word undeclared");
    user[14] = uint32_t(guest); user[15] = uint32_t(guest >> 32);
    r.check(allowed({14, read_offset, 4, 0}), "range: exact last adjacent pair refused");
    r.check(!allowed({15, read_offset, 4, 0}), "range: pair extends beyond last slot");
    r.check(!allowed({pair_slot, read_offset, 4, 0}, 17), "range: declared count exceeds storage");
    r.check(!allowed({pair_slot, read_offset, 4, 0}, 16, 0xffffu & ~(1u << pair_slot)), "range: missing low written bit");
    r.check(!allowed({pair_slot, read_offset, 4, 0}, 16, 0xffffu & ~(1u << (pair_slot + 1))), "range: missing high written bit");
    r.check(!pb::metal_fragment_read_in_pool({pair_slot, read_offset, 4, 0}, nullptr, 16, 0xffffu, guest, gpu, bytes), "range: null user accepted");
    r.check(!allowed({pair_slot, read_offset, 0, 0}) && !allowed({pair_slot, read_offset, 8, 0}), "range: wrong read width accepted");
    r.check(!allowed({pair_slot, read_offset + 1, 4, 0}), "range: unaligned offset accepted");
    r.check(!allowed({pair_slot, UINT64_MAX - 3, 4, 0}), "range: offset end overflow accepted");
    r.check(!allowed({pair_slot, UINT64_MAX - 15, 4, 0}), "range: pointer plus offset overflow accepted");
    r.check(!allowed({pair_slot, read_offset, 4, 0}, 16, 0xffffu, guest, UINT64_MAX - 7), "range: GPU address overflow accepted");
    r.check(!allowed({pair_slot, read_offset, 4, 0}, 16, 0xffffu, guest, gpu + 1), "range: native alignment ignored");
    r.check(!allowed({pair_slot, read_offset, 4, 0}, 16, 0xffffu, guest, UINT64_MAX - 15), "range: native read end overflow accepted");
    r.check(allowed({pair_slot, read_offset, 4, 0}, 16, 0xffffu, guest, 0x10000), "range: valid modulo anchor subtraction wrap refused");
    r.check(!allowed({UINT32_MAX, read_offset, 4, 0}), "range: enormous SGPR index accepted");
}

void switch64_tests(Results &r)
{
    for (Layout layout : {Layout::Dense, Layout::Reversed}) {
        const auto words = make_switch64_module(layout);
        ++r.cpu_modules;
        r.artifacts.emplace_back("lower-switch64-" + std::to_string(unsigned(layout)) + "-original", words);
        spirv_cross::CompilerMSL stock(words.data(), words.size());
        pb::MetalFragmentIR ir(words.data(), words.size()); const auto before = ir.serialize();
        bool refused = false;
        try {(void)pb::metal_fragment_dce(ir, {{pair_slot, read_offset, 4, true}}, owned_anchor);}
        catch (const std::exception &error) {
            refused = std::string(error.what()) == "fragment Function proof has unsupported control";
        }
        r.check(refused, "lower UInt64 Switch must refuse before treating literal words as CFG targets");
        r.check(ir.serialize() == before, "lower UInt64 Switch refusal must preserve the original IR transactionally");
    }
}

void cpu_tests(Results &r)
{
    range_tests(r);
    for (bool reversed : {false, true}) for (unsigned source = 0; source < 4; ++source)
        (void)positive(r, "raw-" + std::to_string(reversed) + "-" + std::to_string(source), make_module(reversed, source));
    for (bool reversed : {false, true}) for (Layout layout : {Layout::LiteralCollisions, Layout::Reversed})
        (void)positive(r, "remap-" + std::to_string(reversed) + "-" + std::to_string(unsigned(layout)), make_module(reversed, 3, Shape::Ordinary, layout));
    for (bool reversed : {false, true})
        (void)positive(r, "math-" + std::to_string(reversed), make_module(reversed, 3, Shape::Math));
    (void)positive(r, "scc-cross-block-overwrite", make_module(false, 3, Shape::SccOverwrite));
    refusal(r, "scc-read-on-one-arm", make_module(false, 3, Shape::SccReadArm), "live wave vote");
    refusal(r, "scc-read-modify-write", make_module(false, 3, Shape::SccReadModifyWrite), "live wave vote");
    refusal(r, "scc-optional-overwrite", make_module(false, 3, Shape::SccOptionalRead), "live wave vote");
    const auto loop = make_module(false, 3, Shape::SccLoop);
    ++r.cpu_modules; r.artifacts.emplace_back("scc-loop-original", loop.words);
    spirv_cross::CompilerMSL loop_parser(loop.words.data(), loop.words.size());
    pb::MetalFragmentIR loop_ir(loop.words.data(), loop.words.size());
    (void)pb::metal_fragment_dce(loop_ir, {}, owned_anchor);
    const auto loop_lowered = loop_ir.serialize(); r.artifacts.emplace_back("scc-loop-lower-only", loop_lowered);
    r.check(count(loop_lowered, spv::OpGroupNonUniformAny) == 1, "lower SCC: cycle before first overwrite must conservatively retain vote");
    const auto physical = make_module(false, 3, Shape::Pointer);
    const std::vector<pb::MetalDescriptorSpan> exact = {{pair_slot, read_offset, 4, true}};
    (void)positive(r, "span-exact", physical, exact);
    std::array<uint32_t, 16> user{}; user[pair_slot] = 0x23450000;
    const auto coverage = [&](const pb::MetalFragmentRead &read) {
        return pb::metal_fragment_read_in_pool(read, user.data(), 16, 0xffffu, 0x23450000, 0x23450000 + owned_anchor, 64);
    };
    (void)positive(r, "callback-exact", physical, {}, coverage);
    (void)positive(r, "pointer-later", make_module(true, 2, Shape::PointerLater, Layout::LiteralCollisions), {}, coverage);
    refusal(r, "span-missing", physical, "outside validated descriptor spans");
    refusal(r, "span-partial", physical, "outside validated descriptor spans", {{pair_slot, read_offset, 3, true}});
    refusal(r, "span-wrong-pair", physical, "outside validated descriptor spans", {{pair_slot + 2, read_offset, 4, true}});
    refusal(r, "span-direct-origin", physical, "outside validated descriptor spans", {{pair_slot, read_offset, 4, false}});
    user[pair_slot] = 0x23450000 + 52;
    refusal(r, "callback-outside", physical, "outside validated descriptor spans", {}, coverage);
    user[pair_slot] = 0x23450000;
    refusal(r, "wrong-anchor", physical, "canonical descriptor coverage", {}, coverage, owned_anchor + 4);
    refusal(r, "pointer-prior", make_module(false, 3, Shape::PointerPrior), "canonical descriptor coverage", {}, coverage);
    refusal(r, "pointer-conditional", make_module(false, 3, Shape::PointerConditional), "canonical descriptor coverage", {}, coverage);
    const std::pair<Shape, const char *> negatives[] = {
        {Shape::QuadBody, "optional case"}, {Shape::QuadTail, "common prefix"},
        {Shape::BodyInput, "plain Function storage"},
        {Shape::BodyPhysical, "plain Function storage"},
        {Shape::BodyStore, "plain Function storage"}, {Shape::BodyKill, "forward case"},
        {Shape::BodyNested, "closed forward diamond"}, {Shape::Backedge, "forward guest edge"},
        {Shape::UnsupportedExt, "unproved extended instruction"}, {Shape::DerivativeTail, "unknown function instruction"}
    };
    for (const auto &[shape, diagnostic] : negatives)
        refusal(r, "profile-" + std::to_string(unsigned(shape)), make_module(false, 3, shape), diagnostic);
    pb::MetalFragmentIR original(physical.words.data(), physical.words.size()); const auto before = original.serialize();
    try {(void)pb::metal_fragment_dce(original, {}, owned_anchor, 64, [](const pb::MetalFragmentRead &) {return false;});
        r.check(false, "transaction: failed coverage unexpectedly admitted");}
    catch (const std::exception &) {r.check(original.serialize() == before, "transaction: failed coverage changed IR");}
    try {(void)pb::metal_fragment_dce(original, {}, owned_anchor, 64, [](const pb::MetalFragmentRead &) -> bool {throw std::runtime_error("owned validator throw");});
        r.check(false, "transaction: validator exception unexpectedly admitted");}
    catch (const std::exception &e) {r.check(std::string(e.what()) == "owned validator throw" && original.serialize() == before,
        "transaction: validator exception was changed or IR was mutated");}
    switch64_tests(r);
}

/* Distinct low/high half fields make lane selection, half order and result width
   observable. All values are independently chosen IEEE binary16 encodings. */
constexpr std::array<Pixel, 4> half_patterns = {{
    {0x80000000u, 0x00008000u, 0x40003c00u, 0xbc004400u},
    {0x04000001u, 0x80010002u, 0x03ff8400u, 0x83ff0401u},
    {0xfc007c00u, 0x3c007e00u, 0xfe004000u, 0x7c00bc00u},
    {0xfbff7bffu, 0x3800b400u, 0x4800c000u, 0x3555b555u}
}};
uint32_t half_float_bits(uint16_t half)
{
    const uint32_t sign = uint32_t(half & 0x8000u) << 16;
    const unsigned exponent = (half >> 10) & 31u;
    uint32_t fraction = half & 0x3ffu;
    if (exponent == 31) return sign | 0x7f800000u | (fraction << 13);
    if (exponent) return sign | ((exponent + 112u) << 23) | (fraction << 13);
    if (!fraction) return sign;
    unsigned shifts = 0;
    while (!(fraction & 0x400u)) {fraction <<= 1; ++shifts;}
    return sign | ((113u - shifts) << 23) | ((fraction & 0x3ffu) << 13);
}
bool half_component_matches(uint16_t half, uint32_t actual, int *subnormal_policy = nullptr)
{
    // GLSL.std.450 permits half subnormal preservation or consistent flushing.
    // Track one policy across both components and every draw; flushed zero keeps
    // the input sign. Normal values, infinity and signed zero are exact. NaN
    // sign, quieting and payload are not part of this classification oracle.
    if ((half & 0x7c00u) == 0x7c00u && (half & 0x03ffu))
        return (actual & 0x7f800000u) == 0x7f800000u && (actual & 0x007fffffu);
    if (!(half & 0x7c00u) && (half & 0x03ffu)) {
        const int policy = actual == half_float_bits(half) ? 1 :
            actual == (uint32_t(half & 0x8000u) << 16) ? 0 : -1;
        if (policy < 0 || (subnormal_policy && *subnormal_policy >= 0 && *subnormal_policy != policy)) return false;
        if (subnormal_policy) *subnormal_policy = policy;
        return true;
    }
    return actual == half_float_bits(half);
}

void tail_cpu_tests(Results &r)
{
    const unsigned original_modules = r.cpu_modules;
    for (bool reversed : {false, true}) for (Layout layout : {Layout::Dense, Layout::LiteralCollisions, Layout::Reversed}) {
        const std::string label = "tail-half-" + std::to_string(reversed) + "-" + std::to_string(unsigned(layout));
        const auto module = make_module(reversed, 3, Shape::Ordinary, layout, Tail::HalfPair);
        const auto prepared = positive(r, label, module);
        pb::MetalFragmentIR ir(prepared.words.data(), prepared.words.size());
        unsigned unpacks = 0;
        for (const auto &op : ir.ops) if (op.opcode == spv::OpExtInst) {
            ++unpacks;
            r.check(pb::fragment_control_detail::unpack_half_pair(ir, op), label + ": exact tail signature was not recognized");
        }
        r.check(unpacks == 1, label + ": live typed tail unpack was lost");
        r.check(count(prepared.words,spv::OpGroupNonUniformQuadBroadcast) == 1 &&
            count(prepared.words,spv::OpGroupNonUniformQuadSwap) == 3,
            label + ": quad results became dead before the tail observer");
    }
    refusal(r, "tail-signed-input", make_module(false, 3, Shape::Ordinary, Layout::Dense, Tail::SignedHalf),
        "diamond tail has an unproved participation-dependent operation");
    refusal(r, "tail-other-opcode", make_module(false, 3, Shape::Ordinary, Layout::Dense, Tail::UnormPair),
        "diamond tail has an unproved participation-dependent operation");
    refusal(r, "tail-pointer-output", make_module(false, 3, Shape::Ordinary, Layout::Dense, Tail::PointerOutput),
        "alias-free nonvolatile function variables");
    refusal(r, "tail-double-math", make_module(false, 3, Shape::Ordinary, Layout::Dense, Tail::DoubleMath),
        "diamond tail has an unproved participation-dependent operation");

    // These are intentionally malformed signatures, not SPIR-V-valid modules.
    // Test the admission helper directly; do not export them beside valid modules
    // or count an incidental downstream parser rejection as a guard witness.
    const auto module = make_module(false, 3, Shape::Ordinary, Layout::Dense, Tail::HalfPair);
    pb::MetalFragmentIR original(module.words.data(), module.words.size());
    for (unsigned mutation = 1; mutation < 6; ++mutation) {
        auto ir = original;
        size_t ext = 0;
        for (size_t at = 0; at < ir.ops.size(); ++at) if (ir.ops[at].opcode == spv::OpExtInst) ext = at;
        const auto tail = ir.ops.at(ext);
        auto *pair = ir.find(tail.result_type());
        auto *input = ir.find(tail.operands[4]);
        if (!pair || !input) throw std::runtime_error("owned tail type setup failed");
        const uint32_t component_type = pair->operands[1], input_type = input->result_type();
        if (mutation == 1) ir.find(component_type)->operands[1] = 64; // Float64 result component
        if (mutation == 2) pair->operands[2] = 3; // wrong result vector width
        if (mutation == 3) ir.find(tail.operands[2])->operands[1] ^= 1u; // noncanonical import
        if (mutation == 4) ir.ops[ext].operands.push_back(input_type); // extra operand
        if (mutation == 5) ir.find(input_type)->operands[1] = 64; // wrong input integer width
        r.check(!pb::fragment_control_detail::unpack_half_pair(ir, ir.ops[ext]),
            "tail malformed unit-only signature admitted: " + std::to_string(mutation));
    }
    constexpr std::pair<uint16_t, uint32_t> known[] = {
        {0x0000,0x00000000u},{0x8000,0x80000000u},{0x3c00,0x3f800000u},{0x4000,0x40000000u},
        {0xc000,0xc0000000u},{0x0400,0x38800000u},{0x03ff,0x387fc000u},{0x0001,0x33800000u},
        {0x8001,0xb3800000u},{0x7bff,0x477fe000u},{0x7c00,0x7f800000u},{0xfc00,0xff800000u}
    };
    for (const auto &[half,bits] : known) r.check(half_float_bits(half) == bits, "owned half-to-float reference anchor");
    r.check(half_component_matches(0x7e01,0xffc00100u) && !half_component_matches(0x7e01,0x7f800000u) &&
        !half_component_matches(0x7e01,0), "owned half NaN classification oracle");
    int preserve = -1, flush = -1;
    r.check(half_component_matches(0x0001,0x33800000u,&preserve) &&
        half_component_matches(0x8001,0xb3800000u,&preserve) &&
        !half_component_matches(0x0002,0,&preserve), "owned consistent subnormal preservation oracle");
    r.check(half_component_matches(0x0001,0,&flush) && half_component_matches(0x8001,0x80000000u,&flush) &&
        !half_component_matches(0x0002,0x34000000u,&flush) && !half_component_matches(0x8001,0,&flush),
        "owned consistent signed subnormal flushing oracle");
    r.check(r.cpu_modules - original_modules == 10, "tail CPU module matrix changed (expected10 valid modules)");
    std::printf("tail_unpack_cpu_modules=%u malformed_unit_only=5\n", r.cpu_modules - original_modules);
}

std::runtime_error metal_error(const char *operation, NSError *error)
{
    return std::runtime_error(std::string(operation) + ": " + (error ? error.localizedDescription.UTF8String : "no Metal object"));
}
id<MTLLibrary> library(id<MTLDevice> device, const std::string &source)
{
    MTLCompileOptions *options = [MTLCompileOptions new]; options.languageVersion = MTLLanguageVersion2_3;
    if (@available(macOS 15.0, *)) options.mathMode = MTLMathModeSafe;
    else throw std::runtime_error("safe MSL math requires macOS 15 or later");
    NSError *error = nil;
    id<MTLLibrary> lib = [device newLibraryWithSource:[NSString stringWithUTF8String:source.c_str()] options:options error:&error];
    if (!lib) throw metal_error("MSL compilation", error);
    return lib;
}
struct Gpu {
    id<MTLDevice> device;
    id<MTLCommandQueue> queue;
    id<MTLBuffer> fault;
    id<MTLTexture> target;
    id<MTLFunction> vertex;
    Gpu()
    {
        device = MTLCreateSystemDefaultDevice();
        if (!device || !device.hasUnifiedMemory) throw std::runtime_error("a unified-memory Metal device is required");
        queue = [device newCommandQueue];
        fault = [device newBufferWithLength:sizeof(fault_initial) options:MTLResourceStorageModeShared];
        MTLTextureDescriptor *desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA32Uint width:side height:side mipmapped:NO];
        desc.storageMode = MTLStorageModeShared; desc.usage = MTLTextureUsageRenderTarget;
        target = [device newTextureWithDescriptor:desc];
        if (!queue || !fault || !fault.gpuAddress || !target) throw std::runtime_error("Metal fixture allocation failed");
        vertex = [library(device, R"(
#include <metal_stdlib>
using namespace metal;
vertex float4 diamond_vertex(uint index [[vertex_id]], constant uint &tiny [[buffer(0)]]) {
    const float2 full[3] = {float2(-1,-1),float2(3,-1),float2(-1,3)};
    const float2 edge[3] = {float2(-1,1),float2(-0.5625,1),float2(-1,0.5625)};
    return float4(tiny ? edge[index] : full[index],0,1);
})") newFunctionWithName:@"diamond_vertex"];
        if (!vertex) throw std::runtime_error("owned vertex entry missing");
        std::printf("device=%s MSL=2.3 safe-math target=RGBA32Uint\n", device.name.UTF8String);
    }
    id<MTLRenderPipelineState> pipeline(const Module &module)
    {
        const auto prepared = pb::prepare_metal_fragment(module.words.data(), module.words.size(), {}, owned_anchor);
        const auto generated = generate(prepared, fault.gpuAddress + sizeof(uint32_t));
        id<MTLLibrary> lib = library(device, generated.source);
        MTLRenderPipelineDescriptor *desc = [MTLRenderPipelineDescriptor new]; desc.vertexFunction = vertex;
        desc.fragmentFunction = [lib newFunctionWithName:[NSString stringWithUTF8String:generated.entry.c_str()]];
        desc.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA32Uint;
        NSError *error = nil; id<MTLRenderPipelineState> p = [device newRenderPipelineStateWithDescriptor:desc error:&error];
        if (!p) throw metal_error("diamond pipeline", error);
        return p;
    }
    std::array<Pixel, side * side> draw(id<MTLRenderPipelineState> pipeline, const std::array<uint32_t, 16> &input, bool tiny)
    {
        std::memcpy(fault.contents, fault_initial.data(), sizeof(fault_initial));
        MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = target; pass.colorAttachments[0].loadAction = MTLLoadActionClear;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        pass.colorAttachments[0].clearColor = MTLClearColorMake(double(UINT32_MAX), double(UINT32_MAX), 0, double(UINT32_MAX));
        id<MTLCommandBuffer> command = [queue commandBuffer];
        id<MTLRenderCommandEncoder> encoder = [command renderCommandEncoderWithDescriptor:pass];
        if (!command || !encoder) throw std::runtime_error("Metal draw encoder missing");
        [encoder setRenderPipelineState:pipeline]; [encoder setViewport:MTLViewport{0,0,side,side,0,1}];
        const uint32_t edge = tiny; [encoder setVertexBytes:&edge length:sizeof(edge) atIndex:0];
        std::array<uint32_t, 32> push{}; std::copy(input.begin(), input.end(), push.begin() + 16);
        [encoder setFragmentBytes:push.data() length:sizeof(push) atIndex:0];
        [encoder useResource:fault usage:MTLResourceUsageRead | MTLResourceUsageWrite stages:MTLRenderStageFragment];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3]; [encoder endEncoding];
        [command commit]; [command waitUntilCompleted];
        if (command.status != MTLCommandBufferStatusCompleted || command.error) throw metal_error("GPU command", command.error);
        std::array<Pixel, side * side> pixels;
        [target getBytes:pixels.data() bytesPerRow:side * sizeof(Pixel) fromRegion:MTLRegionMake2D(0,0,side,side) mipmapLevel:0];
        return pixels;
    }
};

uint32_t packed_truth(uint32_t mask, unsigned lane)
{
    uint32_t value = 0;
    for (unsigned direction = 0; direction < 4; ++direction) value |= ((mask >> (lane ^ direction)) & 1u) << direction;
    return value;
}
void inspect(Results &r, Gpu &gpu, const Module &module, const std::array<uint32_t, 16> &input, bool tiny,
             const std::array<Pixel, side * side> &pixels)
{
    ++r.native_draws;
    const std::string label = "draw=" + std::to_string(r.native_draws) + " reverse=" + std::to_string(module.reversed) +
        " source=" + std::to_string(module.source) + " seed=" + std::to_string(input[4]) +
        " tiny=" + std::to_string(tiny) + " math=" + std::to_string(module.math);
    const auto *fault = static_cast<const uint32_t *>(gpu.fault.contents);
    for (unsigned word = 0; word < fault_initial.size(); ++word)
        r.check(fault[word] == fault_initial[word], label + ": fault/canary word " + std::to_string(word));
    for (unsigned y = 0; y < side; ++y) for (unsigned x = 0; x < side; ++x) {
        const bool covered = !tiny || (!x && !y);
        const unsigned lane = (x & 1) | ((y & 1) << 1), quad = (x >> 1) | ((y >> 1) << 2);
        const uint32_t mask = input[5] ? input[4] ^ quad : input[4];
        const bool predicate = (mask >> lane) & 1u, body = predicate != module.reversed;
        Pixel expected = clear_pixel;
        if (covered) {
            ++r.raster_covered; ++r.expected_written;
            uint32_t value = input[module.source];
            if (module.math) {float f; std::memcpy(&f, &input[6], sizeof f); f *= .5f; std::memcpy(&value, &f, sizeof value);}
            expected = {body ? value : baseline_red, uint32_t(body), body ? packed_truth(mask,lane) : baseline_blue,
                        predicate ? false_tag : true_tag};
        }
        const auto &actual = pixels[y * side + x];
        r.observed_written += actual != clear_pixel;
        bool same = actual == expected;
        if (covered && module.math && body) {
            // Positive powers of two keep the mathematical oracle simple. Allow two
            // Float32 ULPs for Log2/Exp2 rather than claim exact library accuracy.
            const uint32_t delta = actual[0] > expected[0] ? actual[0] - expected[0] : expected[0] - actual[0];
            same = delta <= 2 && actual[1] == expected[1] && actual[2] == expected[2] && actual[3] == expected[3];
        }
        r.check(same, label + ": pixel(" + std::to_string(x) + "," + std::to_string(y) + ") expected=" +
            std::to_string(expected[0]) + "," + std::to_string(expected[1]) + "," + std::to_string(expected[2]) + "," +
            std::to_string(expected[3]) + " got=" + std::to_string(actual[0]) + "," + std::to_string(actual[1]) + "," +
            std::to_string(actual[2]) + "," + std::to_string(actual[3]));
    }
}
void native_tests(Results &r)
{
    Gpu gpu;
    for (bool reversed : {false, true}) for (unsigned source = 0; source < 4; ++source) {
        const auto module = make_module(reversed, source); const auto pipeline = gpu.pipeline(module);
        for (unsigned pattern = 0; pattern < patterns.size(); ++pattern) {
            std::array<uint32_t, 16> input{}; std::copy(patterns[pattern].begin(), patterns[pattern].end(), input.begin());
            input[4] = pattern ? 15 : 0; input[5] = 1;
            inspect(r,gpu,module,input,false,gpu.draw(pipeline,input,false));
            if (source == 3) for (unsigned mask = 0; mask < 16; ++mask) {
                input[4] = mask; input[5] = 0;
                inspect(r,gpu,module,input,true,gpu.draw(pipeline,input,true));
            }
        }
    }
    for (bool reversed : {false, true}) {
        const auto module = make_module(reversed, 3, Shape::Math); const auto pipeline = gpu.pipeline(module);
        for (uint32_t power : {0x40000000u, 0x41000000u}) {
            std::array<uint32_t, 16> input{}; std::copy(patterns[0].begin(),patterns[0].end(),input.begin());
            input[4] = power == 0x40000000u ? 0 : 15; input[5] = 1; input[6] = power;
            inspect(r,gpu,module,input,false,gpu.draw(pipeline,input,false));
        }
    }
    r.check(r.native_draws == 84 && r.raster_covered == 1344 && r.expected_written == 1344,
        "native matrix count changed (expected84 draws,1344 covered/written pixels)");
    r.check(r.observed_written == r.expected_written, "native observed-written count differs from expected");
}

void inspect_tail(Results &r, Gpu &gpu, const Module &module, const std::array<uint32_t, 16> &input,
                  unsigned pattern, bool tiny, const std::array<Pixel, side * side> &pixels)
{
    ++r.tail_draws;
    const std::string label = "tail-unpack draw=" + std::to_string(r.tail_draws) + " reverse=" +
        std::to_string(module.reversed) + " pattern=" + std::to_string(pattern) + " seed=" +
        std::to_string(input[4]) + " varying=" + std::to_string(input[5]) + " tiny=" + std::to_string(tiny);
    const auto *fault = static_cast<const uint32_t *>(gpu.fault.contents);
    for (unsigned word = 0; word < fault_initial.size(); ++word)
        r.check(fault[word] == fault_initial[word], label + ": fault/canary word " + std::to_string(word));
    for (unsigned y = 0; y < side; ++y) for (unsigned x = 0; x < side; ++x) {
        const bool covered = !tiny || (!x && !y);
        const unsigned lane = (x & 1) | ((y & 1) << 1), quad = (x >> 1) | ((y >> 1) << 2);
        const uint32_t mask = input[5] ? input[4] ^ quad : input[4];
        const bool predicate = (mask >> lane) & 1u, body = predicate != module.reversed;
        const auto &actual = pixels[y * side + x];
        bool same = actual == clear_pixel;
        if (covered) {
            ++r.tail_covered;
            const uint32_t packed = input[lane];
            same = half_component_matches(uint16_t(packed),actual[0],&r.tail_subnormal_policy) &&
                half_component_matches(uint16_t(packed >> 16),actual[1],&r.tail_subnormal_policy) &&
                actual[2] == (((body ? packed_truth(mask,lane) : baseline_blue) << 1) | uint32_t(body)) &&
                actual[3] == ((predicate ? false_tag : true_tag) ^ (body ? input[module.source] : baseline_red));
        }
        r.tail_written += actual != clear_pixel;
        r.check(same, label + ": pixel(" + std::to_string(x) + "," + std::to_string(y) + ") input=" +
            std::to_string(input[lane]) + " got=" + std::to_string(actual[0]) + "," + std::to_string(actual[1]) + "," +
            std::to_string(actual[2]) + "," + std::to_string(actual[3]));
    }
}
void tail_native_tests(Results &r)
{
    Gpu gpu;
    for (bool reversed : {false, true}) {
        const auto module = make_module(reversed, 3, Shape::Ordinary, Layout::Dense, Tail::HalfPair);
        const auto pipeline = gpu.pipeline(module);
        for (unsigned pattern = 0; pattern < half_patterns.size(); ++pattern) {
            std::array<uint32_t,16> input{};
            std::copy(half_patterns[pattern].begin(),half_patterns[pattern].end(),input.begin());
            for (unsigned mask = 0; mask < 16; ++mask) {
                input[4] = mask;
                inspect_tail(r,gpu,module,input,pattern,false,gpu.draw(pipeline,input,false));
                if (pattern == 0) inspect_tail(r,gpu,module,input,pattern,true,gpu.draw(pipeline,input,true));
            }
            input[4] = pattern; input[5] = 1;
            inspect_tail(r,gpu,module,input,pattern,false,gpu.draw(pipeline,input,false));
        }
    }
    r.check(r.tail_draws == 168 && r.tail_covered == 8736,
        "tail-unpack matrix count changed (expected168 draws,8736 covered pixels)");
    r.check(r.tail_written == r.tail_covered, "tail-unpack observed-written count differs from expected");
    r.check(r.tail_subnormal_policy >= 0, "tail-unpack subnormal policy was never observed");
    std::printf("tail_unpack_draws=%u tail_covered=%u tail_written=%u subnormals=%s\n",
        r.tail_draws,r.tail_covered,r.tail_written,r.tail_subnormal_policy == 1 ? "preserved" : r.tail_subnormal_policy == 0 ? "flushed" : "unobserved");
}

void export_artifacts(const Results &r, const std::string &directory)
{
    for (const auto &[name, words] : r.artifacts) {
        const std::string path = directory + "/" + name + ".spv";
        const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
        if (fd < 0) throw std::runtime_error("owned SPIR-V export open failed: " + path);
        const auto *data = reinterpret_cast<const unsigned char *>(words.data());
        size_t written = 0; const size_t bytes = words.size() * sizeof(uint32_t); int failure = 0;
        while (written < bytes) {
            const ssize_t n = write(fd, data + written, bytes - written);
            if (n > 0) written += size_t(n);
            else if (n < 0 && errno == EINTR) continue;
            else {failure = n < 0 ? errno : EIO; break;}
        }
        if (close(fd) && !failure) failure = errno;
        if (failure) throw std::runtime_error("owned SPIR-V export write/close failed: " + path);
    }
    std::printf("owned_spirv_exports=%zu directory=%s (external validation required)\n", r.artifacts.size(), directory.c_str());
}
} // namespace

int main(int argc, char **argv)
{
    bool cpu_only = false; std::string export_directory;
    for (int arg = 1; arg < argc; ++arg) {
        if (std::strcmp(argv[arg], "--cpu-only") == 0) cpu_only = true;
        else if (std::strcmp(argv[arg], "--export-dir") == 0 && arg + 1 < argc) export_directory = argv[++arg];
        else {std::fprintf(stderr, "usage: %s [--cpu-only] [--export-dir EXISTING_EMPTY_DIRECTORY]\n", argv[0]); return 2;}
    }
    @autoreleasepool {
        Results results;
        try {cpu_tests(results); tail_cpu_tests(results);}
        catch (const std::exception &error) {results.check(false, std::string("CPU fixture exception: ") + error.what());}
        // Export any already assembled original even if its preparation failed. External
        // validation can then distinguish fixture setup from an admission regression.
        try {if (!export_directory.empty()) export_artifacts(results, export_directory);}
        catch (const std::exception &error) {results.check(false, std::string("export exception: ") + error.what());}
        try {if (!cpu_only && !results.failures) {native_tests(results); tail_native_tests(results);}}
        catch (const std::exception &error) {results.check(false, std::string("native fixture exception: ") + error.what());}
        std::printf("cpu_modules=%u native_draws=%u raster_covered=%u expected_written=%u observed_written=%u checks=%u failures=%u\n",
            results.cpu_modules,results.native_draws,results.raster_covered,results.expected_written,results.observed_written,
            results.checks,results.failures);
        return results.failures ? 1 : 0;
    }
}
