// SPDX-License-Identifier: GPL-2.0-or-later
/* GPU library functions observed for the renderer (runtime/observe_hooks.h) until they have
   replacements; a replacement's own observer call takes over when one lands.

   setRenderTarget, 0x014738b0 (command buffer, slot, render target): a target whose first word
   (CB_COLOR_BASE) is not 0 (its validity check, 0x01486ca0, returns that word) has its first 11
   words written to CB_COLOR0_BASE + 15 x slot onward; otherwise CB_COLORn_INFO = 0 switches the slot off.

   The user-data writers each write a stage's user SGPRs from a slot on, as one SET_SH_REG packet at the
   base register of the stage's table entry (0x048d55c0) plus the slot; the library's stage numbers
   are those of rt_shader_stage (docs/RENDERER.md). The one-word, two-word, four-word and eight-word
   writers are replaced (game/render/shader_register_packets.cpp) and report themselves; the one left,
   0x014764a0 (command buffer, stage, slot, a pointer to words, their count), writes them after a
   two-word NOP marker 0x6875000d.
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
   Indirect dispatches: 0x014742f0 (command buffer, address) writes SET_BASE with base index 1 (the
   draw and dispatch indirect table) and the address's bits 3-31 and 32-39; 0x01474de0 (command buffer,
   data offset) writes DISPATCH_INDIRECT with that offset and initiator 1, then an event or a NOP. The
   GPU reads the workgroup counts, three 32-bit words, at the base plus the offset when it runs the
   dispatch (shadPS4's liverpool.cpp reads them there, facts only). Its relative 0x01474e80 takes a
   third argument for the initiator's bits 3-4, not established, so it is not observed yet.
   Indirect draws: 0x014744d0 (command buffer, data offset, stage, base-vertex SGPR, start-instance SGPR)
   writes DRAW_INDIRECT with the offset, then for each SGPR that is not 0 its register offset (the
   stage's user-data base from the table at 0x04b2bf30, plus the SGPR, from 0x2c00; 0 for none), and
   initiator 2 (auto index). The GPU reads the four 32-bit arguments (vertices per instance,
   instances, start vertex, start instance) at the base plus the offset and writes the start vertex and
   instance to those SGPRs. The SGPR arguments are bytes.
   The depth render target's setter 0x014739b0 (command buffer, target) writes context registers from the
   target's words when the target is not NULL and has a depth or a stencil buffer (word 2,
   DB_Z_READ_BASE, or word 3, DB_STENCIL_READ_BASE, not 0: the two checks it calls, 0x01472770 and
   0x01472790, return those words): words 0-7 to DB_Z_INFO onward (0xa010-0xa017), word 11 to
   DB_DEPTH_INFO (0xa00f), word 8 to DB_DEPTH_VIEW (0xa002), word 9 to DB_HTILE_DATA_BASE (0xa005),
   word 10 to DB_HTILE_SURFACE (0xa2af); otherwise DB_Z_INFO and DB_STENCIL_INFO = 0 (no depth target).
   The observer reports the same registers.
   The depth bounds setter 0x01473520 (command buffer, min, max; two floats, in xmm0 and xmm1) writes
   DB_DEPTH_BOUNDS_MIN (0xa008) and DB_DEPTH_BOUNDS_MAX (0xa009) in one packet, the floats' bits as they are.
   The stencil reference setters write DB_STENCILREFMASK (0xa10c) and DB_STENCILREFMASK_BF (0xa10d) in one
   packet: 0x01473190 (command buffer, value) writes the value to both, 0x014731f0 (command buffer,
   front, back) one to each.
   Indexed draws: 0x014752c0 (command buffer, address) writes INDEX_BASE with the address's bits 1-31
   and 32-39 (an even 40-bit address); 0x01475320 (command buffer, count) INDEX_BUFFER_SIZE;
   0x01475270 (command buffer, type) INDEX_TYPE with the type as it is; 0x01474420 (command buffer,
   first, count) DRAW_INDEX_OFFSET_2 with MAX_SIZE = count, INDEX_OFFSET = first, INDEX_COUNT = count and
   initiator 0, then an event or a NOP. 0x01474360 (command buffer, count, index address) writes
   DRAW_INDEX_2: the header 0xc0042700 with bit 0 (the predicate) from bit 0 of the command buffer's
   flags at +0x28, MAX_SIZE = count, INDEX_BASE_LO = the address's low 32 bits with bit 0 cleared,
   INDEX_BASE_HI = its high 32 bits, INDEX_COUNT = count, DRAW_INITIATOR = 0, then an event or a NOP.
   YEBIS's indexed draws go through it (its post-processing passes: tone map, glare, depth of field).
   The geometry-shader register writers (from their disassembly): 0x01475a90 (command buffer, item size) writes
   VGT_ESGS_RING_ITEMSIZE (0xa2ab) = the size & 0x7fff; 0x01475af0 (command buffer, a pointer to four 32-bit
   vertex sizes in dwords, one a stream, the most vertices a primitive) writes VGT_GS_VERT_ITEMSIZE to _3
   (0xa2d7-0xa2da) = each size & 0x7fff, VGT_GSVS_RING_OFFSET_1 to _3 (0xa298-0xa29a) = (the sizes of the
   streams before it) x the most vertices, each & 0x7fff, VGT_GS_MAX_VERT_OUT (0xa2ce) = the most vertices & 0x7ff,
   and VGT_GSVS_RING_ITEMSIZE (0xa2ac) = (the four sizes' sum) x the most vertices & 0x7fff; 0x014756b0
   (command buffer, mode, cut mode) writes VGT_GS_MODE (0xa290) = the mode with bits 4-5 the cut mode's low two
   bits, and, when the mode is 0, VGT_STRMOUT_CONFIG (0xa2e5) = 0.
   The viewport scissor setter 0x014766d0 (command buffer, viewport, left, top, right, bottom, and a seventh
   argument on the stack) writes PA_SC_VPORT_SCISSOR_n_TL (0xa094 + 2 x the viewport) = left & 0x7fff |
   (top << 16) & 0x7fff0000, with bit 31 (WINDOW_OFFSET_DISABLE) set when the seventh argument is 0, and
   _BR (the next register) = right & 0x7fff | (bottom << 16) & 0x7fff0000, in one packet (from its
   disassembly). The deferred lights set each 128x128 screen tile as viewport 0's scissor before a draw.
   The clip control setter 0x01476b30 (command buffer, value) writes PA_CL_CLIP_CNTL (0xa204) = the value & 0x0f7fe03f
   in one SET_CONTEXT_REG packet (bbhost's notes name the function and mask, facts only; its disassembly agrees).
   The command buffer starts with its begin and holds its cursor at +0x10 (game/render/command_packets.h).

   Where a packet lands: each writer asks for a fixed number of words (or one computed from its
   arguments, as below) and, with fewer left before the buffer's limit, first calls the buffer's space
   callback (command_reservation.h), which may move the buffer to a new chunk; the packet then starts at
   the cursor the callback leaves. An observer runs before the writer, so it makes that request itself,
   with the same arguments, and reports the packet where it lands; the writer then finds the room and
   does not ask again. Reported at the cursor from before the request, a packet that opened a new chunk
   was filed past the end of the old one, never replayed: a dispatch whose user data opened a chunk ran
   with the previous dispatch's (CS 0x20d1ceb800 read through CS 0x20d1b00100's two V#s at the clinic).
   When the callback refuses, nothing is reported (the writer asks again and writes nothing). The words
   each writer asks for, from their disassembly (the compare before the callback):
     stencil references, depth bounds, indirect base, viewport scissor 4; index type and index buffer
     size 2; index base, ESGS item size, clip control 3; DRAW_INDEX_2 8; DRAW_INDEX_OFFSET_2, indirect
     draw and both dispatches 7; indirect dispatch 5; GS ring sizes 17; GS mode 6 with mode 0, else 3;
     render target 15 for a valid target, else 3; depth target 24 with a depth or stencil buffer, else 6;
     compute user data: one word 3, a pointer 4, four words 8, eight words 12; counted words: the count + 4. */
