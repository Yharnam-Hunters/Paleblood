/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Deliberately wrong replacements for the function families (game/leaf/family_cases.py): every
   family's cases must fail on these, which shows the cases can tell a wrong body from a right
   one. Built as libbbfamily_mutants.so for tools/verify.py family; never part of the game
   library. */
#include <stdint.h>

#include "runtime/guest.h"
#include "runtime/host.h"

/* Where the loader mapped the guest image; game-specific mutants (game/<system>/tests/) reach
   guest memory through rt_guest (runtime/guest.h), as replacements do. */
unsigned char *rt_image;

/* The harness runs a replacement library's bbgame_init first: record the image as
   runtime/plugin.c does. The mutants install no hooks. */
int bbgame_init(const rt_host *host)
{
    rt_image = host->image;
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

/* The delegate at +0x18 called on its bound object without the adjustment (and, unset, slot 0x80
 * of the argument given 0 for the address). */
uint64_t bb_mutant_delegate_at_0x18_without_adjustment(uint8_t *owner, uint8_t *argument)
{
    typedef uint64_t (*handler)(void *, void *, uint64_t);
    uintptr_t function = *(uintptr_t *)(owner + 0x30);
    if (function == 0)
        return (*(handler *)(*(uint8_t **)argument + 0x80))(argument, 0, 0);
    uint8_t *object = *(uint8_t **)(owner + 0x18);
    handler call = function & 1 ? *(handler *)(*(uint8_t **)object + function - 1) : (handler)function;
    return call(object, argument, *(uint32_t *)(owner + 0x40));
}

/* Slot 0x28 of the object's table called, but the object never deleted. */
void bb_mutant_virtual_0x28_never_deletes(uint8_t *object)
{
    typedef void (*destructor)(void *);
    (*(destructor *)(*(uint8_t **)object + 0x28))(object);
}

/* Slot 0x18 of the object at +8 called when there is one, but no vtable stored. */
void bb_mutant_releases_member_at_8_only(uint8_t *object)
{
    typedef void (*release)(void *member);
    uint8_t *member = *(uint8_t **)(object + 8);
    if (member)
        (*(release *)(*(uint8_t **)member + 0x18))(member);
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

/* Controls for the value callback: the sink's write function, or the text, read again after the
   record's value is asked for (the originals read both before). */
static const void *bb_mutant_wide_text(const uint8_t *string)
{
    return *(const uint64_t *)(string + 0x18) < 8 ? (const void *)string : *(const void *const *)string;
}

static const void *bb_mutant_text_at_0x28(uint8_t *record)
{
    return bb_mutant_wide_text(record + 0x28);
}

static const void *bb_mutant_text_through_0x20(uint8_t *record)
{
    return bb_mutant_wide_text(*(uint8_t **)(record + 0x20) + 8);
}

static const void *bb_mutant_text_at_0x20(uint8_t *record)
{
    return *(const void **)(record + 0x20);
}

#define BB_MUTANT_SINK_RELOADS(name, text_of) \
    uint64_t name##_write_read_after_value(uint8_t *record, void *sink) \
    { \
        const void *text = text_of(record); \
        uint64_t value = bb_mutant_slot(record, 0x60)(record); \
        return bb_mutant_slot(sink, 0x90)(sink, text, value); \
    } \
    uint64_t name##_text_read_after_value(uint8_t *record, void *sink) \
    { \
        bb_mutant_record_slot write = bb_mutant_slot(sink, 0x90); \
        uint64_t value = bb_mutant_slot(record, 0x60)(record); \
        return write(sink, text_of(record), value); \
    }
BB_MUTANT_SINK_RELOADS(bb_mutant_wide_string_at_0x28, bb_mutant_text_at_0x28)
BB_MUTANT_SINK_RELOADS(bb_mutant_wide_string_through_0x20, bb_mutant_text_through_0x20)
BB_MUTANT_SINK_RELOADS(bb_mutant_text_at_0x20, bb_mutant_text_at_0x20)

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

/* Controls for the flush callbacks: the flush slot read from the table before prepare runs, or
   the flags cleared from their value before the calls (the original reads both again). */
static uint8_t *bb_mutant_pending_to_flush(uint8_t *object)
{
    uint8_t **holder = *(uint8_t ***)(object + 0x40);
    uint8_t *pending = holder ? *holder : 0;
    if (pending == 0 || !(pending[0xc] & 1) || *(uint32_t *)(pending + 0x10) == 0 || (pending[0xc] & 2))
        return 0;
    return pending;
}

void bb_mutant_flushes_through_table_read_before_prepare(uint8_t *object, uint64_t argument)
{
    uint8_t *pending = bb_mutant_pending_to_flush(object);
    if (pending == 0)
        return;
    pending[0xc] |= 2;
    bb_mutant_record_slot flush = bb_mutant_slot(pending, 0x20);
    bb_mutant_slot(pending, 0x18)(pending, argument);
    flush(pending, argument);
    pending[0xc] &= 0xfd;
    *(uint32_t *)(pending + 0x10) = 0;
}

void bb_mutant_flushes_with_flags_read_before_calls(uint8_t *object, uint64_t argument)
{
    uint8_t *pending = bb_mutant_pending_to_flush(object);
    if (pending == 0)
        return;
    uint8_t flags = pending[0xc] | 2;
    pending[0xc] = flags;
    bb_mutant_slot(pending, 0x18)(pending, argument);
    bb_mutant_slot(pending, 0x20)(pending, argument);
    pending[0xc] = flags & 0xfd;
    *(uint32_t *)(pending + 0x10) = 0;
}

/* Calls slot 0x68 of the object itself, after slot 0x38. */
uint64_t bb_mutant_calls_0x68_of_object_itself(void *object, uint64_t second)
{
    bb_mutant_slot(object, 0x38)(object, second);
    return bb_mutant_slot(object, 0x68)(object, second);
}

/* Frees the node and its left children but never the right subtrees. */
void bb_mutant_erases_left_spine_only(uint8_t *tree, uint8_t *node)
{
    void *allocator = *(void **)(tree + 0x18);
    while (!node[0x19]) {
        uint8_t *left = *(uint8_t **)node;
        bb_mutant_slot(allocator, 0x70)(allocator, node);
        node = left;
    }
}

/* Frees every node (right subtrees first) without resetting its wide string. */
void bb_mutant_erases_nodes_without_their_strings(uint8_t *tree, uint8_t *node)
{
    void *allocator = *(void **)(tree + 0x18);
    while (!node[0x19]) {
        bb_mutant_erases_nodes_without_their_strings(tree, *(uint8_t **)(node + 0x10));
        uint8_t *left = *(uint8_t **)node;
        bb_mutant_slot(allocator, 0x70)(allocator, node);
        node = left;
    }
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

/* Returns -1 in eax only: the upper half of rax stays clear. */
uint64_t bb_mutant_returns_low_all_bits(void)
{
    return 0xffffffffu;
}

/* The wide string's inline text whatever its capacity (the string pointer at +0x30). */
uint64_t bb_mutant_wide_string_text_inline(const uint8_t *object)
{
    return (uint64_t)(uintptr_t)(*(const uint8_t *const *)(object + 0x30) + 8);
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

/* Key matches that skip the size check, or compare the second halves even for a zero first half. */
uint64_t bb_mutant_key_ignores_size(const uint64_t *object, const uint64_t *key, uint32_t size)
{
    (void)size;
    return key[0] == object[3] && (key[0] == 0 || key[1] == object[4]);
}

uint64_t bb_mutant_key_compares_both_halves(const uint64_t *object, const uint64_t *key, uint32_t size)
{
    return size == 16 && key[0] == object[3] && key[1] == object[4];
}

/* List walks one node off (the list at +0x30, next at +0x10, value at +0x18). */
uint64_t bb_mutant_counts_from_second_node(const uint8_t *object)
{
    uint32_t count = 0;
    const uint8_t *node = *(const uint8_t *const *)(object + 0x30);
    if (node != 0)
        node = *(const uint8_t *const *)(node + 0x10);
    for (; node != 0; node = *(const uint8_t *const *)(node + 0x10))
        count++;
    return count;
}

uint64_t bb_mutant_reads_next_list_node(const uint8_t *object, uint32_t index)
{
    const uint8_t *node = *(const uint8_t *const *)(object + 0x30);
    for (; node != 0; node = *(const uint8_t *const *)(node + 0x10), index--) {
        if (index == 0) {
            const uint8_t *next = *(const uint8_t *const *)(node + 0x10);
            return next != 0 ? *(const uint32_t *)(next + 0x18) : 0;
        }
    }
    return 0;
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

/* Constant first-field stores of the wrong value or width, for the constant setter families. */
void bb_mutant_clears_first_qword(uint64_t *object)
{
    *object = 0;
}

void bb_mutant_clears_first_dword(uint32_t *object)
{
    *object = 0;
}

void bb_mutant_clears_first_byte(uint8_t *object)
{
    *object = 0;
}

void bb_mutant_stores_one_in_first_byte(uint8_t *object)
{
    *object = 1;
}

void bb_mutant_stores_one_in_first_dword(uint32_t *object)
{
    *object = 1;
}
