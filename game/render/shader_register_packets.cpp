// SPDX-License-Identifier: GPL-2.0-or-later
// Render: the packet writers that set shader registers in a command buffer (command_packets.h).
// Each reserves the words it writes (command_reservation.h: the space callback is asked when too
// few are left; a refusal writes nothing, a grant is trusted without checking the limit again).
// The register writers take a stage and a user-data slot: the register is the stage's base from
// the game's table plus the slot, counted from 0x2c00 (shader_packet_layout.h), and bit 1 of the
// buffer's flag word (command_flags.h) is carried into the SET_SH_REG header.
// - render_select_shader_command_mode_01476030: one context register (0x2d5) set to the mode, only
//   for the modes the game uses; any other mode writes nothing, after the space was reserved.
// - render_emit_shader_descriptor16_014761a0 / _descriptor32_01476240 / _sampler16_014762f0: a
//   two-word NOP carrying a descriptor control word, then SET_SH_REG with 16 or 32 bytes copied
//   from the descriptor. The cursor moves past the NOP first, and past the rest by reading it
//   again, as the original does: the packet may overlap the buffer's prefix.
// - render_emit_shader_address_01476390: SET_SH_REG with a 64-bit address (low word first); the
//   cursor is read again when it is moved.
// - render_emit_shader_word_01476420: SET_SH_REG with one word; the cursor moves from the value
//   it was written at.
#include <cstdint>

#include "command_flags.h"
#include "command_packets.h"
#include "command_reservation.h"
#include "shader_packet_layout.h"

namespace {

using render::Field;
using render::PacketBuffer;

constexpr uint32_t set_sh_reg_one_word = 0xc0017600;
constexpr uint32_t set_sh_reg_two_words = 0xc0027600;
constexpr uint32_t set_sh_reg_four_words = 0xc0047600;
constexpr uint32_t set_sh_reg_eight_words = 0xc0087600;
constexpr uint32_t descriptor16_control = 0x68750004;
constexpr uint32_t descriptor32_control = 0x68750005;
constexpr uint32_t sampler16_control = 0x68750006;
constexpr uint32_t descriptor_prefix_words = 4;
constexpr uint32_t descriptor16_quads = 2;
constexpr uint32_t descriptor32_quads = 4;

// A one-register packet: SET_CONTEXT_REG or SET_SH_REG, the register, the value.
struct WordPacket {
    Field<uint32_t> header;
    Field<uint32_t> register_index;
    Field<uint32_t> value;
};

uint32_t sh_reg_header(const PacketBuffer *buffer, uint32_t header)
{
    return header | (render::packet_flags(buffer) & render::shader_packet_flag_mask);
}

uint32_t sh_register(uint32_t stage, uint32_t slot)
{
    return render::shader_stage_base(stage) + slot - render::shader_register_origin;
}

void advance_from_buffer(PacketBuffer *buffer, uint32_t bytes)
{
    render::set_packet_cursor(buffer, render::packet_cursor(buffer) + bytes);
}

// The descriptor packets: a NOP with the control word, SET_SH_REG, then the payload. The payload
// is copied as the original copies it: for 32 bytes the upper half first, the fourth quad word
// then the third, each read and written in turn; then the lower half, both quad words read before
// either is written (the second first). 16 bytes copy only the lower half.
void emit_descriptor(PacketBuffer *buffer, uint32_t stage, uint32_t slot, const void *descriptor,
                     uint32_t control, uint32_t header, uint32_t quads)
{
    const uint32_t words = descriptor_prefix_words + quads * 2;
    uintptr_t cursor;
    if (!render::reserve_words(buffer, words, cursor))
        return;
    const uint32_t flags = render::packet_flags(buffer);
    const uint32_t base = render::shader_stage_base(stage);
    render::shader_write_descriptor_header(buffer, cursor, flags, base, slot, control, header);
    enum Quad { first_quad, second_quad, third_quad, fourth_quad };
    auto *out = reinterpret_cast<render::ShaderDescriptorPacket *>(cursor);
    const auto *in = static_cast<const render::ShaderDescriptorWords *>(descriptor);
    if (quads == descriptor32_quads) {
        out->payload[fourth_quad].set(in->words[fourth_quad].get());
        out->payload[third_quad].set(in->words[third_quad].get());
    }
    const uint64_t first = in->words[first_quad].get();
    const uint64_t second = in->words[second_quad].get();
    out->payload[second_quad].set(second);
    out->payload[first_quad].set(first);
    // The cursor already stands past the NOP: move it past SET_SH_REG's header, register and payload.
    constexpr uint32_t register_header_bytes = 2 * sizeof(uint32_t);
    advance_from_buffer(buffer, register_header_bytes + quads * sizeof(uint64_t));
}

constexpr bool is_known_command_mode(int32_t mode)
{
    return mode == 0 || mode == 0x45 || mode == 0xad || mode == 0xb0 || mode == 0x200;
}

}  // namespace

