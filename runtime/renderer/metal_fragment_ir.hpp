/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PB_METAL_FRAGMENT_IR_HPP
#define PB_METAL_FRAGMENT_IR_HPP
#include "spirv.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace pb {
namespace fragment_ir_detail {
struct DcePass;
/* Only these known layouts define IDs. Opaque instructions still retain every possible use. */
inline unsigned result_layout(spv::Op op)
{
    switch (op) {
    case spv::OpTypeVoid: case spv::OpTypeBool: case spv::OpTypeInt: case spv::OpTypeFloat:
    case spv::OpTypeVector: case spv::OpTypeMatrix: case spv::OpTypeImage: case spv::OpTypeSampler:
    case spv::OpTypeSampledImage: case spv::OpTypeArray: case spv::OpTypeRuntimeArray:
    case spv::OpTypeStruct: case spv::OpTypeOpaque: case spv::OpTypePointer: case spv::OpTypeFunction:
    case spv::OpString: case spv::OpExtInstImport: case spv::OpDecorationGroup: case spv::OpLabel: return 1;
    case spv::OpUndef: case spv::OpConstantTrue: case spv::OpConstantFalse: case spv::OpConstant:
    case spv::OpConstantComposite: case spv::OpConstantNull: case spv::OpSpecConstantTrue:
    case spv::OpSpecConstantFalse: case spv::OpSpecConstant: case spv::OpSpecConstantComposite:
    case spv::OpSpecConstantOp: case spv::OpFunction: case spv::OpFunctionParameter:
    case spv::OpFunctionCall: case spv::OpVariable: case spv::OpLoad: case spv::OpAccessChain:
    case spv::OpInBoundsAccessChain: case spv::OpPtrAccessChain: case spv::OpArrayLength:
    case spv::OpCopyObject: case spv::OpCompositeConstruct: case spv::OpCompositeExtract:
    case spv::OpCompositeInsert: case spv::OpVectorShuffle: case spv::OpVectorExtractDynamic:
    case spv::OpVectorInsertDynamic: case spv::OpExtInst: case spv::OpPhi:
    case spv::OpConvertFToU: case spv::OpConvertFToS: case spv::OpConvertSToF: case spv::OpConvertUToF:
    case spv::OpUConvert: case spv::OpSConvert: case spv::OpFConvert: case spv::OpBitcast:
    case spv::OpConvertUToPtr: case spv::OpConvertPtrToU: case spv::OpSNegate: case spv::OpFNegate:
    case spv::OpIAdd: case spv::OpISub: case spv::OpIMul: case spv::OpUDiv: case spv::OpSDiv:
    case spv::OpFAdd: case spv::OpFSub: case spv::OpFMul: case spv::OpFDiv:
    case spv::OpBitwiseAnd: case spv::OpBitwiseOr: case spv::OpBitwiseXor: case spv::OpNot:
    case spv::OpShiftLeftLogical: case spv::OpShiftRightLogical: case spv::OpShiftRightArithmetic:
    case spv::OpLogicalNot: case spv::OpLogicalAnd: case spv::OpLogicalOr: case spv::OpLogicalEqual:
    case spv::OpLogicalNotEqual: case spv::OpSelect: case spv::OpIEqual: case spv::OpINotEqual:
    case spv::OpULessThan: case spv::OpUGreaterThan: case spv::OpULessThanEqual: case spv::OpUGreaterThanEqual:
    case spv::OpSLessThan: case spv::OpSGreaterThan: case spv::OpSLessThanEqual: case spv::OpSGreaterThanEqual:
    case spv::OpFOrdLessThan: case spv::OpFOrdGreaterThan: case spv::OpFOrdLessThanEqual:
    case spv::OpFOrdGreaterThanEqual: case spv::OpFOrdEqual: case spv::OpFOrdNotEqual:
    case spv::OpGroupNonUniformAny: case spv::OpGroupNonUniformAll:
    case spv::OpGroupNonUniformQuadSwap: case spv::OpGroupNonUniformQuadBroadcast:
    case spv::OpAtomicLoad: case spv::OpAtomicExchange: case spv::OpAtomicCompareExchange:
    case spv::OpAtomicIAdd: case spv::OpImageRead: case spv::OpImageFetch:
    case spv::OpImageSampleImplicitLod: case spv::OpImageSampleExplicitLod: case spv::OpSampledImage:
        return 2;
    default: return 0;
    }
}
inline bool literal_decoration(uint32_t kind)
{
    switch (static_cast<spv::Decoration>(kind)) {
    case spv::DecorationRelaxedPrecision: case spv::DecorationSpecId: case spv::DecorationBlock:
    case spv::DecorationBufferBlock: case spv::DecorationRowMajor: case spv::DecorationColMajor:
    case spv::DecorationArrayStride: case spv::DecorationMatrixStride: case spv::DecorationBuiltIn:
    case spv::DecorationNoPerspective: case spv::DecorationFlat: case spv::DecorationPatch:
    case spv::DecorationCentroid: case spv::DecorationSample: case spv::DecorationInvariant:
    case spv::DecorationRestrict: case spv::DecorationAliased: case spv::DecorationVolatile:
    case spv::DecorationConstant: case spv::DecorationCoherent: case spv::DecorationNonWritable:
    case spv::DecorationNonReadable: case spv::DecorationUniform: case spv::DecorationStream:
    case spv::DecorationLocation: case spv::DecorationComponent: case spv::DecorationIndex:
    case spv::DecorationBinding: case spv::DecorationDescriptorSet: case spv::DecorationOffset:
    case spv::DecorationXfbBuffer: case spv::DecorationXfbStride: case spv::DecorationFuncParamAttr:
    case spv::DecorationFPRoundingMode: case spv::DecorationFPFastMathMode: case spv::DecorationLinkageAttributes:
    case spv::DecorationNoContraction: case spv::DecorationInputAttachmentIndex:
    case spv::DecorationAlignment: case spv::DecorationMaxByteOffset: return true;
    default: return false;
    }
}
inline bool pure(spv::Op op)
{
    switch (op) {
    case spv::OpAccessChain: case spv::OpInBoundsAccessChain: case spv::OpConvertUToPtr:
    case spv::OpUConvert: case spv::OpSConvert: case spv::OpFConvert: case spv::OpBitcast:
    case spv::OpCopyObject: case spv::OpCompositeConstruct: case spv::OpCompositeExtract:
    case spv::OpCompositeInsert: case spv::OpVectorShuffle: case spv::OpIAdd: case spv::OpISub:
    case spv::OpIMul: case spv::OpBitwiseAnd: case spv::OpBitwiseOr: case spv::OpBitwiseXor:
    case spv::OpNot: case spv::OpShiftLeftLogical: case spv::OpShiftRightLogical:
    case spv::OpShiftRightArithmetic: case spv::OpLogicalNot: case spv::OpLogicalAnd:
    case spv::OpLogicalOr: case spv::OpLogicalEqual: case spv::OpLogicalNotEqual: case spv::OpSelect:
    case spv::OpFAdd: case spv::OpFSub: case spv::OpFMul: case spv::OpFOrdLessThan:
    case spv::OpFOrdGreaterThan: case spv::OpFOrdLessThanEqual: case spv::OpFOrdGreaterThanEqual:
    case spv::OpFOrdEqual: case spv::OpFOrdNotEqual: case spv::OpIEqual: case spv::OpINotEqual:
    case spv::OpULessThan: case spv::OpUGreaterThan: case spv::OpUGreaterThanEqual: case spv::OpULessThanEqual:
    case spv::OpSLessThan: case spv::OpSGreaterThan: case spv::OpSLessThanEqual: case spv::OpSGreaterThanEqual:
    case spv::OpGroupNonUniformAny: case spv::OpGroupNonUniformQuadSwap: case spv::OpGroupNonUniformQuadBroadcast:
        return true;
    default: return false;
    }
}
} // namespace fragment_ir_detail

