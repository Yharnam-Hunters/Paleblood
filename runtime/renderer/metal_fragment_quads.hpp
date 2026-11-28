/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PB_METAL_FRAGMENT_QUADS_HPP
#define PB_METAL_FRAGMENT_QUADS_HPP

#include "spirv_msl.hpp"
#include "metal_fragment_control.hpp"
#include "metal_fragment_wqm_source.hpp"
#include <cstdint>
#include <map>
#include <set>
#include <vector>
#include <utility>
#include <stdexcept>
#include <string>

namespace pb {
inline std::string metal_fragment_quad_source(const std::string &name)
{
    return "namespace " + name + " {\n" + R"(using namespace metal;
static inline uint read(uint bits, uint source, uint lane, device atomic_uint *fault)
{
    uint2 payload = lane == source ? uint2(bits, 1u) : uint2(0u);
    uint2 acquired = quad_or(payload);
    if (acquired.y != 1u) {
        atomic_fetch_or_explicit(fault, 1u, memory_order_relaxed);
        discard_fragment();
        return 0u;
    }
    return acquired.x;
}
)" + "\n}";
}

/* Quad permutations cannot read an inactive producer. An active-thread reduction instead
   acquires the requested producer's value and presence together, without an undefined read.
   The atomic word is separate from the common translator's ordinary runtime-stop word. */
class MetalFragmentQuadCompiler : public spirv_cross::CompilerMSL {
public:
    MetalFragmentQuadCompiler(const uint32_t *words, size_t count, uint64_t fault_address)
        : CompilerMSL(words, count), fault_address_(fault_address)
    {
        initialize(words, count);
    }

    MetalFragmentQuadCompiler(const MetalPreparedFragment &prepared, uint64_t fault_address)
        : CompilerMSL(prepared.words.data(), prepared.words.size()), fault_address_(fault_address), controlled_(true)
    {
        initialize(prepared.words.data(), prepared.words.size());
    }

protected:
    void emit_function_prototype(spirv_cross::SPIRFunction &function,
                                 const spirv_cross::Bitset &flags) override
    {
        if (!guarded_ || function.self != ir.default_entry_point) {
            CompilerMSL::emit_function_prototype(function, flags);
            return;
        }
        /* The pinned emitter writes one complete declaration through this supported statement
           redirection. Add a native entry-point builtin without changing the SPIR-V interface,
           platform selection, dependency source, or the generated shader body. */
        spirv_cross::SmallVector<std::string> declaration;
        auto *previous = redirect_statement;
        redirect_statement = &declaration;
        try {
            CompilerMSL::emit_function_prototype(function, flags);
        } catch (...) {
            redirect_statement = previous;
            throw;
        }
        redirect_statement = previous;
        if (declaration.size() != 1 || declaration.front().empty() || declaration.front().back() != ')')
            throw std::runtime_error("unexpected Metal fragment entry declaration");
        lane_name_ = "pb_quad_lane";
        while (resource_names.count(lane_name_) || local_variable_names.count(lane_name_)) lane_name_ += '_';
        resource_names.insert(lane_name_);
        local_variable_names.insert(lane_name_);
        std::string text = declaration.front();
        text.pop_back();
        if (text.back() != '(') text += ", ";
        text += "uint " + lane_name_ + " [[thread_index_in_quadgroup]])";
        statement(text);
    }

