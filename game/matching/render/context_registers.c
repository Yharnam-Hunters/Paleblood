// SPDX-License-Identifier: GPL-2.0-or-later
// Matching source (docs/MATCHING.md) for the command-buffer writers that set one or two GPU
// context registers with PM4 SET_CONTEXT_REG packets. Each reserves space first (asking the
// buffer's grow callback when the free space is short) and gives up when the callback declines.
#include <stdbool.h>
#include <stdint.h>

// PM4 type-3 headers: SET_CONTEXT_REG and SET_UCONFIG_REG with one register value, and the
// two-word NUM_INSTANCES and EVENT_WRITE packets.
#define SET_CONTEXT_REG_ONE_VALUE 0xc0016900u
#define SET_UCONFIG_REG_ONE_VALUE 0xc0017900u
#define NUM_INSTANCES_HEADER 0xc0002f00u
#define EVENT_WRITE_HEADER 0xc0004600u
#define TWO_WORD_PACKET_WORDS 2
#define CONTEXT_REG_PACKET_WORDS 3
#define CONTEXT_REG_PAIR_WORDS 6

// Context register indices (relative to the context register base).
#define DB_RENDER_CONTROL 0
#define DB_STENCIL_CLEAR 0xa
#define DB_DEPTH_CLEAR 0xb
#define CB_TARGET_MASK 0x8e
#define PA_SC_SCREEN_OFFSET 0x8d
#define DB_STENCIL_CONTROL 0x10b
#define CB_BLEND0_CONTROL 0x1e0
#define DB_DEPTH_CONTROL 0x200
#define CB_COLOR_CONTROL 0x202
#define PA_CL_VTE_CNTL 0x206
#define PA_SC_MODE_CNTL_0 0x292
#define PA_CL_GB_VERT_CLIP_ADJ 0x2fa
#define PA_CL_GB_VERT_DISC_ADJ 0x2fb
#define PA_CL_GB_HORZ_CLIP_ADJ 0x2fc
#define PA_CL_GB_HORZ_DISC_ADJ 0x2fd

// User-config register index (relative to the user-config register base).
#define VGT_PRIMITIVE_TYPE 0x242
#define VGT_PRIMITIVE_TYPE_MASK 0x3fu

#define DB_RENDER_CONTROL_MASK 0xfffu
#define EVENT_TYPE_MASK 0x3fu
#define EVENT_INDEX_MASK 0xfu
#define EVENT_INDEX_SHIFT 8

#define CB_BLEND_CONTROL_MASK 0xffff1fffu
#define DB_DEPTH_CONTROL_MASK 0xc07007ffu
#define DB_STENCIL_CONTROL_MASK 0x00ffffffu
#define PA_CL_VTE_CNTL_MASK 0x00000f3fu

#define COLOR_MODE_SHIFT 4
#define COLOR_MODE_MASK 0x70u
#define ROP3_SHIFT 16
#define ROP3_MASK 0x00ff0000u

#define SCREEN_OFFSET_X_MASK 0x1fcu
#define SCREEN_OFFSET_Y_SHIFT 16
#define SCREEN_OFFSET_Y_MASK 0x01fc0000u

#define MSAA_ENABLE_BIT 1u
#define VPORT_SCISSOR_ENABLE_SHIFT 1
#define VPORT_SCISSOR_ENABLE_BIT 2u

typedef struct CommandBuffer CommandBuffer;

// A one-value register packet: header, register index, value.
typedef struct {
    uint32_t header;
    uint32_t index;
    uint32_t value;
} RegisterPacket;

// A one-value register packet with a float value.
typedef struct {
    uint32_t header;
    uint32_t index;
    float value;
} FloatRegisterPacket;

// Two one-value register packets back to back, with float values.
typedef struct {
    uint32_t first_header;
    uint32_t first_index;
    float first_value;
    uint32_t second_header;
    uint32_t second_index;
    float second_value;
} FloatRegisterPairPacket;

// A packet word that may alias anything, the command buffer's own fields included: the two-word
// writers reload the cursor after storing their packet.
typedef uint32_t __attribute__((may_alias)) AliasedPacketWord;

// The EVENT_WRITE event index of each event type.
extern const uint32_t record_control_mode_words[];

// Makes room for `words` more words; false when no room could be made.
typedef bool (*CommandBufferGrow)(CommandBuffer *buffer, uint32_t words, void *argument);

