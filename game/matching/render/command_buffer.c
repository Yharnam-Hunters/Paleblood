// SPDX-License-Identifier: GPL-2.0-or-later
// Matching source (docs/MATCHING.md) for setting up a command buffer and for the command-buffer
// writers of the present path that are not single-register setters: the submission marker, a
// color target's clear color and the default hardware-state section.
#include <stdbool.h>
#include <stdint.h>

#define COMMAND_WORD_BYTES 4

// The submission marker: a NOP packet spanning 64 words whose first payload word is a tag.
#define SUBMISSION_MARKER_WORDS 64
#define SUBMISSION_MARKER_HEADER 0xc03e1000u
#define SUBMISSION_MARKER_TAG 0x68750777u

// PM4 type-3 SET_CONTEXT_REG header carrying two register values.
#define SET_CONTEXT_REG_TWO_VALUES 0xc0026900u
#define CLEAR_COLOR_PACKET_WORDS 4
// CB_COLOR0_CLEAR_WORD0, and the register distance between color targets.
#define CB_COLOR0_CLEAR_WORD0 0x323
#define COLOR_TARGET_REGISTER_STRIDE 15

// The default hardware-state section asks for this many free words.
#define HARDWARE_STATE_SECTION_WORDS 0x100

typedef struct CommandBuffer CommandBuffer;

// Makes room for `words` more words; false when no room could be made.
typedef bool (*CommandBufferGrow)(CommandBuffer *buffer, uint32_t words, void *argument);

struct CommandBuffer {
    uint32_t *begin;
    uint32_t *end;
    uint32_t *cursor;
    CommandBufferGrow grow;
    void *grow_argument;
};

// A command buffer with its capacity.
typedef struct {
    CommandBuffer commands;
    uint8_t unknown_0x28[0x10];
    uint32_t capacity_words;
} CommandStream;

// The clear-color packet. It may alias anything, the command buffer's own fields included: the
// writer reloads the cursor after storing it.
typedef struct {
    uint32_t header;
    uint32_t index;
    uint64_t clear_color;
} __attribute__((may_alias)) ClearColorPacket;

// The grow callback used when the caller gives none.
extern bool original_default_command_space_callback(CommandBuffer *buffer, uint32_t words, void *argument);

// Writes the default hardware state into `output` (at most `available_words` words) and returns
// the number of words written.
extern uint32_t sceGnmDrawInitDefaultHardwareState200(uint32_t *output, uint64_t available_words);

// True when the buffer has room for `words` more words, after growing it if needed.
static inline bool command_buffer_reserve(CommandBuffer *buffer, uint32_t words)
{
    if ((uint32_t)(buffer->end - buffer->cursor) >= words)
        return true;
    return buffer->grow(buffer, words, buffer->grow_argument);
}

// Starts an empty command buffer over `size` bytes at `begin` (whole words only). Without a grow
// callback, the default one is used.
void render_command_buffer_init(CommandStream *stream, uint32_t *begin, uint32_t size,
                                CommandBufferGrow grow, void *grow_argument)
{
    stream->commands.cursor = begin;
    stream->commands.begin = begin;
    stream->capacity_words = size / COMMAND_WORD_BYTES;
    stream->commands.end = begin + stream->capacity_words;
    if (grow) {
        stream->commands.grow = grow;
        stream->commands.grow_argument = grow_argument;
    } else {
        stream->commands.grow = original_default_command_space_callback;
        stream->commands.grow_argument = 0;
    }
}

// Reserves the submission marker's 64 words and writes its header and tag.
void render_command_prepare_submission(CommandBuffer *buffer)
{
    if (!command_buffer_reserve(buffer, SUBMISSION_MARKER_WORDS))
        return;
    uint32_t *marker = buffer->cursor;
    marker[0] = SUBMISSION_MARKER_HEADER;
    marker[1] = SUBMISSION_MARKER_TAG;
    buffer->cursor = marker + SUBMISSION_MARKER_WORDS;
}

// CB_COLORn_CLEAR_WORD0 and WORD1 of one color target: the 64-bit clear color.
void render_present_slot_emit(CommandBuffer *buffer, uint32_t target, const uint64_t *clear_color)
{
    if (!command_buffer_reserve(buffer, CLEAR_COLOR_PACKET_WORDS))
        return;
    ClearColorPacket *packet = (ClearColorPacket *)buffer->cursor;
    uint32_t index = target * COLOR_TARGET_REGISTER_STRIDE + CB_COLOR0_CLEAR_WORD0;
    packet->header = SET_CONTEXT_REG_TWO_VALUES;
    packet->index = index;
    packet->clear_color = *clear_color;
    buffer->cursor += CLEAR_COLOR_PACKET_WORDS;
}

// Appends the default hardware-state section, written into the free space.
void render_emit_present_context_section(CommandBuffer *buffer)
{
    if (!command_buffer_reserve(buffer, HARDWARE_STATE_SECTION_WORDS))
        return;
    uintptr_t free_bytes = (uintptr_t)buffer->end - (uintptr_t)buffer->cursor;
    uint32_t written = sceGnmDrawInitDefaultHardwareState200(buffer->cursor, free_bytes / COMMAND_WORD_BYTES);
    buffer->cursor += written;
}
