/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Independently owned SPIR-V and MSL probes. No shader or instruction stream is taken
   from a game. These test numeric quad reads, including helpers at a raster edge;
   they do not establish whole-quad execution or wave-wide control semantics. */
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include "metal_fragment_quads.hpp"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Words = std::vector<uint32_t>;
constexpr unsigned side = 8, channels = 4;
constexpr uint32_t valid_marker = 1, bypass_marker = 2;
constexpr uint32_t selected_away = 0x13579bdfu, bypass_bits = 0xb16b00b5u;
constexpr std::array<uint32_t, 4> fault_initial = {0x51e701d5u, 0, 0x682ac173u, 0xd04f26a9u};
constexpr std::array<std::array<uint32_t, 4>, 2> patterns = {{
    {0, UINT32_MAX, 0x7fc01234u, 0x80000000u},
    {0x3fc00000u, 0xffc05678u, 0, UINT32_MAX}
}};

struct Results {
    unsigned checks = 0, failures = 0, cpu_cases = 0;
    unsigned adapter_draws = 0, packet_draws = 0, pixels = 0, covered_pixels = 0;
    unsigned expected_written_pixels = 0, written_pixels = 0;
    void check(bool condition, const std::string &message)
    {
        ++checks;
        if (condition) return;
        ++failures;
        if (failures <= 30) std::fprintf(stderr, "%s\n", message.c_str());
    }
};

void emit(Words &out, spv::Op opcode, std::initializer_list<uint32_t> operands)
{
    out.push_back((uint32_t(operands.size() + 1) << 16) | uint32_t(opcode));
    out.insert(out.end(), operands.begin(), operands.end());
}

void named(Words &out, spv::Op opcode, std::initializer_list<uint32_t> prefix,
           const char *name, std::initializer_list<uint32_t> suffix = {})
{
    const size_t start = out.size(), bytes = std::strlen(name) + 1;
    out.push_back(0);
    out.insert(out.end(), prefix.begin(), prefix.end());
    const size_t text = out.size();
    out.resize(text + (bytes + 3) / 4, 0);
    std::memcpy(out.data() + text, name, bytes);
    out.insert(out.end(), suffix.begin(), suffix.end());
    out[start] = (uint32_t(out.size() - start) << 16) | uint32_t(opcode);
}

enum class Kind { Unsigned, Signed, Float };
enum class Shape {
    Straight, Dispatch, ConditionalRead, PriorKill, PriorReturn, LateKill, LateReturn, MultiCase,
    MutablePc, MissingPc, AliasPc, UnknownSelector, GuestLoop, CaseLoop, CaseUnreachable, PrefixLoop,
    HelperFunction, NonEntryRead, DynamicAny, ConstantAny, AnyWrongScope, AnyFloatScope,
    AnyVectorResult, AnyPointerResult, QuadSwap
};
enum class Metadata {
    Ordinary, ExtraOperand, MissingOperand, BoolResult, VectorResult, NarrowResult, WideResult,
    PointerResult, ArrayResult, MismatchedInput, PointerInput, DynamicSource,
    SpecializedSource, FloatSource, WideSource, VectorSource, BoolSource,
    SourceFour, WrongScope, DynamicScope, FloatScope, SignedScope, SignedSource
};

/* This assembler uses the public SPIR-V instruction grammar. The malformed metadata
   cases below are adapter-refusal probes, never submitted to Metal. A stock parser
   is also exercised so its own rejection cannot be mistaken for an adapter check. */
struct Module {
    Words words;
    bool selects_after_read = false;
};

