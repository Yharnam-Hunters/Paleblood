/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Single-step tracing of guest code (runtime/include/runtime/trace.h).

   The call runs on the calling thread with the trap flag set, so the processor stops after every
   instruction and the kernel delivers SIGTRAP. The handler decodes the next instruction before it
   runs, works out the memory each operand will read or write from the saved registers, and notes
   it in tables allocated before the call (a signal handler may not allocate). Values are read
   with process_vm_readv on our own process, so an address that would fault is reported instead
   of crashing the handler. */
#define _GNU_SOURCE
#include "runtime/trace.h"

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <ucontext.h>
#include <unistd.h>

#include <capstone/capstone.h>

enum {
    TABLE_BITS = 20,                       /* bytes noted per call: up to half of 2^20 */
    QWORD_LIMIT = 1 << 16,
    PRIVATE_STACK = 16 << 20,
    TRAP_FLAG = 0x100,
    DIRECTION_FLAG = 0x400,
    IMPORT_CALL_LIMIT = 256,
    WRITE_LIMIT = 4096,
    WRITE_BYTES = 1 << 20,
    SNAPSHOT = 4096,                       /* bytes compared behind each pointer argument */
};
#define DEFAULT_STEP_LIMIT 200000ull

/* rt_trace_frame: the layout runtime/trace_entry.S reads and writes. */
typedef struct {
    uint64_t entry, stack_top;
    uint64_t gpr[6];
    uint8_t xmm[8][16];
    uint64_t host_rsp;
    uint64_t rax, rdx;
    uint8_t xmm0[16], xmm1[16];
} rt_trace_frame;
_Static_assert(__builtin_offsetof(rt_trace_frame, host_rsp) == 192, "frame layout");
_Static_assert(__builtin_offsetof(rt_trace_frame, xmm1) == 232, "frame layout");

void rt_trace_enter(rt_trace_frame *frame);
void rt_trace_landing(void);

typedef struct {
    uint64_t addr;
    uint8_t value, flags;
} slot;

typedef struct {
    csh cs;
    cs_insn *insn;
    rt_code_range code[RT_TRACE_MAX_RANGES];
    size_t ranges;
    uintptr_t stack_lo, stack_top, args_lo, args_hi, stack_hi;
    slot *table;
    size_t used;
    uint64_t *qword;
    size_t qwords;
    unsigned stack_slots;
    uint64_t steps, step_limit;
    unsigned xmm_written;              /* bit n: xmm n written by the traced code */
    const char *incomplete;
    pid_t pid;
    /* The import running now (between its entry and the return pad). */
    int in_import;
    uint64_t import_return;
    uint64_t import_rsp;                /* rsp at the import's entry: its return slot */
    uint64_t snap_ptr[6];
    size_t snap_len[6];
    uint8_t *snap;                     /* 6 * SNAPSHOT bytes */
    rt_trace_import_call *calls;
    size_t ncalls;
    rt_trace_write *writes;
    size_t nwrites;
    uint8_t *write_bytes;
    size_t write_used;
} trace_state;

static __thread trace_state *current;
static const rt_trace_redirect *redirects;
static size_t redirect_count;
/* Imports by the native address their slot holds, and their slots (reads of a slot are not
   inputs: the verifier binds its own stub there). */
typedef struct {
    uint64_t target, slot;
    const char *name;
} import_target;
static import_target *imports;
static size_t import_count;
static uint8_t *return_pad;            /* an int3 the imports return to */
static struct sigaction previous;
static pthread_once_t installed = PTHREAD_ONCE_INIT;
static int install_error;

int rt_trace_available(void) { return 1; }

static void stop(trace_state *t, ucontext_t *uc, const char *why)
{
    if (!t->incomplete)
        t->incomplete = why;
    uc->uc_mcontext.gregs[REG_EFL] &= ~(greg_t)TRAP_FLAG;
}

