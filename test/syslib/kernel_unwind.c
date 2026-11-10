/* SPDX-License-Identifier: GPL-2.0-or-later */
/* What libc.elf's exception and exit machinery imports (runtime/syslib/kernel_unwind.c): module
   information by address, the executable-segment test, the context switch to a landing pad,
   sigreturn, the process memory map sysctl and the backtrace request. */
#include "../syslib_test.h"

#include "runtime/guest_errno.h"
#include "runtime/process.h"

#include <stdint.h>
#include <string.h>

typedef int32_t (*info_fn)(uint64_t, int32_t, void *);
typedef int32_t (*match_fn)(const void *, uint64_t);
typedef void (*context_fn)(void *);
typedef int32_t (*sysctl_fn)(const int32_t *, uint32_t, void *, uint64_t *, const void *, uint64_t);
typedef int32_t (*void_fn)(void);

enum { INFO_SIZE = 0x1a8, ESRCH_SCE = (int32_t)0x80020003u, EFAULT_SCE = (int32_t)0x8002000eu, EINVAL_SCE = (int32_t)0x80020016u };

/* A synthetic module: code, data, and an exception frame index after its frames. */
static uint8_t image[0x8000] __attribute__((aligned(0x4000)));
enum { FRAMES = 0x1000, FRAMES_SIZE = 0x100, HDR = FRAMES + FRAMES_SIZE, HDR_SIZE = 0x40 };

static info_fn info_of;
static context_fn set_context;
static sysctl_fn sysctl_;

static void write_hdr(uint8_t version, uint8_t encoding, int64_t value)
{
    uint8_t *hdr = image + HDR;
    hdr[0] = version, hdr[1] = encoding, hdr[2] = 0x03, hdr[3] = 0x3b;
    if ((encoding & 0x0f) == 0x04) memcpy(hdr + 4, &value, 8);
    else {
        const int32_t narrow = (int32_t)value;
        memcpy(hdr + 4, &narrow, 4);
    }
}

static void info_bad_version(void)
{
    uint8_t info[INFO_SIZE];
    write_hdr(2, 0x1b, -(FRAMES_SIZE + 4));
    info_of((uint64_t)(uintptr_t)image, 1, info);
}
static void info_frames_after_index(void)
{
    uint8_t info[INFO_SIZE];
    write_hdr(1, 0x1b, 0x100);
    info_of((uint64_t)(uintptr_t)image, 1, info);
}
static void info_unknown_encoding(void)
{
    uint8_t info[INFO_SIZE];
    write_hdr(1, 0x3b, 0); /* datarel */
    info_of((uint64_t)(uintptr_t)image, 1, info);
}

/* The landing pad: records every register as it arrives, then jumps back to the test. */
static uint64_t landing_registers[20] __asm__("pb_test_landing_registers") __attribute__((used));
static void *landing_jump[5];
void landing_done(void) __asm__("pb_test_landing_done") __attribute__((used, noreturn));
void landing_done(void) { __builtin_longjmp(landing_jump, 1); }
void test_landing(void) __asm__("pb_test_landing");
__asm__(".text\n"
        "pb_test_landing:\n"
        "  mov %rax, pb_test_landing_registers+0(%rip)\n"
        "  mov %rbx, pb_test_landing_registers+8(%rip)\n"
        "  mov %rcx, pb_test_landing_registers+16(%rip)\n"
        "  mov %rdx, pb_test_landing_registers+24(%rip)\n"
        "  mov %rsi, pb_test_landing_registers+32(%rip)\n"
        "  mov %rdi, pb_test_landing_registers+40(%rip)\n"
        "  mov %rbp, pb_test_landing_registers+48(%rip)\n"
        "  mov %r8, pb_test_landing_registers+56(%rip)\n"
        "  mov %r9, pb_test_landing_registers+64(%rip)\n"
        "  mov %r10, pb_test_landing_registers+72(%rip)\n"
        "  mov %r11, pb_test_landing_registers+80(%rip)\n"
        "  mov %r12, pb_test_landing_registers+88(%rip)\n"
        "  mov %r13, pb_test_landing_registers+96(%rip)\n"
        "  mov %r14, pb_test_landing_registers+104(%rip)\n"
        "  mov %r15, pb_test_landing_registers+112(%rip)\n"
        "  mov %rsp, pb_test_landing_registers+120(%rip)\n"
        "  pushfq\n"
        "  popq pb_test_landing_registers+128(%rip)\n"
        "  stmxcsr pb_test_landing_registers+136(%rip)\n"
        "  movq %xmm0, pb_test_landing_registers+144(%rip)\n"
        "  cld\n"
        "  and $-16, %rsp\n"
        "  call pb_test_landing_done\n"
        "  ud2\n");