Module make_module(Kind kind = Kind::Unsigned, unsigned source = 0, bool collisions = false,
                   Shape shape = Shape::Straight, Metadata metadata = Metadata::Ordinary)
{
    uint32_t bound = 1;
    auto id = [&]() { return bound++; };
    Words capabilities, entries, debug, annotations, definitions, body;
    const uint32_t void_type = id(), function_type = id(), bool_type = id();
    const uint32_t uint_type = id(), int_type = id(), float_type = id();
    const uint32_t float4_type = id(), uint4_type = id(), uint2_type = id();
    const uint32_t input_pointer = id(), output_pointer = id(), local_pointer = id();
    const uint32_t push_struct = id(), push_pointer = id(), push_vector_pointer = id();
    std::array<uint32_t, 5> constants;
    for (uint32_t &constant : constants) constant = id();
    const uint32_t true_constant = id(), fixed_constant = id(), signed_scope = id(), signed_source = id();
    const uint32_t float_source = id(), spec_source = id(), vector_constant = id();
    const uint32_t frag_coord = id(), output = id(), push = id(), main_function = id();
    const uint32_t entry_block = id(), local = id(), pc = id();
    const uint32_t position = id(), x = id(), y = id(), ux = id(), uy = id();
    const uint32_t parity_x = id(), parity_y = id(), high_bit = id(), index = id();
    const uint32_t condition = id(), pattern_pointer = id(), pattern_vector = id(), pattern_bits = id();
    const uint32_t loaded_bits = id(), typed_bits = id(), read_result = id(), result_bits = id();
    const uint32_t selected_bits = id(), final_output = id();
    const uint32_t loop_header = id(), dispatch = id(), shade = id(), merge = id(), loop_continue = id(), exit = id();
    const uint32_t selector = id(), alias = id(), yes = id(), no = id(), after_if = id();
    const uint32_t helper_function = id(), helper_label = id(), helper_result = id(), subgroup_result = id();
    const uint32_t narrow_type = id(), wide_type = id(), wide_constant = id();
    const uint32_t array_type = id(), array_constant = id(), bool2_type = id();
    const bool any = shape == Shape::DynamicAny || shape == Shape::ConstantAny ||
        shape == Shape::AnyWrongScope || shape == Shape::AnyFloatScope ||
        shape == Shape::AnyVectorResult || shape == Shape::AnyPointerResult;

    emit(capabilities, spv::OpCapability, {spv::CapabilityShader});
    emit(capabilities, spv::OpCapability, {spv::CapabilityGroupNonUniform});
    emit(capabilities, spv::OpCapability, {spv::CapabilityGroupNonUniformQuad});
    if (metadata == Metadata::NarrowResult) emit(capabilities, spv::OpCapability, {spv::CapabilityInt16});
    if (metadata == Metadata::WideResult || metadata == Metadata::WideSource)
        emit(capabilities, spv::OpCapability, {spv::CapabilityInt64});
    if (any)
        emit(capabilities, spv::OpCapability, {spv::CapabilityGroupNonUniformVote});
    emit(capabilities, spv::OpMemoryModel, {spv::AddressingModelLogical, spv::MemoryModelGLSL450});
    named(entries, spv::OpEntryPoint, {spv::ExecutionModelFragment, main_function}, "quad_values", {frag_coord, output});
    emit(entries, spv::OpExecutionMode, {main_function, spv::ExecutionModeOriginUpperLeft});
    named(debug, spv::OpName, {push_struct}, "PatternBlock");
    named(debug, spv::OpName, {push}, "patterns");
    if (collisions) {
        named(debug, spv::OpName, {constants[4]}, "pb_fragment_quad");
        named(debug, spv::OpName, {constants[2]}, "pb_fragment_quad_");
        named(debug, spv::OpName, {local}, "pb_quad_lane");
    }
    emit(annotations, spv::OpDecorate, {frag_coord, spv::DecorationBuiltIn, spv::BuiltInFragCoord});
    emit(annotations, spv::OpDecorate, {output, spv::DecorationLocation, 0});
    emit(annotations, spv::OpDecorate, {push_struct, spv::DecorationBlock});
    emit(annotations, spv::OpMemberDecorate, {push_struct, 0, spv::DecorationOffset, 0});
    emit(definitions, spv::OpTypeVoid, {void_type});
    emit(definitions, spv::OpTypeFunction, {function_type, void_type});
    emit(definitions, spv::OpTypeBool, {bool_type});
    emit(definitions, spv::OpTypeInt, {uint_type, 32, 0});
    emit(definitions, spv::OpTypeInt, {int_type, 32, 1});
    emit(definitions, spv::OpTypeFloat, {float_type, 32});
    emit(definitions, spv::OpTypeVector, {float4_type, float_type, 4});
    emit(definitions, spv::OpTypeVector, {uint4_type, uint_type, 4});
    emit(definitions, spv::OpTypeVector, {uint2_type, uint_type, 2});
    if (shape == Shape::AnyVectorResult) emit(definitions, spv::OpTypeVector, {bool2_type, bool_type, 2});
    emit(definitions, spv::OpTypePointer, {input_pointer, spv::StorageClassInput, float4_type});
    emit(definitions, spv::OpTypePointer, {output_pointer, spv::StorageClassOutput, uint4_type});
    emit(definitions, spv::OpTypePointer, {local_pointer, spv::StorageClassFunction, uint_type});
    emit(definitions, spv::OpTypeStruct, {push_struct, uint4_type});
    emit(definitions, spv::OpTypePointer, {push_pointer, spv::StorageClassPushConstant, push_struct});
    emit(definitions, spv::OpTypePointer, {push_vector_pointer, spv::StorageClassPushConstant, uint4_type});
    for (uint32_t i = 0; i < constants.size(); ++i)
        emit(definitions, spv::OpConstant, {uint_type, constants[i], i});
    emit(definitions, spv::OpConstantTrue, {bool_type, true_constant});
    emit(definitions, spv::OpConstant, {uint_type, fixed_constant, selected_away});
    emit(definitions, spv::OpConstant, {int_type, signed_scope, spv::ScopeSubgroup});
    emit(definitions, spv::OpConstant, {int_type, signed_source, source});
    emit(definitions, spv::OpConstant, {float_type, float_source, 0x40400000u});
    emit(definitions, spv::OpSpecConstant, {uint_type, spec_source, source});
    emit(definitions, spv::OpConstantComposite, {uint2_type, vector_constant, constants[0], constants[1]});
    if (metadata == Metadata::NarrowResult) emit(definitions, spv::OpTypeInt, {narrow_type, 16, 0});
    if (metadata == Metadata::WideResult || metadata == Metadata::WideSource) {
        emit(definitions, spv::OpTypeInt, {wide_type, 64, 0});
        emit(definitions, spv::OpConstant, {wide_type, wide_constant, source, 0});
    }
    if (metadata == Metadata::ArrayResult) {
        emit(definitions, spv::OpTypeArray, {array_type, uint_type, constants[4]});
        emit(definitions, spv::OpConstantComposite, {array_type, array_constant,
            constants[0], constants[1], constants[2], constants[3]});
    }
    emit(definitions, spv::OpVariable, {input_pointer, frag_coord, spv::StorageClassInput});
    emit(definitions, spv::OpVariable, {output_pointer, output, spv::StorageClassOutput});
    emit(definitions, spv::OpVariable, {push_pointer, push, spv::StorageClassPushConstant});

    emit(body, spv::OpFunction, {void_type, main_function, spv::FunctionControlMaskNone, function_type});
    emit(body, spv::OpLabel, {entry_block});
    emit(body, spv::OpVariable, {local_pointer, local, spv::StorageClassFunction});
    emit(body, spv::OpVariable, {local_pointer, pc, spv::StorageClassFunction});
    emit(body, spv::OpLoad, {float4_type, position, frag_coord});
    emit(body, spv::OpCompositeExtract, {float_type, x, position, 0});
    emit(body, spv::OpCompositeExtract, {float_type, y, position, 1});
    emit(body, spv::OpConvertFToU, {uint_type, ux, x});
    emit(body, spv::OpConvertFToU, {uint_type, uy, y});
    emit(body, spv::OpBitwiseAnd, {uint_type, parity_x, ux, constants[1]});
    emit(body, spv::OpBitwiseAnd, {uint_type, parity_y, uy, constants[1]});
    emit(body, spv::OpShiftLeftLogical, {uint_type, high_bit, parity_y, constants[1]});
    emit(body, spv::OpBitwiseOr, {uint_type, index, parity_x, high_bit});
    emit(body, spv::OpIEqual, {bool_type, condition, parity_y, constants[0]});
    emit(body, spv::OpAccessChain, {push_vector_pointer, pattern_pointer, push, constants[0]});
    emit(body, spv::OpLoad, {uint4_type, pattern_vector, pattern_pointer});
    emit(body, spv::OpVectorExtractDynamic, {uint_type, pattern_bits, pattern_vector, index});
    uint32_t input = pattern_bits;
    if (collisions) {
        emit(body, spv::OpStore, {local, pattern_bits});
        emit(body, spv::OpLoad, {uint_type, loaded_bits, local});
        input = loaded_bits;
    }
    uint32_t result_type = kind == Kind::Unsigned ? uint_type : kind == Kind::Signed ? int_type : float_type;
    if (kind != Kind::Unsigned) {
        emit(body, spv::OpBitcast, {result_type, typed_bits, input});
        input = typed_bits;
    }
    uint32_t scope_id = constants[spv::ScopeSubgroup], source_id = constants.at(source);
    switch (metadata) {
    case Metadata::BoolResult: result_type = bool_type; input = true_constant; break;
    case Metadata::VectorResult: result_type = uint2_type; input = vector_constant; break;
    case Metadata::NarrowResult: result_type = narrow_type; break;
    case Metadata::WideResult: result_type = wide_type; input = wide_constant; break;
    case Metadata::PointerResult: result_type = local_pointer; input = local; break;
    case Metadata::ArrayResult: result_type = array_type; input = array_constant; break;
    case Metadata::MismatchedInput:
        emit(body, spv::OpBitcast, {int_type, typed_bits, input}); input = typed_bits; break;
    case Metadata::PointerInput: input = local; break;
    case Metadata::DynamicSource: source_id = index; break;
    case Metadata::SpecializedSource: source_id = spec_source; break;
    case Metadata::FloatSource: source_id = float_source; break;
    case Metadata::WideSource: source_id = wide_constant; break;
    case Metadata::VectorSource: source_id = vector_constant; break;
    case Metadata::BoolSource: source_id = true_constant; break;
    case Metadata::SourceFour: source_id = constants[4]; break;
    case Metadata::WrongScope: scope_id = constants[spv::ScopeWorkgroup]; break;
    case Metadata::DynamicScope: scope_id = index; break;
    case Metadata::FloatScope: scope_id = float_source; break;
    case Metadata::SignedScope: scope_id = signed_scope; break;
    case Metadata::SignedSource: source_id = signed_source; break;
    default: break;
    }

    const bool dispatcher = shape == Shape::Dispatch || shape == Shape::MultiCase ||
        shape == Shape::MutablePc || shape == Shape::MissingPc || shape == Shape::AliasPc ||
        shape == Shape::UnknownSelector || shape == Shape::CaseLoop || shape == Shape::CaseUnreachable ||
        shape == Shape::PrefixLoop;
    if (dispatcher) {
        if (shape != Shape::MissingPc) emit(body, spv::OpStore, {pc, constants[0]});
        if (shape == Shape::AliasPc) {
            emit(body, spv::OpCopyObject, {local_pointer, alias, pc});
            emit(body, spv::OpStore, {alias, constants[1]});
        }
        emit(body, spv::OpBranch, {loop_header});
        emit(body, spv::OpLabel, {loop_header});
        emit(body, spv::OpLoopMerge, {exit, loop_continue, spv::LoopControlMaskNone});
        if (shape == Shape::PrefixLoop) {
            /* The sole dispatch is valid but unreachable behind an infinite prefix loop.
               Starting the proof at case 0 would silently ignore the prefix cycle. */
            emit(body, spv::OpBranch, {loop_continue});
            emit(body, spv::OpLabel, {loop_continue});
            emit(body, spv::OpBranch, {loop_header});
            emit(body, spv::OpLabel, {exit});
        }
        emit(body, spv::OpBranch, {dispatch});
        emit(body, spv::OpLabel, {dispatch});
        emit(body, spv::OpLoad, {uint_type, selector, pc});
        emit(body, spv::OpSelectionMerge, {merge, spv::SelectionControlMaskNone});
        const uint32_t dispatch_value = shape == Shape::UnknownSelector ? constants[0] : selector;
        if (shape == Shape::MultiCase) emit(body, spv::OpSwitch, {dispatch_value, merge, 0, shade, 1, shade});
        else emit(body, spv::OpSwitch, {dispatch_value, merge, 0, shade});
        emit(body, spv::OpLabel, {shade});
        if (shape == Shape::MutablePc) emit(body, spv::OpStore, {pc, constants[1]});
    } else if (shape == Shape::GuestLoop) {
        emit(body, spv::OpBranch, {loop_header});
        emit(body, spv::OpLabel, {loop_header});
        emit(body, spv::OpLoopMerge, {exit, loop_continue, spv::LoopControlMaskNone});
        emit(body, spv::OpBranch, {shade});
        emit(body, spv::OpLabel, {shade});
    }
    if (shape == Shape::LateKill || shape == Shape::LateReturn) {
        /* Block layout does not order execution: a lane can terminate on this edge
           even though its terminating block appears after the quad-read block. */
        emit(body, spv::OpSelectionMerge, {after_if, spv::SelectionControlMaskNone});
        emit(body, spv::OpBranchConditional, {condition, yes, after_if});
        emit(body, spv::OpLabel, {after_if});
    }
    if (shape == Shape::ConditionalRead || shape == Shape::PriorKill || shape == Shape::PriorReturn) {
        emit(body, spv::OpSelectionMerge, {after_if, spv::SelectionControlMaskNone});
        emit(body, spv::OpBranchConditional, {condition, yes, no});
        emit(body, spv::OpLabel, {yes});
        if (shape == Shape::PriorKill || shape == Shape::PriorReturn) {
            emit(body, shape == Shape::PriorKill ? spv::OpKill : spv::OpReturn, {});
            emit(body, spv::OpLabel, {no});
            emit(body, spv::OpBranch, {after_if});
            emit(body, spv::OpLabel, {after_if});
        }
    }
    if (any) {
        const uint32_t any_scope = shape == Shape::AnyWrongScope ? constants[2] :
            shape == Shape::AnyFloatScope ? float_source : constants[3];
        const uint32_t any_type = shape == Shape::AnyVectorResult ? bool2_type :
            shape == Shape::AnyPointerResult ? local_pointer : bool_type;
        emit(body, spv::OpGroupNonUniformAny, {any_type, subgroup_result, any_scope,
            shape == Shape::DynamicAny ? condition : true_constant});
    }
    if (shape == Shape::QuadSwap)
        emit(body, spv::OpGroupNonUniformQuadSwap, {uint_type, subgroup_result, constants[3], pattern_bits, constants[0]});
    if (shape != Shape::NonEntryRead) {
        if (metadata == Metadata::ExtraOperand)
            emit(body, spv::OpGroupNonUniformQuadBroadcast, {result_type, read_result, scope_id, input, source_id, constants[0]});
        else if (metadata == Metadata::MissingOperand)
            emit(body, spv::OpGroupNonUniformQuadBroadcast, {result_type, read_result, scope_id, input});
        else emit(body, spv::OpGroupNonUniformQuadBroadcast, {result_type, read_result, scope_id, input, source_id});
    }
    uint32_t raw_result = shape == Shape::NonEntryRead ? pattern_bits : read_result;
    if (kind != Kind::Unsigned) {
        emit(body, spv::OpBitcast, {uint_type, result_bits, raw_result});
        raw_result = result_bits;
    }
    if (shape == Shape::ConditionalRead) {
        emit(body, spv::OpStore, {local, raw_result});
        emit(body, spv::OpBranch, {after_if});
        emit(body, spv::OpLabel, {no});
        emit(body, spv::OpStore, {local, constants[0]});
        emit(body, spv::OpBranch, {after_if});
        emit(body, spv::OpLabel, {after_if});
        emit(body, spv::OpLoad, {uint_type, loaded_bits, local});
        raw_result = loaded_bits;
    }
    if (collisions) {
        /* Source rows 1 still have to participate when their own result is unused.
           Forwarding the native read into ?: would make sources 2/3 disappear. */
        emit(body, spv::OpSelect, {uint_type, selected_bits, condition, raw_result, fixed_constant});
        raw_result = selected_bits;
    }
    emit(body, spv::OpCompositeConstruct, {uint4_type, final_output, raw_result, constants[1], index, constants[1]});
    emit(body, spv::OpStore, {output, final_output});
    if (shape == Shape::GuestLoop || shape == Shape::CaseLoop) emit(body, spv::OpBranch, {loop_continue});
    else if (shape == Shape::CaseUnreachable) emit(body, spv::OpUnreachable, {});
    else emit(body, spv::OpReturn, {});
    if (shape == Shape::PrefixLoop) {
        emit(body, spv::OpLabel, {merge});
        emit(body, spv::OpReturn, {});
    } else if (dispatcher || shape == Shape::GuestLoop) {
        if (dispatcher) {
            emit(body, spv::OpLabel, {merge});
            emit(body, spv::OpBranch, {loop_continue});
        }
        emit(body, spv::OpLabel, {loop_continue});
        emit(body, spv::OpBranch, {loop_header});
        emit(body, spv::OpLabel, {exit});
        emit(body, spv::OpReturn, {});
    }
    if (shape == Shape::LateKill || shape == Shape::LateReturn) {
        emit(body, spv::OpLabel, {yes});
        emit(body, shape == Shape::LateKill ? spv::OpKill : spv::OpReturn, {});
    }
    emit(body, spv::OpFunctionEnd, {});
    if (shape == Shape::HelperFunction || shape == Shape::NonEntryRead) {
        emit(body, spv::OpFunction, {void_type, helper_function, spv::FunctionControlMaskNone, function_type});
        emit(body, spv::OpLabel, {helper_label});
        if (shape == Shape::NonEntryRead)
            emit(body, spv::OpGroupNonUniformQuadBroadcast, {uint_type, helper_result, constants[3], constants[0], constants[0]});
        emit(body, spv::OpReturn, {});
        emit(body, spv::OpFunctionEnd, {});
    }
    Module module{{spv::MagicNumber, 0x00010300, 0, bound, 0}, collisions};
    for (const Words *section : {&capabilities, &entries, &debug, &annotations, &definitions, &body})
        module.words.insert(module.words.end(), section->begin(), section->end());
    return module;
}

