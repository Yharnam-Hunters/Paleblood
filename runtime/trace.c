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
#include <asm/prctl.h>
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
    STACK_POISON = 1 << 20,                /* bytes below the return slot filled with POISON_BYTE */
    POISON_BYTE = 0xa5,
    DEAD_STACK = 1 << 20,               /* how far below the caller's rsp import changes are ignored */
    TRAP_FLAG = 0x100,
    DIRECTION_FLAG = 0x400,
    IMPORT_CALL_LIMIT = 256,
    WRITE_LIMIT = 4096,
    WRITE_BYTES = 1 << 20,
    SNAPSHOT = 4096,                       /* bytes compared behind each pointer argument */
    WRITE_REACH = 256,
    LINK_BITS = 15,                        /* far reaches noted per call: up to half of 2^LINK_BITS */
    LINK_NEAR = 256,                       /* a reach this close to its base pointer is not noted */
    LINK_FAR = 1 << 20,                    /* nor one this far (the case's buffers fit in 16 MiB) */                     /* how far past what it wrote before an import's write may lie */
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
    uint8_t now;                         /* a written byte's value after its last write (KNOWN) */
} slot;

/* Slot flags beyond rt_trace_log's RT_TRACE_READ and RT_TRACE_WRITTEN. */
enum { KNOWN = 4 };

enum { PENDING_LIMIT = 8 };              /* memory operands one instruction writes */

typedef struct {
    csh cs;
    cs_insn *insn;
    rt_code_range code[RT_TRACE_MAX_RANGES];
    size_t ranges;
    uintptr_t stack_lo, stack_top, args_lo, args_hi, stack_hi;
    uintptr_t dead_lo, dead_hi;         /* the calling thread's stack below its caller's rsp */
    slot *table;
    size_t used;
    uint64_t *qword;
    size_t qwords;
    uint64_t (*link)[2];               /* far reaches: {base pointer, address reached} */
    size_t links;
    uint32_t *link_index;              /* hash of link, 1-based; 2^LINK_BITS entries */
    uint64_t origin[16];               /* per general register (gpr_index): the pointer its value
                                          was computed from by address arithmetic, or 0 */
    unsigned stack_slots;
    uint64_t steps, step_limit;
    unsigned xmm_written;              /* bit n: xmm n written by the traced code */
    /* Thread-local storage. Guest code reads its thread pointer at gs:[0] (the loader rewrites
       the game's fs:[0] to gs) and reaches its variables at that pointer plus an offset. */
    uint64_t thread_base;              /* the calling thread's GS base, or 0 when it has none */
    uint64_t thread_pointer;           /* the value the code read at gs:[0], once it has */
    uint64_t thread_lo, thread_hi;     /* [lo, hi): bytes reached through gs or the thread pointer */
    unsigned stale_args;               /* bit n: argument register n (rdi..r9) is what an import left */
    /* The bytes the last instruction wrote: their values are read once it has run. */
    uint64_t pending_addr[PENDING_LIMIT], pending_len[PENDING_LIMIT];
    unsigned pending;
    const char *incomplete;
    uint64_t stopped_at;               /* the instruction tracing stopped at */
    pid_t pid;
    /* The import running now (between its entry and the return pad). */
    int in_import;
    uint64_t import_return;
    uint64_t import_rsp;                /* rsp at the import's entry: its return slot */
    unsigned import_stale;              /* stale_args at the import's entry */
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
    if (!t->incomplete) {
        t->incomplete = why;
        t->stopped_at = (uint64_t)uc->uc_mcontext.gregs[REG_RIP];
    }
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

/* The general register (rax = 0 ... r15 = 15, in REG_* order of the map below) a 64-bit or
   32-bit register name stands for, or -1. A 32-bit write also replaces the whole register. */
static int gpr_index(x86_reg reg)
{
    static const x86_reg r64[16] = {X86_REG_RAX, X86_REG_RBX, X86_REG_RCX, X86_REG_RDX, X86_REG_RSI, X86_REG_RDI,
                                    X86_REG_RBP, X86_REG_RSP, X86_REG_R8,  X86_REG_R9,  X86_REG_R10, X86_REG_R11,
                                    X86_REG_R12, X86_REG_R13, X86_REG_R14, X86_REG_R15};
    static const x86_reg r32[16] = {X86_REG_EAX, X86_REG_EBX, X86_REG_ECX, X86_REG_EDX, X86_REG_ESI, X86_REG_EDI,
                                    X86_REG_EBP, X86_REG_ESP, X86_REG_R8D, X86_REG_R9D, X86_REG_R10D, X86_REG_R11D,
                                    X86_REG_R12D, X86_REG_R13D, X86_REG_R14D, X86_REG_R15D};
    for (int i = 0; i < 16; i++)
        if (reg == r64[i] || reg == r32[i]) return i;
    return -1;
}

/* syscall is variadic: use the kernel's unsigned-long count/flag types. In particular,
   a 32-bit zero can leave the upper half of flags' stack argument uninitialized. */
static int read_self(trace_state *t, uint64_t addr, void *out, size_t n)
{
    struct iovec local = {out, n}, remote = {(void *)(uintptr_t)addr, n};
    return syscall(SYS_process_vm_readv, t->pid, &local, 1UL, &remote, 1UL, 0UL) == (long)n ? 0 : -1;
}

static slot *find(trace_state *t, uint64_t addr)
{
    const size_t mask = ((size_t)1 << TABLE_BITS) - 1;
    for (size_t i = (size_t)((addr * 0x9e3779b97f4a7c15ull) >> (64 - TABLE_BITS));; i = (i + 1) & mask)
        if (t->table[i].addr == addr || t->table[i].addr == 0)
            return &t->table[i];
}

enum { NOTE_CHANGED = -2 };

/* Notes `n` bytes at `addr` as read (their first value is an input unless written first) or
   written. Returns 0, -1 when a limit is reached or a value cannot be read, or NOTE_CHANGED when a
   read sees an input byte with another value than its first read: something outside the traced
   code (another thread) wrote it, and a case, which gives each input one value, cannot replay
   the call. */
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
            } else if (!(s->flags & RT_TRACE_WRITTEN) && s->value != value[i]) {
                return NOTE_CHANGED;
            } else if ((s->flags & KNOWN) && s->now != value[i]) {
                /* The traced code (or a scripted import) wrote it, and something else has changed
                   it since: a replay would read what was written. */
                return NOTE_CHANGED;
            }
        }
        done += chunk;
    }
    return 0;
}

