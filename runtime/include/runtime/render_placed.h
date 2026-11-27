/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef RUNTIME_RENDER_PLACED_H
#define RUNTIME_RENDER_PLACED_H

/* Checking the calls placed by guest address (render_calls.h, rt_render_record_*_at) against the command
   buffer they were placed in.

   A GNM call that binds a shader fills the space the engine reserved with one PM4 type-3 NOP packet (a
   header with its count, then zero dwords: runtime/syslib/gnm.c) and places its call at that address.
   The game rebuilds its command buffers over the same memory, in another layout from frame to frame
   (YEBIS's post-processing passes do), so a placed call whose buffer was rewritten before a submission
   took it would land inside another packet of a later buffer and bind an old shader over the one bound
   since. With a reader set, a placed call keeps the header it was placed on, and a submission takes it
   only where the memory still holds that header followed by the zero dwords it counts; a call placed
   at an address that already holds one replaces the older (that memory was written again).
   With a reader, a submission also walks the buffer's PM4 headers from its start and leaves out every
   call that stands inside an earlier packet's body, which the GPU skips (the game widens a NOP over
   work it turns off): rt_render_take's copy and rt_render_submitted's array (render_calls.h) hold only
   the calls left; rt_render_submitted's is then a copy kept with the list, valid until the buffer is
   recorded into or submitted again, NULL with count 0 when no call is left, and count SIZE_MAX when
   there is no memory for it. Without a reader (the record's own tests, with addresses that are only
   numbers) placed calls are not checked and every call is taken. */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Reads the 32-bit word at a guest address; NULL stops the checking. */
typedef uint32_t (*rt_render_word_reader)(uint64_t address);
void rt_render_check_placed_calls(rt_render_word_reader read);

#ifdef __cplusplus
}
#endif

#endif
