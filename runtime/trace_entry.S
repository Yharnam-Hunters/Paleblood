/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Entry and landing for rt_trace_call (runtime/trace.c). The frame, laid out by rt_trace_frame:
     0 entry, 8 private stack top, 16 rdi rsi rdx rcx r8 r9, 64 xmm0..xmm7, 192 saved host rsp,
     200 rax, 208 rdx, 216 xmm0, 232 xmm1 (results)
   The private stack top holds the return address (rt_trace_landing), then the stack arguments,
   then the frame's address, which the landing reads back once the call has returned. */

    .text
    .globl rt_trace_enter
    .hidden rt_trace_enter
    .type rt_trace_enter, @function
rt_trace_enter:
    push %rbx
    push %rbp
    push %r12
    push %r13
    push %r14
    push %r15
    mov %rsp, 192(%rdi)
    mov 8(%rdi), %rsp
    movdqu 64(%rdi), %xmm0
    movdqu 80(%rdi), %xmm1
    movdqu 96(%rdi), %xmm2
    movdqu 112(%rdi), %xmm3
    movdqu 128(%rdi), %xmm4
    movdqu 144(%rdi), %xmm5
    movdqu 160(%rdi), %xmm6
    movdqu 176(%rdi), %xmm7
    mov 24(%rdi), %rsi
    mov 32(%rdi), %rdx
    mov 40(%rdi), %rcx
    mov 48(%rdi), %r8
    mov 56(%rdi), %r9
    mov 0(%rdi), %r11
    mov 16(%rdi), %rdi
    /* The trap flag takes effect after the next instruction: the first trap is at the entry. */
    pushfq
    orq $0x100, (%rsp)
    popfq
    jmp *%r11
    .size rt_trace_enter, . - rt_trace_enter

    .globl rt_trace_landing
    .hidden rt_trace_landing
    .type rt_trace_landing, @function
rt_trace_landing:
    mov 512(%rsp), %r11         /* RT_TRACE_STACK_SLOTS * 8 past the return address slot */
    mov %rax, 200(%r11)
    mov %rdx, 208(%r11)
    movdqu %xmm0, 216(%r11)
    movdqu %xmm1, 232(%r11)
    mov 192(%r11), %rsp
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %rbp
    pop %rbx
    ret
    .size rt_trace_landing, . - rt_trace_landing

    .section .note.GNU-stack, "", @progbits