struct MetalFragmentOp {
    spv::Op opcode;
    std::vector<uint32_t> operands;
    size_t offset;
    bool removed = false;
    uint32_t result_id() const
    {
        const unsigned layout = fragment_ir_detail::result_layout(opcode);
        return layout && operands.size() >= layout ? operands[layout - 1] : 0;
    }
    uint32_t result_type() const
    {
        return fragment_ir_detail::result_layout(opcode) == 2 && operands.size() >= 2 ? operands[0] : 0;
    }
};


namespace fragment_ir_detail {
inline bool shape(const MetalFragmentOp &op)
{
    const size_t n = op.operands.size();
    switch (op.opcode) {
    case spv::OpTypeVoid: case spv::OpTypeBool: case spv::OpLabel: return n == 1;
    case spv::OpTypeInt: case spv::OpTypeVector: case spv::OpTypeMatrix:
    case spv::OpTypePointer: case spv::OpTypeArray: return n == 3;
    case spv::OpVariable: return n == 3 || n == 4;
    case spv::OpFunction: return n == 4;
    case spv::OpFunctionParameter: return n == 2;
    case spv::OpLoad: return n >= 3;
    case spv::OpStore: return n >= 2;
    case spv::OpAccessChain: case spv::OpInBoundsAccessChain: return n >= 3;
    case spv::OpConvertUToPtr: case spv::OpUConvert: case spv::OpSConvert:
    case spv::OpFConvert: case spv::OpBitcast: case spv::OpCopyObject:
    case spv::OpNot: case spv::OpLogicalNot: return n == 3;
    case spv::OpSelect: case spv::OpGroupNonUniformQuadSwap:
    case spv::OpGroupNonUniformQuadBroadcast: return n == 5;
    case spv::OpCompositeConstruct: return n >= 2;
    case spv::OpCompositeExtract: return n >= 4;
    case spv::OpCompositeInsert: case spv::OpVectorShuffle: return n >= 5;
    case spv::OpFunctionEnd: case spv::OpReturn: case spv::OpKill: case spv::OpUnreachable: return n == 0;
    default: return !pure(op.opcode) || n == 4;
    }
}
} // namespace fragment_ir_detail

