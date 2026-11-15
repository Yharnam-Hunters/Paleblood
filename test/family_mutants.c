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

/* Record initializers (zeroes_record_and_stores_first_argument): leave the last cleared word,
   store the record's own address as the owner, or return the owner instead of zero. */
uint64_t bb_mutant_record_last_word_left(void *owner, uint64_t *record)
{
    for (int i = 0; i < 14; i++)
        record[i] = 0;
    record[15] = (uint64_t)(uintptr_t)owner;
    return 0;
}

uint64_t bb_mutant_record_owner_is_record(void *owner, uint64_t *record)
{
    (void)owner;
    for (int i = 0; i < 15; i++)
        record[i] = 0;
    record[15] = (uint64_t)(uintptr_t)record;
    return 0;
}

uint64_t bb_mutant_record_returns_owner(void *owner, uint64_t *record)
{
    for (int i = 0; i < 15; i++)
        record[i] = 0;
    record[15] = (uint64_t)(uintptr_t)owner;
    return (uint64_t)(uintptr_t)owner;
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

/* The count's new value instead of the old one. */
uint64_t bb_mutant_increment_returns_new_count(uint8_t *object)
{
    return __atomic_add_fetch((uint32_t *)(object + 8), 1u, __ATOMIC_SEQ_CST);
}

/* A 64-bit increment at the count: a wrapping count carries into the next four bytes. */
uint64_t bb_mutant_increments_qword_at_8(uint8_t *object)
{
    return (uint32_t)__atomic_fetch_add((uint64_t *)(object + 8), 1u, __ATOMIC_SEQ_CST);
}

/* Clears of the first 8, 16 or 24 bytes: too few or too many for a record clear. */
void bb_mutant_clears_first_8_bytes(uint64_t *object)
{
    object[0] = 0;
}

void bb_mutant_clears_first_16_bytes(uint64_t *object)
{
    object[0] = 0;
    object[1] = 0;
}

void bb_mutant_clears_first_24_bytes(uint64_t *object)
{
    object[0] = 0;
    object[1] = 0;
    object[2] = 0;
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