/* Machine context offsets libc.elf's getcontext writes. */
enum { RDI = 0x48, RSI = 0x50, RDX = 0x58, RCX = 0x60, R8 = 0x68, R9 = 0x70, RAX = 0x78, RBX = 0x80, RBP = 0x88,
       R10 = 0x90, R11 = 0x98, R12 = 0xa0, R13 = 0xa8, R14 = 0xb0, R15 = 0xb8, RIP = 0xe0, RFLAGS = 0xf0,
       RSP = 0xf8, LENGTH = 0x108, FORMAT = 0x110, FXSAVE = 0x120, CONTEXT_BYTES = 0x480,
       FXSAVE_MXCSR = 24, FXSAVE_XMM0 = 160 };
static uint8_t context[CONTEXT_BYTES + 16] __attribute__((aligned(16)));
static uint8_t landing_stack[0x10000] __attribute__((aligned(16)));

static void put(uint8_t *at, uint64_t value) { memcpy(at, &value, sizeof value); }
static uint64_t register_value(int offset) { return 0x1000000000000000ull + (uint64_t)offset * 0x10101; }
static const int restored[] = {RAX, RBX, RCX, RDX, RSI, RDI, RBP, R8, R9, R10, R11, R12, R13, R14, R15};

static void fill_context(uint8_t *c)
{
    memset(c, 0, CONTEXT_BYTES);
    for (size_t i = 0; i < sizeof restored / sizeof *restored; i++) put(c + restored[i], register_value(restored[i]));
    put(c + RIP, (uint64_t)(uintptr_t)test_landing);
    put(c + RSP, (uint64_t)(uintptr_t)(landing_stack + sizeof landing_stack - 8)); /* as just after a call */
    put(c + RFLAGS, 0x243);                                                         /* CF and ZF set */
    put(c + LENGTH, CONTEXT_BYTES);
    put(c + FORMAT, 0x10002);
    __asm__ volatile("fxsave %0" : "=m"(*(uint8_t(*)[512])(c + FXSAVE)));
    const uint32_t mxcsr = 0x7f80; /* round toward zero */
    memcpy(c + FXSAVE + FXSAVE_MXCSR, &mxcsr, sizeof mxcsr);
    put(c + FXSAVE + FXSAVE_XMM0, 0x0123456789abcdefull);
}

static void context_null(void) { set_context(NULL); }
static void context_other_length(void) { fill_context(context); put(context + LENGTH, 0x400); set_context(context); }
static void context_other_format(void) { fill_context(context); put(context + FORMAT, 0x10001); set_context(context); }
static void context_misaligned(void)
{
    static uint8_t odd[CONTEXT_BYTES + 32] __attribute__((aligned(16)));
    put(odd + 8 + LENGTH, CONTEXT_BYTES); /* a valid header, but fxrstor would fault */
    put(odd + 8 + FORMAT, 0x10002);
    set_context(odd + 8);
}
static match_fn match_entry;
static void match_no_info(void) { match_entry(NULL, 0); }
static void sigreturn_call(void) { ((context_fn)fn("libkernel", "sigreturn"))(context); }
static void sysctl_other(void)
{
    const int32_t name[] = {1, 6};
    sysctl_(name, 2, NULL, NULL, NULL, 0);
}