    void emit_subgroup_op(const spirv_cross::Instruction &instruction) override
    {
        if (guarded_ && controlled_ && instruction.op == spv::OpGroupNonUniformQuadSwap) {
            if (instruction.length != 5)
                throw std::runtime_error("invalid Boolean fragment quad swap operand count");
            const auto *operands = stream(instruction);
            const auto &type = get<spirv_cross::SPIRType>(operands[0]);
            const auto *scope = maybe_get<spirv_cross::SPIRConstant>(operands[2]);
            const auto *direction = maybe_get<spirv_cross::SPIRConstant>(operands[4]);
            if (!scalar_boolean(type) || !scalar_boolean(expression_type(operands[3])) ||
                !integer_constant(scope) || scope->scalar() != spv::ScopeSubgroup ||
                !integer_constant(direction) || direction->scalar() >= 3)
                throw std::runtime_error("a Boolean fragment quad swap needs a constant subgroup direction in 0-2");
            const std::string result = helper_namespace_ + "::swap(" + to_unpacked_expression(operands[3]) + ", " +
                std::to_string(direction->scalar() + 1) + "u, " + lane_name_ +
                ", reinterpret_cast<device atomic_uint *>(ulong(" + std::to_string(fault_address_) + "ul)))";
            /* Each call must run before a later logical OR can short-circuit its peers. */
            emit_op(operands[0], operands[1], result, false);
            inherit_expression_dependencies(operands[1], operands[3]);
            register_control_dependent_expression(operands[1]);
            return;
        }
        if (!guarded_ || instruction.op != spv::OpGroupNonUniformQuadBroadcast) {
            CompilerMSL::emit_subgroup_op(instruction);
            return;
        }
        if (instruction.length != 5)
            throw std::runtime_error("invalid fragment quad broadcast operand count");
        const auto *operands = stream(instruction);
        const auto &type = get<spirv_cross::SPIRType>(operands[0]);
        const auto *scope = maybe_get<spirv_cross::SPIRConstant>(operands[2]);
        const auto *source = maybe_get<spirv_cross::SPIRConstant>(operands[4]);
        if (!integer_constant(scope) || scope->scalar() != spv::ScopeSubgroup ||
            !integer_constant(source) ||
            get<spirv_cross::SPIRType>(source->constant_type).basetype != spirv_cross::SPIRType::UInt ||
            source->scalar() >= 4)
            throw std::runtime_error("a fragment quad read needs a constant subgroup source in 0-3");
        require_quad_type(type);
        const auto &input_type = expression_type(operands[3]);
        if (input_type.pointer || input_type.basetype != type.basetype || input_type.width != type.width ||
            input_type.vecsize != 1 || input_type.columns != 1 || !input_type.array.empty())
            throw std::runtime_error("fragment quad read input and result types differ");
        std::string input = to_unpacked_expression(operands[3]);
        if (type.basetype != spirv_cross::SPIRType::UInt) input = "as_type<uint>(" + input + ")";
        std::string result = helper_namespace_ + "::read(" + input + ", " +
            std::to_string(source->scalar()) + "u, " + lane_name_ +
            ", reinterpret_cast<device atomic_uint *>(ulong(" + std::to_string(fault_address_) + "ul)))";
        if (type.basetype != spirv_cross::SPIRType::UInt)
            result = "as_type<" + type_to_glsl(type) + ">(" + result + ")";
        /* Materialize the call before Boolean operators and selections can short-circuit it. */
        emit_op(operands[0], operands[1], result, false);
        inherit_expression_dependencies(operands[1], operands[3]);
        register_control_dependent_expression(operands[1]);
    }

private:
    void initialize(const uint32_t *words, size_t count)
    {
        if (get_execution_model() != spv::ExecutionModelFragment) {
            if (controlled_) throw std::runtime_error("prepared quad control requires a fragment entry");
            return;
        }
        for (size_t at = 5; at < count;) {
            const uint32_t extent = words[at] >> 16;
            if (!extent || extent > count - at) throw std::runtime_error("invalid fragment instruction extent");
            const auto opcode = static_cast<spv::Op>(words[at] & 0xffffu);
            if (controlled_ && (opcode == spv::OpSwitch || opcode == spv::OpLoopMerge ||
                                opcode == spv::OpGroupNonUniformAny))
                throw std::runtime_error("prepared fragment still contains unproved wave or dispatch control");
            guarded_ |= opcode == spv::OpGroupNonUniformQuadBroadcast ||
                (controlled_ && opcode == spv::OpGroupNonUniformQuadSwap);
            at += extent;
        }
        if (!guarded_) return;
        validate_control(words, count);
        if (!fault_address_ || fault_address_ % sizeof(uint32_t))
            throw std::runtime_error("a quad shader needs an aligned atomic fault word");
        for (;;) {
            bool collision = get_entry_point().name == helper_namespace_;
            for (uint32_t id = 1; id < get_current_id_bound(); ++id)
                collision |= get_name(id) == helper_namespace_;
            if (!collision) break;
            helper_namespace_ += '_';
        }
        add_header_line(metal_fragment_quad_source(helper_namespace_));
        if (controlled_) add_header_line(metal_fragment_wqm_source(helper_namespace_));
        }

