// SPDX-License-Identifier: GPL-2.0-or-later
/* The compute command buffer's user-data writers (game/render/observe_gpu_library.c): each observer
   reports COMPUTE_USER_DATA from the slot it is given, with the words the call passes, at the command
   buffer's cursor. The record's functions are stubs here that keep the last call. The indirect
   dispatch's two writers report the base address and the data offset they are given. The depth bounds
   setter reports its two float arguments, from the saved frame's xmm0 and xmm1, as the two registers.
   The shader stage setter reports VGT_SHADER_STAGES_EN for the values it writes, and nothing for others.
   The geometry-shader register writers report the registers they write, with the values they compute.
   The addressed indexed draw (DRAW_INDEX_2) reports the packet's fields, its predicate from the command buffer's flags.
   The viewport scissor setter reports the pair of registers it writes, its window-offset bit from its stack argument.
   The clip control setter reports PA_CL_CLIP_CNTL with the value masked as the setter masks it. */
#include "runtime/observe_hooks.h"
#include "runtime/render_observer.h"
#include "runtime/render_observer_hooks.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { MOST_WORDS = 16, COMMAND_WORDS = 64, CURSOR_WORD = 6, SLOT = 2, COUNTED_SLOT = 4, COUNTED_WORDS = 10, RECORD_CHUNK = 8 };
enum { ONE_WORD = 1, TWO_WORDS = 2, FOUR_WORDS = 4, EIGHT_WORDS = 8, FIRST_COUNTED_VALUE = 21, WORD_BITS = 32, GROUP_AXES = 3 };
/* the writers' addresses (observe_gpu_library.c) */
enum { WRITE_ONE = 0x0148ada0, WRITE_POINTER = 0x0148ac90, WRITE_FOUR_MARKER_4 = 0x0148aad0, WRITE_FOUR_MARKER_6 = 0x0148ac00,
       WRITE_EIGHT = 0x0148ab60, WRITE_COUNTED = 0x0148ad00, SET_INDIRECT_BASE = 0x014742f0, DISPATCH_INDIRECT = 0x01474de0,
       DRAW_INDIRECT = 0x014744d0 };
enum { INDIRECT_DATA_OFFSET = 0x24, SGPR_WORD = 0x1205, SGPR_BYTE = 0x05, ZERO_LOW_BYTE = 0x3400 };
enum { DRAW_INDEX = 0x01474360, INDEX_COUNT = 36, ODD_BIT = 1, PREDICATED = 1, TWO_DRAWS = 2 };
static const uint32_t DRAW_INDEX_2_HEADER = 0xc0042700u;
enum { SET_DEPTH_BOUNDS = 0x01473520, DB_DEPTH_BOUNDS_MIN = 0xa008, DB_DEPTH_BOUNDS_MAX = 0xa009 };
enum { SET_GS_MODE = 0x014756b0, SET_ESGS_ITEMSIZE = 0x01475a90, SET_GS_RING_SIZES = 0x01475af0 };
enum { VGT_GS_MODE = 0xa290, VGT_GSVS_RING_OFFSET_1 = 0xa298, VGT_ESGS_RING_ITEMSIZE = 0xa2ab, VGT_GSVS_RING_ITEMSIZE = 0xa2ac,
       VGT_GS_MAX_VERT_OUT = 0xa2ce, VGT_GS_VERT_ITEMSIZE = 0xa2d7, VGT_STRMOUT_CONFIG = 0xa2e5, GS_STREAMS = 4, GS_VERTICES = 40,
       ITEMSIZE_ARGUMENT = 0x1803c, STREAM_0 = 12, STREAM_1 = 2, STREAM_2 = 0, STREAM_3 = 1, ITEMSIZE_WRITTEN = 0x3c, GS_MODE_ON = 2, GS_CUT_MODE = 1, GS_MODE_WRITTEN = 0x12 };
enum { SET_VIEWPORT_SCISSOR = 0x014766d0, PA_SC_VPORT_SCISSOR_0_TL = 0xa094, SCISSOR_VIEWPORT = 1, SCISSOR_LEFT = 128,
       SCISSOR_TOP = 0x18080, SCISSOR_RIGHT = 256, SCISSOR_BOTTOM = 384, SCISSOR_BR_WRITTEN = 0x01800100,
       SCISSOR_TL_WITH_OFFSET = 0x00800080, SCISSOR_FRAME_WORDS = RT_OBSERVED_STACK + 1, WINDOW_OFFSET_ENABLED = 1 };