void configure(spirv_cross::CompilerMSL &compiler)
{
    auto options = compiler.get_msl_options();
    options.set_msl_version(2, 3);
    compiler.set_msl_options(options);
    spirv_cross::MSLResourceBinding binding{};
    binding.stage = spv::ExecutionModelFragment;
    binding.desc_set = spirv_cross::ResourceBindingPushConstantDescriptorSet;
    binding.binding = spirv_cross::ResourceBindingPushConstantBinding;
    binding.msl_buffer = 0;
    compiler.add_msl_resource_binding(binding);
}

struct Generated { std::string source, entry; };
Generated generate(const Module &module, uint64_t fault_address, Results *results = nullptr)
{
    pb::MetalFragmentQuadCompiler compiler(module.words.data(), module.words.size(), fault_address);
    configure(compiler);
    const std::string source = compiler.compile();
    if (results) {
        ++results->cpu_cases;
        /* The pinned backend destructively lowers interface blocks on public compile().
           Regeneration follows the runtime's one-shot compiler lifecycle; a second call
           on an already lowered CompilerMSL instance is not a supported contract. */
        pb::MetalFragmentQuadCompiler regenerated(module.words.data(), module.words.size(), fault_address);
        configure(regenerated);
        results->check(source == regenerated.compile(), "fresh-instance regeneration of the owned module changed its MSL");
        results->check(source.find("[[thread_index_in_quadgroup]]") != std::string::npos,
                       "native quad-lane argument missing");
        results->check(source.find("quad_or(payload)") != std::string::npos,
                       "production packet reduction missing");
        if (module.selects_after_read) {
            results->check(source.find("namespace pb_fragment_quad__") != std::string::npos,
                           "namespace collision did not reserve a distinct helper name");
            results->check(source.find(" ? ") != std::string::npos,
                           "owned varying post-read select missing");
        }
    }
    return {source, compiler.get_cleansed_entry_point_name("quad_values", spv::ExecutionModelFragment)};
}

