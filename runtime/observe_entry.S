/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Entry of an observed call (runtime/observe_hooks.c). The hook's thunk loads its observed_hook into
   r11 and jumps here, with rsp still the caller's and the return address at [rsp]. The argument
   registers (and rax, xmm0-7, which a call may also pass) are kept, rt_observe_hook_dispatch gets the
   hook and the six integer arguments, then the registers are put back and the call goes on into the
   original's relocated entry, the hook's first field. Frame: 0 rdi rsi rdx rcx r8 r9, 48 rax,
   56 hook, 64 xmm0..xmm7. */

#if defined(__APPLE__) && defined(__MACH__)
#define RT_SYMBOL(name) _##name
#else
#define RT_SYMBOL(name) name
#endif

    .text
    .globl RT_SYMBOL(rt_observe_entry)
#if defined(__APPLE__) && defined(__MACH__)
    .private_extern RT_SYMBOL(rt_observe_entry)
#else
    .hidden rt_observe_entry
    .type rt_observe_entry, @function
#endif
RT_SYMBOL(rt_observe_entry):
    sub $200, %rsp                  /* 8 mod 16 at entry: the call below is aligned */
    mov %rdi, 0(%rsp)
    mov %rsi, 8(%rsp)
    mov %rdx, 16(%rsp)
    mov %rcx, 24(%rsp)
    mov %r8, 32(%rsp)
    mov %r9, 40(%rsp)
    mov %rax, 48(%rsp)
    mov %r11, 56(%rsp)
    movdqu %xmm0, 64(%rsp)
    movdqu %xmm1, 80(%rsp)
    movdqu %xmm2, 96(%rsp)
    movdqu %xmm3, 112(%rsp)
    movdqu %xmm4, 128(%rsp)
    movdqu %xmm5, 144(%rsp)
    movdqu %xmm6, 160(%rsp)
    movdqu %xmm7, 176(%rsp)
    mov %r11, %rdi
    mov %rsp, %rsi
    call RT_SYMBOL(rt_observe_hook_dispatch)
    movdqu 64(%rsp), %xmm0
    movdqu 80(%rsp), %xmm1
    movdqu 96(%rsp), %xmm2
    movdqu 112(%rsp), %xmm3
    movdqu 128(%rsp), %xmm4
    movdqu 144(%rsp), %xmm5
    movdqu 160(%rsp), %xmm6
    movdqu 176(%rsp), %xmm7
    mov 0(%rsp), %rdi
    mov 8(%rsp), %rsi
    mov 16(%rsp), %rdx
    mov 24(%rsp), %rcx
    mov 32(%rsp), %r8
    mov 40(%rsp), %r9
    mov 48(%rsp), %rax
    mov 56(%rsp), %r11
    add $200, %rsp
    jmp *(%r11)
#if !defined(__APPLE__) || !defined(__MACH__)
    .size rt_observe_entry, .-rt_observe_entry
    .section .note.GNU-stack,"",@progbits
#endif
