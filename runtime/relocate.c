/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "runtime/trace.h"
#include <string.h>
#include <capstone/capstone.h>

int rt_trace_relocation_available(void) { return 1; }

/* Entry relocation and branch scans (rt_trace_relocate, rt_trace_branches_into). */

static int is_relative_branch(const cs_insn *in)
{
    for (uint8_t i = 0; i < in->detail->groups_count; i++)
        if (in->detail->groups[i] == X86_GRP_BRANCH_RELATIVE) return 1;
    return 0;
}

static uint64_t branch_target(const cs_insn *in)
{
    const cs_x86 *x = &in->detail->x86;
    return x->op_count > 0 && x->operands[0].type == X86_OP_IMM ? (uint64_t)x->operands[0].imm : 0;
}

static int fits_int32(int64_t v) { return v >= INT32_MIN && v <= INT32_MAX; }

int rt_trace_relocate(const void *src, size_t min_bytes, void *dst, size_t *copied, size_t *written)
{
    csh cs;
    if (cs_open(CS_ARCH_X86, CS_MODE_64, &cs) != CS_ERR_OK) return -1;
    cs_option(cs, CS_OPT_DETAIL, CS_OPT_ON);
    cs_insn *in = cs_malloc(cs);
    uint8_t out[RT_RELOCATE_MAX];
    size_t n = 0, done = 0;
    int ok = in != NULL;
    const uint64_t base = (uint64_t)(uintptr_t)src, to = (uint64_t)(uintptr_t)dst;
    while (ok && done < min_bytes) {
        const uint8_t *code = (const uint8_t *)src + done;
        size_t size = 16;
        uint64_t address = base + done;
        if (!cs_disasm_iter(cs, &code, &size, &address, in) || n + in->size + 14 > sizeof out) {
            ok = 0;
            break;
        }
        uint8_t *p = out + n;
        memcpy(p, in->bytes, in->size);
        const cs_x86 *x = &in->detail->x86;
        const int64_t shift = (int64_t)(base + done) - (int64_t)(to + n);
        if (is_relative_branch(in)) {
            const uint64_t target = branch_target(in);
            /* Only rel32 moves; a branch into the copied bytes would run the hook's jump. */
            if (x->encoding.imm_size != 4 || (target > base && target < base + min_bytes + 16)) {
                ok = 0;
                break;
            }
            const int64_t rel = (int64_t)target - (int64_t)(to + n + in->size);
            if (!fits_int32(rel)) { ok = 0; break; }
            const int32_t r32 = (int32_t)rel;
            memcpy(p + x->encoding.imm_offset, &r32, 4);
        }
        for (uint8_t i = 0; ok && i < x->op_count; i++) {
            if (x->operands[i].type != X86_OP_MEM || x->operands[i].mem.base != X86_REG_RIP) continue;
            int32_t disp;
            if (x->encoding.disp_size != 4) { ok = 0; break; }
            memcpy(&disp, p + x->encoding.disp_offset, 4);
            const int64_t moved = (int64_t)disp + shift;
            if (!fits_int32(moved)) { ok = 0; break; }
            disp = (int32_t)moved;
            memcpy(p + x->encoding.disp_offset, &disp, 4);
        }
        n += in->size;
        done += in->size;
    }
    if (ok) {
        /* jmp qword ptr [rip]; .quad src + done */
        const uint8_t jump[6] = {0xff, 0x25, 0, 0, 0, 0};
        const uint64_t back = base + done;
        memcpy(out + n, jump, sizeof jump);
        memcpy(out + n + 6, &back, 8);
        n += 14;
        memcpy(dst, out, n);
        *copied = done;
        *written = n;
    }
    if (in) cs_free(in, 1);
    cs_close(&cs);
    return ok ? 0 : -1;
}

int rt_trace_branches_into(const void *fn, size_t size, size_t n)
{
    csh cs;
    if (cs_open(CS_ARCH_X86, CS_MODE_64, &cs) != CS_ERR_OK) return 1;
    cs_option(cs, CS_OPT_DETAIL, CS_OPT_ON);
    cs_insn *in = cs_malloc(cs);
    const uint64_t base = (uint64_t)(uintptr_t)fn;
    int found = in == NULL;
    for (size_t off = 0; !found && off < size;) {
        const uint8_t *code = (const uint8_t *)fn + off;
        size_t left = size - off;
        uint64_t address = base + off;
        if (!cs_disasm_iter(cs, &code, &left, &address, in)) {
            off++;                          /* data or padding inside the function: skip a byte */
            continue;
        }
        if (is_relative_branch(in)) {
            const uint64_t target = branch_target(in);
            found = target > base && target < base + n;
        }
        off += in->size;
    }
    if (in) cs_free(in, 1);
    cs_close(&cs);
    return found;
}