#include "runtime/observe_hooks.h"
#include "runtime/render_observer.h"
#include "runtime/render_observer_hooks.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The command buffer object (game/render/command_packets.h's PacketBuffer, then the flags). */
typedef struct observed_commands observed_commands;
struct observed_commands {
    uint64_t begin, limit, cursor;
    uint8_t (*space_callback)(observed_commands *commands, uint32_t words, uint64_t argument); /* 0: refused */
    uint64_t callback_argument;
    uint32_t flags; /* bit 0 predicates the packets */
};
static_assert(offsetof(observed_commands, cursor) == 0x10, "cursor at +0x10");
static_assert(offsetof(observed_commands, space_callback) == 0x18, "space callback at +0x18");
static_assert(offsetof(observed_commands, flags) == 0x28, "flags at +0x28");
enum { PACKET_WORD_BYTES = 4 };

typedef struct {
    uint64_t buffer;
    uint32_t offset;
} packet_position;

/* Where the writer called on this command buffer will put its packet of `words` words, asking the space
   callback for room as the writer would (see the top of the file). 0 when the callback refuses. */
static int packet_at(uint64_t command_buffer, uint32_t words, packet_position *at)
{
    observed_commands *commands = (observed_commands *)(uintptr_t)command_buffer;
    const uint32_t left = (uint32_t)((commands->limit - commands->cursor) / PACKET_WORD_BYTES);
    if (left < words && !commands->space_callback(commands, words, commands->callback_argument)) return 0;
    at->buffer = commands->begin;
    at->offset = (uint32_t)(commands->cursor - commands->begin);
    return 1;
}
/* the arguments: (command buffer, stage, slot, value or words) and (command buffer, slot, target) */
enum { ARGUMENT_COMMAND_BUFFER = 0, ARGUMENT_STAGE = 1, ARGUMENT_SLOT = 2, ARGUMENT_VALUE = 3,
       ARGUMENT_TARGET_SLOT = 1, ARGUMENT_TARGET = 2, ARGUMENT_GROUPS_X = 1 };
