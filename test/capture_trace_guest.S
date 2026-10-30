/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Synthetic guest code for test/test_capture_trace.cpp: two pages copied into a mapped image
   (code, then a page of globals). Offsets are fixed so the replay test can build the same image
   as an executable: guest address = 0x400000 + offset. */

    .section .rodata.capture_trace_guest, "a"
    .p2align 12
    .globl capture_trace_guest_start
capture_trace_guest_start:

/* 0x000: int32_t scale(int32_t *out, const Outer *s, int32_t factor)
   { *out = s->inner->value * factor + global; return *out; }  Outer: +8 Inner *inner; Inner: +4 value */
    .org 0x000
    mov 8(%rsi), %rax
    mov 4(%rax), %eax
    imul %edx, %eax
    add global(%rip), %eax
    mov %eax, (%rdi)
    ret

/* 0x040: int64_t stack_args(int64_t a, b, c, d, e, f, int64_t g7, int64_t g8)
   { return g7 * 10 + g8 + a; } */
    .org 0x040
    mov 8(%rsp), %rax
    imul $10, %rax, %rax
    add 16(%rsp), %rax
    add %rdi, %rax
    ret

/* 0x080: float scale_float(float x, const float *p) { return x * *p; } */
    .org 0x080
    mulss (%rdi), %xmm0
    ret

/* 0x0c0: int32_t twice(int32_t *p) { increment(p); return increment(p); } */
    .org 0x0c0
    push %rbx
    mov %rdi, %rbx
    call increment
    mov %rbx, %rdi
    call increment
    pop %rbx
    ret

/* 0x100: int32_t increment(int32_t *p) { return ++*p; } */
    .org 0x100
increment:
    mov (%rdi), %eax
    add $1, %eax
    mov %eax, (%rdi)
    ret

/* 0x140: void copy(char *dst, const char *src, size_t n) { memcpy(dst, src, n); } */
    .org 0x140
    mov %rdx, %rcx
    rep movsb
    ret

/* 0x180: reads xmm2, an argument a case cannot give: no case. */
    .org 0x180
    movaps %xmm2, %xmm0
    ret

/* 0x1c0: thread-local storage: no case. */
    .org 0x1c0
    mov %gs:0, %rax
    ret

/* 0x200: calls native code through a pointer: no case. */
    .org 0x200
    sub $8, %rsp
    call *(%rdi)
    add $8, %rsp
    ret

/* 0x240: a long loop, for the instruction limit: no case when the limit is low. */
    .org 0x240
    mov $1000, %ecx
1:  dec %ecx
    jnz 1b
    ret

/* 0x280: int32_t count_down(int32_t n) { while (n) n--; return 7; } its loop jumps back into
   the first bytes, which a hook overwrites: this function cannot be recorded. */
    .org 0x280
    mov %edi, %ecx
2:  test %ecx, %ecx
    jz 3f
    dec %ecx
    jmp 2b
3:  mov $7, %eax
    ret

    .org 0x1000
global:
    .long 1000
    .org 0x2000
    .globl capture_trace_guest_end
capture_trace_guest_end:

    .section .note.GNU-stack, "", @progbits
