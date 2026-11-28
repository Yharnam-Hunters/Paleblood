/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PB_METAL_FRAGMENT_CONTROL_HPP
#define PB_METAL_FRAGMENT_CONTROL_HPP

#include "metal_fragment_ir.hpp"
#include <map>
#include <set>

namespace pb {
struct MetalPreparedFragment {
    std::vector<uint32_t> words;
    MetalFragmentDce dce;
    size_t elided_skips = 0;
    bool linearized = false;
};

namespace fragment_control_detail {
using fragment_ir_detail::DcePass;

[[noreturn]] inline void refuse(const char *reason) {throw std::runtime_error(reason);}
inline const MetalFragmentOp &definition(const MetalFragmentIR &ir, uint32_t id)
{
    const auto *op = ir.find(id);
    if (!op) refuse("fragment control references an undefined ID");
    return *op;
}
inline bool boolean(const MetalFragmentIR &ir, uint32_t type)
{
    const auto *op = ir.find(type);
    return op && op->opcode == spv::OpTypeBool && op->operands.size() == 1;
}
inline uint32_t integer_width(const MetalFragmentIR &ir, uint32_t type, bool unsigned_only = false)
{
    const auto *op = ir.find(type);
    if (!op || op->opcode != spv::OpTypeInt || op->operands.size() != 3 || op->operands[2] > 1 ||
        (unsigned_only && op->operands[2]) || (op->operands[1] != 32 && op->operands[1] != 64)) return 0;
    return op->operands[1];
}
inline bool number32(const MetalFragmentIR &ir, uint32_t type)
{
    const auto *op = ir.find(type);
    return integer_width(ir, type) == 32 ||
        (op && op->opcode == spv::OpTypeFloat && op->operands.size() == 2 && op->operands[1] == 32);
}
inline uint64_t constant(const MetalFragmentIR &ir, uint32_t id, uint32_t width = 32, bool unsigned_only = true)
{
    const auto &op = definition(ir, id);
    if ((width != 32 && width != 64) || op.opcode != spv::OpConstant || integer_width(ir, op.result_type(), unsigned_only) != width ||
        op.operands.size() != 2 + width / 32) refuse("fragment control needs an ordinary integer constant");
    return uint64_t(op.operands[2]) | (width == 64 ? uint64_t(op.operands[3]) << 32 : 0);
}
inline bool literal_boolean(const MetalFragmentIR &ir, uint32_t id)
{
    const auto *op = ir.find(id);
    return op && (op->opcode == spv::OpConstantTrue || op->opcode == spv::OpConstantFalse) &&
        op->operands.size() == 2 && boolean(ir, op->result_type());
}
inline bool value_type(const MetalFragmentIR &ir, uint32_t id, uint32_t type)
{
    const auto *value = ir.find(id);
    return value && value->result_type() == type;
}

/* Validate before DCE: a malformed or width-dependent subgroup operation must not gain
   admission merely because its result later becomes unused. No extension group operation,
   grouped decoration, subgroup identity, or requested wave width is part of this profile. */
inline void validate_metadata(MetalFragmentIR &ir)
{
    fragment_ir_detail::require_supported_memory_metadata(ir);
    DcePass pass(ir);
    size_t functions = 0, entries = 0;
    uint32_t entry_function = 0, function = 0;
    for (size_t i = 0; i < ir.ops.size(); ++i) {
        const auto &op = ir.ops[i]; const auto &a = op.operands;
        if (op.removed) continue;
        if (uint32_t(op.opcode) >= 4000 || op.opcode == spv::OpGroupDecorate || op.opcode == spv::OpDecorateId ||
            op.opcode == spv::OpGroupMemberDecorate || op.opcode == spv::OpDecorationGroup)
            refuse("fragment control does not admit extension instructions or grouped decorations");
        if (op.opcode == spv::OpEntryPoint) {
            if (a.size() < 3 || a[0] != spv::ExecutionModelFragment) refuse("fragment control needs one fragment entry");
            entry_function = a[1]; ++entries;
        }
        if (op.opcode == spv::OpFunction) {
            if (a.size() != 4 || a[2]) refuse("fragment control needs one inlined void function");
            const auto &result = definition(ir, a[0]), &signature = definition(ir, a[3]);
            if (result.opcode != spv::OpTypeVoid || result.operands.size() != 1 ||
                signature.opcode != spv::OpTypeFunction || signature.operands != std::vector<uint32_t>{a[3], a[0]})
                refuse("fragment control needs a void function without parameters");
            function = op.result_id(); ++functions;
        }
        if (op.opcode == spv::OpFunctionEnd && !a.empty()) refuse("fragment control has an invalid function end");
        if (op.opcode == spv::OpFunctionCall || op.opcode == spv::OpFunctionParameter || op.opcode == spv::OpPhi)
            refuse("fragment control does not admit calls, parameters, or phi nodes");
        if ((op.opcode >= spv::OpAtomicLoad && op.opcode <= spv::OpAtomicXor) ||
            op.opcode == spv::OpAtomicFlagTestAndSet || op.opcode == spv::OpAtomicFlagClear)
            refuse("fragment control does not admit atomic subgroup-scope or ordering assumptions");
        if ((op.opcode >= spv::OpGroupAsyncCopy && op.opcode <= spv::OpGroupSMax) ||
            (op.opcode >= spv::OpGroupReserveReadPipePackets && op.opcode <= spv::OpGroupCommitWritePipe))
            refuse("fragment control does not admit legacy group operations");
        if (op.opcode == spv::OpDecorate || op.opcode == spv::OpMemberDecorate) {
            const size_t field = op.opcode == spv::OpDecorate ? 1 : 2;
            if (a.size() <= field) refuse("fragment control has an invalid decoration");
            if (a[field] == spv::DecorationBuiltIn) {
                if (a.size() != field + 2) refuse("fragment control has an invalid builtin decoration");
                switch (a[field + 1]) {
                case spv::BuiltInFragCoord: case spv::BuiltInPointCoord: case spv::BuiltInFrontFacing:
                case spv::BuiltInSampleId: case spv::BuiltInSamplePosition: case spv::BuiltInSampleMask:
                case spv::BuiltInFragDepth: case spv::BuiltInHelperInvocation: case spv::BuiltInPrimitiveId:
                case spv::BuiltInLayer: case spv::BuiltInViewportIndex: case spv::BuiltInClipDistance: case spv::BuiltInCullDistance:
                    break;
                default: refuse("fragment control does not admit subgroup identity or unknown fragment builtins");
                }
            }
        }
        if (op.opcode == spv::OpExecutionMode || op.opcode == spv::OpExecutionModeId) {
            if (a.size() < 2) refuse("fragment control has an invalid execution mode");
            if (op.opcode != spv::OpExecutionMode || a.size() != 2 ||
                (a[1] != spv::ExecutionModePixelCenterInteger && a[1] != spv::ExecutionModeOriginUpperLeft &&
                 a[1] != spv::ExecutionModeOriginLowerLeft && a[1] != spv::ExecutionModeEarlyFragmentTests &&
                 a[1] != spv::ExecutionModeDepthReplacing && a[1] != spv::ExecutionModeDepthGreater &&
                 a[1] != spv::ExecutionModeDepthLess && a[1] != spv::ExecutionModeDepthUnchanged))
                refuse("fragment control does not admit subgroup or unknown fragment execution modes");
        }
        if (op.opcode == spv::OpVariable && pass.functions[i]) {
            if ((a.size() != 3 && a.size() != 4) || a[2] != spv::StorageClassFunction ||
                !pass.locals.count(op.result_id())) refuse("fragment control needs alias-free nonvolatile function variables");
            const auto *type = pass.pointer_type(op.result_id());
            if (!type) refuse("fragment control has an unsupported function variable type");
            const auto &pointee = definition(ir, type->operands[2]);
            bool numeric = boolean(ir, pointee.result_id()) || integer_width(ir, pointee.result_id()) || number32(ir, pointee.result_id());
            if (pointee.opcode == spv::OpTypeVector && pointee.operands.size() == 3 &&
                pointee.operands[2] >= 2 && pointee.operands[2] <= 4 && number32(ir, pointee.operands[1])) numeric = true;
            if (!numeric || (a.size() == 4 && !value_type(ir, a[3], type->operands[2])))
                refuse("fragment control has an unsupported function variable type");
            for (const auto &[use, position] : pass.uses(op.result_id())) {
                (void)position;
                const auto &access = ir.ops[use];
                if (access.opcode == spv::OpLoad && access.result_type() != type->operands[2])
                    refuse("fragment control local load has a mismatched type");
                if (access.opcode == spv::OpStore && !value_type(ir, access.operands[1], type->operands[2]))
                    refuse("fragment control local store has a mismatched type");
            }
        }
        if (op.opcode < spv::OpGroupNonUniformElect || op.opcode > spv::OpGroupNonUniformQuadSwap) continue;
        if (op.opcode != spv::OpGroupNonUniformAny && op.opcode != spv::OpGroupNonUniformQuadSwap &&
            op.opcode != spv::OpGroupNonUniformQuadBroadcast)
            refuse("fragment control does not admit observable wave operations");
        const size_t arity = op.opcode == spv::OpGroupNonUniformAny ? 4 : 5;
        if (a.size() != arity || constant(ir, a[2], 32, false) != spv::ScopeSubgroup ||
            !value_type(ir, a[3], op.result_type())) refuse("fragment control has invalid subgroup operands");
        if (op.opcode == spv::OpGroupNonUniformQuadBroadcast) {
            if (!number32(ir, op.result_type()) || constant(ir, a[4]) >= 4)
                refuse("fragment control needs a scalar 32-bit quad broadcast from a constant source");
        } else {
            if (!boolean(ir, op.result_type())) refuse("fragment control needs scalar Boolean votes and quad swaps");
            if (op.opcode == spv::OpGroupNonUniformQuadSwap && constant(ir, a[4]) > 2)
                refuse("fragment control needs a constant quad swap direction in 0-2");
        }
    }
    if (functions != 1 || entries != 1 || entry_function != function)
        refuse("fragment control needs exactly one inlined fragment entry");
    for (const auto &op : ir.ops) if (!op.removed && op.opcode == spv::OpExecutionMode && op.operands[0] != function)
        refuse("fragment execution mode targets a different function");
}
inline void fold_constant_votes(MetalFragmentIR &ir)
{
    for (auto &op : ir.ops) if (!op.removed && op.opcode == spv::OpGroupNonUniformAny &&
        literal_boolean(ir, op.operands[3])) {
        op.opcode = spv::OpCopyObject;
        op.operands = {op.operands[0], op.operands[1], op.operands[3]};
    }
    ir.reindex();
}

struct Block {
    uint32_t id = 0;
    std::vector<size_t> instructions;
    std::vector<uint32_t> edges;
    uint32_t selection = 0, loop_merge = 0, loop_continue = 0;
    size_t terminator = 0;
};
class Control {
    MetalFragmentIR &ir;
    DcePass pass;
    std::map<uint32_t, Block> blocks;
    std::vector<uint32_t> order;
    std::vector<uint32_t> instruction_block;
    std::map<uint32_t, uint32_t> cases, owner;
    std::map<uint32_t, std::set<uint32_t>> successors;
    std::map<uint32_t, size_t> pc_stores;
    std::map<uint32_t, std::set<uint32_t>> regions;
    uint32_t first = 0, dispatch = 0, loop = 0, shared = 0, continuation = 0, fallback = 0, pc = 0;
    size_t switch_index = 0;