enum { ONE_WORD = 1, TWO_WORDS = 2, FOUR_WORDS = 4, EIGHT_WORDS = 8 };

/* the words each compute user-data writer asks for: one word 3, a pointer 4, four words 8, eight words 12 */
enum { ONE_WORD_PACKET = 3, TWO_WORDS_PACKET = 4, FOUR_WORDS_PACKET = 8, EIGHT_WORDS_PACKET = 12, COUNTED_PACKET_EXTRA = 4 };

static void observe(const uint64_t args[RT_OBSERVED_ARGUMENTS], const uint32_t *words, unsigned count, uint32_t packet_words)
{
    packet_position at;
    if (!packet_at(args[ARGUMENT_COMMAND_BUFFER], packet_words, &at)) return;
    rt_observe_user_data(at.buffer, at.offset, (rt_shader_stage)(uint32_t)args[ARGUMENT_STAGE], (uint32_t)args[ARGUMENT_SLOT], words, count);
}

static void one_word(const uint64_t args[RT_OBSERVED_ARGUMENTS])
{
    const uint32_t value = (uint32_t)args[ARGUMENT_VALUE];
    observe(args, &value, ONE_WORD, ONE_WORD_PACKET);
}

static void two_words(const uint64_t args[RT_OBSERVED_ARGUMENTS])
{
    const uint32_t words[TWO_WORDS] = {(uint32_t)args[ARGUMENT_VALUE], (uint32_t)(args[ARGUMENT_VALUE] >> 32)};
    observe(args, words, TWO_WORDS, TWO_WORDS_PACKET);
}

static void four_words(const uint64_t args[RT_OBSERVED_ARGUMENTS])
{
    observe(args, (const uint32_t *)(uintptr_t)args[ARGUMENT_VALUE], FOUR_WORDS, FOUR_WORDS_PACKET);
}

static void eight_words(const uint64_t args[RT_OBSERVED_ARGUMENTS])
{
    observe(args, (const uint32_t *)(uintptr_t)args[ARGUMENT_VALUE], EIGHT_WORDS, EIGHT_WORDS_PACKET);
}