extern "C" void bb_render_select_shader_command_mode_01476030(PacketBuffer *buffer, int32_t mode)
{
    constexpr uint32_t words = 3;
    constexpr uint32_t set_context_reg_one_word = 0xc0016900;
    constexpr uint32_t command_mode_register = 0x2d5;
    uintptr_t cursor;
    if (!render::reserve_words(buffer, words, cursor))
        return;
    if (!is_known_command_mode(mode))
        return;
    auto *out = reinterpret_cast<WordPacket *>(cursor);
    out->header.set(set_context_reg_one_word);
    out->register_index.set(command_mode_register);
    out->value.set(static_cast<uint32_t>(mode));
    render::set_packet_cursor(buffer, cursor + sizeof(WordPacket));
}

extern "C" void bb_render_emit_shader_descriptor16_014761a0(PacketBuffer *buffer, uint32_t stage,
                                                            uint32_t slot, const void *descriptor)
{
    emit_descriptor(buffer, stage, slot, descriptor, descriptor16_control, set_sh_reg_four_words, descriptor16_quads);
}

extern "C" void bb_render_emit_shader_descriptor32_01476240(PacketBuffer *buffer, uint32_t stage,
                                                            uint32_t slot, const void *descriptor)
{
    emit_descriptor(buffer, stage, slot, descriptor, descriptor32_control, set_sh_reg_eight_words, descriptor32_quads);
}

extern "C" void bb_render_emit_shader_sampler16_014762f0(PacketBuffer *buffer, uint32_t stage,
                                                         uint32_t slot, const void *sampler)
{
    emit_descriptor(buffer, stage, slot, sampler, sampler16_control, set_sh_reg_four_words, descriptor16_quads);
}

extern "C" void bb_render_emit_shader_address_01476390(PacketBuffer *buffer, uint32_t stage,
                                                       uint32_t slot, const void *address)
{
    constexpr uint32_t words = 4;
    uintptr_t cursor;
    if (!render::reserve_words(buffer, words, cursor))
        return;
    const uint32_t header = sh_reg_header(buffer, set_sh_reg_two_words);
    const uint32_t index = sh_register(stage, slot);
    const auto value = reinterpret_cast<uint64_t>(address);
    auto *out = reinterpret_cast<render::ShaderPacketWords *>(cursor);
    out->header.set(header);
    out->register_index.set(index);
    out->value_low.set(static_cast<uint32_t>(value));
    out->value_high.set(static_cast<uint32_t>(value >> render::shader_word_bits));
    advance_from_buffer(buffer, sizeof(render::ShaderPacketWords));
}

extern "C" void bb_render_emit_shader_word_01476420(PacketBuffer *buffer, uint32_t stage, uint32_t slot,
                                                    uint32_t value)
{
    constexpr uint32_t words = 3;
    uintptr_t cursor;
    if (!render::reserve_words(buffer, words, cursor))
        return;
    const uint32_t header = sh_reg_header(buffer, set_sh_reg_one_word);
    const uint32_t index = sh_register(stage, slot);
    auto *out = reinterpret_cast<WordPacket *>(cursor);
    out->header.set(header);
    out->register_index.set(index);
    out->value.set(value);
    render::set_packet_cursor(buffer, cursor + sizeof(WordPacket));
}
