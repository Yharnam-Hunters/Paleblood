// SPDX-License-Identifier: GPL-2.0-or-later
/* The compute command buffer's user-data writers (game/render/observe_gpu_library.c): each observer
   reports COMPUTE_USER_DATA from the slot it is given, with the words the call passes, at the command
   buffer's cursor. The record's functions are stubs here that keep the last call. */
#include "runtime/observe_hooks.h"
#include "runtime/render_observer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { MOST_WORDS = 16, COMMAND_WORDS = 64, CURSOR_WORD = 6, SLOT = 2, COUNTED_SLOT = 4, COUNTED_WORDS = 10, RECORD_CHUNK = 8 };
enum { ONE_WORD = 1, TWO_WORDS = 2, FOUR_WORDS = 4, EIGHT_WORDS = 8, FIRST_COUNTED_VALUE = 21, WORD_BITS = 32, GROUP_AXES = 3 };
/* the writers' addresses (observe_gpu_library.c) */
enum { WRITE_ONE = 0x0148ada0, WRITE_POINTER = 0x0148ac90, WRITE_FOUR_MARKER_4 = 0x0148aad0, WRITE_FOUR_MARKER_6 = 0x0148ac00,
       WRITE_EIGHT = 0x0148ab60, WRITE_COUNTED = 0x0148ad00 };
static const uint32_t one_value = 0x11223344u;
static const uint64_t pointer_value = 0x0000002043490060ull;

/* the command buffer object: its begin at +0 and cursor at +0x10, as the observers read them */
typedef struct {
    uint64_t begin, limit, cursor, callback;
} command_buffer;

static struct {
    int calls;
    uint64_t buffer;
    uint32_t offset;
    rt_shader_stage stage;
    unsigned slot, count;
    uint32_t words[MOST_WORDS];
} last;

void rt_observe_user_data(uint64_t buffer, uint32_t offset, rt_shader_stage stage, unsigned slot, const uint32_t *words, unsigned count)
{
    last.calls++, last.buffer = buffer, last.offset = offset, last.stage = stage, last.slot = slot, last.count = count;
    memcpy(last.words, words, count * sizeof *words);
}
void rt_observe_color_target(uint64_t b, uint32_t o, unsigned s, const uint32_t w[RT_COLOR_TARGET_WORDS]) { (void)b, (void)o, (void)s, (void)w; }
void rt_observe_dispatch(uint64_t b, uint32_t o, const uint32_t g[GROUP_AXES]) { (void)b, (void)o, (void)g; }
void rt_observe_register(uint64_t b, uint32_t o, uint32_t a, uint32_t v) { (void)b, (void)o, (void)a, (void)v; }
void rt_observe_index_base(uint64_t b, uint32_t o, uint64_t a) { (void)b, (void)o, (void)a; }
void rt_observe_index_size(uint64_t b, uint32_t o, uint32_t c) { (void)b, (void)o, (void)c; }
void rt_observe_index_type(uint64_t b, uint32_t o, uint32_t t) { (void)b, (void)o, (void)t; }
void rt_observe_draw_index_offset(uint64_t b, uint32_t o, uint32_t f, uint32_t c) { (void)b, (void)o, (void)f, (void)c; }

extern const rt_observe_hook game_observe_hooks[];
extern const unsigned game_observe_hook_count;

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static rt_observer_fn observer(uint32_t address)
{
    for (unsigned i = 0; i < game_observe_hook_count; i++)
        if (game_observe_hooks[i].address == address) return game_observe_hooks[i].observe;
    return NULL;
}

int main(void)
{
    static uint32_t commands[COMMAND_WORDS];
    command_buffer object = {.begin = (uint64_t)(uintptr_t)commands, .cursor = (uint64_t)(uintptr_t)(commands + CURSOR_WORD)};
    const uint64_t object_address = (uint64_t)(uintptr_t)&object;
    const uint32_t four[FOUR_WORDS] = {1, 2, 3, 4}, eight[EIGHT_WORDS] = {11, 12, 13, 14, 15, 16, 17, 18};
    uint32_t counted_values[COUNTED_WORDS];
    for (unsigned k = 0; k < COUNTED_WORDS; k++) counted_values[k] = FIRST_COUNTED_VALUE + k;
    static const struct {
        uint32_t address;
        unsigned count;
    } writers[] = {{WRITE_ONE, ONE_WORD}, {WRITE_POINTER, TWO_WORDS}, {WRITE_FOUR_MARKER_4, FOUR_WORDS}, {WRITE_FOUR_MARKER_6, FOUR_WORDS},
                   {WRITE_EIGHT, EIGHT_WORDS}};
    for (unsigned w = 0; w < sizeof writers / sizeof *writers; w++) {
        const rt_observer_fn fn = observer(writers[w].address);
        CHECK(fn != NULL);
        if (!fn) continue;
        memset(&last, 0, sizeof last);
        const unsigned count = writers[w].count;
        const uint64_t value = count == ONE_WORD ? one_value : count == TWO_WORDS ? pointer_value
                             : (uint64_t)(uintptr_t)(count == FOUR_WORDS ? four : eight);
        const uint64_t args[RT_OBSERVED_ARGUMENTS] = {object_address, SLOT, value};
        fn(args);
        CHECK(last.calls == 1 && last.stage == RT_STAGE_COMPUTE && last.slot == SLOT && last.count == count);
        CHECK(last.buffer == object.begin && last.offset == CURSOR_WORD * sizeof(uint32_t));
        if (count == ONE_WORD) CHECK(last.words[0] == one_value);
        if (count == TWO_WORDS) CHECK(last.words[0] == (uint32_t)pointer_value && last.words[1] == (uint32_t)(pointer_value >> WORD_BITS));
        if (count == FOUR_WORDS) CHECK(!memcmp(last.words, four, sizeof four));
        if (count == EIGHT_WORDS) CHECK(!memcmp(last.words, eight, sizeof eight));
    }
    /* the counted writer: ten words from slot 4, reported eight then two */
    const rt_observer_fn counted = observer(WRITE_COUNTED);
    CHECK(counted != NULL);
    if (counted) {
        memset(&last, 0, sizeof last);
        const uint64_t args[RT_OBSERVED_ARGUMENTS] = {object_address, COUNTED_SLOT, (uint64_t)(uintptr_t)counted_values, COUNTED_WORDS};
        counted(args);
        const unsigned rest = COUNTED_WORDS - RECORD_CHUNK;
        CHECK(last.calls == TWO_WORDS && last.stage == RT_STAGE_COMPUTE && last.slot == COUNTED_SLOT + RECORD_CHUNK && last.count == rest);
        CHECK(!memcmp(last.words, counted_values + RECORD_CHUNK, rest * sizeof *counted_values));
    }
    if (failures) fprintf(stderr, "%d failure(s)\n", failures);
    else printf("compute user data observer: ok\n");
    return failures != 0;
}