class MetalFragmentIR {
public:
    std::array<uint32_t, 5> header;
    std::vector<MetalFragmentOp> ops;
    MetalFragmentIR(const uint32_t *words, size_t count)
    {
        constexpr size_t limit = 1u << 20;
        if (!words || count < 5 || count > limit) throw std::runtime_error("fragment IR has an invalid module extent");
        std::copy_n(words, 5, header.begin());
        if (header[0] != spv::MagicNumber || !header[3] || header[3] > limit || header[4])
            throw std::runtime_error("fragment IR has an invalid header or ID bound");
        for (size_t at = 5; at < count;) {
            const size_t extent = words[at] >> 16;
            if (!extent || extent > count - at) throw std::runtime_error("fragment IR has an invalid instruction extent");
            ops.push_back({static_cast<spv::Op>(words[at] & 0xffffu),
                std::vector<uint32_t>(words + at + 1, words + at + extent), at, false});
            at += extent;
        }
        reindex();
    }
    void reindex()
    {
        definitions_.clear();
        for (size_t i = 0; i < ops.size(); ++i) {
            const auto &op = ops[i];
            if (op.removed) continue;
            if (!fragment_ir_detail::shape(op)) throw std::runtime_error("fragment IR has a malformed known instruction");
            const unsigned layout = fragment_ir_detail::result_layout(op.opcode);
            if (!layout) continue;
            if (op.operands.size() < layout || !op.result_id() || op.result_id() >= header[3] ||
                (layout == 2 && (!op.result_type() || op.result_type() >= header[3])) ||
                !definitions_.emplace(op.result_id(), i).second)
                throw std::runtime_error("fragment IR has an invalid or redefined result ID");
        }
    }
    const MetalFragmentOp *find(uint32_t id) const
    {
        const auto found = definitions_.find(id);
        return found == definitions_.end() || ops[found->second].removed ? nullptr : &ops[found->second];
    }
    MetalFragmentOp *find(uint32_t id)
    {
        const auto found = definitions_.find(id);
        return found == definitions_.end() || ops[found->second].removed ? nullptr : &ops[found->second];
    }
    std::vector<std::pair<size_t, size_t>> uses(uint32_t id) const
    {
        std::vector<std::pair<size_t, size_t>> found;
        if (!id) return found;
        for (size_t i = 0; i < ops.size(); ++i)
            if (!ops[i].removed)
                for (size_t p = 0; p < ops[i].operands.size(); ++p)
                    if (ops[i].operands[p] == id && id_operand(ops[i], p)) found.emplace_back(i, p);
        return found;
    }
    std::vector<uint32_t> serialize() const
    {
        std::vector<uint32_t> result(header.begin(), header.end());
        for (const auto &op : ops) {
            if (op.removed) continue;
            if (op.operands.size() >= 65535) throw std::runtime_error("fragment IR instruction is too large");
            result.push_back((uint32_t(op.operands.size() + 1) << 16) | uint32_t(op.opcode));
            result.insert(result.end(), op.operands.begin(), op.operands.end());
        }
        return result;
    }
private:
    friend struct fragment_ir_detail::DcePass;
    std::unordered_map<uint32_t, size_t> definitions_;
    bool id_operand(const MetalFragmentOp &op, size_t p) const
    {
        const auto &a = op.operands;
        const unsigned layout = fragment_ir_detail::result_layout(op.opcode);
        if (layout && p == layout - 1) return false;
        if (layout == 2 && p == 0) return true;
        switch (op.opcode) {
        case spv::OpNop: case spv::OpCapability: case spv::OpMemoryModel: case spv::OpNoLine: case spv::OpModuleProcessed:
        case spv::OpSourceExtension: case spv::OpSourceContinued: case spv::OpExtension:
        case spv::OpString: case spv::OpExtInstImport: case spv::OpTypeVoid: case spv::OpTypeBool:
        case spv::OpTypeInt: case spv::OpTypeFloat: case spv::OpTypeSampler: case spv::OpTypeOpaque:
        case spv::OpConstantTrue: case spv::OpConstantFalse: case spv::OpConstant: case spv::OpConstantNull:
        case spv::OpSpecConstantTrue: case spv::OpSpecConstantFalse: case spv::OpSpecConstant:
        case spv::OpLabel: case spv::OpFunctionEnd: case spv::OpReturn: case spv::OpKill:
        case spv::OpUnreachable: return false;
        case spv::OpTypeVector: case spv::OpTypeMatrix: case spv::OpTypeImage: case spv::OpTypeSampledImage:
        case spv::OpTypeRuntimeArray: return p == 1;
        case spv::OpTypePointer: return p == 2;
        case spv::OpTypeForwardPointer: return p == 0;
        case spv::OpName: case spv::OpMemberName: case spv::OpExecutionMode: case spv::OpLine: return p == 0;
        case spv::OpDecorate:
            return p == 0 || (p >= 2 && a.size() >= 2 && !fragment_ir_detail::literal_decoration(a[1]));
        case spv::OpMemberDecorate:
            return p == 0 || (p >= 3 && a.size() >= 3 && !fragment_ir_detail::literal_decoration(a[2]));
        case spv::OpSource: return p == 2;
        case spv::OpExecutionModeId: return p != 1;
        case spv::OpGroupMemberDecorate: return p == 0 || p % 2 == 1;
        case spv::OpEntryPoint: {
            if (p == 1) return true;
            if (p < 2) return false;
            size_t end = 2;
            for (; end < a.size(); ++end) {
                const uint32_t word = a[end];
                if (!(word & 0xffu) || !(word & 0xff00u) || !(word & 0xff0000u) || !(word & 0xff000000u)) break;
            }
            return p > end;
        }
        case spv::OpVariable: return p == 3;
        case spv::OpFunction: return p == 3;
        case spv::OpFunctionParameter: return false;
        case spv::OpSelectionMerge: return p == 0;
        case spv::OpLoopMerge: {
            if (p < 2) return true;
            if (p == 2 || a.size() < 3) return false;
            const uint32_t known = spv::LoopControlUnrollMask | spv::LoopControlDontUnrollMask |
                spv::LoopControlDependencyInfiniteMask | spv::LoopControlDependencyLengthMask |
                spv::LoopControlMinIterationsMask | spv::LoopControlMaxIterationsMask |
                spv::LoopControlIterationMultipleMask | spv::LoopControlPeelCountMask | spv::LoopControlPartialCountMask;
            return (a[2] & ~known) != 0; // Unknown extensions retain every possible ID operand.
        }
        case spv::OpBranchConditional: return p < 3;
        case spv::OpSwitch: {
            if (p < 2) return true;
            const auto *selector = a.empty() ? nullptr : find(a[0]);
            const auto *type = selector ? find(selector->result_type()) : nullptr;
            if (type && type->opcode == spv::OpTypeInt && type->operands.size() == 3) {
                if (type->operands[1] == 32) return p % 2 == 1;
                if (type->operands[1] == 64) return p % 3 == 1;
            }
            return true;
        }
        case spv::OpLoad: return p == 2 || (p >= 4 && !(a.size() == 5 && a[3] == spv::MemoryAccessAlignedMask));
        case spv::OpStore: return p < 2 || (p >= 3 && !(a.size() == 4 && a[2] == spv::MemoryAccessAlignedMask));
        case spv::OpCompositeExtract: return p == 2;
        case spv::OpCompositeInsert: case spv::OpVectorShuffle: return p == 2 || p == 3;
        case spv::OpArrayLength: return p == 2;
        case spv::OpExtInst: return p != 3;
        case spv::OpSpecConstantOp: return p != 2;
        default: return true;
        }
    }
};

