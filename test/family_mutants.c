/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Deliberately wrong replacements for the function families (game/leaf/family_cases.py): every
   family's cases must fail on these, which shows the cases can tell a wrong body from a right
   one. Built as libbbfamily_mutants.so for tools/verify.py family; never part of the game
   library. */
#include <stdint.h>

/* The harness runs a replacement library's bbgame_init first; these need no setup. */
int bbgame_init(const void *host)
{
    (void)host;
    return 0;
}

/* Does nothing: wrong for every family that returns a value (rax keeps its entry value). */
void bb_mutant_does_nothing(void)
{
}

/* Writes through its first argument: wrong for a body that must not touch memory. */
void bb_mutant_writes_first_argument(uint8_t *first)
{
    *first = 0;
}

/* Returns zero: wrong where rax must keep its entry value or hold another value. */
uint64_t bb_mutant_returns_zero(void)
{
    return 0;
}

/* Returns one. */
uint64_t bb_mutant_returns_one(void)
{
    return 1;
}

/* Returns its first argument. */
uint64_t bb_mutant_returns_first_argument(uint64_t first)
{
    return first;
}

/* Reads the first 32 bits of its object: the wrong width for a 64-bit getter. */
uint64_t bb_mutant_reads_first_dword(const uint32_t *object)
{
    return *object;
}

/* Reads the first 64 bits of its object: the wrong width for a 32-bit getter. */
uint64_t bb_mutant_reads_first_qword(const uint64_t *object)
{
    return *object;
}

/* Returns its second argument. */
uint64_t bb_mutant_returns_second_argument(uint64_t first, uint64_t second)
{
    (void)first;
    return second;
}

/* Stores the low 32 bits of its second argument at the start of its object: the wrong width for
   a 64-bit setter. */
void bb_mutant_writes_first_dword(uint32_t *object, uint64_t value)
{
    *object = (uint32_t)value;
}

/* Stores all 64 bits of its second argument at the start of its object: the wrong width for a
   32-bit setter. */
void bb_mutant_writes_first_qword(uint64_t *object, uint64_t value)
{
    *object = value;
}

/* Clears its second argument register and returns: wrong for a body that must leave the
   argument registers alone (the cases compare them after the call). */
__attribute__((naked)) void bb_mutant_clobbers_second_argument(void)
{
    __asm__("xorl %esi, %esi\n\tret");
}

/* Returns 1.0f: the wrong float. */
float bb_mutant_returns_float_one(void)
{
    return 1.0f;
}

/* Returns zero correctly but also clears its second argument register: caught only by the
   argument-register comparison. */
__attribute__((naked)) void bb_mutant_returns_zero_clobbers_second_argument(void)
{
    __asm__("xorl %eax, %eax\n\txorl %esi, %esi\n\tret");
}

/* Stores its second argument at the start of its object correctly, then clears eax: caught only
   because a setter must leave rax. */
__attribute__((naked)) void bb_mutant_writes_first_qword_clears_eax(void)
{
    __asm__("movq %rsi, (%rdi)\n\txorl %eax, %eax\n\tret");
}

/* Zeroes only the low float of xmm0, keeping its other lanes: caught only because all of xmm0
   must be zero. */
__attribute__((naked)) void bb_mutant_zeroes_low_float_only(void)
{
    __asm__("xorl %eax, %eax\n\tpinsrd $0, %eax, %xmm0\n\tret");
}

/* Zeroes xmm0 correctly and xmm1 as well: caught only because xmm1 is compared. */
__attribute__((naked)) void bb_mutant_zeroes_xmm0_and_xmm1(void)
{
    __asm__("pxor %xmm0, %xmm0\n\tpxor %xmm1, %xmm1\n\tret");
}
