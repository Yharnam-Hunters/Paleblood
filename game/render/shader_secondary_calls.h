// SPDX-License-Identifier: GPL-2.0-or-later
// Upload, descriptor and preparation calls made by the secondary shader consumer.
#pragma once
#include "byte_copy_calls.h"
#include "secondary_calls.h"
#include "shader_preparation_state.h"
namespace render {
struct ShaderUploadArena;
RT_ORIGINAL(0x014761a0, emit_shader_descriptor16,
            void(PacketBuffer *, uint32_t, uint32_t, const void *));
RT_ORIGINAL(0x01476240, emit_shader_descriptor32,
            void(PacketBuffer *, uint32_t, uint32_t, const void *));
RT_ORIGINAL(0x014762f0, emit_shader_sampler16,
            void(PacketBuffer *, uint32_t, uint32_t, const void *));
RT_ORIGINAL(0x01476390, emit_shader_address,
            void(PacketBuffer *, uint32_t, uint32_t, const void *));
RT_ORIGINAL(0x01476420, emit_shader_word,
            void(PacketBuffer *, uint32_t, uint32_t, uint32_t));
RT_ORIGINAL(0x02aaddc0, allocate_shader_upload,
            uint8_t *(ShaderUploadArena *, PacketBuffer **, uint64_t));
RT_ORIGINAL(0x02ab9850, flush_shader_preparation,
            void(ShaderPreparationState *, PacketBuffer *));
}  // namespace render