struct MetalDescriptorSpan { uint32_t pointer_sgpr; uint64_t offset; uint32_t bytes; bool from_memory; };
struct MetalFragmentRead { uint32_t pointer_sgpr; uint64_t offset; uint32_t bytes; size_t instruction_offset; };
struct MetalFragmentDce {
    size_t removed_stores = 0, removed_values = 0;
    std::vector<MetalFragmentRead> removed_reads;
};
namespace fragment_ir_detail {
inline bool plain_memory(const MetalFragmentOp &op)
{
    if (op.opcode != spv::OpLoad && op.opcode != spv::OpStore) return false;
    const size_t first = op.opcode == spv::OpLoad ? 3 : 2;
    const auto &a = op.operands;
    return a.size() == first || (a.size() == first + 1 && a[first] == 0) ||
        (a.size() == first + 2 && a[first] == spv::MemoryAccessAlignedMask &&
         a[first + 1] && !(a[first + 1] & (a[first + 1] - 1)));
}
/* Decorations on aggregate members or a pointer-chain base can qualify a derived load.
   This profile does not propagate them, so reject them before any dead-state removal. */
inline void require_supported_memory_metadata(const MetalFragmentIR &ir)
{
    for (const auto &op : ir.ops) {
        if (op.removed || (op.opcode != spv::OpDecorate && op.opcode != spv::OpMemberDecorate)) continue;
        const size_t field = op.opcode == spv::OpDecorate ? 1 : 2;
        if (op.operands.size() > field &&
            (op.operands[field] == spv::DecorationVolatile || op.operands[field] == spv::DecorationCoherent))
            throw std::runtime_error("fragment control does not admit volatile or coherent memory decorations");
    }
}
inline bool memory_decoration(const MetalFragmentIR &ir, uint32_t id)
{
    for (const auto &op : ir.ops)
        if (!op.removed && op.opcode == spv::OpDecorate && op.operands.size() >= 2 && op.operands[0] == id &&
            (op.operands[1] == spv::DecorationVolatile || op.operands[1] == spv::DecorationCoherent)) return true;
    return false;
}
struct DcePass {
    MetalFragmentIR &ir;
    std::unordered_map<uint32_t, std::vector<std::pair<size_t, size_t>>> references;
    std::vector<uint32_t> functions, blocks;
    std::unordered_map<uint32_t, uint32_t> entries;
    std::unordered_set<uint32_t> locals;
    explicit DcePass(MetalFragmentIR &module) : ir(module), functions(ir.ops.size()), blocks(ir.ops.size())
    {
        ir.reindex();
        uint32_t function = 0, block = 0;
        for (size_t i = 0; i < ir.ops.size(); ++i) {
            const auto &op = ir.ops[i];
            if (op.removed) continue;
            if (op.opcode == spv::OpFunction) {
                if (function) throw std::runtime_error("fragment IR has nested functions");
                function = op.result_id(); block = 0;
            }
            if (op.opcode == spv::OpLabel) {
                if (!function) throw std::runtime_error("fragment IR label is outside a function");
                block = op.result_id(); entries.emplace(function, block);
            }
            functions[i] = function; blocks[i] = block;
            for (size_t p = 0; p < op.operands.size(); ++p)
                if (op.operands[p] && op.operands[p] < ir.header[3] && ir.id_operand(op, p))
                    references[op.operands[p]].emplace_back(i, p);
            if (op.opcode == spv::OpFunctionEnd) {
                if (!function) throw std::runtime_error("fragment IR has an unmatched function end");
                function = block = 0;
            }
        }
        if (function) throw std::runtime_error("fragment IR function is not terminated");
        for (size_t i = 0; i < ir.ops.size(); ++i) {
            const auto &op = ir.ops[i];
            if (op.removed || op.opcode != spv::OpVariable || !functions[i] || op.operands.size() < 3 ||
                op.operands[2] != spv::StorageClassFunction || observable(op.result_id())) continue;
            const auto *type = pointer_type(op.result_id());
            if (!type || type->operands[1] != spv::StorageClassFunction) continue;
            bool direct = true;
            for (const auto &[use, position] : uses(op.result_id())) {
                const auto &other = ir.ops[use];
                const bool load = other.opcode == spv::OpLoad && position == 2;
                const bool store = other.opcode == spv::OpStore && position == 0;
                if ((!load && !store) || !plain_memory(other) || functions[use] != functions[i]) {direct = false; break;}
            }
            if (direct) locals.insert(op.result_id());
        }
    }
    const std::vector<std::pair<size_t, size_t>> &uses(uint32_t id) const
    {
        static const std::vector<std::pair<size_t, size_t>> empty;
        const auto found = references.find(id);
        return found == references.end() ? empty : found->second;
    }
    const MetalFragmentOp *pointer_type(uint32_t id) const
    {
        const auto *value = ir.find(id);
        const auto *type = value ? ir.find(value->result_type()) : nullptr;
        return type && type->opcode == spv::OpTypePointer && type->operands.size() == 3 ? type : nullptr;
    }
    bool observable(uint32_t pointer) const
    {
        if (memory_decoration(ir, pointer)) return true;
        const auto *type = pointer_type(pointer);
        return type && (memory_decoration(ir, type->result_id()) || memory_decoration(ir, type->operands[2]));
    }
};

/* This proof concerns only host-prevalidated descriptor-table reads. It does not establish
   arbitrary guest memory validity, nor does it invent resource origins from shader values. */
class DescriptorAddress {
    enum Kind { Constant, User, High16, WideUser, WideHigh16, HighWord, Pair48, Address };
    struct Value { Kind kind; uint32_t width = 0, user = 0; uint64_t number = 0, offset = 0; };
    DcePass &pass;
    const MetalFragmentIR &ir;
    uint64_t anchor;
    uint32_t push_offset;
    std::unordered_map<uint32_t, Value> cache;
    std::unordered_set<uint32_t> visiting;
    [[noreturn]] static void refuse() {throw std::runtime_error("fragment WQM dead physical read lacks canonical descriptor coverage");}
    const MetalFragmentOp &definition(uint32_t id) const
    {
        const auto *op = ir.find(id);
        if (!op) refuse();
        /* An opaque instruction could use this word as an unknown definition/alias operand.
           Never derive a removable read from that ambiguous provenance. */
        for (const auto &[use, position] : pass.uses(id)) {
            (void)position;
            const auto code = ir.ops[use].opcode;
            if (result_layout(code)) continue;
            switch (code) {
            case spv::OpName: case spv::OpMemberName: case spv::OpDecorate: case spv::OpMemberDecorate:
            case spv::OpEntryPoint: case spv::OpExecutionMode: case spv::OpExecutionModeId:
            case spv::OpSource: case spv::OpLine: case spv::OpStore: case spv::OpAtomicStore:
            case spv::OpBranch: case spv::OpBranchConditional: case spv::OpSwitch:
            case spv::OpSelectionMerge: case spv::OpLoopMerge: case spv::OpReturnValue:
            case spv::OpControlBarrier: case spv::OpMemoryBarrier: break;
            default: refuse();
            }
        }
        return *op;
    }
    uint32_t width(uint32_t type) const
    {
        const auto &op = definition(type);
        if (op.opcode != spv::OpTypeInt || op.operands.size() != 3 || op.operands[2] ||
            (op.operands[1] != 32 && op.operands[1] != 64)) refuse();
        return op.operands[1];
    }
    Value constant(uint32_t id) const
    {
        const auto &op = definition(id);
        if (op.opcode != spv::OpConstant) refuse();
        const uint32_t bits = width(op.result_type());
        if (op.operands.size() != 2 + bits / 32) refuse();
        return {Constant, bits, 0, uint64_t(op.operands[2]) | (bits == 64 ? uint64_t(op.operands[3]) << 32 : 0), 0};
    }
    bool decoration(uint32_t id, spv::Decoration kind, uint32_t literal, bool member = false) const
    {
        unsigned count = 0;
        for (const auto &op : ir.ops) {
            if (op.removed || op.opcode != (member ? spv::OpMemberDecorate : spv::OpDecorate)) continue;
            const auto &a = op.operands;
            const size_t field = member ? 2 : 1;
            if (a.size() <= field || a[0] != id || a[field] != uint32_t(kind)) continue;
            if (member && a[1] != 0) refuse();
            if (kind == spv::DecorationBlock) {if (a.size() != 2) refuse();}
            else if (a.size() != field + 2 || a[field + 1] != literal) refuse();
            ++count;
        }
        return count == 1;
    }
    Value push_word(uint32_t pointer) const
    {
        const auto &access = definition(pointer);
        const auto &a = access.operands;
        const auto *type = pass.pointer_type(pointer);
        if ((access.opcode != spv::OpAccessChain && access.opcode != spv::OpInBoundsAccessChain) || a.size() != 5 ||
            !type || type->operands[1] != spv::StorageClassPushConstant || width(type->operands[2]) != 32 ||
            pass.observable(pointer)) refuse();
        const auto &base = definition(a[2]);
        const auto *base_type = pass.pointer_type(base.result_id());
        if (base.opcode != spv::OpVariable || base.operands.size() != 3 || base.operands[2] != spv::StorageClassPushConstant ||
            !base_type || base_type->operands[1] != spv::StorageClassPushConstant || pass.observable(base.result_id()) ||
            pass.functions[size_t(&base - ir.ops.data())]) refuse();
        const auto &structure = definition(base_type->operands[2]);
        if (structure.opcode != spv::OpTypeStruct || structure.operands.size() != 2 ||
            !decoration(structure.result_id(), spv::DecorationBlock, 0) ||
            !decoration(structure.result_id(), spv::DecorationOffset, push_offset, true)) refuse();
        const auto &array = definition(structure.operands[1]);
        if (array.opcode != spv::OpTypeArray || array.operands.size() != 3 || width(array.operands[1]) != 32 ||
            !decoration(array.result_id(), spv::DecorationArrayStride, 4)) refuse();
        const Value count = constant(array.operands[2]), member = constant(a[3]), index = constant(a[4]);
        if (count.width != 32 || !count.number || count.number > (UINT32_MAX - push_offset) / 4 ||
            member.width != 32 || member.number || index.width != 32 || index.number >= count.number) refuse();
        for (const auto &[use, position] : pass.uses(base.result_id())) {
            const auto &op = ir.ops[use];
            if (!((op.opcode == spv::OpAccessChain || op.opcode == spv::OpInBoundsAccessChain) && position == 2) &&
                !(op.opcode == spv::OpEntryPoint && position >= 3)) refuse();
        }
        for (const auto &[use, position] : pass.uses(pointer)) {
            const auto &op = ir.ops[use];
            if (op.opcode != spv::OpLoad || position != 2 || !plain_memory(op)) refuse();
        }
        return {User, 32, uint32_t(index.number), 0, 0};
    }
    uint32_t stable_value(const MetalFragmentOp &load) const
    {
        const uint32_t pointer = load.operands[2];
        if (!pass.locals.count(pointer)) refuse();
        const auto &variable = definition(pointer);
        const size_t variable_index = size_t(&variable - ir.ops.data()), load_index = size_t(&load - ir.ops.data());
        const uint32_t function = pass.functions[variable_index];
        const auto entry = pass.entries.find(function);
        if (entry == pass.entries.end() || pass.functions[load_index] != function ||
            pass.blocks[variable_index] != entry->second) refuse();
        uint32_t selected = variable.operands.size() == 4 ? variable.operands[3] : 0;
        size_t last = variable_index;
        for (const auto &[use, position] : pass.uses(pointer)) {
            const auto &op = ir.ops[use];
            if (op.opcode != spv::OpStore || position != 0) continue;
            if (pass.blocks[use] != entry->second || op.operands.size() < 2) refuse();
            if (use >= last) {selected = op.operands[1]; last = use;}
        }
        if (!selected || (pass.blocks[load_index] == entry->second && load_index <= last)) refuse();
        /* The entry assignment must not be revisited by a back edge. Opaque entry-label uses refuse. */
        for (const auto &[use, position] : pass.uses(entry->second)) {
            const auto &op = ir.ops[use];
            if (!(op.opcode == spv::OpPhi && position >= 3 && position % 2 == 1)) refuse();
        }
        if (definition(selected).result_type() != load.result_type()) refuse();
        return selected;
    }
    Value value(uint32_t id)
    {
        const auto found = cache.find(id);
        if (found != cache.end()) return found->second;
        if (visiting.size() >= 128 || !visiting.insert(id).second) refuse();
        const auto &op = definition(id);
        const auto &a = op.operands;
        Value result{Constant};
        if (op.opcode == spv::OpConstant) result = constant(id);
        else if (op.opcode == spv::OpCopyObject && a.size() == 3) {
            result = value(a[2]); if (result.width != width(op.result_type())) refuse();
        } else if (op.opcode == spv::OpLoad && a.size() >= 3 && plain_memory(op)) {
            const auto *type = pass.pointer_type(a[2]);
            if (!type || type->operands[2] != op.result_type() || pass.observable(a[2])) refuse();
            if (type->operands[1] == spv::StorageClassPushConstant) result = push_word(a[2]);
            else if (type->operands[1] == spv::StorageClassFunction) result = value(stable_value(op));
            else refuse();
            if (result.width != width(op.result_type())) refuse();
        } else if (op.opcode == spv::OpUConvert && a.size() == 3 && width(op.result_type()) == 64) {
            result = value(a[2]);
            if (result.width != 32) refuse();
            if (result.kind == User) result.kind = WideUser;
            else if (result.kind == High16) result.kind = WideHigh16;
            else if (result.kind != Constant) refuse();
            result.width = 64;
        } else if (op.opcode == spv::OpBitwiseAnd && a.size() == 4 && width(op.result_type()) == 32) {
            Value left = value(a[2]), right = value(a[3]);
            if (left.kind == Constant) std::swap(left, right);
            if (left.kind != User || right.kind != Constant || right.width != 32 || right.number != 0xffffu) refuse();
            result = left; result.kind = High16;
        } else if (op.opcode == spv::OpShiftLeftLogical && a.size() == 4 && width(op.result_type()) == 64) {
            result = value(a[2]); const Value by = constant(a[3]);
            if (result.kind != WideHigh16 || by.number != 32) refuse();
            result.kind = HighWord;
        } else if (op.opcode == spv::OpBitwiseOr && a.size() == 4 && width(op.result_type()) == 64) {
            Value left = value(a[2]), right = value(a[3]);
            if (left.kind == HighWord) std::swap(left, right);
            if (left.kind != WideUser || right.kind != HighWord || left.user == UINT32_MAX || right.user != left.user + 1) refuse();
            result = {Pair48, 64, left.user, 0, 0};
        } else if (op.opcode == spv::OpIAdd && a.size() == 4) {
            Value left = value(a[2]), right = value(a[3]); const uint32_t bits = width(op.result_type());
            if (left.width != bits || right.width != bits) refuse();
            if (left.kind == Constant && right.kind == Constant) {
                const uint64_t maximum = bits == 32 ? UINT32_MAX : UINT64_MAX;
                if (left.number > maximum - right.number) refuse();
                result = {Constant, bits, 0, left.number + right.number, 0};
            } else {
                if (left.kind == Constant) std::swap(left, right);
                if (bits != 64 || right.kind != Constant) refuse();
                if (left.kind == Pair48 && right.number == anchor) result = {Address, 64, left.user, 0, 0};
                else if (left.kind == Address && right.number <= UINT64_MAX - left.offset) {
                    result = left; result.offset += right.number;
                } else refuse();
            }
        } else refuse();
        visiting.erase(id); cache.emplace(id, result); return result;
    }
public:
    DescriptorAddress(DcePass &state, uint64_t address_offset, uint32_t member_offset)
        : pass(state), ir(state.ir), anchor(address_offset), push_offset(member_offset) {}
    bool push_pointer(uint32_t pointer) const { (void)push_word(pointer); return true; }
    MetalFragmentRead read(const MetalFragmentOp &load)
    {
        const auto &a = load.operands;
        if (a.size() != 5 || a[3] != spv::MemoryAccessAlignedMask || a[4] != 4 || width(load.result_type()) != 32) refuse();
        const auto &pointer = definition(a[2]); const auto *type = pass.pointer_type(a[2]);
        if (pointer.opcode != spv::OpConvertUToPtr || pointer.operands.size() != 3 || !type ||
            type->operands[1] != spv::StorageClassPhysicalStorageBuffer || type->operands[2] != load.result_type() ||
            pass.observable(a[2])) refuse();
        const Value address = value(pointer.operands[2]);
        if (address.kind != Address || address.offset % 4 || address.offset > UINT64_MAX - 4) refuse();
        return {address.user, address.offset, 4, load.offset};
    }
};
} // namespace fragment_ir_detail