void refusal(Results &results, const char *name, const Module &module, const char *diagnostic,
             uint64_t address = 0x100004)
{
    ++results.cpu_cases;
    try {
        spirv_cross::CompilerMSL parser(module.words.data(), module.words.size());
    } catch (const std::exception &error) {
        results.check(false, std::string(name) + ": stock SPIR-V parser rejected fixture: " + error.what());
        return;
    }
    try {
        (void)generate(module, address);
        results.check(false, std::string(name) + ": adapter unexpectedly admitted fixture");
    } catch (const std::exception &error) {
        results.check(std::string(error.what()).find(diagnostic) != std::string::npos,
                       std::string(name) + ": expected '" + diagnostic + "', got '" + error.what() + "'");
    }
}

void cpu_tests(Results &results)
{
    for (Kind kind : {Kind::Unsigned, Kind::Signed, Kind::Float})
        for (unsigned source = 0; source < 4; ++source)
            for (bool collision : {false, true})
                (void)generate(make_module(kind, source, collision), 0x100004, &results);
    for (Shape shape : {Shape::Dispatch, Shape::ConstantAny})
        (void)generate(make_module(Kind::Unsigned, 2, false, shape), 0x100004, &results);
    (void)generate(make_module(Kind::Unsigned, 3, false, Shape::Straight, Metadata::SignedScope), 0x100004, &results);
    const auto ordinary = make_module();
    refusal(results, "zero fault address", ordinary, "aligned atomic fault word", 0);
    refusal(results, "misaligned fault address", ordinary, "aligned atomic fault word", 0x100002);
    struct MetadataCase { const char *name; Metadata metadata; const char *diagnostic; };
    const MetadataCase cases[] = {
        {"extra broadcast operand", Metadata::ExtraOperand, "invalid fragment quad broadcast operand count"},
        {"missing broadcast source", Metadata::MissingOperand, "invalid fragment quad broadcast operand count"},
        {"Boolean result", Metadata::BoolResult, "require a scalar 32-bit integer or float"},
        {"vector result", Metadata::VectorResult, "require a scalar 32-bit integer or float"},
        {"16-bit result", Metadata::NarrowResult, "require a scalar 32-bit integer or float"},
        {"64-bit result", Metadata::WideResult, "require a scalar 32-bit integer or float"},
        {"pointer result", Metadata::PointerResult, "require a scalar 32-bit integer or float"},
        {"array result", Metadata::ArrayResult, "require a scalar 32-bit integer or float"},
        {"different input type", Metadata::MismatchedInput, "input and result types differ"},
        {"pointer input", Metadata::PointerInput, "input and result types differ"},
        {"dynamic source", Metadata::DynamicSource, "constant subgroup source in 0-3"},
        {"specialization source", Metadata::SpecializedSource, "constant subgroup source in 0-3"},
        {"signed source index", Metadata::SignedSource, "constant subgroup source in 0-3"},
        {"float source", Metadata::FloatSource, "constant subgroup source in 0-3"},
        {"64-bit source", Metadata::WideSource, "constant subgroup source in 0-3"},
        {"vector source", Metadata::VectorSource, "constant subgroup source in 0-3"},
        {"Boolean source", Metadata::BoolSource, "constant subgroup source in 0-3"},
        {"source four", Metadata::SourceFour, "constant subgroup source in 0-3"},
        {"workgroup scope", Metadata::WrongScope, "constant subgroup source in 0-3"},
        {"dynamic scope", Metadata::DynamicScope, "constant subgroup source in 0-3"},
        {"float scope", Metadata::FloatScope, "constant subgroup source in 0-3"}
    };
    for (const auto &item : cases)
        refusal(results, item.name, make_module(Kind::Unsigned, 0, false, Shape::Straight, item.metadata), item.diagnostic);
    struct ControlCase { const char *name; Shape shape; const char *diagnostic; };
    const ControlCase controls[] = {
        {"conditional read with reconvergence", Shape::ConditionalRead, "inside per-lane control are unsupported"},
        {"kill before read", Shape::PriorKill, "termination before a later quad read is unsupported"},
        {"return before read", Shape::PriorReturn, "termination before a later quad read is unsupported"},
        {"late-layout kill before read", Shape::LateKill, "termination before a later quad read is unsupported"},
        {"late-layout return before read", Shape::LateReturn, "termination before a later quad read is unsupported"},
        {"multiple dispatch cases", Shape::MultiCase, "single straight-line guest block"},
        {"mutable dispatch", Shape::MutablePc, "cannot change the guest dispatch block"},
        {"missing dispatch initialization", Shape::MissingPc, "unknown fragment dispatch initialization"},
        {"dispatch pointer alias", Shape::AliasPc, "local fragment dispatch pointer escapes"},
        {"constant dispatch selector", Shape::UnknownSelector, "unknown fragment dispatch selector"},
        {"guest loop", Shape::GuestLoop, "without guest loops"},
        {"case returning to dispatcher", Shape::CaseLoop, "quad-read case cannot loop"},
        {"case without a defined termination", Shape::CaseUnreachable, "quad-read case must terminate"},
        {"loop before an unreachable dispatch", Shape::PrefixLoop, "quad-read case cannot loop"},
        {"helper function", Shape::HelperFunction, "without guest loops"},
        {"non-entry quad read", Shape::NonEntryRead, "without guest loops"},
        {"dynamic subgroup Any", Shape::DynamicAny, "wave-wide fragment operations"},
        {"literal Any with workgroup scope", Shape::AnyWrongScope, "wave-wide fragment operations"},
        {"literal Any with float scope", Shape::AnyFloatScope, "wave-wide fragment operations"},
        {"literal Any with Boolean vector result", Shape::AnyVectorResult, "wave-wide fragment operations"},
        {"literal Any with pointer result", Shape::AnyPointerResult, "wave-wide fragment operations"},
        {"quad swap", Shape::QuadSwap, "whole-quad shader operations"}
    };
    for (const auto &item : controls)
        refusal(results, item.name, make_module(Kind::Unsigned, 0, false, item.shape), item.diagnostic);
}