    static bool scalar_boolean(const spirv_cross::SPIRType &type)
    {
        return !type.pointer && type.basetype == spirv_cross::SPIRType::Boolean &&
            type.vecsize == 1 && type.columns == 1 && type.array.empty();
    }

    void require_quad_type(const spirv_cross::SPIRType &type) const
    {
        if (type.pointer || type.width != 32 || type.vecsize != 1 || type.columns != 1 || !type.array.empty() ||
            (type.basetype != spirv_cross::SPIRType::UInt && type.basetype != spirv_cross::SPIRType::Int &&
             type.basetype != spirv_cross::SPIRType::Float))
            throw std::runtime_error("fragment quad reads require a scalar 32-bit integer or float");
    }

    bool integer_constant(const spirv_cross::SPIRConstant *constant) const
    {
        if (!constant || constant->specialization) return false;
        const auto &type = get<spirv_cross::SPIRType>(constant->constant_type);
        return !type.pointer && type.width == 32 && type.vecsize == 1 && type.columns == 1 && type.array.empty() &&
            (type.basetype == spirv_cross::SPIRType::UInt || type.basetype == spirv_cross::SPIRType::Int);
    }

    /* Raw modules retain the single-block profile. Prepared modules have their dispatcher
       removed by the empty-EXEC proof. In both forms, quad reads must run outside per-lane
       selections so helpers cannot fault alone before covered lanes later reconverge. */
    void validate_control(const uint32_t *words, size_t count)
    {
        std::map<uint32_t, std::vector<uint32_t>> edges;
        std::map<uint32_t, uint32_t> merges, loaded_from;
        struct Store { uint32_t pointer, value, block; size_t at; };
        struct Conditional { uint32_t block, yes, no; };
        std::vector<Store> stores;
        std::vector<Conditional> conditionals;
        std::set<uint32_t> reads, terminal_blocks;
        std::map<uint32_t, size_t> load_positions;
        uint32_t block = 0, first_block = 0, selector = 0, case_entry = 0, switch_block = 0;
        size_t functions = 0, switches = 0, loops = 0;
        for (size_t at = 5; at < count; at += words[at] >> 16) {
            const auto opcode = static_cast<spv::Op>(words[at] & 0xffffu);
            const uint32_t extent = words[at] >> 16;
            const auto need = [extent](uint32_t minimum) {
                if (extent < minimum) throw std::runtime_error("invalid fragment control operand count");
            };
            if (opcode == spv::OpFunction) ++functions;
            if (opcode == spv::OpFunctionCall || opcode == spv::OpPhi ||
                opcode == spv::OpDemoteToHelperInvocation || opcode == spv::OpTerminateInvocation)
                throw std::runtime_error("fragment quad reads require a single inlined entry without demotion");
            if (opcode >= spv::OpGroupNonUniformElect && opcode <= spv::OpGroupNonUniformQuadSwap &&
                opcode != spv::OpGroupNonUniformQuadBroadcast &&
                !(controlled_ && opcode == spv::OpGroupNonUniformQuadSwap)) {
                if (opcode == spv::OpGroupNonUniformQuadSwap)
                    throw std::runtime_error("whole-quad shader operations are outside the Metal draw profile");
                /* Constant Any is independent of wave width and carries no helper-derived data. */
                bool constant_any = false;
                if (opcode == spv::OpGroupNonUniformAny && extent == 5) {
                    const auto *scope = maybe_get<spirv_cross::SPIRConstant>(words[at + 3]);
                    const auto *value = maybe_get<spirv_cross::SPIRConstant>(words[at + 4]);
                    const auto &result = get<spirv_cross::SPIRType>(words[at + 1]);
                    if (integer_constant(scope) && scope->scalar() == spv::ScopeSubgroup &&
                        scalar_boolean(result) && value && !value->specialization)
                        constant_any = scalar_boolean(get<spirv_cross::SPIRType>(value->constant_type));
                }
                if (!constant_any)
                    throw std::runtime_error("wave-wide fragment operations need a separate control-flow profile");
            }
            switch (opcode) {
            case spv::OpLabel:
                need(2); block = words[at + 1]; if (!first_block) first_block = block; break;
            case spv::OpLoopMerge: ++loops; break;
            case spv::OpSelectionMerge: need(3); merges[block] = words[at + 1]; break;
            case spv::OpLoad:
                need(4); loaded_from[words[at + 2]] = words[at + 3];
                load_positions[words[at + 2]] = at; break;
            case spv::OpStore:
                need(3); stores.push_back({words[at + 1], words[at + 2], block, at}); break;
            case spv::OpSwitch:
                if (extent != 5 || words[at + 3] != 0 || ++switches != 1)
                    throw std::runtime_error("fragment quad reads need a single straight-line guest block");
                selector = words[at + 1]; case_entry = words[at + 4]; switch_block = block;
                edges[block] = {words[at + 2], words[at + 4]}; break;
            case spv::OpBranch: need(2); edges[block] = {words[at + 1]}; break;
            case spv::OpBranchConditional:
                need(4); conditionals.push_back({block, words[at + 2], words[at + 3]});
                edges[block] = {words[at + 2], words[at + 3]}; break;
            case spv::OpGroupNonUniformQuadBroadcast:
                if (extent != 6) throw std::runtime_error("invalid fragment quad broadcast operand count");
                require_quad_type(get<spirv_cross::SPIRType>(words[at + 1]));
                reads.insert(block); break;
            case spv::OpGroupNonUniformQuadSwap:
                if (!controlled_ || extent != 6 || !scalar_boolean(get<spirv_cross::SPIRType>(words[at + 1])))
                    throw std::runtime_error("Boolean fragment quad swaps require prepared control");
                reads.insert(block); break;
            case spv::OpKill: case spv::OpReturn: case spv::OpReturnValue:
                terminal_blocks.insert(block); break;
            default: break;
            }
        }
        if (functions != 1 || loops > switches)
            throw std::runtime_error("fragment quad reads require one inlined entry without guest loops");
        if (switches) {
            const auto load = loaded_from.find(selector);
            if (load == loaded_from.end()) throw std::runtime_error("unknown fragment dispatch selector");
            size_t writes = 0;
            for (const auto &store : stores) if (store.pointer == load->second) {
                const auto *value = maybe_get<spirv_cross::SPIRConstant>(store.value);
                if (store.block != first_block || store.at >= load_positions.at(selector) || !integer_constant(value) || value->scalar() != 0)
                    throw std::runtime_error("fragment quad reads cannot change the guest dispatch block");
                ++writes;
            }
            if (writes != 1) throw std::runtime_error("unknown fragment dispatch initialization");
            const auto *variable = maybe_get<spirv_cross::SPIRVariable>(load->second);
            if (!variable || variable->storage != spv::StorageClassFunction)
                throw std::runtime_error("fragment dispatch must use a local variable");
            require_dispatch_non_escape(words, count, load->second);
            edges[switch_block] = {case_entry};
        }
        /* Prune the proven-zero switch, then require every entry path to terminate. This
           includes the dispatch prefix and excludes the artificial default backedge. */
        std::map<uint32_t, unsigned> visiting;
        std::vector<std::pair<uint32_t, bool>> pending = {{first_block, false}};
        while (!pending.empty()) {
            const auto [next, leaving] = pending.back(); pending.pop_back();
            if (leaving) { visiting[next] = 2; continue; }
            if (terminal_blocks.count(next) || visiting[next] == 2) continue;
            if (visiting[next] == 1) throw std::runtime_error("a fragment quad-read case cannot loop");
            const auto successors = edges.find(next);
            if (successors == edges.end() || successors->second.empty())
                throw std::runtime_error("a fragment quad-read case must terminate");
            visiting[next] = 1; pending.push_back({next, true});
            for (uint32_t successor : successors->second) pending.push_back({successor, false});
        }
        for (const auto &conditional : conditionals) {
            const auto merge = merges.find(conditional.block);
            if (merge == merges.end()) throw std::runtime_error("unstructured fragment conditional");
            std::vector<uint32_t> pending = {conditional.yes, conditional.no};
            std::set<uint32_t> visited;
            while (!pending.empty()) {
                const uint32_t next = pending.back(); pending.pop_back();
                if (next == merge->second || !visited.insert(next).second) continue;
                if (reads.count(next))
                    throw std::runtime_error("fragment quad reads inside per-lane control are unsupported");
                const auto successors = edges.find(next);
                if (successors != edges.end())
                    pending.insert(pending.end(), successors->second.begin(), successors->second.end());
            }
        }
        /* Binary block order does not describe execution order. Every read block must
           dominate every reachable exit, including a kill block laid out after the read. */
        for (uint32_t read : reads) {
            std::vector<uint32_t> pending = {first_block};
            std::set<uint32_t> visited;
            while (!pending.empty()) {
                const uint32_t next = pending.back(); pending.pop_back();
                if (next == read || !visited.insert(next).second) continue;
                if (terminal_blocks.count(next))
                    throw std::runtime_error("fragment termination before a later quad read is unsupported");
                const auto successors = edges.find(next);
                if (successors != edges.end())
                    pending.insert(pending.end(), successors->second.begin(), successors->second.end());
            }
        }
    }