/* Transactional: an uncovered fault-capable read refuses without leaving a partially pruned module. */
inline MetalFragmentDce metal_fragment_dce(MetalFragmentIR &ir, const std::vector<MetalDescriptorSpan> &spans,
                                         uint64_t address_offset, uint32_t push_offset = 64)
{
    using namespace fragment_ir_detail;
    require_supported_memory_metadata(ir);
    MetalFragmentIR candidate = ir;
    MetalFragmentDce result;
    for (size_t iteration = 0; iteration <= candidate.ops.size(); ++iteration) {
        DcePass pass(candidate);
        bool changed = false;
        for (auto &op : candidate.ops) {
            if (op.removed || op.opcode != spv::OpStore || op.operands.size() < 2 || !pass.locals.count(op.operands[0])) continue;
            bool read = false;
            for (const auto &[use, position] : pass.uses(op.operands[0]))
                read |= candidate.ops[use].opcode == spv::OpLoad && position == 2;
            if (!read) {op.removed = true; ++result.removed_stores; changed = true;}
        }
        if (changed) continue;
        DescriptorAddress addresses(pass, address_offset, push_offset);
        for (size_t i = 0; i < candidate.ops.size(); ++i) {
            auto &op = candidate.ops[i];
            if (op.removed || !pass.functions[i] || !op.result_id() || !pass.uses(op.result_id()).empty()) continue;
            bool removable = pure(op.opcode);
            if (op.opcode == spv::OpLoad) {
                removable = false;
                if (op.operands.size() < 3 || !plain_memory(op) || pass.observable(op.operands[2])) continue;
                const auto *type = pass.pointer_type(op.operands[2]);
                if (!type) continue;
                const uint32_t storage = type->operands[1];
                if (storage == spv::StorageClassFunction) removable = pass.locals.count(op.operands[2]) != 0;
                else if (storage == spv::StorageClassInput) removable = true;
                else if (storage == spv::StorageClassPushConstant) removable = addresses.push_pointer(op.operands[2]);
                else if (storage == spv::StorageClassPhysicalStorageBuffer) {
                    const MetalFragmentRead read = addresses.read(op);
                    bool covered = false;
                    for (const auto &span : spans)
                        if (span.from_memory && span.pointer_sgpr == read.pointer_sgpr && span.offset <= read.offset &&
                            span.offset <= UINT64_MAX - span.bytes && span.bytes >= read.bytes &&
                            read.offset - span.offset <= uint64_t(span.bytes - read.bytes)) covered = true;
                    if (!covered) throw std::runtime_error("fragment WQM dead physical read is outside validated descriptor spans");
                    result.removed_reads.push_back(read); removable = true;
                }
            }
            if (removable) {op.removed = true; ++result.removed_values; changed = true;}
        }
        if (!changed) {candidate.reindex(); ir = std::move(candidate); return result;}
    }
    throw std::runtime_error("fragment DCE did not converge within its instruction bound");
}
} // namespace pb
#endif