/* Keeps the value now in memory of each noted byte among the n at `addr`, as what a later read
   must see: the traced code's own write, or the scripted write of an import. */
static void remember_written(trace_state *t, uint64_t addr, uint64_t n)
{
    uint8_t value[64];
    for (uint64_t done = 0; done < n;) {
        const uint64_t chunk = n - done < sizeof value ? n - done : sizeof value;
        const int readable = !read_self(t, addr + done, value, chunk);
        for (uint64_t i = 0; i < chunk; i++) {
            slot *s = find(t, addr + done + i);
            if (s->addr != addr + done + i) continue;   /* the private stack has no slots */
            if (readable) {
                s->now = value[i];
                s->flags |= KNOWN;
            } else {
                s->flags &= (uint8_t)~KNOWN;
            }
        }
        done += chunk;
    }
}

/* Reads what the last instruction wrote, now that it has run. */
static void settle_writes(trace_state *t)
{
    for (unsigned i = 0; i < t->pending; i++)
        remember_written(t, t->pending_addr[i], t->pending_len[i]);
    t->pending = 0;
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

/* The argument register (rdi = 0 ... r9 = 5) that `r` is part of, or -1. */
static int argument_index(x86_reg r)
{
    switch (r) {
    case X86_REG_RDI: case X86_REG_EDI: case X86_REG_DI: case X86_REG_DIL: return 0;
    case X86_REG_RSI: case X86_REG_ESI: case X86_REG_SI: case X86_REG_SIL: return 1;
    case X86_REG_RDX: case X86_REG_EDX: case X86_REG_DX: case X86_REG_DL: case X86_REG_DH: return 2;
    case X86_REG_RCX: case X86_REG_ECX: case X86_REG_CX: case X86_REG_CL: case X86_REG_CH: return 3;
    case X86_REG_R8: case X86_REG_R8D: case X86_REG_R8W: case X86_REG_R8B: return 4;
    case X86_REG_R9: case X86_REG_R9D: case X86_REG_R9W: case X86_REG_R9B: return 5;
    default: return -1;
    }
}

/* Vector argument registers the case cannot set (only xmm0 and xmm1 are case arguments) must
   not be read before the traced code writes them. Also notes the argument registers the
   instruction sets: one the traced code set since the last import holds a value of its own. */
static int check_registers(trace_state *t, const cs_insn *in)
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
        int a = argument_index((x86_reg)written[i]);
        if (a >= 0)
            t->stale_args &= ~(1u << a);
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
        const long got = t->snap_ptr[i] >= 4096 ? syscall(SYS_process_vm_readv, t->pid, &local, 1UL, &remote, 1UL, 0UL) : -1;
        t->snap_len[i] = got > 0 ? (size_t)got : 0;
    }
    uint64_t ret;
    const uint64_t rsp = (uint64_t)g[REG_RSP];
    if (read_self(t, rsp, &ret, 8)) return -1;
    t->calls[t->ncalls] = (rt_trace_import_call){imp->name, 0, t->nwrites, 0};
    t->import_return = ret;
    t->import_rsp = rsp;
    t->import_stale = t->stale_args;
    *(uint64_t *)(uintptr_t)rsp = (uint64_t)(uintptr_t)return_pad;
    t->in_import = 1;
    g[REG_EFL] &= ~(greg_t)TRAP_FLAG;
    return 0;
}