/* The value of a register used in an address, as the instruction about to run will see it. */
static int reg_value(const ucontext_t *uc, x86_reg reg, uint64_t next_rip, uint64_t *out)
{
    const greg_t *g = uc->uc_mcontext.gregs;
    static const struct { x86_reg r64, r32; int greg; } map[] = {
        {X86_REG_RAX, X86_REG_EAX, REG_RAX}, {X86_REG_RBX, X86_REG_EBX, REG_RBX},
        {X86_REG_RCX, X86_REG_ECX, REG_RCX}, {X86_REG_RDX, X86_REG_EDX, REG_RDX},
        {X86_REG_RSI, X86_REG_ESI, REG_RSI}, {X86_REG_RDI, X86_REG_EDI, REG_RDI},
        {X86_REG_RBP, X86_REG_EBP, REG_RBP}, {X86_REG_RSP, X86_REG_ESP, REG_RSP},
        {X86_REG_R8, X86_REG_R8D, REG_R8}, {X86_REG_R9, X86_REG_R9D, REG_R9},
        {X86_REG_R10, X86_REG_R10D, REG_R10}, {X86_REG_R11, X86_REG_R11D, REG_R11},
        {X86_REG_R12, X86_REG_R12D, REG_R12}, {X86_REG_R13, X86_REG_R13D, REG_R13},
        {X86_REG_R14, X86_REG_R14D, REG_R14}, {X86_REG_R15, X86_REG_R15D, REG_R15},
    };
    if (reg == X86_REG_INVALID) {
        *out = 0;
        return 0;
    }
    if (reg == X86_REG_RIP) {
        *out = next_rip;
        return 0;
    }
    for (size_t i = 0; i < sizeof map / sizeof map[0]; i++) {
        if (reg == map[i].r64) {
            *out = (uint64_t)g[map[i].greg];
            return 0;
        }
        if (reg == map[i].r32) {
            *out = (uint64_t)g[map[i].greg] & 0xffffffffu;
            return 0;
        }
    }
    return -1;
}

static int read_self(trace_state *t, uint64_t addr, void *out, size_t n)
{
    struct iovec local = {out, n}, remote = {(void *)(uintptr_t)addr, n};
    return syscall(SYS_process_vm_readv, t->pid, &local, 1, &remote, 1, 0) == (long)n ? 0 : -1;
}

static slot *find(trace_state *t, uint64_t addr)
{
    const size_t mask = ((size_t)1 << TABLE_BITS) - 1;
    for (size_t i = (size_t)((addr * 0x9e3779b97f4a7c15ull) >> (64 - TABLE_BITS));; i = (i + 1) & mask)
        if (t->table[i].addr == addr || t->table[i].addr == 0)
            return &t->table[i];
}

/* Notes `n` bytes at `addr` as read (their first value is an input unless written first) or
   written. Returns 0, or -1 when a limit is reached or a value cannot be read. */
static int note(trace_state *t, uint64_t addr, uint64_t n, int write, int *input)
{
    if (addr >= t->stack_lo && addr < t->stack_hi) {
        /* The private stack: the function's own frame, or the stack arguments copied for it. */
        if (!write && addr + n > t->args_lo) {
            if (addr < t->args_lo || addr + n > t->args_hi)
                return -1;              /* beyond the stack arguments copied for the call */
            unsigned last = (unsigned)((addr + n - 1 - t->args_lo) / 8) + 1;
            if (last > t->stack_slots)
                t->stack_slots = last;
        }
        return 0;
    }
    /* Ranges after the first are code the runtime made (relocated entries): the jump back reads
       its own target, which is not an input of the function. */
    for (size_t i = 1; i < t->ranges; i++)
        if (addr >= (uintptr_t)t->code[i].lo && addr + n <= (uintptr_t)t->code[i].hi)
            return 0;
    if (!addr || addr + n < addr)
        return -1;
    uint8_t value[64];
    for (uint64_t done = 0; done < n;) {
        uint64_t chunk = n - done < sizeof value ? n - done : sizeof value;
        if (!write && read_self(t, addr + done, value, chunk))
            return -1;
        for (uint64_t i = 0; i < chunk; i++) {
            slot *s = find(t, addr + done + i);
            if (s->addr == 0) {
                if (t->used >= ((size_t)1 << TABLE_BITS) / 2)
                    return -1;
                t->used++;
                s->addr = addr + done + i;
                s->flags = write ? RT_TRACE_WRITTEN : RT_TRACE_READ;
                s->value = write ? 0 : value[i];
                if (!write && input)
                    *input = 1;
            } else if (write) {
                s->flags |= RT_TRACE_WRITTEN;
            }
        }
        done += chunk;
    }
    return 0;
}