std::runtime_error metal_error(const char *operation, NSError *error)
{
    return std::runtime_error(std::string(operation) + ": " +
        (error ? error.localizedDescription.UTF8String : "no Metal object returned"));
}

id<MTLLibrary> library(id<MTLDevice> device, const std::string &source)
{
    MTLCompileOptions *options = [MTLCompileOptions new];
    options.languageVersion = MTLLanguageVersion2_3;
    if (@available(macOS 15.0, *)) options.mathMode = MTLMathModeSafe;
    else throw std::runtime_error("safe MSL math requires macOS 15 or later");
    NSError *error = nil;
    id<MTLLibrary> compiled = [device newLibraryWithSource:[NSString stringWithUTF8String:source.c_str()]
        options:options error:&error];
    if (!compiled) throw metal_error("MSL compilation", error);
    return compiled;
}

constexpr const char *native_source = R"(
#include <metal_stdlib>
using namespace metal;
vertex float4 quad_vertex(uint vertex_id [[vertex_id]], constant uint &tiny [[buffer(0)]])
{
    const float2 full[3] = {float2(-1, -1), float2(3, -1), float2(-1, 3)};
    const float2 edge[3] = {float2(-1, 1), float2(-0.5625, 1), float2(-1, 0.5625)};
    return float4(tiny ? edge[vertex_id] : full[vertex_id], 0, 1);
}
)";

