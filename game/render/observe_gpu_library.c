// SPDX-License-Identifier: GPL-2.0-or-later
/* GPU library functions observed for the renderer (runtime/observe_hooks.h) until they have
   replacements; a replacement's own observer call takes over when one lands.

   setRenderTarget, 0x014738b0 (command buffer, slot, render target): a target whose first word
   (CB_COLOR_BASE) is not 0 (its validity check, 0x01486ca0, returns that word) has its first 11
   words written to CB_COLOR0_BASE + 15 x slot onward; otherwise CB_COLORn_INFO = 0 switches the slot off.

   The user-data writers each write a stage's user SGPRs from a slot on, as one SET_SH_REG packet at the
   base register of the stage's table entry (0x048d55c0) plus the slot; the library's stage numbers
   are those of rt_shader_stage (docs/RENDERER.md). Their arguments, from their disassembly, are
   (command buffer, stage, slot, then):
     0x01476420  one 32-bit value                        one word;
     0x01476390  one 64-bit value (a pointer), low first  two words;
     0x014761a0  a pointer to four words, after a two-word NOP marker 0x68750004;
     0x014762f0  the same, after the marker 0x68750006;
     0x01476240  a pointer to eight words, after the marker 0x68750005;
     0x014764a0  a pointer to words and their count (fifth argument), after the marker 0x6875000d.
   GCN's buffer and sampler descriptors are four words and its image descriptors eight; which the
   markers name is not established here.
   The compute command buffer's writers (the class whose dispatch writer is 0x0148a350) take no stage:
   they always write COMPUTE_USER_DATA_0 (0x240) plus the slot, with the same packets and markers, and
   their arguments are (command buffer, slot, then the same):
     0x0148ada0  one 32-bit value;                      0x0148ac90  one 64-bit value (a pointer);
     0x0148aad0  four words after the marker 0x68750004; 0x0148ac00  four words after 0x68750006;
     0x0148ab60  eight words after 0x68750005;           0x0148ad00  counted words after 0x6875000d.
   Unobserved, a compute pass on that buffer ran with whatever user data the record held from before
   (the LUT pass read its constant table through another pass's pointer, and now and then faulted
   the GPU).
   The dispatch writers 0x01474c50 and 0x0148a350 (command buffer, x, y, z) write DISPATCH_DIRECT
   for x x y x z workgroups with initiator 1 (COMPUTE_SHADER_EN), then an event or a NOP. Their two
   relatives 0x01474d10 and 0x0148a410 take a fifth argument that sets the initiator's low two bits;
   what those select is not established, so they are not observed yet.
   The depth render target's setter 0x014739b0 (command buffer, target) writes context registers from the
   target's words when the target is not NULL and has a depth or a stencil buffer (word 2,
   DB_Z_READ_BASE, or word 3, DB_STENCIL_READ_BASE, not 0: the two checks it calls, 0x01472770 and
   0x01472790, return those words): words 0-7 to DB_Z_INFO onward (0xa010-0xa017), word 11 to
   DB_DEPTH_INFO (0xa00f), word 8 to DB_DEPTH_VIEW (0xa002), word 9 to DB_HTILE_DATA_BASE (0xa005),
   word 10 to DB_HTILE_SURFACE (0xa2af); otherwise DB_Z_INFO and DB_STENCIL_INFO = 0 (no depth target).
   The observer reports the same registers.
   The stencil reference setters write DB_STENCILREFMASK (0xa10c) and DB_STENCILREFMASK_BF (0xa10d) in one
   packet: 0x01473190 (command buffer, value) writes the value to both, 0x014731f0 (command buffer,
   front, back) one to each.
   Indexed draws: 0x014752c0 (command buffer, address) writes INDEX_BASE with the address's bits 1-31
   and 32-39 (an even 40-bit address); 0x01475320 (command buffer, count) INDEX_BUFFER_SIZE;
   0x01475270 (command buffer, type) INDEX_TYPE with the type as it is; 0x01474420 (command buffer,
   first, count) DRAW_INDEX_OFFSET_2 with MAX_SIZE = count, INDEX_OFFSET = first, INDEX_COUNT = count and
   initiator 0, then an event or a NOP.
   The command buffer starts with its begin and holds its cursor at +0x10 (game/render/command_packets.h). */
#include "runtime/observe_hooks.h"
#include "runtime/render_observer.h"

#include <stdint.h>