static int xmm_index(x86_reg r)
{
    if (r >= X86_REG_XMM0 && r <= X86_REG_XMM31) return r - X86_REG_XMM0;
    if (r >= X86_REG_YMM0 && r <= X86_REG_YMM31) return r - X86_REG_YMM0;
    if (r >= X86_REG_ZMM0 && r <= X86_REG_ZMM31) return r - X86_REG_ZMM0;
    return -1;
}

static int unsupported(const cs_insn *in)
{
    switch (in->id) {
    case X86_INS_XSAVE: case X86_INS_XSAVEC: case X86_INS_XSAVEOPT: case X86_INS_XSAVES:
    case X86_INS_XRSTOR: case X86_INS_XRSTORS: case X86_INS_FXSAVE: case X86_INS_FXSAVE64:
    case X86_INS_FXRSTOR: case X86_INS_FXRSTOR64: case X86_INS_FNSAVE: case X86_INS_FRSTOR:
    case X86_INS_FNSTENV: case X86_INS_FLDENV: case X86_INS_XLATB: case X86_INS_MASKMOVDQU:
    case X86_INS_VMASKMOVDQU: case X86_INS_SYSCALL: case X86_INS_SYSENTER: case X86_INS_INT:
    case X86_INS_INT3: case X86_INS_INTO: case X86_INS_UD2:
        return 1;
    default:
        return 0;
    }
}

/* Instructions whose memory operand is not an access. */
static int no_access(const cs_insn *in)
{
    switch (in->id) {
    case X86_INS_LEA: case X86_INS_NOP: case X86_INS_PREFETCH: case X86_INS_PREFETCHNTA:
    case X86_INS_PREFETCHT0: case X86_INS_PREFETCHT1: case X86_INS_PREFETCHT2: case X86_INS_PREFETCHW:
    case X86_INS_CLFLUSH: case X86_INS_CLFLUSHOPT:
        return 1;
    default:
        return 0;
    }
}

static int string_op(const cs_insn *in)
{
    switch (in->id) {
    case X86_INS_MOVSB: case X86_INS_MOVSW: case X86_INS_MOVSD: case X86_INS_MOVSQ:
    case X86_INS_STOSB: case X86_INS_STOSW: case X86_INS_STOSD: case X86_INS_STOSQ:
    case X86_INS_LODSB: case X86_INS_LODSW: case X86_INS_LODSD: case X86_INS_LODSQ:
    case X86_INS_CMPSB: case X86_INS_CMPSW: case X86_INS_CMPSD: case X86_INS_CMPSQ:
    case X86_INS_SCASB: case X86_INS_SCASW: case X86_INS_SCASD: case X86_INS_SCASQ:
        return 1;
    default:
        return 0;
    }
}

/* xor reg, reg and its vector forms only write the register. */
static int zeroing_idiom(const cs_insn *in)
{
    const cs_x86 *x = &in->detail->x86;
    switch (in->id) {
    case X86_INS_PXOR: case X86_INS_XORPS: case X86_INS_XORPD:
        return x->op_count == 2 && x->operands[0].type == X86_OP_REG && x->operands[1].type == X86_OP_REG &&
               x->operands[0].reg == x->operands[1].reg;
    case X86_INS_VPXOR: case X86_INS_VXORPS: case X86_INS_VXORPD:
        return x->op_count == 3 && x->operands[1].type == X86_OP_REG && x->operands[2].type == X86_OP_REG &&
               x->operands[1].reg == x->operands[2].reg;
    default:
        return 0;
    }
}

/* Vector argument registers the case cannot set (only xmm0 and xmm1 are case arguments) must
   not be read before the traced code writes them. */
static int check_vector_registers(trace_state *t, const cs_insn *in)
{
    cs_regs read, written;
    uint8_t nread = 0, nwritten = 0;
    if (cs_regs_access(t->cs, in, read, &nread, written, &nwritten) != CS_ERR_OK)
        return -1;
    if (!zeroing_idiom(in))
        for (unsigned i = 0; i < nread; i++) {
            int x = xmm_index((x86_reg)read[i]);
            if (x >= 2 && x <= 7 && !(t->xmm_written & (1u << x)))
                return -1;
        }
    for (unsigned i = 0; i < nwritten; i++) {
        int x = xmm_index((x86_reg)written[i]);
        if (x >= 0 && x < 32)
            t->xmm_written |= 1u << x;
    }
    return 0;
}

int rt_trace_active(void) { return current != NULL; }

void rt_trace_set_redirects(const rt_trace_redirect *table, size_t count)
{
    redirects = table;
    redirect_count = count;
}