/* Below the caller's rsp, on the calling thread's stack: nothing the caller can see. The private
   stack the call runs on may be mapped right below that stack; it is never dead. */
static int dead(const trace_state *t, uint64_t addr)
{
    return addr >= t->dead_lo && addr < t->dead_hi && !(addr >= t->stack_lo && addr < t->stack_hi);
}

/* 1 when the traced code read or wrote any of the n bytes at `addr` before now. */
static int touched(trace_state *t, uint64_t addr, size_t n)
{
    for (size_t i = 0; i < n; i++)
        if (find(t, addr + i)->addr == addr + i) return 1;
    return 0;
}

/* Trims [*lo, *hi) by the bytes an earlier write of the same import call already scripts (an
   earlier argument register pointing at the same memory). Returns 0 when nothing is left. */
static int unclaimed(const trace_state *t, const rt_trace_import_call *c, uint64_t *lo, uint64_t *hi)
{
    for (size_t w = c->first_write; w < t->nwrites && *lo < *hi; w++) {
        const uint64_t a = t->snap_ptr[t->writes[w].arg] + t->writes[w].offset, b = a + t->writes[w].size;
        if (a <= *lo && *lo < b) *lo = b < *hi ? b : *hi;
        if (a < *hi && *hi <= b) *hi = a > *lo ? a : *lo;
    }
    return *lo < *hi;
}

/* Back from the import at the pad: record its result and the bytes it changed, then go on
   tracing at its real return address.

   The import's argument count is not known, so every argument register's memory is compared,
   but only some changes are the import's writes for the caller (each is scripted as a write
   through that register in a replay, where the other registers hold other values):
   - a register an earlier import left (and the traced code did not set since) is not an
     argument the traced code passed: a replay's register holds something else;
   - bytes an earlier register already scripts are not scripted again;
   - bytes below the import's return slot are its own frames, and bytes below the caller's rsp
     the recording thread's: nothing the caller sees;
   - other threads change memory behind any pointer, so a change counts only in the traced call's
     own frame, within WRITE_REACH bytes after the pointer or after the last change kept through
     it (a filled buffer, with runs that kept their value), or on bytes the traced code touched. */
