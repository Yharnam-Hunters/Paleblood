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

/* Records that call virtual functions (game/leaf/virtual_records.cpp), each missing one rule. */
typedef uint64_t (*bb_mutant_record_slot)(void *, ...);

static bb_mutant_record_slot bb_mutant_slot(const void *object, unsigned offset)
{
    return *(const bb_mutant_record_slot *)(*(const uint8_t *const *)object + offset);
}

/* The sink gets the inline text whatever the capacity. */
uint64_t bb_mutant_sink_gets_inline_text_at_0x28(uint8_t *record, void *sink)
{
    bb_mutant_record_slot write = bb_mutant_slot(sink, 0x90);
    return write(sink, record + 0x28, bb_mutant_slot(record, 0x60)(record));
}

uint64_t bb_mutant_sink_gets_inline_text_through_0x20(uint8_t *record, void *sink)
{
    bb_mutant_record_slot write = bb_mutant_slot(sink, 0x90);
    uint8_t *string = *(uint8_t **)(record + 0x20);
    return write(sink, string + 8, bb_mutant_slot(record, 0x60)(record));
}

/* The sink gets the text but 0 for the value (the record's slot is still called). */
uint64_t bb_mutant_sink_gets_text_without_value(uint8_t *record, void *sink)
{
    bb_mutant_record_slot write = bb_mutant_slot(sink, 0x90);
    bb_mutant_slot(record, 0x60)(record);
    return write(sink, *(void **)(record + 0x20), (uint64_t)0);
}

/* Switches through slot 0 even when already on. */
#define BB_MUTANT_TOGGLE_IGNORES_STATE(name, state_at) \
    uint64_t name(uint8_t *object, uint64_t on) \
    { \
        uint64_t result = bb_mutant_slot(object, 0)(object, on); \
        if ((uint32_t)result != 0) \
            return result; \
        object[state_at] = on != 0; \
        return 0; \
    }
BB_MUTANT_TOGGLE_IGNORES_STATE(bb_mutant_toggle_at_8_ignores_state, 8)
BB_MUTANT_TOGGLE_IGNORES_STATE(bb_mutant_toggle_at_0x20_ignores_state, 0x20)

/* Empties the wide string without freeing its heap text. */
#define BB_MUTANT_RESET_WITHOUT_FREE(name, string_at) \
    void name(uint8_t *object) \
    { \
        uint64_t *string = (uint64_t *)(object + (string_at)); \
        string[3] = 7; \
        string[2] = 0; \
        *(uint16_t *)string = 0; \
    }
BB_MUTANT_RESET_WITHOUT_FREE(bb_mutant_resets_wide_string_at_8_without_free, 8)
BB_MUTANT_RESET_WITHOUT_FREE(bb_mutant_resets_wide_string_at_0x10_without_free, 0x10)

/* Calls slot 0 of the node after the nth (or returns 0 when there is none). */
uint64_t bb_mutant_calls_node_after_the_nth(uint8_t *object, uint64_t index, uint64_t third, uint64_t fourth,
                                            uint64_t fifth, uint64_t sixth)
{
    uint8_t *node = *(uint8_t **)(object + 0x30);
    for (uint32_t steps = (uint32_t)index + 1; node != 0 && steps != 0; --steps)
        node = *(uint8_t **)(node + 0x10);
    if (node == 0)
        return 0;
    return bb_mutant_slot(node, 0)(node, (uint64_t)0, third, fourth, fifth, sixth);
}

/* Slot 0x88 with the arguments as they came. */
uint64_t bb_mutant_virtual_0x88_keeps_second_argument(void *object, uint64_t second, uint64_t third, uint64_t fourth,
                                                      uint64_t fifth, uint64_t sixth)
{
    return bb_mutant_slot(object, 0x88)(object, second, third, fourth, fifth, sixth);
}

/* Stops and starts without storing the dword at +0x20. */
uint64_t bb_mutant_restarts_without_storing_dword(uint8_t *object, uint64_t setting)
{
    (void)setting;
    bb_mutant_slot(object, 0x78)(object, (uint64_t)0);
    uint64_t result = bb_mutant_slot(object, 0xa8)(object);
    object[8] = 1;
    return result;
}

/* Flushes even while bit 1 (a flush running) is set. */
void bb_mutant_flushes_while_flushing(uint8_t *object, uint64_t argument)
{
    uint8_t **holder = *(uint8_t ***)(object + 0x40);
    uint8_t *pending = holder ? *holder : 0;
    if (pending == 0 || !(pending[0xc] & 1) || *(uint32_t *)(pending + 0x10) == 0)
        return;
    pending[0xc] |= 2;
    bb_mutant_slot(pending, 0x18)(pending, argument);
    bb_mutant_slot(pending, 0x20)(pending, argument);
    pending[0xc] &= 0xfd;
    *(uint32_t *)(pending + 0x10) = 0;
}

/* Calls slot 0x68 of the object itself, after slot 0x38. */
uint64_t bb_mutant_calls_0x68_of_object_itself(void *object, uint64_t second)
{
    bb_mutant_slot(object, 0x38)(object, second);
    return bb_mutant_slot(object, 0x68)(object, second);
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