static const void *redirected(uint64_t rip)
{
    size_t lo = 0, hi = redirect_count;
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        const uintptr_t from = (uintptr_t)redirects[mid].from;
        if (from == rip) return redirects[mid].to;
        if (from < rip) lo = mid + 1;
        else hi = mid;
    }
    return NULL;
}

void rt_trace_set_imports(const rt_trace_import *table, size_t count)
{
    import_target *next = calloc(count ? count : 1, sizeof *next);
    size_t n = 0;
    for (size_t i = 0; next && i < count; i++) {
        uint64_t target;
        memcpy(&target, table[i].slot, sizeof target);
        if (target)
            next[n++] = (import_target){target, (uint64_t)(uintptr_t)table[i].slot, table[i].name};
    }
    for (size_t i = 1; i < n; i++)              /* by target; few entries, insertion sort */
        for (size_t j = i; j > 0 && next[j - 1].target > next[j].target; j--) {
            const import_target x = next[j];
            next[j] = next[j - 1];
            next[j - 1] = x;
        }
    imports = next;                             /* set once, before any trace; never freed */
    import_count = n;
}

static const import_target *import_at(uint64_t target)
{
    size_t lo = 0, hi = import_count;
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        if (imports[mid].target == target) return &imports[mid];
        if (imports[mid].target < target) lo = mid + 1;
        else hi = mid;
    }
    return NULL;
}

static int import_slot(uint64_t addr, uint64_t n)
{
    for (size_t i = 0; i < import_count; i++)
        if (addr < imports[i].slot + 8 && addr + n > imports[i].slot) return 1;
    return 0;
}

static int read_self(trace_state *t, uint64_t addr, void *out, size_t n);
static int note(trace_state *t, uint64_t addr, uint64_t n, int write, int *input);
static void step(trace_state *t, ucontext_t *uc);

/* Native code reached through an import's slot: snapshot what each pointer argument points at,
   send the return to the pad, and let the import run untraced. Any argument register may hold a
   stale pointer, so only bytes above the return slot are compared: below it are the import's own
   frames (and the trap handler's), which the caller cannot see once the import returns. */
static int import_enter(trace_state *t, ucontext_t *uc, const import_target *imp)
{
    greg_t *g = uc->uc_mcontext.gregs;
    if (t->ncalls >= IMPORT_CALL_LIMIT) return -1;
    static const int args[6] = {REG_RDI, REG_RSI, REG_RDX, REG_RCX, REG_R8, REG_R9};
    for (int i = 0; i < 6; i++) {
        t->snap_ptr[i] = (uint64_t)g[args[i]];
        struct iovec local = {t->snap + i * SNAPSHOT, SNAPSHOT}, remote = {(void *)(uintptr_t)t->snap_ptr[i], SNAPSHOT};
        const long got = t->snap_ptr[i] >= 4096 ? syscall(SYS_process_vm_readv, t->pid, &local, 1, &remote, 1, 0) : -1;
        t->snap_len[i] = got > 0 ? (size_t)got : 0;
    }
    uint64_t ret;
    const uint64_t rsp = (uint64_t)g[REG_RSP];
    if (read_self(t, rsp, &ret, 8)) return -1;
    t->calls[t->ncalls] = (rt_trace_import_call){imp->name, 0, t->nwrites, 0};
    t->import_return = ret;
    t->import_rsp = rsp;
    *(uint64_t *)(uintptr_t)rsp = (uint64_t)(uintptr_t)return_pad;
    t->in_import = 1;
    g[REG_EFL] &= ~(greg_t)TRAP_FLAG;
    return 0;
}

/* Back from the import at the pad: record its result and the bytes it changed, then go on
   tracing at its real return address. */
