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

/* 0x1c0: an fs access: the host's own thread block, which a case cannot give: no case. */
    .org 0x1c0
    mov %fs:0, %rax
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

/* 0x2c0: int32_t use_import(int32_t *out) { return fill(out, 7) + *out; } where fill is an
   import, called through a thunk and its slot as executables call system functions. rdx is left
   pointing below the stack, as compiled code often leaves an unused argument register: what the
   import's own frames do there is not something it wrote for the caller. */
    .org 0x2c0
    push %rbx
    mov %rdi, %rbx
    mov $7, %esi
    lea -512(%rsp), %rdx
    call import_thunk
    add (%rbx), %eax
    pop %rbx
    ret

    .org 0x2e0
import_thunk:
    jmp *import_slot(%rip)

/* 0x300: void store_field(struct *s, int32_t v) { s->field_40 = v; } touches only +0x40. */
    .org 0x300
    mov %esi, 0x40(%rdi)
    ret
/* 0x340: int32_t read_far(const char *table) { return *(int32_t *)(table + 0x1000); } */
    .org 0x340
    mov 0x1000(%rdi), %eax
    ret
/* 0x380: int32_t frame_import(void) { int32_t local; fill(&local, 7); return local; } and it
   stores its stack pointer in frame_seen, so the test knows where its frame was. */
    .org 0x380
    sub $24, %rsp
    mov %rsp, frame_seen(%rip)
    mov %rsp, %rdi
    mov $7, %esi
    call import_thunk
    mov (%rsp), %eax
    add $24, %rsp
    ret
/* 0x3c0: int32_t read_returned(int32_t *p) { return *import(p, 7); } with an import that
   returns its first argument. */
    .org 0x3c0
    sub $8, %rsp
    mov $7, %esi
    call import_thunk
    mov (%rax), %eax
    add $8, %rsp
    ret
/* Native imports (test/test_capture_trace.cpp) called through their own slots below:
   plain stores 21 at rdi; far also changes the byte 1024 bytes further on, as another thread
   might; stale counts its calls in a counter of its own, stores the count at rdi and leaves rdx
   pointing at the counter. */
/* 0x400: int32_t stale_argument(int32_t *a) { stale(a); stale(a); return *a; } the second call
   leaves rdx as the first returned it. */
    .org 0x400
    push %rbx
    mov %rdi, %rbx
    call stale_thunk
    mov %rbx, %rdi
    call stale_thunk
    mov (%rbx), %eax
    pop %rbx
    ret
    .org 0x430
stale_thunk:
    jmp *stale_slot(%rip)
/* 0x440: void same_pointer(int32_t *a) { plain(a) } with rsi = rdi too. */
    .org 0x440
    sub $8, %rsp
    mov %rdi, %rsi
    call plain_thunk
    add $8, %rsp
    ret
    .org 0x470
plain_thunk:
    jmp *plain_slot(%rip)
/* 0x480: void far_untouched(char *p) { far(p); } */
    .org 0x480
    sub $8, %rsp
    call far_thunk
    add $8, %rsp
    ret
    .org 0x4b0
far_thunk:
    jmp *far_slot(%rip)
/* 0x4c0: int32_t far_touched(char *p) { int32_t v = p[1024]; far(p); return v; } */
    .org 0x4c0
    push %rbx
    movzbl 1024(%rdi), %ebx
    call far_thunk
    mov %ebx, %eax
    pop %rbx
    ret
/* 0x500: void two_fields(struct *s) { s->field_0 = 1; s->field_1000 = 2; } */
    .org 0x500
    movl $1, (%rdi)
    movl $2, 0x1000(%rdi)
    ret
/* 0x540: void store_last(const struct { int64_t pad, count; } *n, int64_t *slots)
   { slots[n->count - 1] = 2; } */
    .org 0x540
    mov 8(%rdi), %rcx
    movq $2, -8(%rsi,%rcx,8)
    ret
/* 0x580: int32_t index_after(const char *table)
   { const char *index = table + *(uint32_t *)table; return *(int32_t *)(index + 8); }
   the index pointer is computed by adding a loaded size, as the param lookups do. */
    .org 0x580
    mov (%rdi), %eax
    add %rdi, %rax
    mov 8(%rax), %eax
    ret
/* 0x5c0: the same through lea: index = table + size, then index[2]. */
    .org 0x5c0
    mov (%rdi), %eax
    lea (%rdi,%rax), %rcx
    mov 8(%rcx), %eax
    ret
/* 0x600: int32_t offset_into(const int32_t *table, const char *other)
   { return *(int32_t *)(other + table[0]); } with rax first pointing into the table: the 32-bit
   load replaces rax, so the access through it is no reach from the table. */
    .org 0x600
    lea 16(%rdi), %rax
    mov (%rdi), %eax
    mov (%rax,%rsi), %eax
    ret