/* This caller deliberately sits outside the SPIR-V adapter's admitted control-flow
   profile. Odd lanes bypass permanently; it is not a reconvergence/WQM proof.
   Discard suppresses the caller's color output, not earlier writes or an entire draw. */
std::string packet_source()
{
    return std::string(native_source) + pb::metal_fragment_quad_source("owned_packet") + R"(
struct PacketInput { uint4 patterns; uint source; };
fragment uint4 quad_packet(float4 position [[position]], uint lane [[thread_index_in_quadgroup]],
                           constant PacketInput &input [[buffer(0)]], device atomic_uint *fault [[buffer(1)]])
{
    uint index = (uint(position.x) & 1u) | ((uint(position.y) & 1u) << 1u);
    if ((lane & 1u) != 0u) return uint4(0xb16b00b5u, 2u, index, 1u);
    uint bits = owned_packet::read(input.patterns[index], input.source, lane, fault);
    return uint4(bits, 1u, index, 1u);
}
)";
}

struct alignas(16) PacketInput { std::array<uint32_t, 4> patterns; uint32_t source, padding[3]; };
static_assert(sizeof(PacketInput) == 32);

struct Gpu {
    id<MTLDevice> device;
    id<MTLCommandQueue> queue;
    id<MTLBuffer> fault;
    id<MTLTexture> target;
    id<MTLFunction> vertex;