    const Block &block(uint32_t id) const
    {
        const auto found = blocks.find(id);
        if (found == blocks.end()) refuse("fragment control branch targets an unknown block");
        return found->second;
    }
    static bool terminal(spv::Op op) {return op == spv::OpReturn || op == spv::OpKill;}
    bool scaffolding(uint32_t id) const
    {
        return id == dispatch || id == loop || id == shared || id == continuation || id == fallback;
    }
    static bool debug(spv::Op op) {return op == spv::OpNop || op == spv::OpLine || op == spv::OpNoLine;}
    std::vector<size_t> meaningful(uint32_t id) const
    {
        std::vector<size_t> result;
        for (const size_t at : block(id).instructions)
            if (ir.ops[at].opcode != spv::OpLabel && !debug(ir.ops[at].opcode)) result.push_back(at);
        return result;
    }
    void only_branch(uint32_t id, uint32_t target) const
    {
        const auto body = meaningful(id);
        if (body.size() != 1 || ir.ops[body[0]].opcode != spv::OpBranch || ir.ops[body[0]].operands != std::vector<uint32_t>{target})
            refuse("fragment control has a nontrivial dispatcher scaffold");
    }
    void acyclic(const std::set<uint32_t> &labels) const
    {
        std::map<uint32_t, size_t> indegree;
        for (const uint32_t label : labels) indegree[label] = 0;
        for (const uint32_t label : labels) for (const uint32_t next : block(label).edges)
            if (labels.count(next)) ++indegree.at(next);
        std::vector<uint32_t> ready;
        for (const auto &[label, degree] : indegree) if (!degree) ready.push_back(label);
        size_t visited = 0;
        while (!ready.empty()) {
            const uint32_t label = ready.back(); ready.pop_back(); ++visited;
            for (const uint32_t next : block(label).edges) if (labels.count(next) && !--indegree.at(next)) ready.push_back(next);
        }
        if (visited != labels.size()) refuse("fragment control does not admit guest loops");
    }
    std::set<uint32_t> reachable(uint32_t start, uint32_t stop = 0, uint32_t excluded = 0) const
    {
        std::set<uint32_t> seen;
        std::vector<uint32_t> pending{start};
        while (!pending.empty()) {
            const uint32_t label = pending.back(); pending.pop_back();
            if (label == excluded || !seen.insert(label).second || label == stop) continue;
            for (const uint32_t next : block(label).edges) pending.push_back(next);
        }
        return seen;
    }
    void build()
    {
        uint32_t current = 0; bool inside = false;
        size_t switches = 0, loops = 0;
        for (size_t i = 0; i < ir.ops.size(); ++i) {
            const auto &op = ir.ops[i]; const auto &a = op.operands;
            if (op.removed) continue;
            if (op.opcode == spv::OpFunction) {inside = true; continue;}
            if (op.opcode == spv::OpFunctionEnd) {inside = false; current = 0; continue;}
            if (!inside) continue;
            if (op.opcode == spv::OpLabel) {
                if (a.size() != 1 || (current && !blocks.at(current).terminator))
                    refuse("fragment control has an unterminated or malformed block");
                current = op.result_id(); if (!first) first = current;
                Block next; next.id = current;
                if (!blocks.emplace(current, std::move(next)).second) refuse("fragment control repeats a block");
                order.push_back(current);
            }
            if (!current) refuse("fragment control instruction is outside a block");
            auto &b = blocks.at(current);
            if (b.terminator) refuse("fragment control has an instruction after a terminator");
            b.instructions.push_back(i); instruction_block[i] = current;
            switch (op.opcode) {
            case spv::OpSelectionMerge:
                if (a.size() != 2 || a[1] || b.selection || b.loop_merge) refuse("fragment control has an invalid selection merge");
                b.selection = a[0]; break;
            case spv::OpLoopMerge:
                if (a.size() != 3 || a[2] || b.loop_merge || b.selection || ++loops != 1)
                    refuse("fragment control needs a single ordinary dispatcher loop");
                b.loop_merge = a[0]; b.loop_continue = a[1]; loop = current; break;
            case spv::OpBranch:
                if (a.size() != 1) refuse("fragment control has an invalid branch");
                b.edges = {a[0]}; b.terminator = i; break;
            case spv::OpBranchConditional:
                if (a.size() != 3 && a.size() != 5) refuse("fragment control has an invalid conditional branch");
                if (!boolean(ir, definition(ir, a[0]).result_type())) refuse("fragment branch predicate is not Boolean");
                b.edges = {a[1], a[2]}; b.terminator = i; break;
            case spv::OpSwitch:
                if (a.size() < 4 || a.size() % 2 || ++switches != 1 ||
                    integer_width(ir, definition(ir, a[0]).result_type(), true) != 32)
                    refuse("fragment control needs one UInt32 guest dispatcher");
                dispatch = current; switch_index = i; fallback = a[1]; b.edges.push_back(fallback);
                for (size_t p = 2; p < a.size(); p += 2) {
                    if (!cases.emplace(a[p], a[p + 1]).second) refuse("fragment control repeats a guest case");
                    b.edges.push_back(a[p + 1]);
                }
                b.terminator = i; break;
            case spv::OpReturn: case spv::OpKill: case spv::OpUnreachable:
                if (!a.empty()) refuse("fragment control has an invalid terminal instruction");
                b.terminator = i; break;
            case spv::OpReturnValue: case spv::OpFunctionCall: case spv::OpPhi:
            case spv::OpControlBarrier: case spv::OpMemoryBarrier:
                refuse("fragment control has unsupported function or synchronization control");
            default:
                if (!fragment_ir_detail::result_layout(op.opcode) && !debug(op.opcode) &&
                    op.opcode != spv::OpStore && op.opcode != spv::OpImageWrite && op.opcode != spv::OpAtomicStore)
                    refuse("fragment control has an unknown function instruction");
                break;
            }
        }
        if (!first || blocks.empty()) refuse("fragment control needs a function body");
        for (const auto &[label, b] : blocks) {
            (void)label;
            if (!b.terminator) refuse("fragment control has an unterminated block");
            for (const uint32_t next : b.edges) (void)block(next);
            if (b.selection) (void)block(b.selection);
            if (b.loop_merge) {(void)block(b.loop_merge); (void)block(b.loop_continue);}
        }
        if (!switches) {
            if (loops) refuse("fragment control does not admit guest loops");
            std::set<uint32_t> all(order.begin(), order.end()); acyclic(all);
        } else if (loops != 1) refuse("fragment control needs the ordinary guest dispatcher loop");
    }
    void dispatcher()
    {
        const auto &s = ir.ops[switch_index];
        const auto &load = definition(ir, s.operands[0]);
        if (load.opcode != spv::OpLoad || load.operands.size() != 3 || !pass.locals.count(load.operands[2]) ||
            instruction_block.at(size_t(&load - ir.ops.data())) != dispatch)
            refuse("fragment dispatcher must load an alias-free local PC");
        pc = load.operands[2];
        const auto *type = pass.pointer_type(pc);
        const auto &variable = definition(ir, pc);
        if (!type || integer_width(ir, type->operands[2], true) != 32 || variable.operands.size() != 3)
            refuse("fragment dispatcher needs an uninitialized UInt32 local PC");
        shared = block(dispatch).selection; continuation = block(loop).loop_continue;
        const uint32_t loop_exit = block(loop).loop_merge;
        const std::set<uint32_t> distinct{first, loop, dispatch, shared, continuation, fallback, loop_exit};
        if (!shared || distinct.size() != 7) refuse("fragment control has overlapping dispatcher scaffolding");
        only_branch(shared, continuation); only_branch(continuation, loop); only_branch(fallback, shared);
        const auto header = meaningful(loop), dispatch_body = meaningful(dispatch);
        if (header.size() != 2 || ir.ops[header[0]].opcode != spv::OpLoopMerge ||
            ir.ops[header[1]].opcode != spv::OpBranch || block(loop).edges != std::vector<uint32_t>{dispatch} ||
            dispatch_body.size() != 3 || dispatch_body[0] != size_t(&load - ir.ops.data()) ||
            ir.ops[dispatch_body[1]].opcode != spv::OpSelectionMerge || dispatch_body[2] != switch_index ||
            ir.ops[block(first).terminator].opcode != spv::OpBranch || block(first).edges != std::vector<uint32_t>{loop} ||
            block(first).selection || block(first).loop_merge)
            refuse("fragment control has an unsupported dispatcher entry or header");
        uint32_t expected = 0;
        std::set<uint32_t> entries;
        for (const auto &[number, label] : cases) {
            if (number != expected++ || scaffolding(label) || label == first || label == loop_exit || !entries.insert(label).second)
                refuse("fragment control needs distinct contiguous guest cases starting at zero");
        }
        for (const auto &[number, entry] : cases) {
            auto labels = reachable(entry, shared); labels.erase(shared);
            for (const uint32_t label : labels) {
                if (scaffolding(label) || label == first || label == loop_exit || (entries.count(label) && label != entry) ||
                    !owner.emplace(label, number).second) refuse("fragment control has overlapping or escaping guest cases");
            }
            acyclic(labels);
            for (const uint32_t label : labels) {
                const auto &b = block(label);
                if (b.loop_merge || (b.selection && !labels.count(b.selection)) ||
                    ir.ops[b.terminator].opcode == spv::OpSwitch || ir.ops[b.terminator].opcode == spv::OpUnreachable)
                    refuse("fragment control has an escaping case selection or guest loop");
                for (const uint32_t next : b.edges) if (next != shared && !labels.count(next))
                    refuse("fragment control has an escaping guest branch");
            }
            regions.emplace(number, std::move(labels));
        }
        size_t initialized = 0;
        for (const auto &[use, position] : pass.uses(pc)) {
            const auto &op = ir.ops[use];
            if (op.opcode == spv::OpLoad && position == 2 && use == size_t(&load - ir.ops.data())) continue;
            if (op.opcode != spv::OpStore || position || !fragment_ir_detail::plain_memory(op) ||
                op.operands.size() < 2 || !value_type(ir, op.operands[1], type->operands[2]))
                refuse("fragment PC escapes direct dispatcher stores");
            const uint32_t label = instruction_block[use];
            if (label == first) {
                if (constant(ir, op.operands[1]) || ++initialized != 1)
                    refuse("fragment PC must be initialized to zero exactly once");
            } else {
                const auto found = owner.find(label);
                if (found == owner.end() || !pc_stores.emplace(found->second, use).second)
                    refuse("fragment case must assign its PC exactly once");
            }
        }
        if (initialized != 1) refuse("fragment PC lacks its zero initialization");
        for (const auto &[number, labels] : regions) {
            bool leaves = false, ends = false;
            size_t exits = 0;
            for (const uint32_t label : labels) {
                const auto &b = block(label);
                const bool exits_to_shared = std::find(b.edges.begin(), b.edges.end(), shared) != b.edges.end();
                if (exits_to_shared) {
                    ++exits;
                    if (ir.ops[b.terminator].opcode != spv::OpBranch)
                        refuse("fragment case requires an unconditional shared-merge exit");
                }
                leaves |= exits_to_shared;
                ends |= terminal(ir.ops[b.terminator].opcode);
            }
            if (number + 1 == cases.size()) {
                if (leaves || !ends || pc_stores.count(number)) refuse("fragment final case must terminate without a PC write");
                successors[number] = {}; continue;
            }
            if (!leaves || exits != 1 || ends || !pc_stores.count(number))
                refuse("fragment forward case must exit through one PC assignment");
            const size_t store = pc_stores.at(number);
            if (reachable(cases.at(number), shared, instruction_block[store]).count(shared))
                refuse("fragment PC assignment does not dominate all case exits");
            const auto &assigned = definition(ir, ir.ops[store].operands[1]);
            std::set<uint32_t> next;
            if (assigned.opcode == spv::OpConstant) next.insert(uint32_t(constant(ir, assigned.result_id())));
            else if (assigned.opcode == spv::OpSelect && assigned.operands.size() == 5) {
                next.insert(uint32_t(constant(ir, assigned.operands[3])));
                next.insert(uint32_t(constant(ir, assigned.operands[4])));
            } else refuse("fragment PC assignment must select constant forward cases");
            for (const uint32_t target : next) if (target <= number || !cases.count(target))
                refuse("fragment PC assignment is not a forward guest edge");
            successors[number] = std::move(next);
        }
        /* Cases communicate only through the checked Function variables. In particular, a value
           computed in an elided body cannot escape through SSA into a later case or scaffold. */
        for (size_t i = 0; i < ir.ops.size(); ++i) {
            const auto &op = ir.ops[i];
            if (op.removed || !op.result_id() || op.opcode == spv::OpLabel || !owner.count(instruction_block[i])) continue;
            const uint32_t source = owner.at(instruction_block[i]);
            for (const auto &[use, position] : pass.uses(op.result_id())) {
                (void)position;
                if (pass.functions[use] && (!owner.count(instruction_block[use]) || owner.at(instruction_block[use]) != source))
                    refuse("fragment SSA value escapes its guest case");
            }
        }
        /* The only paths into a case are dispatcher entries or branches inside that same case.
           This also rules out an unreachable prefix/scaffold changing the PC on a hidden edge. */
        for (const auto &[label, b] : blocks) for (const uint32_t next : b.edges) if (owner.count(next)) {
            if (label != dispatch && (!owner.count(label) || owner.at(label) != owner.at(next)))
                refuse("fragment case has an external incoming branch");
        }
    }

