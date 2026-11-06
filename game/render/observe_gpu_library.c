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
     0x01476240  a pointer to eight words, after the marker 0x68750005.
   GCN's buffer and sampler descriptors are four words and its image descriptors eight; which the
   markers name is not established here.
   The dispatch writers 0x01474c50 and 0x0148a350 (command buffer, x, y, z) write DISPATCH_DIRECT
   for x x y x z workgroups with initiator 1 (COMPUTE_SHADER_EN), then an event or a NOP. Their two
   relatives 0x01474d10 and 0x0148a410 take a fifth argument that sets the initiator's low two bits;
   what those select is not established, so they are not observed yet.
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

const rt_observe_hook game_observe_hooks[] = {
    {0x014738b0, 246, "render_set_render_target", render_target},
    {0x01474c50, 177, "render_dispatch", dispatch},
    {0x014761a0, 156, "render_set_user_data_4_words_marker_4", four_words},
    {0x01476240, 172, "render_set_user_data_8_words", eight_words},
    {0x014762f0, 156, "render_set_user_data_4_words_marker_6", four_words},
    {0x01476390, 133, "render_set_user_data_2_words", two_words},
    {0x01476420, 126, "render_set_user_data_1_word", one_word},
    {0x0148a350, 177, "render_dispatch_compute_buffer", dispatch},
};
const unsigned game_observe_hook_count = sizeof game_observe_hooks / sizeof *game_observe_hooks;