    Gpu()
    {
        device = MTLCreateSystemDefaultDevice();
        if (!device || !device.hasUnifiedMemory)
            throw std::runtime_error("a unified-memory Metal device is required");
        queue = [device newCommandQueue];
        fault = [device newBufferWithLength:sizeof(fault_initial) options:MTLResourceStorageModeShared];
        MTLTextureDescriptor *descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA32Uint
            width:side height:side mipmapped:NO];
        descriptor.storageMode = MTLStorageModeShared;
        descriptor.usage = MTLTextureUsageRenderTarget;
        target = [device newTextureWithDescriptor:descriptor];
        vertex = [library(device, native_source) newFunctionWithName:@"quad_vertex"];
        if (!queue || !fault || !target || !vertex) throw std::runtime_error("Metal fixture allocation failed");
        std::printf("device=%s MSL=2.3 math=safe target=RGBA32Uint\n", device.name.UTF8String);
    }

    id<MTLRenderPipelineState> pipeline(id<MTLFunction> fragment)
    {
        if (!fragment) throw std::runtime_error("Metal fragment entry missing");
        MTLRenderPipelineDescriptor *descriptor = [MTLRenderPipelineDescriptor new];
        descriptor.vertexFunction = vertex;
        descriptor.fragmentFunction = fragment;
        descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA32Uint;
        NSError *error = nil;
        id<MTLRenderPipelineState> pipeline = [device newRenderPipelineStateWithDescriptor:descriptor error:&error];
        if (!pipeline) throw metal_error("render pipeline", error);
        return pipeline;
    }

    std::array<uint32_t, side * side * channels> draw(id<MTLRenderPipelineState> pipeline,
                                                     const void *input, size_t bytes, bool tiny, bool packet)
    {
        std::memcpy(fault.contents, fault_initial.data(), sizeof(fault_initial));
        MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = target;
        pass.colorAttachments[0].loadAction = MTLLoadActionClear;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        pass.colorAttachments[0].clearColor = MTLClearColorMake(double(UINT32_MAX), 0, 0, 0);
        id<MTLCommandBuffer> command = [queue commandBuffer];
        id<MTLRenderCommandEncoder> encoder = [command renderCommandEncoderWithDescriptor:pass];
        if (!command || !encoder) throw std::runtime_error("Metal render encoder missing");
        [encoder setRenderPipelineState:pipeline];
        [encoder setViewport:MTLViewport{0, 0, side, side, 0, 1}];
        const uint32_t edge = tiny;
        [encoder setVertexBytes:&edge length:sizeof(edge) atIndex:0];
        [encoder setFragmentBytes:input length:bytes atIndex:0];
        if (packet) [encoder setFragmentBuffer:fault offset:sizeof(uint32_t) atIndex:1];
        [encoder useResource:fault usage:MTLResourceUsageRead | MTLResourceUsageWrite stages:MTLRenderStageFragment];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        [encoder endEncoding];
        [command commit];
        [command waitUntilCompleted];
        if (command.status != MTLCommandBufferStatusCompleted) throw metal_error("GPU command", command.error);
        std::array<uint32_t, side * side * channels> pixels;
        [target getBytes:pixels.data() bytesPerRow:side * channels * sizeof(uint32_t)
            fromRegion:MTLRegionMake2D(0, 0, side, side) mipmapLevel:0];
        return pixels;
    }
};