struct CommandBuffer {
    uint32_t *begin;
    uint32_t *end;
    uint32_t *cursor;
    CommandBufferGrow grow;
    void *grow_argument;
};

// True when the buffer has room for `words` more words, after growing it if needed.
static inline bool command_buffer_reserve(CommandBuffer *buffer, uint32_t words)
{
    if ((uint32_t)(buffer->end - buffer->cursor) >= words)
        return true;
    return buffer->grow(buffer, words, buffer->grow_argument);
}

// Writes one SET_CONTEXT_REG packet at the cursor; the caller has reserved its words.
static inline void write_context_register(CommandBuffer *buffer, uint32_t index, uint32_t value)
{
    RegisterPacket *packet = (RegisterPacket *)buffer->cursor;
    packet->header = SET_CONTEXT_REG_ONE_VALUE;
    packet->index = index;
    packet->value = value;
    buffer->cursor = (uint32_t *)(packet + 1);
}

// Reserves and writes one SET_CONTEXT_REG packet.
static inline void set_context_register(CommandBuffer *buffer, uint32_t index, uint32_t value)
{
    if (!command_buffer_reserve(buffer, CONTEXT_REG_PACKET_WORDS))
        return;
    write_context_register(buffer, index, value);
}

// Writes two SET_CONTEXT_REG packets of float values (their bit patterns).
static inline void set_context_register_float_pair(CommandBuffer *buffer, uint32_t first_index,
                                                   float first, uint32_t second_index, float second)
{
    if (!command_buffer_reserve(buffer, CONTEXT_REG_PAIR_WORDS))
        return;
    FloatRegisterPairPacket *packet = (FloatRegisterPairPacket *)buffer->cursor;
    packet->first_header = SET_CONTEXT_REG_ONE_VALUE;
    packet->first_index = first_index;
    packet->first_value = first;
    packet->second_header = SET_CONTEXT_REG_ONE_VALUE;
    packet->second_index = second_index;
    packet->second_value = second;
    buffer->cursor = (uint32_t *)(packet + 1);
}

// CB_TARGET_MASK: the write mask of every color target.
void render_set_cb_target_mask(CommandBuffer *buffer, uint32_t mask)
{
    set_context_register(buffer, CB_TARGET_MASK, mask);
}

// CB_BLENDn_CONTROL for one color target.
void render_set_cb_blend_control(CommandBuffer *buffer, uint32_t target, uint32_t control)
{
    uint32_t masked = control & CB_BLEND_CONTROL_MASK;
    if (!command_buffer_reserve(buffer, CONTEXT_REG_PACKET_WORDS))
        return;
    RegisterPacket *packet = (RegisterPacket *)buffer->cursor;
    packet->header = SET_CONTEXT_REG_ONE_VALUE;
    packet->index = target + CB_BLEND0_CONTROL;
    packet->value = masked;
    buffer->cursor = (uint32_t *)(packet + 1);
}

// CB_COLOR_CONTROL: the color mode (bits 4..6) and the raster operation (bits 16..23).
void render_set_cb_color_control(CommandBuffer *buffer, uint32_t color_mode, uint32_t rop3)
{
    set_context_register(buffer, CB_COLOR_CONTROL,
                         ((rop3 << ROP3_SHIFT) & ROP3_MASK) | ((color_mode << COLOR_MODE_SHIFT) & COLOR_MODE_MASK));
}

// DB_DEPTH_CONTROL.
void render_set_db_depth_control(CommandBuffer *buffer, uint32_t control)
{
    set_context_register(buffer, DB_DEPTH_CONTROL, control & DB_DEPTH_CONTROL_MASK);
}

// DB_STENCIL_CONTROL.
void render_set_db_stencil_control(CommandBuffer *buffer, uint32_t control)
{
    set_context_register(buffer, DB_STENCIL_CONTROL, control & DB_STENCIL_CONTROL_MASK);
}

// PA_SC_SCREEN_OFFSET: x in bits 2..8, y in bits 18..24.
void render_set_screen_offset(CommandBuffer *buffer, uint32_t x, uint32_t y)
{
    set_context_register(buffer, PA_SC_SCREEN_OFFSET,
                         (x & SCREEN_OFFSET_X_MASK) | ((y << SCREEN_OFFSET_Y_SHIFT) & SCREEN_OFFSET_Y_MASK));
}