static void import_return(trace_state *t, ucontext_t *uc)
{
    greg_t *g = uc->uc_mcontext.gregs;
    rt_trace_import_call *c = &t->calls[t->ncalls++];
    c->ret = (uint64_t)g[REG_RAX];
    uint8_t now[SNAPSHOT];
    for (int i = 0; i < 6 && !t->incomplete; i++) {
        if (!t->snap_len[i]) continue;
        if (read_self(t, t->snap_ptr[i], now, t->snap_len[i])) continue;
        const uint8_t *before = t->snap + i * SNAPSHOT;
        const uint64_t visible = t->import_rsp + 8;  /* above the return slot */
        size_t off = 0;
        if (t->snap_ptr[i] < visible)
            off = visible - t->snap_ptr[i] < t->snap_len[i] ? (size_t)(visible - t->snap_ptr[i]) : t->snap_len[i];
        while (off < t->snap_len[i]) {
            if (now[off] == before[off]) { off++; continue; }
            size_t end = off;
            while (end < t->snap_len[i] && now[end] != before[end]) end++;
            if (t->nwrites >= WRITE_LIMIT || t->write_used + (end - off) > WRITE_BYTES) {
                t->incomplete = "an import changed too much memory to script";
                break;
            }
            memcpy(t->write_bytes + t->write_used, now + off, end - off);
            t->writes[t->nwrites++] = (rt_trace_write){(uint8_t)i, (uint32_t)off, (uint32_t)(end - off), t->write_used};
            t->write_used += end - off;
            c->writes++;
            /* Later reads of these bytes see what the import wrote, which the case scripts. */
            note(t, t->snap_ptr[i] + off, end - off, 1, NULL);
            off = end;
        }
    }
    t->in_import = 0;
    g[REG_RIP] = (greg_t)t->import_return;
    if (t->incomplete) return;              /* finish untraced */
    g[REG_EFL] |= TRAP_FLAG;
    step(t, uc);
}

static void step(trace_state *t, ucontext_t *uc)
{
    greg_t *g = uc->uc_mcontext.gregs;
    const void *to = redirected((uint64_t)g[REG_RIP]);
    if (to)
        g[REG_RIP] = (greg_t)(uintptr_t)to;
    const uint64_t rip = (uint64_t)g[REG_RIP];
    if (rip == (uint64_t)(uintptr_t)rt_trace_landing) {
        uc->uc_mcontext.gregs[REG_EFL] &= ~(greg_t)TRAP_FLAG;
        return;
    }
    int inside = 0;
    for (size_t i = 0; i < t->ranges; i++)
        inside |= rip >= (uintptr_t)t->code[i].lo && rip < (uintptr_t)t->code[i].hi;
    if (!inside) {
        const import_target *imp = import_at(rip);
        if (imp && !import_enter(t, uc, imp)) return;
        return stop(t, uc, imp ? "too many import calls to script" : "the call left the traced code (native code that is not a listed import)");
    }
    if (++t->steps > t->step_limit)
        return stop(t, uc, "instruction limit reached (BB_TRACE_STEP_LIMIT)");

    const uint8_t *code = (const uint8_t *)(uintptr_t)rip;
    size_t size = 16;
    uint64_t address = rip;
    if (!cs_disasm_iter(t->cs, &code, &size, &address, t->insn))
        return stop(t, uc, "an instruction could not be decoded");
    const cs_insn *in = t->insn;
    const cs_x86 *x = &in->detail->x86;
    if (unsupported(in))
        return stop(t, uc, "an instruction the tracer does not model");
    if (check_vector_registers(t, in))
        return stop(t, uc, "a vector argument register other than xmm0 or xmm1 is read");
    if (no_access(in))
        return;

    const uint64_t next = rip + in->size;
    uint64_t count = 1;
    if (string_op(in) && x->prefix[0] != 0) {
        /* rep, repe, repne: rcx elements; repe/repne may stop sooner, so this can note more
           bytes than are read, never fewer. */
        count = (uint64_t)g[REG_RCX];
        if (!count)
            return;
    }
    for (unsigned i = 0; i < x->op_count; i++) {
        const cs_x86_op *op = &x->operands[i];
        if (op->type != X86_OP_MEM || !(op->access & (CS_AC_READ | CS_AC_WRITE)))
            continue;
        if (op->mem.segment != X86_REG_INVALID)
            return stop(t, uc, "a segment-relative access (thread-local storage)");
        if (xmm_index(op->mem.index) >= 0)
            return stop(t, uc, "a vector-indexed access (gather or scatter)");
        uint64_t base, index;
        if (reg_value(uc, op->mem.base, next, &base) || reg_value(uc, op->mem.index, next, &index))
            return stop(t, uc, "an address register the tracer does not model");
        uint64_t ea = base + index * (uint64_t)op->mem.scale + (uint64_t)op->mem.disp;
        uint64_t n = (uint64_t)op->size * count;
        if (!op->size || n / count != op->size)
            return stop(t, uc, "an operand of unknown size");
        if (count > 1 && (g[REG_EFL] & DIRECTION_FLAG))
            ea -= (count - 1) * op->size;
        if (import_slot(ea, n))
            continue;                       /* an import's slot: the verifier binds its stub there */
        if (op->access & CS_AC_READ) {
            int input = 0;
            if (note(t, ea, n, 0, &input))
                return stop(t, uc, "a read the tracer cannot note (limit, or unreadable memory)");
            if (input && op->size == 8 && count == 1) {
                if (t->qwords >= QWORD_LIMIT)
                    return stop(t, uc, "too many eight-byte inputs");
                t->qword[t->qwords++] = ea;
            }
        }
        if ((op->access & CS_AC_WRITE) && note(t, ea, n, 1, NULL))
            return stop(t, uc, "a write the tracer cannot note (limit)");
    }
}