/* 0x640: int32_t zero_local(void) { int32_t local; zero(&local); return local; } where the
   import writes 0, what a fresh stack already holds. */
    .org 0x640
    sub $24, %rsp
    lea 8(%rsp), %rdi
    call zero_thunk
    mov 8(%rsp), %eax
    add $24, %rsp
    ret
    .org 0x670
zero_thunk:
    jmp *zero_slot(%rip)
/* 0x680: void nap(void) { sceKernelUsleep(10000); } */
    .org 0x680
    sub $8, %rsp
    mov $10000, %edi
    call usleep_thunk
    add $8, %rsp
    ret
    .org 0x6b0
usleep_thunk:
    jmp *usleep_slot(%rip)
/* 0x6c0: int32_t thread_counter(void) { int32_t *p = *(int32_t **)(tp + tls_offset); return ++*p; }
   A thread-local variable reached as the game reaches one: the thread pointer from gs:[0] (the
   loader's rewrite of fs:[0]) plus the variable's offset, read from a global. */
    .org 0x6c0
    mov %gs:0, %rax
    add tls_offset(%rip), %rax
    mov (%rax), %rax
    mov (%rax), %ecx
    add $1, %ecx
    mov %ecx, (%rax)
    mov %ecx, %eax
    ret
/* 0x700: int32_t reread_counter(int32_t *a) { int32_t *c = stale(a); *c; stale(a); return *c; }
   the counter is read, then changed by the second call behind a register the first call left
   (not an argument, so not the import's write), as another thread might change it, and read
   again: no case. */
    .org 0x700
    push %rbx
    push %r12
    sub $8, %rsp
    mov %rdi, %rbx
    call stale_thunk
    mov %rdx, %r12
    mov (%r12), %eax
    mov %rbx, %rdi
    call stale_thunk
    mov (%r12), %eax
    add $8, %rsp
    pop %r12
    pop %rbx
    ret
/* 0x740: int32_t rewritten_counter(int32_t *a) { int32_t *c = stale(a); *c = 5; stale(a); return *c; }
   the counter is written, then changed by the second call behind a register the first call left,
   as another thread might change memory the code wrote, and read again: a replay would read the
   5 it wrote, so no case. */
    .org 0x740
    push %rbx
    push %r12
    sub $8, %rsp
    mov %rdi, %rbx
    call stale_thunk
    mov %rdx, %r12
    movl $5, (%r12)
    mov %rbx, %rdi
    call stale_thunk
    mov (%r12), %eax
    add $8, %rsp
    pop %r12
    pop %rbx
    ret
/* 0x780: void *take(Pool *p, uint64_t n) { the next n bytes of [top, end), or 0 }  Pool: +0x10 top,
   +0x18 end. Its short ja lies in the first 14 bytes (a hook's jump), with its target just past
   them: relocation writes it in its 32-bit form. */
    .org 0x780
    mov 0x10(%rdi), %rcx
    add %rcx, %rsi
    xor %eax, %eax
    cmp 0x18(%rdi), %rsi
    ja 1f
    mov %rsi, 0x10(%rdi)
    mov %rcx, %rax
1:  ret
/* 0x7c0: a short branch back into the first 14 bytes: cannot be relocated. */
    .org 0x7c0
    xor %eax, %eax
1:  inc %eax
    cmp $3, %eax
    jb 1b
    xor %edx, %edx
    xor %ecx, %ecx
    xor %esi, %esi
    ret
/* 0x800: int32_t bump(int32_t x) { return x >= 0 ? x + 1 : -x; } with a short jns and a short jmp
   in the first 14 bytes, the jns landing on the first byte after them. */
    .org 0x800
    mov %edi, %eax
    test %eax, %eax
    jns 1f
    neg %eax
    jmp 2f
    xor %edx, %edx
    xor %ecx, %ecx
1:  add $1, %eax
2:  ret
/* 0x840: int32_t bit_set(const int32_t *p) { return (*p & 4) != 0; } reading *p only through
   `test m, r`, whose memory operand Capstone marks with no access: still an input. */
    .org 0x840
    xor %eax, %eax
    mov $4, %ecx
    test %ecx, (%rdi)
    setne %al
    ret
    .org 0x1000
global:
    .long 1000

/* 0x1800: the import's slot; the test (or the verifier's loader) binds it. */
    .org 0x1800
import_slot:
    .quad 0
/* 0x1808: where frame_import leaves its stack pointer. */
frame_seen:
    .quad 0
/* 0x1810..0x1820: the native imports' slots. */
stale_slot:
    .quad 0
plain_slot:
    .quad 0
far_slot:
    .quad 0
zero_slot:
    .quad 0
usleep_slot:
    .quad 0
/* 0x1838: thread_counter's variable, 4 KiB below the thread pointer (TLS variant II). */
tls_offset:
    .quad -0x1000
    .org 0x2000
    .globl capture_trace_guest_end
capture_trace_guest_end:

    .section .note.GNU-stack, "", @progbits
