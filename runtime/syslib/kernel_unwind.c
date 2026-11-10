/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System call group: kernel, threads and memory. What the title's C library (libc.elf) imports from
   libkernel for C++ exceptions and its exit-time handlers: HP libunwind's questions about modules
   and process memory, its context switch to a landing pad, and FreeBSD's __cxa_finalize helper.

   - sceKernelGetModuleInfoFromAddr(address, flags, info): the module that holds an address, in the
     0x1a8-byte structure shadPS4's module.h calls OrbisKernelModuleInfoEx (facts only): the size
     (0x0), the name (0x8, 256 bytes), the module id (0x108), TLS fields (0x10c-0x127), the init and
     fini functions (0x128, 0x130), the exception frame index and the frames themselves with their
     sizes (0x148, 0x150, 0x158, 0x15c), and up to four segments of {address, u32 size, s32 prot}
     (0x160) with their count (0x1a0). libunwind asks with flags 1 for the frame index of the code
     it unwinds through (libc.elf +0x61d34, which reads 0x148 and checks the index's version byte);
     __cxa_finalize with flags 2. As in shadPS4: flags 3 or more zero the structure and answer
     EINVAL (0x80020016), no structure EFAULT (0x8002000e), an address in no module ESRCH
     (0x80020003). The runtime answers from its record of the modules it mapped (rt_process.modules,
     runtime/boot.c): name, index as id, segments with their ELF flags (shadPS4 reports the program
     header's flags too) and sizes in memory, init and fini, and the frame index with the frames it
     points to (the index's second field, decoded as DWARF's pointer encoding says). The TLS fields
     are left 0: no caller reads them.
   - __elf_phdr_match_addr(info, address): FreeBSD libc's helper for __cxa_finalize (lib/libc/gen/
     elf_utils.c): whether an address lies in an executable segment of the module, so the
     handlers a module registered run when it is finalized. On the PS4 the module is described by
     the structure above (libc.elf +0x2f39e passes it): 1 when a segment with the execute flag
     (PF_X, 1) holds the address, else 0.
   - __Ux86_64_setcontext(context): libunwind's last step, which resumes execution at a landing pad
     with the registers it computed. No other project implements it. The layout is the one libc.elf's
     own getcontext writes (+0x53cd0, FreeBSD's ucontext_t with the machine context at 0x40): rdi 0x48,
     rsi 0x50, rdx 0x58, rcx 0x60, r8 0x68, r9 0x70, rax 0x78, rbx 0x80, rbp 0x88, r10 0x90, r11 0x98,
     r12-r15 0xa0-0xb8, rip 0xe0, rflags 0xf0, rsp 0xf8, the context's length 0x108 (0x480), the
     floating point format 0x110 (0x10002, FXSAVE) and the FXSAVE area at 0x120. Every register is
     restored, the x87/SSE state with fxrstor (maintainer's ruling of 2026-11-10); the return
     address is pushed just below the new stack pointer and returned to, as libunwind's own
     setcontext does. A context of another length or format stops by name.
   - sigreturn(context): returns from a signal handler. The runtime delivers no guest signals and
     answers _is_signal_return with 0 (runtime/syslib/kernel_libc_support.c), so libunwind never
     takes this path; reaching it stops by name (maintainer's ruling).
   - sysctl(name, length, old, old_length, new, new_length): FreeBSD sysctl(3). libunwind's only call
     (+0x52f50) asks for {CTL_KERN 1, KERN_PROC 14, KERN_PROC_VMMAP 32, pid}, the process's memory
     map, to check addresses it is about to read. No project implements it; the runtime answers -1
     with ENOENT (maintainer's ruling), which libunwind takes as "no map", and it falls back to its
     msync probe. Any other name stops by name.
   - sceKernelPrintBacktraceWithModuleInfo: libc.elf's fatal paths ask the system to print the
     calling thread's backtrace. The runtime cannot walk guest frames, so it says so on standard
     error and answers 0 (PS4Recomp and the deadinside28 port do the same; facts only). */
#include "runtime/guest_errno.h"
#include "runtime/process.h"
#include "runtime/syslib.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SCE_KERNEL_ERROR_ESRCH ((int32_t)0x80020003u)
#define SCE_KERNEL_ERROR_EFAULT ((int32_t)0x8002000eu)
#define SCE_KERNEL_ERROR_EINVAL ((int32_t)0x80020016u)
enum { MODULE_INFO_FLAGS_LIMIT = 3, MODULE_NAME_BYTES = 256, MODULE_INFO_SEGMENTS = 4, PF_EXECUTE = 1 };
enum { BSD_ENOENT = 2, CTL_KERN = 1, KERN_PROC = 14, KERN_PROC_VMMAP = 32, VMMAP_NAME_LENGTH = 4 };

typedef struct {
    uint64_t address;
    uint32_t size;
    int32_t prot;
} guest_module_segment;

typedef struct {
    uint64_t size;
    char name[MODULE_NAME_BYTES];
    int32_t id;
    uint32_t tls_index;
    uint64_t tls_init_address;
    uint32_t tls_init_size, tls_size, tls_offset, tls_align;
    uint64_t init, fini;
    uint64_t reserved[2];
    uint64_t eh_frame_hdr, eh_frame;
    uint32_t eh_frame_hdr_size, eh_frame_size;
    guest_module_segment segments[MODULE_INFO_SEGMENTS];
    uint32_t segment_count;
} guest_module_info;
_Static_assert(sizeof(guest_module_info) == 0x1a8, "the module information structure is 0x1a8 bytes");
_Static_assert(offsetof(guest_module_info, init) == 0x128, "init function at 0x128");
_Static_assert(offsetof(guest_module_info, eh_frame_hdr) == 0x148, "frame index at 0x148");
_Static_assert(offsetof(guest_module_info, segments) == 0x160, "segments at 0x160");
_Static_assert(offsetof(guest_module_info, segment_count) == 0x1a0, "segment count at 0x1a0");

static const rt_module *module_holding(uint64_t address)
{
    for (uint32_t i = 0; i < rt_process.module_count; i++) {
        const rt_module *m = &rt_process.modules[i];
        const uint64_t base = (uint64_t)(uintptr_t)m->image;
        if (address >= base && address - base < m->image_size) return m;
    }
    return NULL;
}

/* DWARF pointer encodings an .eh_frame_hdr uses for its frame pointer (LSB 4.1, "DWARF Extensions"):
   the value's format in the low four bits, what it is relative to in the next three. */
enum { DW_EH_PE_udata4 = 0x03, DW_EH_PE_udata8 = 0x04, DW_EH_PE_sdata4 = 0x0b, DW_EH_PE_sdata8 = 0x0c,
       DW_EH_PE_FORMAT = 0x0f, DW_EH_PE_absptr = 0x00, DW_EH_PE_pcrel = 0x10, DW_EH_PE_RELATION = 0x70 };
enum { EH_FRAME_HDR_VERSION = 1, EH_FRAME_HDR_POINTER = 4 };

/* Where the frames an exception frame index describes start: the index's version byte, the
   pointer's encoding, two more encodings, then the encoded pointer. */
static uint64_t eh_frame_start(const char *caller, uint64_t eh_frame_hdr)
{
    const uint8_t *hdr = (const uint8_t *)(uintptr_t)eh_frame_hdr;
    if (hdr[0] != EH_FRAME_HDR_VERSION) rt_fatal(caller, "an exception frame index of another version");
    const uint8_t encoding = hdr[1];
    const uint8_t *field = hdr + EH_FRAME_HDR_POINTER;
    int64_t value;
    switch (encoding & DW_EH_PE_FORMAT) {
    case DW_EH_PE_udata4: { uint32_t v; memcpy(&v, field, sizeof v); value = v; break; }
    case DW_EH_PE_sdata4: { int32_t v; memcpy(&v, field, sizeof v); value = v; break; }
    case DW_EH_PE_udata8:
    case DW_EH_PE_sdata8: memcpy(&value, field, sizeof value); break;
    default: rt_fatal(caller, "an exception frame pointer in a format the runtime does not decode");
    }
    switch (encoding & DW_EH_PE_RELATION) {
    case DW_EH_PE_absptr: return (uint64_t)value;
    case DW_EH_PE_pcrel: return (uint64_t)(uintptr_t)field + (uint64_t)value;
    default: rt_fatal(caller, "an exception frame pointer relative to something other than itself");
    }
}

static int32_t get_module_info_from_address(uint64_t address, int32_t flags, guest_module_info *info)
{
    static const char fn[] = "sceKernelGetModuleInfoFromAddr";
    if (flags >= MODULE_INFO_FLAGS_LIMIT) {
        if (info) memset(info, 0, sizeof *info);
        return SCE_KERNEL_ERROR_EINVAL;
    }
    if (!info) return SCE_KERNEL_ERROR_EFAULT;
    const rt_module *m = module_holding(address);
    if (!m) return SCE_KERNEL_ERROR_ESRCH;
    memset(info, 0, sizeof *info);
    info->size = sizeof *info;
    snprintf(info->name, sizeof info->name, "%s", m->name ? m->name : "");
    info->id = (int32_t)(m - rt_process.modules);
    info->init = m->init;
    info->fini = m->fini;
    if (m->eh_frame_hdr_size) {
        info->eh_frame_hdr = m->eh_frame_hdr;
        info->eh_frame_hdr_size = (uint32_t)m->eh_frame_hdr_size;
        info->eh_frame = eh_frame_start(fn, m->eh_frame_hdr);
        /* The frames run up to the index, as a linker lays them out (.eh_frame, then .eh_frame_hdr);
           an index before its frames gives no end, and is not met in practice. */
        if (info->eh_frame > m->eh_frame_hdr) rt_fatal(fn, "exception frames after their index: their size is not known");
        info->eh_frame_size = (uint32_t)(m->eh_frame_hdr - info->eh_frame);
    }
    for (uint32_t i = 0; i < m->segment_count && i < MODULE_INFO_SEGMENTS; i++)
        info->segments[i] = (guest_module_segment){m->segments[i].address, (uint32_t)m->segments[i].size,
                                                   (int32_t)m->segments[i].flags};
    info->segment_count = m->segment_count < MODULE_INFO_SEGMENTS ? m->segment_count : MODULE_INFO_SEGMENTS;
    return 0;
}

static int32_t elf_phdr_match_address(const guest_module_info *info, uint64_t address)
{
    if (!info) rt_fatal("__elf_phdr_match_addr", "no module information");
    const uint32_t count = info->segment_count < MODULE_INFO_SEGMENTS ? info->segment_count : MODULE_INFO_SEGMENTS;
    for (uint32_t i = 0; i < count; i++) {
        const guest_module_segment *s = &info->segments[i];
        if ((s->prot & PF_EXECUTE) && address >= s->address && address - s->address < s->size) return 1;
    }
    return 0;
}

/* The machine context as libc.elf's getcontext writes it; only what setcontext checks is named,
   the registers are read by offset in the assembly below. */
enum { CONTEXT_LENGTH_OFFSET = 0x108, CONTEXT_FP_FORMAT_OFFSET = 0x110, CONTEXT_FXSAVE_OFFSET = 0x120 };
enum { CONTEXT_LENGTH = 0x480, CONTEXT_FP_FORMAT_FXSAVE = 0x10002, FXSAVE_ALIGNMENT = 16 };

/* restore_guest_context(context): never returns. fxrstor first, then the flags (popfq on the
   current stack), the general registers, the new stack, the return address pushed onto it, and
   rcx and rdi last, since rdi holds the context. */
_Noreturn void restore_guest_context(const void *context) __asm__("pb_restore_guest_context");
__asm__(".text\n"
        "pb_restore_guest_context:\n"
        "  fxrstor 0x120(%rdi)\n"
        "  pushq 0xf0(%rdi)\n"
        "  popfq\n"
        "  mov 0x50(%rdi), %rsi\n  mov 0x58(%rdi), %rdx\n  mov 0x68(%rdi), %r8\n  mov 0x70(%rdi), %r9\n"
        "  mov 0x78(%rdi), %rax\n  mov 0x80(%rdi), %rbx\n  mov 0x88(%rdi), %rbp\n  mov 0x90(%rdi), %r10\n"
        "  mov 0x98(%rdi), %r11\n  mov 0xa0(%rdi), %r12\n  mov 0xa8(%rdi), %r13\n  mov 0xb0(%rdi), %r14\n"
        "  mov 0xb8(%rdi), %r15\n"
        "  mov 0xf8(%rdi), %rsp\n"
        "  pushq 0xe0(%rdi)\n"
        "  mov 0x60(%rdi), %rcx\n"
        "  mov 0x48(%rdi), %rdi\n"
        "  ret\n");

static void unwind_set_context(const uint8_t *context)
{
    static const char fn[] = "__Ux86_64_setcontext";
    if (!context) rt_fatal(fn, "no context");
    uint64_t length, format;
    memcpy(&length, context + CONTEXT_LENGTH_OFFSET, sizeof length);
    memcpy(&format, context + CONTEXT_FP_FORMAT_OFFSET, sizeof format);
    if (length != CONTEXT_LENGTH || format != CONTEXT_FP_FORMAT_FXSAVE)
        rt_fatal(fn, "a context of another length or floating point format than getcontext writes");
    if ((uintptr_t)(context + CONTEXT_FXSAVE_OFFSET) % FXSAVE_ALIGNMENT)
        rt_fatal(fn, "a floating point area off its 16-byte alignment (fxrstor would fault)");
    restore_guest_context(context);
}

static void kernel_sigreturn(const void *context)
{
    (void)context;
    rt_fatal("sigreturn", "the runtime delivers no guest signals, so there is no signal frame to return from");
}

static int32_t kernel_sysctl(const int32_t *name, uint32_t length, void *old, uint64_t *old_length, const void *new_value,
                             uint64_t new_length)
{
    (void)old, (void)old_length, (void)new_value, (void)new_length;
    if (!name || length != VMMAP_NAME_LENGTH || name[0] != CTL_KERN || name[1] != KERN_PROC || name[2] != KERN_PROC_VMMAP)
        rt_fatal("sysctl", "a name other than the process memory map (kern.proc.vmmap): not modelled");
    *rt_guest_errno() = BSD_ENOENT;
    return -1;
}

static int32_t print_backtrace_with_module_info(void)
{
    fputs("runtime: the guest asked for a backtrace (sceKernelPrintBacktraceWithModuleInfo); guest frames are not walked\n",
          stderr);
    return 0;
}

RT_SYSLIB("libkernel", sceKernelGetModuleInfoFromAddr, get_module_info_from_address);
RT_SYSLIB("libkernel", __elf_phdr_match_addr, elf_phdr_match_address);
RT_SYSLIB("libkernel", __Ux86_64_setcontext, unwind_set_context);
RT_SYSLIB("libkernel", sigreturn, kernel_sigreturn);
RT_SYSLIB("libkernel", sysctl, kernel_sysctl);
RT_SYSLIB("libkernel", sceKernelPrintBacktraceWithModuleInfo, print_backtrace_with_module_info);