// The guard band's vertical and horizontal clip adjustments.
void render_set_guard_band_clip_adjust(CommandBuffer *buffer, float horizontal, float vertical)
{
    set_context_register_float_pair(buffer, PA_CL_GB_VERT_CLIP_ADJ, vertical, PA_CL_GB_HORZ_CLIP_ADJ, horizontal);
}

// The guard band's vertical and horizontal discard adjustments.
void render_set_guard_band_discard_adjust(CommandBuffer *buffer, float horizontal, float vertical)
{
    set_context_register_float_pair(buffer, PA_CL_GB_VERT_DISC_ADJ, vertical, PA_CL_GB_HORZ_DISC_ADJ, horizontal);
}

// PA_CL_VTE_CNTL: the viewport transform enables.
void render_emit_pa_cl_vte_cntl(CommandBuffer *buffer, uint32_t control)
{
    set_context_register(buffer, PA_CL_VTE_CNTL, control & PA_CL_VTE_CNTL_MASK);
}

// PA_SC_MODE_CNTL_0: MSAA enable (bit 0) and viewport scissor enable (bit 1).
void render_set_pa_sc_mode_cntl_0(CommandBuffer *buffer, uint32_t msaa_enable, uint32_t scissor_enable)
{
    set_context_register(buffer, PA_SC_MODE_CNTL_0,
                         (msaa_enable & MSAA_ENABLE_BIT)
                         | ((scissor_enable << VPORT_SCISSOR_ENABLE_SHIFT) & VPORT_SCISSOR_ENABLE_BIT));
}

// DB_RENDER_CONTROL (its low 12 bits).
void render_emit_present_last_record_packet(CommandBuffer *buffer, uint32_t control)
{
    set_context_register(buffer, DB_RENDER_CONTROL, control & DB_RENDER_CONTROL_MASK);
}

// DB_DEPTH_CLEAR: the depth clear value, stored as the float's bits.
void render_emit_present_preparation_scalar(CommandBuffer *buffer, float depth)
{
    if (!command_buffer_reserve(buffer, CONTEXT_REG_PACKET_WORDS))
        return;
    FloatRegisterPacket *packet = (FloatRegisterPacket *)buffer->cursor;
    packet->header = SET_CONTEXT_REG_ONE_VALUE;
    packet->index = DB_DEPTH_CLEAR;
    packet->value = depth;
    buffer->cursor = (uint32_t *)(packet + 1);
}

// DB_STENCIL_CLEAR: the low byte of `stencil`.
void render_emit_present_preparation_byte(CommandBuffer *buffer, uint32_t stencil)
{
    set_context_register(buffer, DB_STENCIL_CLEAR, (uint8_t)stencil);
}

// VGT_PRIMITIVE_TYPE, a user-config register.
void render_set_vgt_primitive_type(CommandBuffer *buffer, uint32_t primitive_type)
{
    uint32_t masked = primitive_type & VGT_PRIMITIVE_TYPE_MASK;
    if (!command_buffer_reserve(buffer, CONTEXT_REG_PACKET_WORDS))
        return;
    RegisterPacket *packet = (RegisterPacket *)buffer->cursor;
    packet->header = SET_UCONFIG_REG_ONE_VALUE;
    packet->index = VGT_PRIMITIVE_TYPE;
    packet->value = masked;
    buffer->cursor = (uint32_t *)(packet + 1);
}

// A NUM_INSTANCES packet: the instance count of the following draws.
void render_emit_num_instances(CommandBuffer *buffer, uint32_t instance_count)
{
    if (!command_buffer_reserve(buffer, TWO_WORD_PACKET_WORDS))
        return;
    AliasedPacketWord *packet = buffer->cursor;
    packet[0] = NUM_INSTANCES_HEADER;
    packet[1] = instance_count;
    buffer->cursor += TWO_WORD_PACKET_WORDS;
}

// An EVENT_WRITE packet: the event type in bits 0..5 and its event index in bits 8..11.
void render_emit_event_write(CommandBuffer *buffer, uint32_t event_type)
{
    if (!command_buffer_reserve(buffer, TWO_WORD_PACKET_WORDS))
        return;
    uint32_t event = (event_type & EVENT_TYPE_MASK)
                   | ((record_control_mode_words[event_type] & EVENT_INDEX_MASK) << EVENT_INDEX_SHIFT);
    AliasedPacketWord *packet = buffer->cursor;
    packet[0] = EVENT_WRITE_HEADER;
    packet[1] = event;
    buffer->cursor += TWO_WORD_PACKET_WORDS;
}