static void on_trap(int sig, siginfo_t *info, void *context)
{
    trace_state *t = current;
    if (t) {
        ucontext_t *uc = (ucontext_t *)context;
        if (t->in_import && (uint64_t)uc->uc_mcontext.gregs[REG_RIP] == (uint64_t)(uintptr_t)return_pad + 1)
            import_return(t, uc);
        else if (!t->in_import)
            step(t, uc);
        return;
    }
    if (previous.sa_flags & SA_SIGINFO) {
        if (previous.sa_sigaction)
            previous.sa_sigaction(sig, info, context);
    } else if (previous.sa_handler != SIG_IGN && previous.sa_handler != SIG_DFL) {
        previous.sa_handler(sig);
    } else {
        signal(sig, SIG_DFL);
        raise(sig);
    }
}

static void install(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = on_trap;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    install_error = sigaction(SIGTRAP, &sa, &previous);
    return_pad = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (return_pad == MAP_FAILED || (return_pad[0] = 0xcc, mprotect(return_pad, 4096, PROT_READ | PROT_EXEC)))
        install_error = -1;
}

static int compare_u64(const void *a, const void *b)
{
    const uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : x > y;
}

static int collect(trace_state *t, rt_trace_log *log)
{
    log->count = t->used;
    log->addr = malloc((t->used ? t->used : 1) * sizeof *log->addr);
    log->value = malloc(t->used ? t->used : 1);
    log->flags = malloc(t->used ? t->used : 1);
    log->qword = malloc((t->qwords ? t->qwords : 1) * sizeof *log->qword);
    if (!log->addr || !log->value || !log->flags || !log->qword)
        return -1;
    size_t n = 0;
    for (size_t i = 0; i < ((size_t)1 << TABLE_BITS); i++)
        if (t->table[i].addr)
            log->addr[n++] = t->table[i].addr;
    qsort(log->addr, n, sizeof *log->addr, compare_u64);
    for (size_t i = 0; i < n; i++) {
        const slot *s = find(t, log->addr[i]);
        log->value[i] = s->value;
        log->flags[i] = s->flags;
    }
    memcpy(log->qword, t->qword, t->qwords * sizeof *t->qword);
    log->imports = malloc((t->ncalls ? t->ncalls : 1) * sizeof *log->imports);
    log->writes = malloc((t->nwrites ? t->nwrites : 1) * sizeof *log->writes);
    log->write_bytes = malloc(t->write_used ? t->write_used : 1);
    if (!log->imports || !log->writes || !log->write_bytes)
        return -1;
    memcpy(log->imports, t->calls, t->ncalls * sizeof *t->calls);
    memcpy(log->writes, t->writes, t->nwrites * sizeof *t->writes);
    memcpy(log->write_bytes, t->write_bytes, t->write_used);
    log->import_count = t->ncalls;
    log->write_count = t->nwrites;
    log->qword_count = t->qwords;
    log->stack_slots = t->stack_slots;
    log->steps = t->steps;
    log->incomplete = t->incomplete;
    return 0;
}

void rt_trace_log_free(rt_trace_log *log)
{
    if (!log)
        return;
    free(log->addr);
    free(log->value);
    free(log->flags);
    free(log->qword);
    free(log->imports);
    free(log->writes);
    free(log->write_bytes);
    memset(log, 0, sizeof *log);
}