    void require_dispatch_non_escape(const uint32_t *words, size_t count, uint32_t variable)
    {
        bool function = false;
        for (size_t at = 5; at < count; at += words[at] >> 16) {
            const auto opcode = static_cast<spv::Op>(words[at] & 0xffffu);
            const uint32_t extent = words[at] >> 16;
            if (opcode == spv::OpFunction) function = true;
            if (!function || opcode == spv::OpLine || opcode == spv::OpNoLine) continue;
            for (uint32_t operand = 1; operand < extent; ++operand) {
                if (words[at + operand] != variable) continue;
                if ((opcode == spv::OpVariable && operand == 2) ||
                    (opcode == spv::OpLoad && operand == 3) ||
                    (opcode == spv::OpStore && operand == 1)) continue;
                /* These are literal fields, not uses of the pointer ID. Unknown instruction
                   layouts remain conservative: an apparent pointer use is rejected. */
                if ((opcode == spv::OpExtInst && operand == 4) ||
                    (opcode == spv::OpCompositeExtract && operand >= 4) ||
                    ((opcode == spv::OpCompositeInsert || opcode == spv::OpVectorShuffle) && operand >= 5) ||
                    (opcode == spv::OpArrayLength && operand == 4) ||
                    (opcode == spv::OpVariable && operand == 3) ||
                    (opcode == spv::OpSelectionMerge && operand == 2) ||
                    (opcode == spv::OpLoopMerge && operand >= 3) ||
                    (opcode == spv::OpBranchConditional && operand >= 4) ||
                    (opcode == spv::OpSwitch && operand >= 3 && operand % 2 == 1) ||
                    (opcode == spv::OpLoad && operand >= 4) ||
                    ((opcode == spv::OpStore || opcode == spv::OpCopyMemory) && operand >= 3) ||
                    (opcode == spv::OpCopyMemorySized && operand >= 4))
                    continue;
                throw std::runtime_error("the local fragment dispatch pointer escapes");
            }
            if (opcode == spv::OpFunctionEnd) function = false;
        }
    }

    uint64_t fault_address_;
    bool guarded_ = false;
    bool controlled_ = false;
    std::string lane_name_;
    std::string helper_namespace_ = "pb_fragment_quad";
};
}
#endif