SYSLIB_TEST(kernel_unwind)
{
    info_of = (info_fn)fn("libkernel", "sceKernelGetModuleInfoFromAddr");
    const match_fn match = (match_fn)fn("libkernel", "__elf_phdr_match_addr");
    set_context = (context_fn)fn("libkernel", "__Ux86_64_setcontext");
    sysctl_ = (sysctl_fn)fn("libkernel", "sysctl");
    const void_fn backtrace = (void_fn)fn("libkernel", "sceKernelPrintBacktraceWithModuleInfo");
    CHECK(is_implemented("libkernel", "sceKernelGetModuleInfoFromAddr") && is_implemented("libkernel", "__elf_phdr_match_addr"));
    CHECK(is_implemented("libkernel", "__Ux86_64_setcontext") && is_implemented("libkernel", "sigreturn"));
    CHECK(is_implemented("libkernel", "sysctl") && is_implemented("libkernel", "sceKernelPrintBacktraceWithModuleInfo"));
    if (!info_of || !match || !set_context || !sysctl_ || !backtrace) return;

    /* Module information, from the runtime's record of the modules it mapped. */
    const uint32_t saved_count = rt_process.module_count;
    const rt_module saved = rt_process.modules[0];
    const uint64_t base = (uint64_t)(uintptr_t)image;
    rt_process.modules[0] = (rt_module){.name = "synthetic.elf", .image = image, .image_size = sizeof image,
                                        .segments = {{base, 0x2000, 5}, {base + 0x4000, 0x1000, 6}},
                                        .segment_count = 2, .eh_frame_hdr = base + HDR, .eh_frame_hdr_size = HDR_SIZE,
                                        .init = base + 0x10, .fini = base + 0x20};
    rt_process.module_count = 1;
    write_hdr(1, 0x1b, -(FRAMES_SIZE + 4)); /* pc-relative: from the field, at HDR + 4, back to FRAMES */

    uint8_t info[INFO_SIZE];
    memset(info, 0xa5, sizeof info);
    CHECK(info_of(base + 0x4800, 1, info) == 0);
    uint64_t u64;
    uint32_t u32;
    int32_t s32;
    memcpy(&u64, info, 8); CHECK(u64 == INFO_SIZE);
    CHECK(strcmp((const char *)info + 8, "synthetic.elf") == 0);
    memcpy(&s32, info + 0x108, 4); CHECK(s32 == 0);
    memcpy(&u64, info + 0x110, 8); CHECK(u64 == 0); /* TLS fields left 0 */
    memcpy(&u64, info + 0x128, 8); CHECK(u64 == base + 0x10);
    memcpy(&u64, info + 0x130, 8); CHECK(u64 == base + 0x20);
    memcpy(&u64, info + 0x148, 8); CHECK(u64 == base + HDR);
    memcpy(&u64, info + 0x150, 8); CHECK(u64 == base + FRAMES);
    memcpy(&u32, info + 0x158, 4); CHECK(u32 == HDR_SIZE);
    memcpy(&u32, info + 0x15c, 4); CHECK(u32 == FRAMES_SIZE);
    memcpy(&u64, info + 0x160, 8); CHECK(u64 == base);
    memcpy(&u32, info + 0x168, 4); CHECK(u32 == 0x2000);
    memcpy(&s32, info + 0x16c, 4); CHECK(s32 == 5);
    memcpy(&u64, info + 0x170, 8); CHECK(u64 == base + 0x4000);
    memcpy(&s32, info + 0x17c, 4); CHECK(s32 == 6);
    memcpy(&u64, info + 0x180, 8); CHECK(u64 == 0); /* unused segments zeroed */
    memcpy(&u32, info + 0x1a0, 4); CHECK(u32 == 2);
    CHECK(info[0x1a7] == 0);

    /* An absolute eight-byte pointer decodes too. */
    write_hdr(1, 0x04, (int64_t)(base + FRAMES));
    CHECK(info_of(base, 2, info) == 0);
    memcpy(&u64, info + 0x150, 8); CHECK(u64 == base + FRAMES);
    write_hdr(1, 0x03, (int64_t)0x812345);
    CHECK(info_of(base, 2, info) == 0);
    memcpy(&u64, info + 0x150, 8); CHECK(u64 == 0x812345); /* unsigned four bytes */

    /* shadPS4's errors: flags 3 or more zero the structure first. */
    memset(info, 0xa5, sizeof info);
    CHECK(info_of(base, 3, info) == EINVAL_SCE && info[0] == 0 && info[INFO_SIZE - 1] == 0);
    CHECK(info_of(base, 3, NULL) == EINVAL_SCE);
    CHECK(info_of(base, 1, NULL) == EFAULT_SCE);
    memset(info, 0xa5, sizeof info);
    CHECK(info_of(base + sizeof image, 1, info) == ESRCH_SCE && info[0] == 0xa5);
    CHECK(info_of(base - 1, 0, info) == ESRCH_SCE);
    CHECK(died_in_fatal(info_bad_version));
    CHECK(died_in_fatal(info_frames_after_index));
    CHECK(died_in_fatal(info_unknown_encoding));

    /* A module without a frame index answers 0 for it. */
    rt_process.modules[0].eh_frame_hdr = rt_process.modules[0].eh_frame_hdr_size = 0;
    write_hdr(2, 0x1b, 0); /* never read */
    CHECK(info_of(base, 1, info) == 0);
    memcpy(&u64, info + 0x148, 8); CHECK(u64 == 0);
    memcpy(&u64, info + 0x150, 8); CHECK(u64 == 0);

    /* The executable-segment test reads the structure the query filled. */
    CHECK(match(info, base) == 1 && match(info, base + 0x1fff) == 1);
    CHECK(match(info, base + 0x2000) == 0);                 /* past the code */
    CHECK(match(info, base + 0x4800) == 0);                 /* data, not executable */
    CHECK(match(info, base - 1) == 0);
    memcpy(info + 0x1a0, &(uint32_t){1}, 4);
    memcpy(info + 0x16c, &(int32_t){6}, 4);
    CHECK(match(info, base) == 0);                          /* the flag, not the place, decides */
    memcpy(info + 0x1a0, &(uint32_t){0}, 4);
    memcpy(info + 0x16c, &(int32_t){5}, 4);
    CHECK(match(info, base) == 0);                          /* only counted segments */
    memcpy(info + 0x1a0, &(uint32_t){9}, 4);
    CHECK(match(info, base) == 1);                          /* a count above four reads four */
    match_entry = match;
    CHECK(died_in_fatal(match_no_info));
    rt_process.modules[0] = saved;
    rt_process.module_count = saved_count;

    /* The landing pad gets every register, the flags and the x87/SSE state. */
    fill_context(context);
    if (__builtin_setjmp(landing_jump) == 0) set_context(context);
    __asm__ volatile("ldmxcsr %0" : : "m"((uint32_t){0x1f80}));
    for (size_t i = 0; i < sizeof restored / sizeof *restored; i++) CHECK(landing_registers[i] == register_value(restored[i]));
    CHECK(landing_registers[15] == (uint64_t)(uintptr_t)(landing_stack + sizeof landing_stack - 8));
    CHECK((landing_registers[16] & 0x8d5) == 0x41);
    CHECK((uint32_t)landing_registers[17] == 0x7f80);
    CHECK(landing_registers[18] == 0x0123456789abcdefull);
    CHECK(died_in_fatal(context_null));
    CHECK(died_in_fatal(context_other_length));
    CHECK(died_in_fatal(context_other_format));
    CHECK(died_in_fatal(context_misaligned));

    /* No guest signals: sigreturn stops by name. */
    CHECK(died_in_fatal(sigreturn_call));

    /* The process memory map: -1 with ENOENT, nothing written; any other name stops. */
    const int32_t vmmap[] = {1, 14, 32, 1000};
    uint8_t old[16] = {0};
    uint64_t old_length = sizeof old;
    *rt_guest_errno() = 0;
    CHECK(sysctl_(vmmap, 4, old, &old_length, NULL, 0) == -1 && *rt_guest_errno() == 2);
    CHECK(old_length == sizeof old && old[0] == 0);
    CHECK(died_in_fatal(sysctl_other));

    CHECK(backtrace() == 0);
}