static void import_return(trace_state *t, ucontext_t *uc)
{
    greg_t *g = uc->uc_mcontext.gregs;
    rt_trace_import_call *c = &t->calls[t->ncalls++];
    c->ret = (uint64_t)g[REG_RAX];
    uint8_t now[SNAPSHOT];
    const uint64_t visible = t->import_rsp + 8;  /* above the return slot */
    for (int i = 0; i < 6 && !t->incomplete; i++) {
        if (!t->snap_len[i] || (t->import_stale & (1u << i))) continue;
        if (read_self(t, t->snap_ptr[i], now, t->snap_len[i])) continue;
        const uint8_t *before = t->snap + i * SNAPSHOT;
        size_t off = 0;
        /* Below the return slot, on the private stack the import runs on: its own frames. */
        if (t->snap_ptr[i] >= t->stack_lo && t->snap_ptr[i] < visible)
            off = visible - t->snap_ptr[i] < t->snap_len[i] ? (size_t)(visible - t->snap_ptr[i]) : t->snap_len[i];
        const uint64_t at = t->snap_ptr[i];
        size_t done = off;                  /* bytes before this are scripted or left alone */
        size_t reach = 0;                   /* the end of the last change kept through this register */
        while (off < t->snap_len[i]) {
            if (now[off] == before[off] || dead(t, at + off)) { off++; continue; }
            size_t end = off;
            while (end < t->snap_len[i] && now[end] != before[end] && !dead(t, at + end))
                end++;
            /* A changed run widens to whole eight-byte words: an import that writes a value whose
               upper bytes happen to equal what was there (a struct timespec's seconds) changes
               only some of its bytes, and the rest must be scripted too, or a replay reads what its
               own stack held there. */
            while (off > done && ((at + off) & 7) && !dead(t, at + off - 1)) off--;
            while (end < t->snap_len[i] && ((at + end) & 7) && !dead(t, at + end)) end++;
            const size_t run_end = end;
            uint64_t lo = at + off, hi = at + end;
            const int frame = lo >= visible && hi <= t->stack_top;
            const int kept = (frame || off <= reach + WRITE_REACH || touched(t, lo, end - off)) && unclaimed(t, c, &lo, &hi);
                if (kept) {
                off = (size_t)(lo - at);
                end = (size_t)(hi - at);
                if (t->nwrites >= WRITE_LIMIT || t->write_used + (end - off) > WRITE_BYTES) {
                    t->incomplete = "an import changed too much memory to script";
                    break;
                }
                memcpy(t->write_bytes + t->write_used, now + off, end - off);
                t->writes[t->nwrites++] =
                    (rt_trace_write){(uint8_t)i, (uint32_t)off, (uint32_t)(end - off), t->write_used, at + off};
                t->write_used += end - off;
                c->writes++;
                /* Later reads of these bytes see what the import wrote, which the case scripts. */
                note(t, at + off, end - off, 1, NULL);
                remember_written(t, at + off, end - off);
                reach = end;
            }
            off = done = run_end;
        }
    }
    /* What the import left in the argument registers is not the traced code's. */
    t->stale_args = 0x3f;
    /* Nor are the pointers behind the registers a call may change. */
    static const int volatile_gprs[] = {0, 2, 3, 4, 5, 8, 9, 10, 11};
    for (size_t i = 0; i < sizeof volatile_gprs / sizeof volatile_gprs[0]; i++)
        t->origin[volatile_gprs[i]] = 0;
    t->in_import = 0;
    g[REG_RIP] = (greg_t)t->import_return;
    if (t->incomplete) return;              /* finish untraced */
    g[REG_EFL] |= TRAP_FLAG;
    step(t, uc);
}

/* Notes that an access through the pointer `base` reached `ea`, far enough from it that the two
   may land in separate buffers: the case must keep them in one, at the same distance. Reaches in
   the same 256-byte block from the same base are noted once. Returns -1 at the limit. */
static int note_link(trace_state *t, uint64_t base, uint64_t ea)
{
    const uint64_t d = ea > base ? ea - base : base - ea;
    if (d <= LINK_NEAR || d > LINK_FAR) return 0;
    const uint64_t block = ea & ~(uint64_t)(LINK_NEAR - 1);
    const size_t mask = ((size_t)1 << LINK_BITS) - 1;
    for (size_t i = (size_t)((((base * 0x9e3779b97f4a7c15ull) ^ block) * 0xff51afd7ed558ccdull) >> (64 - LINK_BITS));;
         i = (i + 1) & mask) {
        const uint32_t k = t->link_index[i];
        if (!k) {
            if (t->links >= ((size_t)1 << LINK_BITS) / 2) return -1;
            t->link[t->links][0] = base;
            t->link[t->links][1] = ea;
            t->link_index[i] = (uint32_t)++t->links;
            return 0;
        }
        if (t->link[k - 1][0] == base && (t->link[k - 1][1] & ~(uint64_t)(LINK_NEAR - 1)) == block) return 0;
    }
}

/* The pointer a 64-bit register operand's value was computed from: its own origin, or its value. */
static uint64_t pointer_of(const trace_state *t, const ucontext_t *uc, x86_reg reg, uint64_t next)
{
    const int i = gpr_index(reg);
    uint64_t v;
    if (i < 0 || reg == X86_REG_RSP || reg_value(uc, reg, next, &v)) return 0;
    return t->origin[i] ? t->origin[i] : v;
}

/* Follows pointers through address arithmetic, so that an access through a computed pointer
   (a table's index at table + its stored size: lea, or add of a loaded offset) is noted as a reach
   from the pointer it came from and lands in that pointer's buffer. Of the two registers added,
   the larger value is taken as the pointer (an offset is the smaller). Any other write to a
   register forgets its origin. */