int rt_trace_call(const void *entry, const rt_trace_args *args, const rt_code_range *code, size_t ranges,
                  rt_trace_result *result, rt_trace_log *log)
{
    memset(log, 0, sizeof *log);
    if (!entry || !args || !result || !code || !ranges || ranges > RT_TRACE_MAX_RANGES || current)
        return -1;                  /* nested tracing on one thread is not supported */
    pthread_once(&installed, install);
    if (install_error)
        return -1;

    trace_state t;
    memset(&t, 0, sizeof t);
    const size_t table_bytes = sizeof(slot) << TABLE_BITS;
    t.table = mmap(NULL, table_bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    unsigned char *stack = mmap(NULL, PRIVATE_STACK, PROT_READ | PROT_WRITE,
                                MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    t.qword = malloc(QWORD_LIMIT * sizeof *t.qword);
    t.snap = malloc(6 * SNAPSHOT);
    t.calls = malloc(IMPORT_CALL_LIMIT * sizeof *t.calls);
    t.writes = malloc(WRITE_LIMIT * sizeof *t.writes);
    t.write_bytes = malloc(WRITE_BYTES);
    int ok = t.table != MAP_FAILED && stack != MAP_FAILED && t.qword && t.snap && t.calls && t.writes && t.write_bytes &&
             cs_open(CS_ARCH_X86, CS_MODE_64, &t.cs) == CS_ERR_OK;
    if (ok && (cs_option(t.cs, CS_OPT_DETAIL, CS_OPT_ON) != CS_ERR_OK || !(t.insn = cs_malloc(t.cs)))) {
        cs_close(&t.cs);
        ok = 0;
    }
    if (!ok) {
        if (t.table != MAP_FAILED && t.table) munmap(t.table, table_bytes);
        if (stack != MAP_FAILED) munmap(stack, PRIVATE_STACK);
        free(t.qword);
        free(t.snap);
        free(t.calls);
        free(t.writes);
        free(t.write_bytes);
        return -1;
    }

    /* Private stack, top down: the frame's address, the stack arguments, the return address. The
       return address slot sits at 8 mod 16, as at any function entry. */
    rt_trace_frame frame;
    memset(&frame, 0, sizeof frame);
    uintptr_t top = ((uintptr_t)stack + PRIVATE_STACK - 64) & ~(uintptr_t)15;
    uintptr_t ret_slot = top - 8 - 8 * RT_TRACE_STACK_SLOTS - 8;   /* 16-aligned + 8 below */
    if (ret_slot % 16 != 8)
        ret_slot -= 8;
    *(uint64_t *)ret_slot = (uint64_t)(uintptr_t)rt_trace_landing;
    memcpy((void *)(ret_slot + 8), args->stack, sizeof args->stack);
    *(rt_trace_frame **)(ret_slot + 8 + 8 * RT_TRACE_STACK_SLOTS) = &frame;

    frame.entry = (uint64_t)(uintptr_t)entry;
    frame.stack_top = ret_slot;
    memcpy(frame.gpr, args->gpr, sizeof frame.gpr);
    memcpy(frame.xmm, args->xmm, sizeof frame.xmm);

    memcpy(t.code, code, ranges * sizeof *code);
    t.ranges = ranges;
    t.stack_lo = (uintptr_t)stack;
    t.stack_hi = (uintptr_t)stack + PRIVATE_STACK;
    t.stack_top = ret_slot;
    t.args_lo = ret_slot + 8;
    t.args_hi = ret_slot + 8 + 8 * RT_TRACE_STACK_SLOTS;
    t.pid = getpid();
    const char *limit = getenv("BB_TRACE_STEP_LIMIT");
    t.step_limit = limit && *limit ? strtoull(limit, NULL, 0) : DEFAULT_STEP_LIMIT;
    t.xmm_written = 0x3;            /* xmm0 and xmm1 come from the case */

    current = &t;
    rt_trace_enter(&frame);
    current = NULL;
    if (t.in_import && !t.incomplete)
        t.incomplete = "an import did not return to the trace (it unwound past it)";

    result->rax = frame.rax;
    result->rdx = frame.rdx;
    memcpy(result->xmm0, frame.xmm0, 16);
    memcpy(result->xmm1, frame.xmm1, 16);
    if (collect(&t, log)) {
        rt_trace_log_free(log);
        log->incomplete = "out of memory while collecting the trace";
    }

    cs_free(t.insn, 1);
    cs_close(&t.cs);
    munmap(t.table, table_bytes);
    munmap(stack, PRIVATE_STACK);
    free(t.qword);
    free(t.snap);
    free(t.calls);
    free(t.writes);
    free(t.write_bytes);
    return 0;
}

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