/* The counted writer: its words go to the record eight at a time, from the slot on (the record takes
   at most eight per call). */
enum { ARGUMENT_COUNT = 4 };

static void counted_words(const uint64_t args[RT_OBSERVED_ARGUMENTS])
{
    const uint32_t *words = (const uint32_t *)(uintptr_t)args[ARGUMENT_VALUE];
    const uint32_t count = (uint32_t)args[ARGUMENT_COUNT], slot = (uint32_t)args[ARGUMENT_SLOT];
    packet_position at;
    if (!packet_at(args[ARGUMENT_COMMAND_BUFFER], count + COUNTED_PACKET_EXTRA, &at)) return;
    for (uint32_t done = 0; done < count; done += EIGHT_WORDS) {
        const uint32_t n = count - done < EIGHT_WORDS ? count - done : EIGHT_WORDS;
        rt_observe_user_data(at.buffer, at.offset, (rt_shader_stage)(uint32_t)args[ARGUMENT_STAGE], slot + done, words + done, n);
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

enum { DISPATCH_PACKET = 7, TARGET_PACKET = 15, NO_TARGET_PACKET = 3 };

static void dispatch(const uint64_t args[RT_OBSERVED_ARGUMENTS])
{
    packet_position at;
    if (!packet_at(args[ARGUMENT_COMMAND_BUFFER], DISPATCH_PACKET, &at)) return;
    const uint32_t groups[3] = {(uint32_t)args[ARGUMENT_GROUPS_X], (uint32_t)args[ARGUMENT_GROUPS_X + 1],
                                (uint32_t)args[ARGUMENT_GROUPS_X + 2]};
    rt_observe_dispatch(at.buffer, at.offset, groups);
}

static void render_target(const uint64_t args[RT_OBSERVED_ARGUMENTS])
{
    const uint32_t *target = (const uint32_t *)(uintptr_t)args[ARGUMENT_TARGET];
    const int valid = target && target[0];
    packet_position at;
    if (!packet_at(args[ARGUMENT_COMMAND_BUFFER], valid ? TARGET_PACKET : NO_TARGET_PACKET, &at)) return;
    rt_observe_color_target(at.buffer, at.offset, (uint32_t)args[ARGUMENT_TARGET_SLOT], valid ? target : NULL);
}

/* setDepthRenderTarget's registers (gfx_7_2_d.h numbers) */
enum { DB_DEPTH_VIEW = 0xa002, DB_HTILE_DATA_BASE = 0xa005, DB_DEPTH_INFO = 0xa00f, DB_Z_INFO = 0xa010, DB_STENCIL_INFO = 0xa011,
       DB_HTILE_SURFACE = 0xa2af };
enum { TARGET_Z_READ_BASE = 2, TARGET_STENCIL_READ_BASE = 3, TARGET_REGISTER_RUN = 8, TARGET_DEPTH_VIEW = 8, TARGET_HTILE_DATA_BASE = 9,
       TARGET_HTILE_SURFACE = 10, TARGET_DEPTH_INFO = 11, ARGUMENT_DEPTH_TARGET = 1, DEPTH_TARGET_PACKET = 24, NO_DEPTH_TARGET_PACKET = 6 };

static void depth_target(const uint64_t args[RT_OBSERVED_ARGUMENTS])
{
    const uint32_t *target = (const uint32_t *)(uintptr_t)args[ARGUMENT_DEPTH_TARGET];
    const int none = !target || (!target[TARGET_Z_READ_BASE] && !target[TARGET_STENCIL_READ_BASE]);
    packet_position at;
    if (!packet_at(args[ARGUMENT_COMMAND_BUFFER], none ? NO_DEPTH_TARGET_PACKET : DEPTH_TARGET_PACKET, &at)) return;
    const uint64_t buffer = at.buffer;
    const uint32_t offset = at.offset;
    if (none) {
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

enum { DB_STENCILREFMASK = 0xa10c, DB_STENCILREFMASK_BF = 0xa10d, ARGUMENT_FRONT = 1, ARGUMENT_BACK = 2, REGISTER_PAIR_PACKET = 4 };

static void stencil_references(const uint64_t args[RT_OBSERVED_ARGUMENTS], uint32_t front, uint32_t back)
{
    packet_position at;
    if (!packet_at(args[ARGUMENT_COMMAND_BUFFER], REGISTER_PAIR_PACKET, &at)) return;
    rt_observe_register(at.buffer, at.offset, DB_STENCILREFMASK, front);
    rt_observe_register(at.buffer, at.offset, DB_STENCILREFMASK_BF, back);
}

static void stencil_reference_both(const uint64_t args[RT_OBSERVED_ARGUMENTS])
{
    stencil_references(args, (uint32_t)args[ARGUMENT_FRONT], (uint32_t)args[ARGUMENT_FRONT]);
}

static void stencil_reference_each(const uint64_t args[RT_OBSERVED_ARGUMENTS])
{
    stencil_references(args, (uint32_t)args[ARGUMENT_FRONT], (uint32_t)args[ARGUMENT_BACK]);
}

enum { DB_DEPTH_BOUNDS_MIN = 0xa008, DB_DEPTH_BOUNDS_MAX = 0xa009, FLOAT_MIN = 0, FLOAT_MAX = 1, BOUNDS = 2 };

static void depth_bounds(const uint64_t frame[RT_OBSERVED_FRAME_WORDS])
{
    packet_position at;
    if (!packet_at(frame[ARGUMENT_COMMAND_BUFFER], REGISTER_PAIR_PACKET, &at)) return;
    const float bounds[BOUNDS] = {rt_observed_float(frame, FLOAT_MIN), rt_observed_float(frame, FLOAT_MAX)};
    uint32_t bits[BOUNDS];
    memcpy(bits, bounds, sizeof bits);
    rt_observe_register(at.buffer, at.offset, DB_DEPTH_BOUNDS_MIN, bits[FLOAT_MIN]);
    rt_observe_register(at.buffer, at.offset, DB_DEPTH_BOUNDS_MAX, bits[FLOAT_MAX]);
}

enum { ARGUMENT_INDEX_VALUE = 1, ARGUMENT_INDEX_COUNT = 2 };
static const uint64_t index_base_mask = 0xfffffffffeull; /* bits 1-39 */

enum { INDEX_TYPE_PACKET = 2, INDEX_SIZE_PACKET = 2, INDEX_BASE_PACKET = 3, DRAW_INDEX_PACKET = 8, DRAW_INDEX_OFFSET_PACKET = 7,
       INDIRECT_BASE_PACKET = 4, DISPATCH_INDIRECT_PACKET = 5, DRAW_INDIRECT_PACKET = 7 };

static void index_base(const uint64_t args[RT_OBSERVED_ARGUMENTS])
{
    packet_position at;
    if (!packet_at(args[ARGUMENT_COMMAND_BUFFER], INDEX_BASE_PACKET, &at)) return;
    rt_observe_index_base(at.buffer, at.offset, args[ARGUMENT_INDEX_VALUE] & index_base_mask);
}

static void index_size(const uint64_t args[RT_OBSERVED_ARGUMENTS])
{
    packet_position at;
    if (!packet_at(args[ARGUMENT_COMMAND_BUFFER], INDEX_SIZE_PACKET, &at)) return;
    rt_observe_index_size(at.buffer, at.offset, (uint32_t)args[ARGUMENT_INDEX_VALUE]);
}

static void index_type(const uint64_t args[RT_OBSERVED_ARGUMENTS])
{
    packet_position at;
    if (!packet_at(args[ARGUMENT_COMMAND_BUFFER], INDEX_TYPE_PACKET, &at)) return;
    rt_observe_index_type(at.buffer, at.offset, (uint32_t)args[ARGUMENT_INDEX_VALUE]);
}

enum { PREDICATE_FLAG = 1, ARGUMENT_INDEX_ADDRESS = 2 };
enum { DRAW_INDEX_2_HEADER = 0xc0042700u, INDEX_ADDRESS_BIT_0 = 1, DRAW_INITIATOR_DMA = 0 };

static void draw_index(const uint64_t args[RT_OBSERVED_ARGUMENTS])
{
    packet_position at;
    if (!packet_at(args[ARGUMENT_COMMAND_BUFFER], DRAW_INDEX_PACKET, &at)) return;
    const uint32_t flags = ((const observed_commands *)(uintptr_t)args[ARGUMENT_COMMAND_BUFFER])->flags;
    const uint32_t count = (uint32_t)args[ARGUMENT_INDEX_VALUE];
    const uint64_t address = args[ARGUMENT_INDEX_ADDRESS] & ~(uint64_t)INDEX_ADDRESS_BIT_0; /* INDEX_BASE_LO drops bit 0 */
    rt_observe_draw_indexed(at.buffer, at.offset, DRAW_INDEX_2_HEADER | (flags & PREDICATE_FLAG), count, address, count,
                            DRAW_INITIATOR_DMA);
}

static void draw_index_offset(const uint64_t args[RT_OBSERVED_ARGUMENTS])
{
    packet_position at;
    if (!packet_at(args[ARGUMENT_COMMAND_BUFFER], DRAW_INDEX_OFFSET_PACKET, &at)) return;
    rt_observe_draw_index_offset(at.buffer, at.offset, (uint32_t)args[ARGUMENT_INDEX_VALUE],
                                 (uint32_t)args[ARGUMENT_INDEX_COUNT]);
}

enum { ARGUMENT_INDIRECT_VALUE = 1 };

static void indirect_base(const uint64_t args[RT_OBSERVED_ARGUMENTS])
{
    packet_position at;
    if (!packet_at(args[ARGUMENT_COMMAND_BUFFER], INDIRECT_BASE_PACKET, &at)) return;
    rt_observe_indirect_base(at.buffer, at.offset, args[ARGUMENT_INDIRECT_VALUE]);
}

static void dispatch_indirect(const uint64_t args[RT_OBSERVED_ARGUMENTS])
{
    packet_position at;
    if (!packet_at(args[ARGUMENT_COMMAND_BUFFER], DISPATCH_INDIRECT_PACKET, &at)) return;
    rt_observe_dispatch_indirect(at.buffer, at.offset, (uint32_t)args[ARGUMENT_INDIRECT_VALUE]);
}

enum { ARGUMENT_INDIRECT_STAGE = 2, ARGUMENT_BASE_VERTEX_SGPR = 3, ARGUMENT_START_INSTANCE_SGPR = 4, SGPR_ARGUMENT_MASK = 0xff };

static void draw_indirect(const uint64_t args[RT_OBSERVED_ARGUMENTS])
{
    packet_position at;
    if (!packet_at(args[ARGUMENT_COMMAND_BUFFER], DRAW_INDIRECT_PACKET, &at)) return;
    rt_observe_draw_indirect(at.buffer, at.offset, (uint32_t)args[ARGUMENT_INDIRECT_VALUE],
                             (rt_shader_stage)(uint32_t)args[ARGUMENT_INDIRECT_STAGE], (uint32_t)args[ARGUMENT_BASE_VERTEX_SGPR] & SGPR_ARGUMENT_MASK,
                             (uint32_t)args[ARGUMENT_START_INSTANCE_SGPR] & SGPR_ARGUMENT_MASK);
}

enum { VGT_GS_MODE = 0xa290, VGT_GSVS_RING_OFFSET_1 = 0xa298, VGT_ESGS_RING_ITEMSIZE = 0xa2ab, VGT_GSVS_RING_ITEMSIZE = 0xa2ac,
       VGT_GS_MAX_VERT_OUT = 0xa2ce, VGT_GS_VERT_ITEMSIZE = 0xa2d7, VGT_STRMOUT_CONFIG = 0xa2e5, ITEMSIZE_MASK = 0x7fff,
       MAX_VERT_OUT_MASK = 0x7ff, GS_STREAMS = 4, GS_CUT_MODE_SHIFT = 4, GS_CUT_MODE_MASK = 0x30, ARGUMENT_GS_SIZES = 1,
       ARGUMENT_GS_MAX_VERTICES = 2, ARGUMENT_GS_MODE = 1, ARGUMENT_GS_CUT_MODE = 2 };
enum { ESGS_ITEMSIZE_PACKET = 3, GS_RING_SIZES_PACKET = 17, GS_MODE_PACKET = 3, GS_MODE_STREAMOUT_PACKET = 6 };

static void esgs_itemsize(const uint64_t args[RT_OBSERVED_ARGUMENTS])
{
    packet_position at;
    if (!packet_at(args[ARGUMENT_COMMAND_BUFFER], ESGS_ITEMSIZE_PACKET, &at)) return;
    rt_observe_register(at.buffer, at.offset, VGT_ESGS_RING_ITEMSIZE, (uint32_t)args[ARGUMENT_INDEX_VALUE] & ITEMSIZE_MASK);
}

static void gs_ring_sizes(const uint64_t args[RT_OBSERVED_ARGUMENTS])
{
    packet_position at;
    if (!packet_at(args[ARGUMENT_COMMAND_BUFFER], GS_RING_SIZES_PACKET, &at)) return;
    const uint64_t buffer = at.buffer;
    const uint32_t offset = at.offset;
    const uint32_t *sizes = (const uint32_t *)(uintptr_t)args[ARGUMENT_GS_SIZES], most = (uint32_t)args[ARGUMENT_GS_MAX_VERTICES];
    uint32_t before = 0;
    for (unsigned stream = 0; stream < GS_STREAMS; stream++) {
        rt_observe_register(buffer, offset, VGT_GS_VERT_ITEMSIZE + stream, sizes[stream] & ITEMSIZE_MASK);
        before += sizes[stream];
        if (stream + 1 < GS_STREAMS) rt_observe_register(buffer, offset, VGT_GSVS_RING_OFFSET_1 + stream, before * most & ITEMSIZE_MASK);
    }
    rt_observe_register(buffer, offset, VGT_GS_MAX_VERT_OUT, most & MAX_VERT_OUT_MASK);
    rt_observe_register(buffer, offset, VGT_GSVS_RING_ITEMSIZE, before * most & ITEMSIZE_MASK);
}

static void gs_mode(const uint64_t args[RT_OBSERVED_ARGUMENTS])
{
    const uint32_t mode = (uint32_t)args[ARGUMENT_GS_MODE], cut = (uint32_t)args[ARGUMENT_GS_CUT_MODE];
    packet_position at;
    if (!packet_at(args[ARGUMENT_COMMAND_BUFFER], mode ? GS_MODE_PACKET : GS_MODE_STREAMOUT_PACKET, &at)) return;
    rt_observe_register(at.buffer, at.offset, VGT_GS_MODE,
                        (mode & ~(uint32_t)GS_CUT_MODE_MASK) | (cut << GS_CUT_MODE_SHIFT & GS_CUT_MODE_MASK));
    if (!mode) rt_observe_register(at.buffer, at.offset, VGT_STRMOUT_CONFIG, 0);
}

enum { PA_SC_VPORT_SCISSOR_0_TL = 0xa094, ARGUMENT_VIEWPORT = 1, ARGUMENT_LEFT = 2, ARGUMENT_TOP = 3, ARGUMENT_RIGHT = 4,
       ARGUMENT_BOTTOM = 5, STACK_WINDOW_OFFSET = 0, SCISSOR_REGISTERS_A_VIEWPORT = 2, SCISSOR_X_MASK = 0x7fff, SCISSOR_Y_SHIFT = 16 };
static const uint32_t scissor_y_mask = 0x7fff0000u, window_offset_disable = 1u << 31;

static void viewport_scissor(const uint64_t *frame)
{
    packet_position at;
    if (!packet_at(frame[ARGUMENT_COMMAND_BUFFER], REGISTER_PAIR_PACKET, &at)) return;
    const uint32_t left = (uint32_t)frame[ARGUMENT_LEFT], top = (uint32_t)frame[ARGUMENT_TOP];
    const uint32_t right = (uint32_t)frame[ARGUMENT_RIGHT], bottom = (uint32_t)frame[ARGUMENT_BOTTOM];
    const uint32_t offset_disable = (uint32_t)rt_observed_stack_argument(frame, STACK_WINDOW_OFFSET) == 0 ? window_offset_disable : 0;
    const uint32_t top_left = (left & SCISSOR_X_MASK) | (top << SCISSOR_Y_SHIFT & scissor_y_mask) | offset_disable;
    const uint32_t bottom_right = (right & SCISSOR_X_MASK) | (bottom << SCISSOR_Y_SHIFT & scissor_y_mask);
    const uint32_t first = PA_SC_VPORT_SCISSOR_0_TL + SCISSOR_REGISTERS_A_VIEWPORT * (uint32_t)frame[ARGUMENT_VIEWPORT];
    rt_observe_register(at.buffer, at.offset, first, top_left);
    rt_observe_register(at.buffer, at.offset, first + 1, bottom_right);
}

enum { PA_CL_CLIP_CNTL = 0xa204, ARGUMENT_CLIP_CONTROL = 1, CLIP_CONTROL_PACKET = 3 };
static const uint32_t clip_control_mask = 0x0f7fe03f; /* the bits the setter keeps */

static void clip_control(const uint64_t args[RT_OBSERVED_ARGUMENTS])
{
    packet_position at;
    if (!packet_at(args[ARGUMENT_COMMAND_BUFFER], CLIP_CONTROL_PACKET, &at)) return;
    rt_observe_register(at.buffer, at.offset, PA_CL_CLIP_CNTL, (uint32_t)args[ARGUMENT_CLIP_CONTROL] & clip_control_mask);
}

const rt_observe_hook game_observe_hooks[] = {
    {0x01473190, 87, "render_set_stencil_reference", stencil_reference_both},
    {0x014731f0, 99, "render_set_stencil_reference_separate", stencil_reference_each},
    {0x01473520, 107, "render_set_depth_bounds", depth_bounds},
    {0x014738b0, 246, "render_set_render_target", render_target},
    {0x014739b0, 344, "render_set_depth_render_target", depth_target},
    {0x014742f0, 107, "render_set_indirect_base", indirect_base},
    {0x01474360, 189, "render_draw_index", draw_index},
    {0x01474420, 176, "render_draw_index_offset", draw_index_offset},
    {0x014744d0, 246, "render_draw_indirect", draw_indirect},
    {0x01474c50, 177, "render_dispatch", dispatch},
    {0x01474de0, 155, "render_dispatch_indirect", dispatch_indirect},
    {0x01475270, 76, "render_set_index_type", index_type},
    {0x014752c0, 92, "render_set_index_base", index_base},
    {0x01475320, 76, "render_set_index_buffer_size", index_size},
    {0x014756b0, 152, "render_set_gs_mode", gs_mode},
    {0x01475a90, 93, "render_set_esgs_ring_itemsize", esgs_itemsize},
    {0x01475af0, 279, "render_set_gs_ring_sizes", gs_ring_sizes},
    {0x01476b30, 93, "render_set_clip_control", clip_control},
    {0x014764a0, 184, "render_set_user_data_words", counted_words},
    {0x014766d0, 179, "render_set_viewport_scissor", viewport_scissor},
    {0x0148a350, 177, "render_dispatch_compute_buffer", dispatch},
    {0x0148aad0, 132, "render_compute_user_data_4_words_marker_4", compute_four_words},
    {0x0148ab60, 148, "render_compute_user_data_8_words", compute_eight_words},
    {0x0148ac00, 132, "render_compute_user_data_4_words_marker_6", compute_four_words},
    {0x0148ac90, 109, "render_compute_user_data_2_words", compute_two_words},
    {0x0148ad00, 146, "render_compute_user_data_words", compute_counted_words},
    {0x0148ada0, 102, "render_compute_user_data_1_word", compute_one_word},
};
const unsigned game_observe_hook_count = sizeof game_observe_hooks / sizeof *game_observe_hooks;
