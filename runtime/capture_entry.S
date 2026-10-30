/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Entry of a hooked call while capture is on (runtime/capture_hooks.c). Each hook's thunk loads
   its capture_hook into r11 and jumps here; rsp is still the caller's, the return address at
   [rsp]. The frame, laid out by rt_capture_frame:
     0 rdi rsi rdx rcx r8 r9, 48 rax (al: vector registers of a variadic call), 56 hook,
     64 caller rsp, 80 xmm0..xmm7, 208 rax, 216 rdx, 224 xmm0, 240 xmm1 (results)
   rt_capture_dispatch returns 0: the registers are restored and the call goes on to the
   replacement exactly as it arrived; or 1: the original ran traced, and its result returns to
   the caller. */

    .text
    .globl rt_capture_entry
    .hidden rt_capture_entry
    .type rt_capture_entry, @function
rt_capture_entry:
    mov %rsp, %r10
    sub $264, %rsp                  /* 8 mod 16 at entry: the call below is aligned */
    mov %rdi, 0(%rsp)
    mov %rsi, 8(%rsp)
    mov %rdx, 16(%rsp)
    mov %rcx, 24(%rsp)
    mov %r8, 32(%rsp)
    mov %r9, 40(%rsp)
    mov %rax, 48(%rsp)
    mov %r11, 56(%rsp)
    mov %r10, 64(%rsp)
    movdqu %xmm0, 80(%rsp)
    movdqu %xmm1, 96(%rsp)
    movdqu %xmm2, 112(%rsp)
    movdqu %xmm3, 128(%rsp)
    movdqu %xmm4, 144(%rsp)
    movdqu %xmm5, 160(%rsp)
    movdqu %xmm6, 176(%rsp)
    movdqu %xmm7, 192(%rsp)
    mov %r11, %rdi
    mov %rsp, %rsi
    call rt_capture_dispatch
    test %eax, %eax
    jnz 1f
    movdqu 80(%rsp), %xmm0
    movdqu 96(%rsp), %xmm1
    movdqu 112(%rsp), %xmm2
    movdqu 128(%rsp), %xmm3
    movdqu 144(%rsp), %xmm4
    movdqu 160(%rsp), %xmm5
    movdqu 176(%rsp), %xmm6
    movdqu 192(%rsp), %xmm7
    mov 0(%rsp), %rdi
    mov 8(%rsp), %rsi
    mov 16(%rsp), %rdx
    mov 24(%rsp), %rcx
    mov 32(%rsp), %r8
    mov 40(%rsp), %r9
    mov 48(%rsp), %rax
    mov 56(%rsp), %r11
    mov 0(%r11), %r11               /* capture_hook.replacement */
    add $264, %rsp
    jmp *%r11
1:
    mov 208(%rsp), %rax
    mov 216(%rsp), %rdx
    movdqu 224(%rsp), %xmm0
    movdqu 240(%rsp), %xmm1
    add $264, %rsp
    ret
    .size rt_capture_entry, . - rt_capture_entry

    .section .note.GNU-stack, "", @progbits