enum { COMMAND_BUFFER_BEGIN = 0x0, COMMAND_BUFFER_CURSOR = 0x10 };
/* the arguments: (command buffer, stage, slot, value or words) and (command buffer, slot, target) */
enum { ARGUMENT_COMMAND_BUFFER = 0, ARGUMENT_STAGE = 1, ARGUMENT_SLOT = 2, ARGUMENT_VALUE = 3,
       ARGUMENT_TARGET_SLOT = 1, ARGUMENT_TARGET = 2, ARGUMENT_GROUPS_X = 1 };
enum { ONE_WORD = 1, TWO_WORDS = 2, FOUR_WORDS = 4, EIGHT_WORDS = 8 };

static void observe(const uint64_t args[RT_OBSERVED_ARGUMENTS], const uint32_t *words, unsigned count)
{
    const uint64_t buffer = *(const uint64_t *)(uintptr_t)(args[ARGUMENT_COMMAND_BUFFER] + COMMAND_BUFFER_BEGIN);
    const uint64_t cursor = *(const uint64_t *)(uintptr_t)(args[ARGUMENT_COMMAND_BUFFER] + COMMAND_BUFFER_CURSOR);
    rt_observe_user_data(buffer, (uint32_t)(cursor - buffer), (rt_shader_stage)(uint32_t)args[ARGUMENT_STAGE], (uint32_t)args[ARGUMENT_SLOT], words, count);
}

static void one_word(const uint64_t args[RT_OBSERVED_ARGUMENTS])
{
    const uint32_t value = (uint32_t)args[ARGUMENT_VALUE];
    observe(args, &value, ONE_WORD);
}

static void two_words(const uint64_t args[RT_OBSERVED_ARGUMENTS])
{
    const uint32_t words[TWO_WORDS] = {(uint32_t)args[ARGUMENT_VALUE], (uint32_t)(args[ARGUMENT_VALUE] >> 32)};
    observe(args, words, TWO_WORDS);
}

static void four_words(const uint64_t args[RT_OBSERVED_ARGUMENTS]) { observe(args, (const uint32_t *)(uintptr_t)args[ARGUMENT_VALUE], FOUR_WORDS); }

static void eight_words(const uint64_t args[RT_OBSERVED_ARGUMENTS]) { observe(args, (const uint32_t *)(uintptr_t)args[ARGUMENT_VALUE], EIGHT_WORDS); }

/* The counted writer: its words go to the record eight at a time, from the slot on (the record takes
   at most eight per call). */
enum { ARGUMENT_COUNT = 4 };

static void counted_words(const uint64_t args[RT_OBSERVED_ARGUMENTS])
{
    const uint32_t *words = (const uint32_t *)(uintptr_t)args[ARGUMENT_VALUE];
    const uint32_t count = (uint32_t)args[ARGUMENT_COUNT], slot = (uint32_t)args[ARGUMENT_SLOT];
    for (uint32_t done = 0; done < count; done += EIGHT_WORDS) {
        const uint32_t n = count - done < EIGHT_WORDS ? count - done : EIGHT_WORDS;
        const uint64_t buffer = *(const uint64_t *)(uintptr_t)(args[ARGUMENT_COMMAND_BUFFER] + COMMAND_BUFFER_BEGIN);
        const uint64_t cursor = *(const uint64_t *)(uintptr_t)(args[ARGUMENT_COMMAND_BUFFER] + COMMAND_BUFFER_CURSOR);
        rt_observe_user_data(buffer, (uint32_t)(cursor - buffer), (rt_shader_stage)(uint32_t)args[ARGUMENT_STAGE], slot + done, words + done, n);
    }
}

/* The compute command buffer's writers: the same calls with the stage fixed and every later argument
   one register earlier. */
static void as_compute(const uint64_t args[RT_OBSERVED_ARGUMENTS], void (*observe_with_stage)(const uint64_t args[RT_OBSERVED_ARGUMENTS]))
{
    const uint64_t shifted[RT_OBSERVED_ARGUMENTS] = {args[0], RT_STAGE_COMPUTE, args[1], args[2], args[3], args[4]};
    observe_with_stage(shifted);
}

static void compute_one_word(const uint64_t args[RT_OBSERVED_ARGUMENTS]) { as_compute(args, one_word); }
static void compute_two_words(const uint64_t args[RT_OBSERVED_ARGUMENTS]) { as_compute(args, two_words); }
static void compute_four_words(const uint64_t args[RT_OBSERVED_ARGUMENTS]) { as_compute(args, four_words); }
static void compute_eight_words(const uint64_t args[RT_OBSERVED_ARGUMENTS]) { as_compute(args, eight_words); }
static void compute_counted_words(const uint64_t args[RT_OBSERVED_ARGUMENTS]) { as_compute(args, counted_words); }