static const uint32_t SCISSOR_TL_WRITTEN = 0x80800080u; /* bit 31: WINDOW_OFFSET_DISABLE */
enum { SET_SHADER_STAGES = 0x01476030, VGT_SHADER_STAGES_EN = 0xa2d5, TESSELLATION = 0x45, NOT_WRITTEN = 0x7 };
/* every bit set: the setter keeps 0x0f7fe03f of them (ZCLIP_NEAR_DISABLE and _FAR_DISABLE, bits 26 and 27, among them) */
enum { SET_CLIP_CONTROL = 0x01476b30, PA_CL_CLIP_CNTL = 0xa204, CLIP_ARGUMENT = 0xffffffff, CLIP_WRITTEN = 0x0f7fe03f };
static const uint32_t one_value = 0x11223344u;
static const uint64_t pointer_value = 0x0000002043490060ull;

/* the command buffer object: its begin at +0 and cursor at +0x10, as the observers read them */
typedef struct {
    uint64_t begin, limit, cursor, callback, callback_data;
    uint32_t flags; /* +0x28: bit 0 predicates the packets */
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
static struct {
    uint64_t buffer, base;
    uint32_t offset, data_offset;
    int bases, dispatches;
} indirect;
void rt_observe_indirect_base(uint64_t b, uint32_t o, uint64_t a) { indirect.bases++, indirect.buffer = b, indirect.offset = o, indirect.base = a; }
void rt_observe_dispatch_indirect(uint64_t b, uint32_t o, uint32_t d) { indirect.dispatches++, indirect.buffer = b, indirect.offset = o, indirect.data_offset = d; }
static struct {
    int draws;
    uint32_t data_offset, base_vertex_sgpr, start_instance_sgpr;
    rt_shader_stage stage;
} indirect_draw;
void rt_observe_draw_indirect(uint64_t b, uint32_t o, uint32_t d, rt_shader_stage s, uint32_t v, uint32_t i)
{
    (void)b, (void)o;
    indirect_draw.draws++, indirect_draw.data_offset = d, indirect_draw.stage = s, indirect_draw.base_vertex_sgpr = v, indirect_draw.start_instance_sgpr = i;
}
enum { KEPT_REGISTERS = 12, BOUNDS_REGISTERS = 2 };
static struct {
    unsigned count;
    uint32_t address[KEPT_REGISTERS], value[KEPT_REGISTERS];
} registers;
void rt_observe_register(uint64_t b, uint32_t o, uint32_t a, uint32_t v)
{
    (void)b, (void)o;
    if (registers.count < KEPT_REGISTERS) registers.address[registers.count] = a, registers.value[registers.count] = v;
    registers.count++;
}
void rt_observe_index_base(uint64_t b, uint32_t o, uint64_t a) { (void)b, (void)o, (void)a; }
void rt_observe_index_size(uint64_t b, uint32_t o, uint32_t c) { (void)b, (void)o, (void)c; }
void rt_observe_index_type(uint64_t b, uint32_t o, uint32_t t) { (void)b, (void)o, (void)t; }
void rt_observe_draw_index_offset(uint64_t b, uint32_t o, uint32_t f, uint32_t c) { (void)b, (void)o, (void)f, (void)c; }
static struct {
    int draws;
    uint64_t buffer, address;
    uint32_t offset, header, max_size, count, initiator;
} addressed_draw;
void rt_observe_draw_indexed(uint64_t b, uint32_t o, uint32_t h, uint32_t m, uint64_t a, uint32_t c, uint32_t i)
{
    addressed_draw.draws++, addressed_draw.buffer = b, addressed_draw.offset = o, addressed_draw.header = h, addressed_draw.max_size = m;
    addressed_draw.address = a, addressed_draw.count = c, addressed_draw.initiator = i;
}

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
    /* the indirect dispatch: the base and the offset as given, at the cursor */
    const rt_observer_fn set_base = observer(SET_INDIRECT_BASE), dispatch_indirect = observer(DISPATCH_INDIRECT);
    CHECK(set_base != NULL && dispatch_indirect != NULL);
    if (set_base && dispatch_indirect) {
        const uint64_t base_args[RT_OBSERVED_ARGUMENTS] = {object_address, pointer_value};
        set_base(base_args);
        CHECK(indirect.bases == 1 && indirect.base == pointer_value && indirect.buffer == object.begin &&
              indirect.offset == CURSOR_WORD * sizeof(uint32_t));
        const uint64_t dispatch_args[RT_OBSERVED_ARGUMENTS] = {object_address, INDIRECT_DATA_OFFSET};
        dispatch_indirect(dispatch_args);
        CHECK(indirect.dispatches == 1 && indirect.data_offset == INDIRECT_DATA_OFFSET);
    }
    /* the indirect draw: offset, stage and the two SGPR bytes (higher bits of the registers ignored) */
    const rt_observer_fn draw = observer(DRAW_INDIRECT);
    CHECK(draw != NULL);
    if (draw) {
        const uint64_t draw_args[RT_OBSERVED_ARGUMENTS] = {object_address, INDIRECT_DATA_OFFSET, RT_STAGE_VERTEX, SGPR_WORD, ZERO_LOW_BYTE};
        draw(draw_args);
        CHECK(indirect_draw.draws == 1 && indirect_draw.data_offset == INDIRECT_DATA_OFFSET && indirect_draw.stage == RT_STAGE_VERTEX &&
              indirect_draw.base_vertex_sgpr == SGPR_BYTE && !indirect_draw.start_instance_sgpr);
    }
    /* DRAW_INDEX_2 (0x01474360): the count as MAX_SIZE and INDEX_COUNT, the address with bit 0 cleared, the
       predicate from the command buffer's flags, initiator 0 */
    const rt_observer_fn draw_index = observer(DRAW_INDEX);
    CHECK(draw_index != NULL);
    if (draw_index) {
        const uint64_t index_args[RT_OBSERVED_ARGUMENTS] = {object_address, INDEX_COUNT, pointer_value | ODD_BIT};
        draw_index(index_args);
        CHECK(addressed_draw.draws == 1 && addressed_draw.buffer == object.begin && addressed_draw.offset == CURSOR_WORD * sizeof(uint32_t));
        CHECK(addressed_draw.header == DRAW_INDEX_2_HEADER && addressed_draw.max_size == INDEX_COUNT && addressed_draw.count == INDEX_COUNT);
        CHECK(addressed_draw.address == pointer_value && addressed_draw.initiator == 0);
        object.flags = PREDICATED;
        draw_index(index_args);
        CHECK(addressed_draw.draws == TWO_DRAWS && addressed_draw.header == (DRAW_INDEX_2_HEADER | PREDICATED));
        object.flags = 0;
    }
    /* the depth bounds: 0.25 and 0.75 in xmm0 and xmm1 of the saved frame */
    const rt_observer_fn bounds = observer(SET_DEPTH_BOUNDS);
    CHECK(bounds != NULL);
    if (bounds) {
        uint64_t frame[RT_OBSERVED_FRAME_WORDS] = {object_address};
        const float low = 0.25f, high = 0.75f;
        memcpy(&frame[RT_OBSERVED_XMM0], &low, sizeof low);
        memcpy(&frame[RT_OBSERVED_XMM0 + RT_OBSERVED_XMM_WORDS], &high, sizeof high);
        bounds(frame);
        uint32_t low_bits, high_bits;
        memcpy(&low_bits, &low, sizeof low_bits), memcpy(&high_bits, &high, sizeof high_bits);
        CHECK(registers.count == BOUNDS_REGISTERS && registers.address[0] == DB_DEPTH_BOUNDS_MIN && registers.value[0] == low_bits &&
              registers.address[1] == DB_DEPTH_BOUNDS_MAX && registers.value[1] == high_bits);
    }
    /* the shader stages: 0x45 (tessellation) is reported, 7 (which the setter does not write) is not */
    const rt_observer_fn stages = observer(SET_SHADER_STAGES);
    CHECK(stages != NULL);
    if (stages) {
        memset(&registers, 0, sizeof registers);
        const uint64_t tessellation[RT_OBSERVED_ARGUMENTS] = {object_address, TESSELLATION}, other[RT_OBSERVED_ARGUMENTS] = {object_address, NOT_WRITTEN};
        stages(tessellation);
        stages(other);
        CHECK(registers.count == 1 && registers.address[0] == VGT_SHADER_STAGES_EN && registers.value[0] == TESSELLATION);
    }
    /* the geometry-shader registers: the ESGS item size masked to 15 bits; stream sizes 12, 2, 0, 1 for 40
       vertices: the four sizes, the next streams' offsets (12, 14, 14 dwords x 40), 40 vertices and 15 x 40 dwords
       a primitive; mode 2 with cut mode 1, then mode 0, which also clears the stream-output configuration */
    const rt_observer_fn esgs = observer(SET_ESGS_ITEMSIZE), rings = observer(SET_GS_RING_SIZES), mode = observer(SET_GS_MODE);
    CHECK(esgs != NULL && rings != NULL && mode != NULL);
    if (esgs && rings && mode) {
        memset(&registers, 0, sizeof registers);
        const uint64_t itemsize[RT_OBSERVED_ARGUMENTS] = {object_address, ITEMSIZE_ARGUMENT};
        esgs(itemsize);
        CHECK(registers.count == 1 && registers.address[0] == VGT_ESGS_RING_ITEMSIZE && registers.value[0] == ITEMSIZE_WRITTEN);
        memset(&registers, 0, sizeof registers);
        static const uint32_t sizes[GS_STREAMS] = {STREAM_0, STREAM_1, STREAM_2, STREAM_3};
        const uint64_t ring_args[RT_OBSERVED_ARGUMENTS] = {object_address, (uint64_t)(uintptr_t)sizes, GS_VERTICES};
        rings(ring_args);
        enum { VERT_1 = VGT_GS_VERT_ITEMSIZE + 1, VERT_2, VERT_3, OFFSET_2 = VGT_GSVS_RING_OFFSET_1 + 1, OFFSET_3 };
        static const struct { uint32_t address, value; } expected[] = {
            {VGT_GS_VERT_ITEMSIZE, STREAM_0}, {VGT_GSVS_RING_OFFSET_1, STREAM_0 * GS_VERTICES}, {VERT_1, STREAM_1},
            {OFFSET_2, (STREAM_0 + STREAM_1) * GS_VERTICES}, {VERT_2, STREAM_2}, {OFFSET_3, (STREAM_0 + STREAM_1 + STREAM_2) * GS_VERTICES},
            {VERT_3, STREAM_3}, {VGT_GS_MAX_VERT_OUT, GS_VERTICES}, {VGT_GSVS_RING_ITEMSIZE, (STREAM_0 + STREAM_1 + STREAM_2 + STREAM_3) * GS_VERTICES}};
        CHECK(registers.count == sizeof expected / sizeof *expected);
        for (unsigned i = 0; i < sizeof expected / sizeof *expected; i++)
            CHECK(registers.address[i] == expected[i].address && registers.value[i] == expected[i].value);
        memset(&registers, 0, sizeof registers);
        const uint64_t on[RT_OBSERVED_ARGUMENTS] = {object_address, GS_MODE_ON, GS_CUT_MODE}, off[RT_OBSERVED_ARGUMENTS] = {object_address, 0, 0};
        mode(on);
        mode(off);
        enum { MODE_ON, MODE_OFF, STREAMOUT_OFF, MODE_REGISTERS };
        CHECK(registers.count == MODE_REGISTERS && registers.address[MODE_ON] == VGT_GS_MODE && registers.value[MODE_ON] == GS_MODE_WRITTEN &&
              registers.address[MODE_OFF] == VGT_GS_MODE && registers.value[MODE_OFF] == 0 && registers.address[STREAMOUT_OFF] == VGT_STRMOUT_CONFIG &&
              registers.value[STREAMOUT_OFF] == 0);
    }
    /* the viewport scissor: viewport 1's pair (0xa096, 0xa097), x and y masked to 15 bits (top 0x18080 keeps
       0x0080), WINDOW_OFFSET_DISABLE set for a stack argument of 0 and clear for 1 */
    const rt_observer_fn scissor = observer(SET_VIEWPORT_SCISSOR);
    CHECK(scissor != NULL);
    if (scissor) {
        uint64_t frame[SCISSOR_FRAME_WORDS] = {object_address, SCISSOR_VIEWPORT, SCISSOR_LEFT, SCISSOR_TOP, SCISSOR_RIGHT, SCISSOR_BOTTOM};
        memset(&registers, 0, sizeof registers);
        scissor(frame);
        frame[RT_OBSERVED_STACK] = WINDOW_OFFSET_ENABLED;
        scissor(frame);
        enum { FIRST_TL, FIRST_BR, SECOND_TL, SECOND_BR, SCISSOR_REGISTERS };
        const uint32_t tl = PA_SC_VPORT_SCISSOR_0_TL + 2 * SCISSOR_VIEWPORT;
        CHECK(registers.count == SCISSOR_REGISTERS && registers.address[FIRST_TL] == tl && registers.value[FIRST_TL] == SCISSOR_TL_WRITTEN &&
              registers.address[FIRST_BR] == tl + 1 && registers.value[FIRST_BR] == SCISSOR_BR_WRITTEN &&
              registers.value[SECOND_TL] == SCISSOR_TL_WITH_OFFSET && registers.value[SECOND_BR] == SCISSOR_BR_WRITTEN);
    }
    const rt_observer_fn clip = observer(SET_CLIP_CONTROL);
    CHECK(clip != NULL);
    if (clip) {
        memset(&registers, 0, sizeof registers);
        const uint64_t clip_args[RT_OBSERVED_ARGUMENTS] = {object_address, CLIP_ARGUMENT};
        clip(clip_args);
        CHECK(registers.count == 1 && registers.address[0] == PA_CL_CLIP_CNTL && registers.value[0] == CLIP_WRITTEN);
    }
    if (failures) fprintf(stderr, "%d failure(s)\n", failures);
    else printf("compute user data observer: ok\n");
    return failures != 0;
}