void inspect(Results &results, Gpu &gpu, const std::array<uint32_t, side * side * channels> &pixels,
             const std::array<uint32_t, 4> &input, unsigned source, bool tiny, bool selects, bool packet,
             const std::string &label)
{
    const uint32_t *fault = static_cast<const uint32_t *>(gpu.fault.contents);
    const bool missing = packet && (source & 1u);
    for (unsigned i = 0; i < fault_initial.size(); ++i)
        results.check(fault[i] == (i == 1 ? uint32_t(missing) : fault_initial[i]),
                       label + ": fault/canary word " + std::to_string(i));
    for (unsigned y = 0; y < side; ++y) for (unsigned x = 0; x < side; ++x) {
        const bool covered = !tiny || (x == 0 && y == 0);
        const unsigned index = (x & 1u) | ((y & 1u) << 1u);
        std::array<uint32_t, 4> expected = {UINT32_MAX, 0, 0, 0};
        if (covered) {
            ++results.covered_pixels;
            if (packet && (index & 1u)) expected = {bypass_bits, bypass_marker, index, 1};
            else if (!missing) expected = {selects && (y & 1u) ? selected_away : input[source], valid_marker, index, 1};
        }
        const uint32_t *actual = pixels.data() + channels * (y * side + x);
        results.expected_written_pixels += expected[1] != 0;
        results.written_pixels += actual[1] == valid_marker || actual[1] == bypass_marker;
        bool matches = true;
        for (unsigned channel = 0; channel < channels; ++channel) matches &= actual[channel] == expected[channel];
        ++results.pixels;
        if (!matches && results.failures < 30)
            std::fprintf(stderr, "%s pixel(%u,%u): %08x,%08x,%08x,%08x expected %08x,%08x,%08x,%08x\n",
                label.c_str(), x, y, actual[0], actual[1], actual[2], actual[3],
                expected[0], expected[1], expected[2], expected[3]);
        results.check(matches, label + ": exact pixel words differ");
    }
}

void gpu_tests(Results &results)
{
    Gpu gpu;
    for (Kind kind : {Kind::Unsigned, Kind::Signed, Kind::Float})
        for (unsigned source = 0; source < 4; ++source)
            for (bool collisions : {false, true}) {
                const auto generated = generate(make_module(kind, source, collisions), gpu.fault.gpuAddress + sizeof(uint32_t));
                id<MTLLibrary> compiled = library(gpu.device, generated.source);
                id<MTLFunction> fragment = [compiled newFunctionWithName:[NSString stringWithUTF8String:generated.entry.c_str()]];
                id<MTLRenderPipelineState> pipeline = gpu.pipeline(fragment);
                for (unsigned pattern = 0; pattern < patterns.size(); ++pattern)
                    for (bool tiny : {false, true}) {
                        const std::string label = "adapter kind=" + std::to_string(unsigned(kind)) +
                            " source=" + std::to_string(source) + " collision/select=" + std::to_string(collisions) +
                            " pattern=" + std::to_string(pattern) + " tiny=" + std::to_string(tiny);
                        const auto pixels = gpu.draw(pipeline, patterns[pattern].data(), sizeof(patterns[pattern]), tiny, false);
                        ++results.adapter_draws;
                        inspect(results, gpu, pixels, patterns[pattern], source, tiny, collisions, false, label);
                    }
            }
    id<MTLLibrary> packet_library = library(gpu.device, packet_source());
    id<MTLRenderPipelineState> packet_pipeline = gpu.pipeline([packet_library newFunctionWithName:@"quad_packet"]);
    for (unsigned source = 0; source < 4; ++source)
        for (unsigned pattern = 0; pattern < patterns.size(); ++pattern)
            for (bool tiny : {false, true}) {
                const PacketInput input{patterns[pattern], source, {0, 0, 0}};
                const std::string label = "packet source=" + std::to_string(source) +
                    " pattern=" + std::to_string(pattern) + " tiny=" + std::to_string(tiny);
                const auto pixels = gpu.draw(packet_pipeline, &input, sizeof(input), tiny, true);
                ++results.packet_draws;
                inspect(results, gpu, pixels, patterns[pattern], source, tiny, false, true, label);
            }
}
}

int main(int argc, char **argv)
{
    bool cpu_only = false;
    if (argc == 2 && std::strcmp(argv[1], "--cpu-only") == 0) cpu_only = true;
    else if (argc != 1) {
        std::fprintf(stderr, "usage: %s [--cpu-only]\n", argv[0]);
        return 2;
    }
    Results results;
    @autoreleasepool {
        try {
            cpu_tests(results);
            if (!cpu_only && !results.failures) gpu_tests(results);
        } catch (const std::exception &error) {
            results.check(false, std::string("fixture exception: ") + error.what());
        }
    }
    std::printf("SUMMARY cpu_cases=%u adapter_draws=%u packet_draws=%u pixels=%u raster_covered_pixels=%u expected_written_pixels=%u written_pixels=%u checks=%u failures=%u gpu=%s\n",
        results.cpu_cases, results.adapter_draws, results.packet_draws, results.pixels,
        results.covered_pixels, results.expected_written_pixels, results.written_pixels, results.checks, results.failures, cpu_only ? "not-run" : "requested");
    return results.failures ? 1 : 0;
}