static void dispatch(const uint64_t args[RT_OBSERVED_ARGUMENTS])
{
    const uint64_t buffer = *(const uint64_t *)(uintptr_t)(args[ARGUMENT_COMMAND_BUFFER] + COMMAND_BUFFER_BEGIN);
    const uint64_t cursor = *(const uint64_t *)(uintptr_t)(args[ARGUMENT_COMMAND_BUFFER] + COMMAND_BUFFER_CURSOR);
    const uint32_t groups[3] = {(uint32_t)args[ARGUMENT_GROUPS_X], (uint32_t)args[ARGUMENT_GROUPS_X + 1],
                                (uint32_t)args[ARGUMENT_GROUPS_X + 2]};
    rt_observe_dispatch(buffer, (uint32_t)(cursor - buffer), groups);
}

static void render_target(const uint64_t args[RT_OBSERVED_ARGUMENTS])
{
    const uint64_t buffer = *(const uint64_t *)(uintptr_t)(args[ARGUMENT_COMMAND_BUFFER] + COMMAND_BUFFER_BEGIN);
    const uint64_t cursor = *(const uint64_t *)(uintptr_t)(args[ARGUMENT_COMMAND_BUFFER] + COMMAND_BUFFER_CURSOR);
    const uint32_t *target = (const uint32_t *)(uintptr_t)args[ARGUMENT_TARGET];
    rt_observe_color_target(buffer, (uint32_t)(cursor - buffer), (uint32_t)args[ARGUMENT_TARGET_SLOT], target && target[0] ? target : NULL);
}

/* setDepthRenderTarget's registers (gfx_7_2_d.h numbers) */
enum { DB_DEPTH_VIEW = 0xa002, DB_HTILE_DATA_BASE = 0xa005, DB_DEPTH_INFO = 0xa00f, DB_Z_INFO = 0xa010, DB_STENCIL_INFO = 0xa011,
       DB_HTILE_SURFACE = 0xa2af };
enum { TARGET_Z_READ_BASE = 2, TARGET_STENCIL_READ_BASE = 3, TARGET_REGISTER_RUN = 8, TARGET_DEPTH_VIEW = 8, TARGET_HTILE_DATA_BASE = 9,
       TARGET_HTILE_SURFACE = 10, TARGET_DEPTH_INFO = 11, ARGUMENT_DEPTH_TARGET = 1 };

static void depth_target(const uint64_t args[RT_OBSERVED_ARGUMENTS])
{
    const uint64_t buffer = *(const uint64_t *)(uintptr_t)(args[ARGUMENT_COMMAND_BUFFER] + COMMAND_BUFFER_BEGIN);
    const uint32_t offset = (uint32_t)(*(const uint64_t *)(uintptr_t)(args[ARGUMENT_COMMAND_BUFFER] + COMMAND_BUFFER_CURSOR) - buffer);
    const uint32_t *target = (const uint32_t *)(uintptr_t)args[ARGUMENT_DEPTH_TARGET];
    if (!target || (!target[TARGET_Z_READ_BASE] && !target[TARGET_STENCIL_READ_BASE])) {
        rt_observe_register(buffer, offset, DB_Z_INFO, 0);
        rt_observe_register(buffer, offset, DB_STENCIL_INFO, 0);
        return;
    }
    for (unsigned i = 0; i < TARGET_REGISTER_RUN; i++) rt_observe_register(buffer, offset, DB_Z_INFO + i, target[i]);
    rt_observe_register(buffer, offset, DB_DEPTH_INFO, target[TARGET_DEPTH_INFO]);
    rt_observe_register(buffer, offset, DB_DEPTH_VIEW, target[TARGET_DEPTH_VIEW]);
    rt_observe_register(buffer, offset, DB_HTILE_DATA_BASE, target[TARGET_HTILE_DATA_BASE]);
    rt_observe_register(buffer, offset, DB_HTILE_SURFACE, target[TARGET_HTILE_SURFACE]);
}

enum { DB_STENCILREFMASK = 0xa10c, DB_STENCILREFMASK_BF = 0xa10d, ARGUMENT_FRONT = 1, ARGUMENT_BACK = 2 };

static void stencil_references(const uint64_t args[RT_OBSERVED_ARGUMENTS], uint32_t front, uint32_t back)
{
    const uint64_t buffer = *(const uint64_t *)(uintptr_t)(args[ARGUMENT_COMMAND_BUFFER] + COMMAND_BUFFER_BEGIN);
    const uint32_t offset = (uint32_t)(*(const uint64_t *)(uintptr_t)(args[ARGUMENT_COMMAND_BUFFER] + COMMAND_BUFFER_CURSOR) - buffer);
    rt_observe_register(buffer, offset, DB_STENCILREFMASK, front);
    rt_observe_register(buffer, offset, DB_STENCILREFMASK_BF, back);
}