static int track_origin(trace_state *t, const ucontext_t *uc, const cs_insn *in, uint64_t next)
{
    const cs_x86 *x = &in->detail->x86;
    const cs_x86_op *op = x->operands;
    int dst = -1;
    uint64_t from = 0;
    if (x->op_count >= 1 && op[0].type == X86_OP_REG && op[0].size == 8) dst = gpr_index(op[0].reg);
    if (dst >= 0 && x->op_count == 2) {
        if (in->id == X86_INS_LEA && op[1].mem.base != X86_REG_RIP) {
            from = pointer_of(t, uc, op[1].mem.base, next);
            if (!from && op[1].mem.scale == 1) from = pointer_of(t, uc, op[1].mem.index, next);
        } else if (in->id == X86_INS_MOV && op[1].type == X86_OP_REG) {
            const int src = gpr_index(op[1].reg);
            from = src >= 0 && op[1].size == 8 ? t->origin[src] : 0;
        } else if ((in->id == X86_INS_ADD || in->id == X86_INS_SUB) && op[1].type == X86_OP_IMM) {
            from = pointer_of(t, uc, op[0].reg, next);
        } else if (in->id == X86_INS_MOV && op[1].type == X86_OP_MEM && op[1].mem.segment == X86_REG_GS &&
                   op[1].mem.base == X86_REG_INVALID && op[1].mem.index == X86_REG_INVALID && op[1].size == 8 &&
                   t->thread_base) {
            /* The thread pointer itself: a TCB's first word points to the TCB. Its value is
               the origin of what is computed from it (pointer_of). */
            uint64_t pointer;
            if (read_self(t, t->thread_base + (uint64_t)op[1].mem.disp, &pointer, sizeof pointer))
                return -1;
            if (pointer)
                t->thread_pointer = pointer;
        } else if (in->id == X86_INS_ADD && op[1].type == X86_OP_MEM && t->thread_pointer &&
                   pointer_of(t, uc, op[0].reg, next) == t->thread_pointer) {
            /* A thread-local variable's offset, loaded from the image, added to the thread pointer. */
            from = t->thread_pointer;
        } else if (in->id == X86_INS_ADD && op[1].type == X86_OP_REG && op[1].size == 8) {
            uint64_t a, b;
            if (!reg_value(uc, op[0].reg, next, &a) && !reg_value(uc, op[1].reg, next, &b))
                from = pointer_of(t, uc, a >= b ? op[0].reg : op[1].reg, next);
        }
    }
    cs_regs read, written;
    uint8_t nread = 0, nwritten = 0;
    if (cs_regs_access(t->cs, in, read, &nread, written, &nwritten) != CS_ERR_OK)
        return -1;
    for (unsigned i = 0; i < nwritten; i++) {
        const int w = gpr_index((x86_reg)written[i]);
        if (w >= 0) t->origin[w] = 0;
    }
    if (dst >= 0 && dst != 7) t->origin[dst] = from;  /* rsp's own reach rules stay */
    return 0;
}

/* Widens the thread-local range to `n` bytes at `addr`. The code finds a variable by adding an
   offset to the thread pointer, so the case must keep the variable and the thread block at their
   distance: a reach from the thread pointer is a link (note_link) only up to LINK_FAR. Returns -1
   when the range would exceed it. */
static int note_thread_local(trace_state *t, uint64_t addr, uint64_t n)
{
    const uint64_t lo = addr < t->thread_lo ? addr : t->thread_lo;
    const uint64_t hi = addr + n > t->thread_hi ? addr + n : t->thread_hi;
    if (hi - lo > LINK_FAR)
        return -1;
    t->thread_lo = lo;
    t->thread_hi = hi;
    return 0;
}