    struct Value {
        enum Kind { Entry, Ssa, False, True, Literal } kind = Ssa;
        uint32_t id = 0;
        bool operator==(const Value &other) const {return kind == other.kind && id == other.id;}
        bool known() const {return kind == False || kind == True;}
    };
    Value literal(uint32_t id) const
    {
        const auto &op = definition(ir, id);
        if (literal_boolean(ir, id)) return {op.opcode == spv::OpConstantTrue ? Value::True : Value::False, 0};
        return {op.opcode == spv::OpConstant ? Value::Literal : Value::Ssa, id};
    }
    std::set<uint32_t> future_reads(uint32_t start) const
    {
        std::set<uint32_t> seen, result;
        std::vector<uint32_t> pending{start};
        while (!pending.empty()) {
            const uint32_t number = pending.back(); pending.pop_back();
            if (!seen.insert(number).second) continue;
            for (const uint32_t next : successors.at(number)) pending.push_back(next);
            for (const uint32_t label : regions.at(number)) for (const size_t at : block(label).instructions) {
                const auto &op = ir.ops[at];
                if (op.opcode == spv::OpLoad && op.operands.size() >= 3 && pass.locals.count(op.operands[2]))
                    result.insert(op.operands[2]);
            }
        }
        return result;
    }
    /* Only total scalar operations are needed on the newly visited empty path. Integer shifts
       require a literal in range; division, conversions from float, extinsts, in-bounds address
       promises and all memory/image/synchronization operations remain outside this proof. */
    void pure_operation(const MetalFragmentOp &op) const
    {
        const auto &a = op.operands;
        const auto same = [&](size_t count) {
            if (a.size() != count) refuse("fragment empty path has an invalid pure operand count");
            for (size_t p = 2; p < count; ++p) if (!value_type(ir, a[p], op.result_type()))
                refuse("fragment empty path has mismatched scalar operands");
        };
        const auto unsigned_scalar = [&] {
            if (!integer_width(ir, op.result_type(), true)) refuse("fragment empty path needs unsigned scalar arithmetic");
        };
        switch (op.opcode) {
        case spv::OpCopyObject: same(3); break;
        case spv::OpLogicalNot: same(3); if (!boolean(ir, op.result_type())) refuse("fragment logical operand is not Boolean"); break;
        case spv::OpLogicalAnd: case spv::OpLogicalOr: case spv::OpLogicalEqual: case spv::OpLogicalNotEqual:
            same(4); if (!boolean(ir, op.result_type())) refuse("fragment logical operand is not Boolean"); break;
        case spv::OpSelect:
            if (a.size() != 5 || !boolean(ir, definition(ir, a[2]).result_type()) ||
                !value_type(ir, a[3], op.result_type()) || !value_type(ir, a[4], op.result_type()) ||
                (!boolean(ir, op.result_type()) && !integer_width(ir, op.result_type()) && !number32(ir, op.result_type())))
                refuse("fragment empty path has an invalid scalar selection");
            break;
        case spv::OpIAdd: case spv::OpISub: case spv::OpIMul:
        case spv::OpBitwiseAnd: case spv::OpBitwiseOr: case spv::OpBitwiseXor:
            same(4); unsigned_scalar(); break;
        case spv::OpNot: same(3); unsigned_scalar(); break;
        case spv::OpShiftLeftLogical: case spv::OpShiftRightLogical:
            if (a.size() != 4 || !value_type(ir, a[2], op.result_type())) refuse("fragment empty path has an invalid shift");
            unsigned_scalar();
            if (constant(ir, a[3], integer_width(ir, definition(ir, a[3]).result_type(), true)) >=
                integer_width(ir, op.result_type(), true)) refuse("fragment empty path has an out-of-range shift");
            break;
        case spv::OpUConvert:
            if (a.size() != 3 || !integer_width(ir, definition(ir, a[2]).result_type(), true))
                refuse("fragment empty path has an invalid unsigned conversion");
            unsigned_scalar(); break;
        case spv::OpBitcast: {
            if (a.size() != 3) refuse("fragment empty path has an invalid bitcast");
            const uint32_t input = definition(ir, a[2]).result_type();
            const uint32_t output_width = number32(ir, op.result_type()) ? 32 : integer_width(ir, op.result_type());
            const uint32_t input_width = number32(ir, input) ? 32 : integer_width(ir, input);
            if (!output_width || input_width != output_width) refuse("fragment empty path has an unsupported bitcast");
            break;
        }
        case spv::OpFAdd: case spv::OpFSub: case spv::OpFMul: {
            same(4); const auto &type = definition(ir, op.result_type());
            if (type.opcode != spv::OpTypeFloat || type.operands.size() != 2 || type.operands[1] != 32)
                refuse("fragment empty path needs scalar float arithmetic");
            break;
        }
        case spv::OpCompositeConstruct: {
            const auto &type = definition(ir, op.result_type());
            if (type.opcode != spv::OpTypeVector || type.operands.size() != 3 || type.operands[2] < 2 || type.operands[2] > 4 ||
                a.size() != size_t(type.operands[2]) + 2 || !number32(ir, type.operands[1]))
                refuse("fragment empty path has an unsupported composite construction");
            for (size_t p = 2; p < a.size(); ++p) if (!value_type(ir, a[p], type.operands[1]))
                refuse("fragment empty path has mismatched composite components");
            break;
        }
        case spv::OpCompositeExtract: {
            if (a.size() != 4) refuse("fragment empty path has an unsupported composite extraction");
            const auto &type = definition(ir, definition(ir, a[2]).result_type());
            if (type.opcode != spv::OpTypeVector || type.operands.size() != 3 || type.operands[2] < 2 || type.operands[2] > 4 ||
                a[3] >= type.operands[2] || op.result_type() != type.operands[1] || !number32(ir, op.result_type()))
                refuse("fragment empty path has an invalid composite index");
            break;
        }
        case spv::OpConvertUToPtr: {
            if (a.size() != 3 || integer_width(ir, definition(ir, a[2]).result_type(), true) != 64)
                refuse("fragment empty path has an invalid address conversion");
            const auto &type = definition(ir, op.result_type());
            if (type.opcode != spv::OpTypePointer || type.operands.size() != 3 || type.operands[1] != spv::StorageClassPhysicalStorageBuffer)
                refuse("fragment empty path has an unsupported address space");
            break;
        }
        case spv::OpGroupNonUniformAny: case spv::OpGroupNonUniformQuadSwap: case spv::OpGroupNonUniformQuadBroadcast:
            break; // Exact arity, scope, scalar type and literal direction/source were checked before DCE.
        default: refuse("fragment empty path contains an unproved operation");
        }
    }
    void empty_identity(uint32_t number, uint32_t mask, uint32_t join) const
    {
        std::map<uint32_t, Value> initial, state, values;
        for (const uint32_t local : pass.locals) initial[local] = {Value::Entry, local};
        initial.at(mask) = {Value::False, 0}; state = initial;
        const auto get = [&](uint32_t id) {
            const auto found = values.find(id);
            return found == values.end() ? literal(id) : found->second;
        };
        uint32_t label = cases.at(number);
        std::set<uint32_t> seen;
        while (label != shared) {
            if (!regions.at(number).count(label) || !seen.insert(label).second)
                refuse("fragment empty path escapes its case or loops");
            uint32_t next = 0;
            for (const size_t at : block(label).instructions) {
                const auto &op = ir.ops[at]; const auto &a = op.operands;
                Value value{Value::Ssa, op.result_id()};
                switch (op.opcode) {
                case spv::OpLabel: case spv::OpSelectionMerge: case spv::OpNop: case spv::OpLine: case spv::OpNoLine: break;
                case spv::OpLoad:
                    if (a.size() < 3 || !pass.locals.count(a[2]) || !fragment_ir_detail::plain_memory(op) || pass.observable(a[2]))
                        refuse("fragment empty path still performs an external or observable read");
                    value = state.at(a[2]); break;
                case spv::OpStore:
                    if (a.size() < 2 || !pass.locals.count(a[0]) || !fragment_ir_detail::plain_memory(op) || pass.observable(a[0]))
                        refuse("fragment empty path performs an external or observable store");
                    state.at(a[0]) = get(a[1]); break;
                case spv::OpBranch: next = a[0]; break;
                case spv::OpBranchConditional: {
                    const Value condition = get(a[0]);
                    if (!condition.known()) refuse("fragment empty path has an unresolved conditional");
                    next = a[condition.kind == Value::True ? 1 : 2]; break;
                }
                default: {
                    pure_operation(op);
                    if (op.opcode == spv::OpCopyObject) value = get(a[2]);
                    else if (op.opcode == spv::OpSelect) {
                        const Value condition = get(a[2]), yes = get(a[3]), no = get(a[4]);
                        value = condition.kind == Value::True ? yes : condition.kind == Value::False ? no : yes == no ? yes : value;
                    } else if (op.opcode == spv::OpLogicalNot) {
                        const Value operand = get(a[2]);
                        if (operand.known()) value = {operand.kind == Value::False ? Value::True : Value::False, 0};
                    } else if (op.opcode == spv::OpLogicalAnd || op.opcode == spv::OpLogicalOr) {
                        const Value left = get(a[2]), right = get(a[3]);
                        if (op.opcode == spv::OpLogicalAnd) {
                            if (left.kind == Value::False || right.kind == Value::False) value = {Value::False, 0};
                            else if (left.kind == Value::True) value = right;
                            else if (right.kind == Value::True) value = left;
                        } else {
                            if (left.kind == Value::True || right.kind == Value::True) value = {Value::True, 0};
                            else if (left.kind == Value::False) value = right;
                            else if (right.kind == Value::False) value = left;
                        }
                    } else if (op.opcode == spv::OpGroupNonUniformAny || op.opcode == spv::OpGroupNonUniformQuadSwap ||
                               op.opcode == spv::OpGroupNonUniformQuadBroadcast) {
                        const Value operand = get(a[3]);
                        if (operand.known() || operand.kind == Value::Literal) value = operand;
                    }
                    break;
                }
                }
                if (op.result_id() && op.opcode != spv::OpLabel) values[op.result_id()] = value;
            }
            if (!next) refuse("fragment empty path terminates before its join");
            label = next;
        }
        for (const uint32_t local : future_reads(join)) if (local != pc && !(state.at(local) == initial.at(local)))
            refuse("fragment empty path changes a live function variable");
        const Value assigned = state.at(pc);
        if (assigned.kind != Value::Literal || constant(ir, assigned.id) != join)
            refuse("fragment empty path does not reach its proved next guest case");
    }
    struct Elision {size_t store; uint32_t body_constant, source, body;};
    std::vector<Elision> prove_skips() const
    {
        std::vector<Elision> result;
        for (size_t i = 0; i < ir.ops.size(); ++i) {
            const auto &vote = ir.ops[i];
            if (vote.removed || vote.opcode != spv::OpGroupNonUniformAny) continue;
            if (!owner.count(instruction_block[i])) refuse("fragment live wave vote is outside a guest case");
            const uint32_t source = owner.at(instruction_block[i]);
            auto consumers = pass.uses(vote.result_id());
            if (consumers.size() != 1) refuse("fragment live wave vote has an observable result");
            size_t selected = consumers[0].first, inverse = 0; bool negated = false;
            if (ir.ops[selected].opcode == spv::OpLogicalNot && consumers[0].second == 2 && ir.ops[selected].operands.size() == 3) {
                negated = true; inverse = selected; consumers = pass.uses(ir.ops[selected].result_id());
                if (consumers.size() != 1) refuse("fragment inverted wave vote escapes its PC selection");
                selected = consumers[0].first;
            }
            const auto &select = ir.ops[selected];
            if (select.opcode != spv::OpSelect || select.operands.size() != 5 || consumers[0].second != 2 ||
                integer_width(ir, select.result_type(), true) != 32 || !pc_stores.count(source))
                refuse("fragment live wave vote is not an empty-mask forward skip");
            const auto &uses = pass.uses(select.result_id()); const size_t store = pc_stores.at(source);
            if (uses.size() != 1 || uses[0] != std::make_pair(store, size_t(1)))
                refuse("fragment wave-controlled PC value has another observer");
            const uint32_t body_id = select.operands[negated ? 4 : 3], join_id = select.operands[negated ? 3 : 4];
            const uint32_t body = uint32_t(constant(ir, body_id)), join = uint32_t(constant(ir, join_id));
            if (source > UINT32_MAX - 2 || body != source + 1 || join != body + 1 || !cases.count(body) || !cases.count(join) ||
                successors.at(body) != std::set<uint32_t>{join})
                refuse("fragment wave skip must bypass exactly one forward guest case");
            const auto &predicate = definition(ir, vote.operands[3]);
            if (predicate.opcode != spv::OpLoad || predicate.operands.size() != 3 || !pass.locals.count(predicate.operands[2]) ||
                !boolean(ir, predicate.result_type())) refuse("fragment wave skip does not read a Boolean EXEC local");
            const size_t load = size_t(&predicate - ir.ops.data()); const uint32_t label = instruction_block[i];
            if (instruction_block[load] != label || instruction_block[selected] != label || instruction_block[store] != label ||
                load >= i || i >= selected || selected >= store ||
                (negated && (instruction_block[inverse] != label || inverse <= i || inverse >= selected ||
                    !boolean(ir, ir.ops[inverse].result_type()) || !value_type(ir, ir.ops[inverse].operands[2], ir.ops[inverse].result_type()))))
                refuse("fragment wave skip must be evaluated in its exiting block");
            for (const size_t at : block(label).instructions) if (at > load && at < store && ir.ops[at].opcode == spv::OpStore &&
                ir.ops[at].operands[0] == predicate.operands[2]) refuse("fragment EXEC changes after the skip predicate load");
            empty_identity(body, predicate.operands[2], join);
            result.push_back({store, body_id, source, body});
        }
        return result;
    }
    void remove_dispatcher()
    {
        std::set<uint32_t> chain;
        uint32_t current = 0;
        while (chain.insert(current).second) {
            const auto &next = successors.at(current);
            if (next.empty()) break;
            if (next.size() != 1) refuse("fragment control still has a wave-dependent case edge");
            current = *next.begin();
        }
        if (chain.size() != cases.size() || !successors.at(current).empty())
            refuse("fragment control is not one complete fixed forward case chain");
        auto &entry_branch = ir.ops[block(first).terminator]; entry_branch.operands[0] = cases.at(0);
        blocks.at(first).edges = {cases.at(0)};
        for (const auto &[number, labels] : regions) for (const uint32_t label : labels) {
            auto &b = blocks.at(label);
            for (uint32_t &next : b.edges) if (next == shared) {
                if (successors.at(number).size() != 1) refuse("fragment case exit lacks a fixed successor");
                const uint32_t target = cases.at(*successors.at(number).begin());
                auto &terminator = ir.ops[b.terminator];
                if (terminator.opcode == spv::OpBranch) terminator.operands[0] = target;
                else if (terminator.opcode == spv::OpBranchConditional) {
                    for (size_t p = 1; p < 3; ++p) if (terminator.operands[p] == shared) terminator.operands[p] = target;
                } else refuse("fragment case has an unsupported shared-merge exit");
                next = target;
            }
        }
        const auto live = reachable(first);
        std::set<uint32_t> removed_ids;
        for (size_t i = 0; i < ir.ops.size(); ++i) if (!ir.ops[i].removed && instruction_block[i] &&
            !live.count(instruction_block[i])) {
            if (ir.ops[i].result_id()) removed_ids.insert(ir.ops[i].result_id());
            ir.ops[i].removed = true;
        }
        for (auto &op : ir.ops) if (!op.removed && (op.opcode == spv::OpName || op.opcode == spv::OpDecorate) &&
            !op.operands.empty() && removed_ids.count(op.operands[0])) op.removed = true;
        /* Switch case bodies need not have been emitted in numerical order. The new forward
           edges make earlier cases dominate later ones, so emit the fixed chain in that order.
           Each case retains its internal block order and all selections. Original instruction
           offsets remain provenance, not an execution-order shortcut. */
        std::vector<uint32_t> schedule{first};
        for (const auto &[number, entry] : cases) {
            (void)entry;
            for (const uint32_t label : order) if (live.count(label) && owner.count(label) && owner.at(label) == number)
                schedule.push_back(label);
        }
        if (std::set<uint32_t>(schedule.begin(), schedule.end()) != live)
            refuse("fragment fixed chain contains an unscheduled live block");
        std::vector<MetalFragmentOp> ordered;
        ordered.reserve(ir.ops.size());
        bool inserted = false;
        for (size_t i = 0; i < ir.ops.size(); ++i) {
            if (instruction_block[i]) {
                if (!inserted) {
                    for (const uint32_t label : schedule) for (const size_t at : block(label).instructions)
                        if (!ir.ops[at].removed) ordered.push_back(ir.ops[at]);
                    inserted = true;
                }
            } else if (!ir.ops[i].removed) ordered.push_back(ir.ops[i]);
        }
        ir.ops = std::move(ordered); ir.reindex();
    }
public:
    explicit Control(MetalFragmentIR &module) : ir(module), pass(module), instruction_block(module.ops.size()) {build();}
    bool normalize(size_t &elided)
    {
        if (!dispatch) return false;
        dispatcher();
        const auto proofs = prove_skips();
        for (const auto &proof : proofs) {
            ir.ops[proof.store].operands[1] = proof.body_constant;
            successors.at(proof.source) = {proof.body};
        }
        remove_dispatcher(); elided += proofs.size(); return true;
    }
};
inline void append(MetalFragmentDce &to, const MetalFragmentDce &from)
{
    to.removed_stores += from.removed_stores; to.removed_values += from.removed_values;
    to.removed_reads.insert(to.removed_reads.end(), from.removed_reads.begin(), from.removed_reads.end());
}
} // namespace fragment_control_detail

/* Normalize only proved empty-mask skips. The host spans certify fault-free removed descriptor
   reads; they do not authorize arbitrary guest loads. Always visiting an identity body removes
   the need for a native wave vote. Removing the artificial loop then exposes the physical CFG
   to the separate quad-participation and per-lane-read admission checks. */
inline MetalPreparedFragment prepare_metal_fragment(const uint32_t *words, size_t count,
    const std::vector<MetalDescriptorSpan> &spans, uint64_t address_offset, uint32_t push_offset = 64)
{
    using namespace fragment_control_detail;
    MetalFragmentIR ir(words, count);
    validate_metadata(ir); fold_constant_votes(ir);
    MetalPreparedFragment result;
    result.dce = metal_fragment_dce(ir, spans, address_offset, push_offset);
    Control control(ir);
    result.linearized = control.normalize(result.elided_skips);
    append(result.dce, metal_fragment_dce(ir, spans, address_offset, push_offset));
    for (const auto &op : ir.ops) if (!op.removed && (op.opcode == spv::OpGroupNonUniformAny ||
        op.opcode == spv::OpSwitch || op.opcode == spv::OpLoopMerge))
        refuse("fragment control retains an observable wave vote or guest dispatcher");
    result.words = ir.serialize(); return result;
}
} // namespace pb
#endif