static void stencil_reference_both(const uint64_t args[RT_OBSERVED_ARGUMENTS])
{
    stencil_references(args, (uint32_t)args[ARGUMENT_FRONT], (uint32_t)args[ARGUMENT_FRONT]);
}

static void stencil_reference_each(const uint64_t args[RT_OBSERVED_ARGUMENTS])
{
    stencil_references(args, (uint32_t)args[ARGUMENT_FRONT], (uint32_t)args[ARGUMENT_BACK]);
}

enum { ARGUMENT_INDEX_VALUE = 1, ARGUMENT_INDEX_COUNT = 2 };
static const uint64_t index_base_mask = 0xfffffffffeull; /* bits 1-39 */

static uint64_t observed_buffer(const uint64_t args[RT_OBSERVED_ARGUMENTS])
{
    return *(const uint64_t *)(uintptr_t)(args[ARGUMENT_COMMAND_BUFFER] + COMMAND_BUFFER_BEGIN);
}

static uint32_t observed_offset(const uint64_t args[RT_OBSERVED_ARGUMENTS])
{
    return (uint32_t)(*(const uint64_t *)(uintptr_t)(args[ARGUMENT_COMMAND_BUFFER] + COMMAND_BUFFER_CURSOR) - observed_buffer(args));
}

static void index_base(const uint64_t args[RT_OBSERVED_ARGUMENTS])
{
    rt_observe_index_base(observed_buffer(args), observed_offset(args), args[ARGUMENT_INDEX_VALUE] & index_base_mask);
}

static void index_size(const uint64_t args[RT_OBSERVED_ARGUMENTS])
{
    rt_observe_index_size(observed_buffer(args), observed_offset(args), (uint32_t)args[ARGUMENT_INDEX_VALUE]);
}

static void index_type(const uint64_t args[RT_OBSERVED_ARGUMENTS])
{
    rt_observe_index_type(observed_buffer(args), observed_offset(args), (uint32_t)args[ARGUMENT_INDEX_VALUE]);
}

static void draw_index_offset(const uint64_t args[RT_OBSERVED_ARGUMENTS])
{
    rt_observe_draw_index_offset(observed_buffer(args), observed_offset(args), (uint32_t)args[ARGUMENT_INDEX_VALUE],
                                 (uint32_t)args[ARGUMENT_INDEX_COUNT]);
}

const rt_observe_hook game_observe_hooks[] = {
    {0x01473190, 87, "render_set_stencil_reference", stencil_reference_both},
    {0x014731f0, 99, "render_set_stencil_reference_separate", stencil_reference_each},
    {0x014738b0, 246, "render_set_render_target", render_target},
    {0x014739b0, 344, "render_set_depth_render_target", depth_target},
    {0x01474420, 176, "render_draw_index_offset", draw_index_offset},
    {0x01474c50, 177, "render_dispatch", dispatch},
    {0x01475270, 76, "render_set_index_type", index_type},
    {0x014752c0, 92, "render_set_index_base", index_base},
    {0x01475320, 76, "render_set_index_buffer_size", index_size},
    {0x014761a0, 156, "render_set_user_data_4_words_marker_4", four_words},
    {0x01476240, 172, "render_set_user_data_8_words", eight_words},
    {0x014762f0, 156, "render_set_user_data_4_words_marker_6", four_words},
    {0x01476390, 133, "render_set_user_data_2_words", two_words},
    {0x01476420, 126, "render_set_user_data_1_word", one_word},
    {0x014764a0, 184, "render_set_user_data_words", counted_words},
    {0x0148a350, 177, "render_dispatch_compute_buffer", dispatch},
    {0x0148aad0, 132, "render_compute_user_data_4_words_marker_4", compute_four_words},
    {0x0148ab60, 148, "render_compute_user_data_8_words", compute_eight_words},
    {0x0148ac00, 132, "render_compute_user_data_4_words_marker_6", compute_four_words},
    {0x0148ac90, 109, "render_compute_user_data_2_words", compute_two_words},
    {0x0148ad00, 146, "render_compute_user_data_words", compute_counted_words},
    {0x0148ada0, 102, "render_compute_user_data_1_word", compute_one_word},
};
const unsigned game_observe_hook_count = sizeof game_observe_hooks / sizeof *game_observe_hooks;