static void step(trace_state *t, ucontext_t *uc)
{
    greg_t *g = uc->uc_mcontext.gregs;
    settle_writes(t);
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
    if (check_registers(t, in))
        return stop(t, uc, "a vector argument register other than xmm0 or xmm1 is read");
    const uint64_t next = rip + in->size;
    /* Memory operands below use the registers' values before this instruction, as does this. */
    uint64_t base_origin[8] = {0};
    for (unsigned i = 0; i < x->op_count && i < 8; i++)
        if (x->operands[i].type == X86_OP_MEM) {
            const int b = gpr_index(x->operands[i].mem.base);
            base_origin[i] = b >= 0 ? t->origin[b] : 0;
        }
    if (track_origin(t, uc, in, next))
        return stop(t, uc, "an instruction's registers could not be read");
    if (no_access(in))
        return;

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
        if (op->type != X86_OP_MEM)
            continue;
        /* Capstone marks some memory sources with no access at all (`test m, r`, a bitmap test
           in the heaps); they are reads. lea, nop and prefetch never reach here (no_access). */
        const uint8_t access = op->access & (CS_AC_READ | CS_AC_WRITE) ? op->access : CS_AC_READ;
        /* gs is the guest's thread pointer block; fs would be the host's own. */
        uint64_t segment_base = 0;
        if (op->mem.segment == X86_REG_GS && t->thread_base)
            segment_base = t->thread_base;
        else if (op->mem.segment != X86_REG_INVALID)
            return stop(t, uc, "a segment-relative access other than the guest thread pointer (gs)");
        if (xmm_index(op->mem.index) >= 0)
            return stop(t, uc, "a vector-indexed access (gather or scatter)");
        uint64_t base, index;
        if (reg_value(uc, op->mem.base, next, &base) || reg_value(uc, op->mem.index, next, &index))
            return stop(t, uc, "an address register the tracer does not model");
        uint64_t ea = segment_base + base + index * (uint64_t)op->mem.scale + (uint64_t)op->mem.disp;
        uint64_t n = (uint64_t)op->size * count;
        if (!op->size || n / count != op->size)
            return stop(t, uc, "an operand of unknown size");
        if (count > 1 && (g[REG_EFL] & DIRECTION_FLAG))
            ea -= (count - 1) * op->size;
        if (import_slot(ea, n))
            continue;                       /* an import's slot: the verifier binds its stub there */
        if ((segment_base || (i < 8 && t->thread_pointer && base_origin[i] == t->thread_pointer)) &&
            note_thread_local(t, ea, n))
            return stop(t, uc, "thread-local storage farther than 1 MiB from the thread pointer");
        if (op->mem.base != X86_REG_INVALID && op->mem.base != X86_REG_RIP && op->mem.base != X86_REG_RSP &&
            (note_link(t, base, ea) || (i < 8 && base_origin[i] && note_link(t, base_origin[i], ea))))
            return stop(t, uc, "too many far reaches from base pointers");
        if (access & CS_AC_READ) {
            int input = 0;
            const int noted = note(t, ea, n, 0, &input);
            if (noted == NOTE_CHANGED)
                return stop(t, uc, "memory the call reads changed during it (written outside the traced code)");
            if (noted)
                return stop(t, uc, "a read the tracer cannot note (limit, or unreadable memory)");
            if (input && op->size == 8 && count == 1) {
                if (t->qwords >= QWORD_LIMIT)
                    return stop(t, uc, "too many eight-byte inputs");
                t->qword[t->qwords++] = ea;
            }
        }
        if (access & CS_AC_WRITE) {
            if (note(t, ea, n, 1, NULL) || t->pending >= PENDING_LIMIT)
                return stop(t, uc, "a write the tracer cannot note (limit)");
            t->pending_addr[t->pending] = ea;
            t->pending_len[t->pending++] = n;
        }
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
        log->flags[i] = s->flags & (RT_TRACE_READ | RT_TRACE_WRITTEN);
    }
    memcpy(log->qword, t->qword, t->qwords * sizeof *t->qword);
    log->link = malloc((t->links ? t->links : 1) * sizeof *log->link);
    if (!log->link)
        return -1;
    memcpy(log->link, t->link, t->links * sizeof *t->link);
    log->link_count = t->links;
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
    log->thread_base = t->thread_hi ? t->thread_base : 0;   /* the code read through gs */
    log->incomplete = t->incomplete;
    log->stopped_at = t->stopped_at;
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
    free(log->link);
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
    t.link = malloc(((size_t)1 << LINK_BITS) / 2 * sizeof *t.link);
    t.link_index = calloc((size_t)1 << LINK_BITS, sizeof *t.link_index);
    t.snap = malloc(6 * SNAPSHOT);
    t.calls = malloc(IMPORT_CALL_LIMIT * sizeof *t.calls);
    t.writes = malloc(WRITE_LIMIT * sizeof *t.writes);
    t.write_bytes = malloc(WRITE_BYTES);
    int ok = t.table != MAP_FAILED && stack != MAP_FAILED && t.qword && t.link && t.link_index && t.snap && t.calls && t.writes && t.write_bytes &&
             cs_open(CS_ARCH_X86, CS_MODE_64, &t.cs) == CS_ERR_OK;
    if (ok && (cs_option(t.cs, CS_OPT_DETAIL, CS_OPT_ON) != CS_ERR_OK || !(t.insn = cs_malloc(t.cs)))) {
        cs_close(&t.cs);
        ok = 0;
    }
    if (!ok) {
        if (t.table != MAP_FAILED && t.table) munmap(t.table, table_bytes);
        if (stack != MAP_FAILED) munmap(stack, PRIVATE_STACK);
        free(t.qword);
        free(t.link);
        free(t.link_index);
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
    /* A fresh mapping is zero: an import that writes zeros into the traced code's frame (a
       result or count of 0) would change nothing and go unscripted, and the replay would read what
       its own stack holds there. Over a filled frame every import write is a change. */
    memset((void *)(ret_slot - STACK_POISON), POISON_BYTE, STACK_POISON);
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
    if (args->caller_rsp > DEAD_STACK) {
        t.dead_lo = args->caller_rsp - DEAD_STACK;
        t.dead_hi = args->caller_rsp;
    }
    t.args_lo = ret_slot + 8;
    t.args_hi = ret_slot + 8 + 8 * RT_TRACE_STACK_SLOTS;
    t.pid = getpid();
    const char *limit = getenv("BB_TRACE_STEP_LIMIT");
    t.step_limit = limit && *limit ? strtoull(limit, NULL, 0) : DEFAULT_STEP_LIMIT;
    t.xmm_written = 0x3;            /* xmm0 and xmm1 come from the case */
    unsigned long thread_base = 0;
    if (!syscall(SYS_arch_prctl, ARCH_GET_GS, &thread_base))
        t.thread_base = thread_base;
    t.thread_lo = UINT64_MAX;

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
    free(t.link);
    free(t.link_index);
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
    uint64_t branch_targets[8];
    size_t n = 0, done = 0, branches = 0;
    int ok = in != NULL;
    const uint64_t base = (uint64_t)(uintptr_t)src, to = (uint64_t)(uintptr_t)dst;
    while (ok && done < min_bytes) {
        const uint8_t *code = (const uint8_t *)src + done;
        size_t size = 16;
        uint64_t address = base + done;
        if (!cs_disasm_iter(cs, &code, &size, &address, in) || n + in->size + 4 + 14 > sizeof out) {
            ok = 0;
            break;
        }
        uint8_t *p = out + n;
        memcpy(p, in->bytes, in->size);
        const cs_x86 *x = &in->detail->x86;
        const int64_t shift = (int64_t)(base + done) - (int64_t)(to + n);
        size_t emitted = in->size;
        if (is_relative_branch(in)) {
            const uint64_t target = branch_target(in);
            /* A branch into the copied bytes would run the hook's jump: refused once the copied
               length is known, below. */
            if (branches == sizeof branch_targets / sizeof branch_targets[0]) { ok = 0; break; }
            branch_targets[branches++] = target;
            /* A short jcc or jmp (rel8, no prefix) is written in its rel32 form, which reaches
               the original's target from the copy; loop and jrcxz have no rel32 form. */
            const uint8_t op = in->bytes[0];
            int imm_offset = x->encoding.imm_offset;
            if (x->encoding.imm_size == 1 && in->size == 2 && op >= 0x70 && op <= 0x7f) {
                p[0] = 0x0f, p[1] = (uint8_t)(0x80 + (op - 0x70));
                imm_offset = 2, emitted = 6;
            } else if (x->encoding.imm_size == 1 && in->size == 2 && op == 0xeb) {
                p[0] = 0xe9;
                imm_offset = 1, emitted = 5;
            } else if (x->encoding.imm_size != 4) {
                ok = 0;
                break;
            }
            const int64_t rel = (int64_t)target - (int64_t)(to + n + emitted);
            if (!fits_int32(rel)) { ok = 0; break; }
            const int32_t r32 = (int32_t)rel;
            memcpy(p + imm_offset, &r32, 4);
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
        n += emitted;
        done += in->size;
    }
    for (size_t i = 0; ok && i < branches; i++)
        if (branch_targets[i] > base && branch_targets[i] < base + done) ok = 0;
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
