// SPDX-License-Identifier: GPL-2.0-or-later
// Initialize a command-buffer view without touching its auxiliary fields. Capacity is stored
// in DWORDs; the end address rounds the byte count down and wraps as an unsigned address.
// A null callback selects the original default callback and discards the supplied argument.
#include "default_command_space_callback.h"

extern "C" void bb_render_command_buffer_init(render::CommandBufferSnapshot *self,
                                               uintptr_t begin, uint32_t size,
                                               render::PacketSpaceCallback callback,
                                               uintptr_t callback_argument)
{
    self->commands.cursor = begin;
    self->commands.begin = begin;
    self->capacity_words = size / render::command_word_bytes;
    const uint32_t capacity_bytes = self->capacity_words * render::command_word_bytes;
    self->commands.limit = begin + capacity_bytes;
    if (callback != nullptr) {
        self->commands.space_callback = callback;
        self->commands.callback_argument = callback_argument;
    } else {
        self->commands.space_callback = render::original_default_command_space_callback_address();
        self->commands.callback_argument = 0;
    }
}
