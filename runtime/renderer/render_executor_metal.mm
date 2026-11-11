/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Executes the renderer's ordered call record on Metal. GCN remains the common translator's
   input; SPIRV-Cross converts its SPIR-V, and Metal compiles MSL at runtime. The pool's second
   mapping stays alive through the buffer's deallocator, including a timed-out submission. */
#include "runtime/render_executor.h"
#include "runtime/gcn_spirv.h"
#include "runtime/render_calls.h"

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <dispatch/dispatch.h>
#include "spirv_msl.hpp"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <vector>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

enum { MAX_SHADERS = 256, MAX_PIPELINES = 256, MAX_TARGETS = 64, MAX_DIMENSION = 4096,
       USER_BYTES = GCN_USER_SGPR_MAX * 4 };
enum { COLOR_8_8_8_8 = 0xa, NUMBER_UNORM = 0, NUMBER_SRGB = 6, SWAP_STD = 0, SWAP_ALT = 1,
       PERSP_CENTER_ENA = 2, VGT_INDEX_16 = 0, VGT_INDEX_32 = 1, DRAW_INITIATOR_DMA = 0, PM4_PREDICATE = 1 };
enum { SPI_SHADER_FP16_ABGR = 4, SPI_SHADER_32_ABGR = 9, SPI_SHADER_4COMP = 4,
       CLIP_ENABLES = 0xff, CLIP_VECTOR_0 = 1 << 22, CLIP_VECTOR_1 = 1 << 23, PARAMETER_LIMIT = 32 };
struct MetalShader {
    uint64_t code;
    unsigned user, output;
    unsigned required_parameters;
    uint32_t user_read;
    bool vertex, flip_y;
    id<MTLFunction> function;
};
struct MetalTarget {
    uint64_t address;
    uint32_t width, height;
    MTLPixelFormat format;
    id<MTLTexture> texture;
    bool initialized = false;
};
struct MetalPipeline {
    id<MTLFunction> vertex, pixel;
    MTLPixelFormat format;
    bool write_color;
    id<MTLRenderPipelineState> pipeline;
};
struct MetalState {
    rt_color_target color{};
    bool color_enabled = false, other[RT_COLOR_TARGETS] = {};
    bool viewport_set = false, vertex_set = false, pixel_set = false;
    float scale[3] = {}, translate[3] = {};
    float zmin = 0, zmax = 1;
    rt_vertex_shader vertex{};
    rt_pixel_shader pixel{};
    uint32_t user[2][GCN_USER_SGPR_MAX] = {};
    uint32_t user_set[2] = {};
    uint32_t blend = 0, depth = 0, color_control = 0, target_mask = 0, primitive = 0, instances = 1;
    bool blend_set = false, depth_set = false, color_control_set = false, target_mask_set = false, primitive_set = false;
    uint32_t scissor_tl = 0, scissor_br = 0;
    bool scissor_tl_set = false, scissor_br_set = false;
    uint32_t index_type = 0, index_size = 0;
    uint64_t index_base = 0;
    bool index_type_set = false, index_base_set = false, index_size_set = false;
    bool index_base_current = false;
};
struct rt_executor {
    id<MTLDevice> device = nil;
    id<MTLCommandQueue> queue = nil;
    id<MTLBuffer> memory = nil;
    id<MTLBuffer> stop = nil;
    void *view = nullptr;
    uint64_t size = 0, guest_base = 0, address_offset = 0;
    bool failed = false;
    MetalState state;
    std::vector<MetalShader> shaders;
    std::vector<MetalTarget> targets;
    std::vector<MetalPipeline> pipelines;
};
/* Own the new mapping until Metal has accepted it, including Objective-C exceptions. */
struct PoolMapping {
    void *address;
    size_t size;
    ~PoolMapping() { if (address != MAP_FAILED) munmap(address, size); }
};
static int failed(char *error, size_t size, const char *format, ...)
{
    if (error && size) {
        va_list args; va_start(args, format); std::vsnprintf(error, size, format, args); va_end(args);
    }
    return -1;
}

/* Validate the supported opaque-triangle profile using AMD GFX7 register fields. */
enum { CB_TARGET_MASK = 0xa08e, DB_STENCIL_CONTROL = 0xa10b, CB_BLEND0_CONTROL = 0xa1e0,
       DB_DEPTH_CONTROL = 0xa200, CB_COLOR_CONTROL = 0xa202, VGT_PRIMITIVE_TYPE = 0xc242,
       PA_SC_SCREEN_SCISSOR_TL = 0xa00c, PA_SC_SCREEN_SCISSOR_BR = 0xa00d };
enum { BLEND_ENABLE = 0x40000000, STENCIL_ENABLE = 1, Z_ENABLE = 2, Z_WRITE_ENABLE = 4,
       COLOR_MODE_MASK = 0x70, COLOR_MODE_SHIFT = 4, CB_NORMAL = 1, ROP3_MASK = 0xff0000,
       ROP3_SHIFT = 16, ROP3_COPY = 0xcc, TARGET0_MASK = 15, DI_PT_TRILIST = 4, DI_PT_RECTLIST = 17 };
static int set_register(MetalState &state, uint32_t address, uint32_t value)
{
    switch (address) {
    case CB_BLEND0_CONTROL: state.blend = value; state.blend_set = true; return 0;
    case DB_DEPTH_CONTROL: state.depth = value; state.depth_set = true; return 0;
    case CB_COLOR_CONTROL: state.color_control = value; state.color_control_set = true; return 0;
    case CB_TARGET_MASK: state.target_mask = value; state.target_mask_set = true; return 0;
    case VGT_PRIMITIVE_TYPE: state.primitive = value; state.primitive_set = true; return 0;
    case PA_SC_SCREEN_SCISSOR_TL: state.scissor_tl = value; state.scissor_tl_set = true; return 0;
    case PA_SC_SCREEN_SCISSOR_BR: state.scissor_br = value; state.scissor_br_set = true; return 0;
    case DB_STENCIL_CONTROL: return 0; /* Stencil testing itself is rejected before drawing. */
    default: return -1;
    }
}

/* Validate the supported state. A zero colour mask still executes shaders and their writes. */
static int check_state(const MetalState &state, char *error, size_t size)
{
    if (state.primitive_set && state.primitive == DI_PT_RECTLIST)
        return failed(error, size, "rectangle lists require a geometry expansion profile, which Metal does not implement");
    if (state.primitive_set && state.primitive != DI_PT_TRILIST)
        return failed(error, size, "primitive type %u: only triangle lists are drawn yet", state.primitive);
    if (state.blend_set && (state.blend & BLEND_ENABLE)) return failed(error, size, "blending is not drawn yet");
    if (state.depth_set && (state.depth & (STENCIL_ENABLE | Z_ENABLE | Z_WRITE_ENABLE)))
        return failed(error, size, "depth or stencil testing is not drawn yet");
    if (state.color_control_set) {
        const uint32_t mode = (state.color_control & COLOR_MODE_MASK) >> COLOR_MODE_SHIFT;
        const uint32_t rop = (state.color_control & ROP3_MASK) >> ROP3_SHIFT;
        if (mode != CB_NORMAL || rop != ROP3_COPY)
            return failed(error, size, "colour mode %u, raster operation %#x: only normal copies are drawn yet", mode, rop);
    }
    if (state.instances != 1) return failed(error, size, "%u instances: only one is drawn yet", state.instances);
    if (state.target_mask_set && (state.target_mask & TARGET0_MASK) != 0 &&
        (state.target_mask & TARGET0_MASK) != TARGET0_MASK)
        return failed(error, size, "colour write mask %#x: only all four channels are drawn yet", state.target_mask & TARGET0_MASK);
    return 0;
}

static int shader_profile(const MetalState &state, char *error, size_t size)
{
    if (state.vertex.vgpr_inputs)
        return failed(error, size, "vertex input VGPR profile %u: only vertex ID in VGPR0 is supported", state.vertex.vgpr_inputs);
    if (state.vertex.user_sgprs > GCN_USER_SGPR_MAX || state.pixel.user_sgprs > GCN_USER_SGPR_MAX)
        return failed(error, size, "shader user SGPR count exceeds the modeled user-data range");
    if (state.vertex.user_sgprs > state.vertex.sgprs || state.pixel.user_sgprs > state.pixel.sgprs)
        return failed(error, size, "shader user SGPR count exceeds its declared allocation");
    const uint32_t control = state.vertex.output_control;
    if (control & ~(CLIP_ENABLES | CLIP_VECTOR_0 | CLIP_VECTOR_1))
        return failed(error, size, "PA_CL_VS_OUT_CNTL misc or cull outputs are not modeled");
    if (((control & 0xf) && !(control & CLIP_VECTOR_0)) || ((control & 0xf0) && !(control & CLIP_VECTOR_1)))
        return failed(error, size, "PA_CL_VS_OUT_CNTL enables clip distances without their export vector");
    uint32_t position_format = SPI_SHADER_4COMP;
    if (control & CLIP_VECTOR_0) position_format = position_format << 4 | SPI_SHADER_4COMP;
    if (control & CLIP_VECTOR_1) position_format = position_format << 4 | SPI_SHADER_4COMP;
    if (state.vertex.position_format != position_format)
        return failed(error, size, "SPI_SHADER_POS_FORMAT must declare float4 position and enabled clip vectors");
    if (state.pixel.input_enable != PERSP_CENTER_ENA || state.pixel.input_address != PERSP_CENTER_ENA)
        return failed(error, size, "pixel-shader inputs beyond perspective-center barycentrics are not modeled");
    if (state.pixel.depth_format) return failed(error, size, "pixel depth exports are not modeled");
    if (state.pixel.color_formats != SPI_SHADER_FP16_ABGR && state.pixel.color_formats != SPI_SHADER_32_ABGR)
        return failed(error, size, "SPI_SHADER_COL_FORMAT must select FP16_ABGR or 32_ABGR for target 0 only");
    if (state.pixel.color_mask != TARGET0_MASK)
        return failed(error, size, "CB_SHADER_MASK must select RGBA on target 0 only");
    if (state.pixel.interpolated > PARAMETER_LIMIT)
        return failed(error, size, "SPI_PS_IN_CONTROL exceeds the modeled parameter range");
    if (state.pixel.barycentric_control) return failed(error, size, "nonzero SPI_BARYC_CNTL is not modeled");
    if (state.pixel.depth_control) return failed(error, size, "nonzero DB_SHADER_CONTROL is not modeled");
    return 0;
}

/* AMD CIK 3D registers, PA_SC_SCREEN_SCISSOR_TL/BR: signed 16-bit fields, negatives clamp
   to zero, upper-left inclusive and bottom-right exclusive. WINDOW_OFFSET does not apply.
   Intersect with the image because Metal requires a scissor wholly within its attachment. */
static int screen_scissor(const MetalState &state, const MetalTarget &target, MTLScissorRect &rect,
                          char *error, size_t size)
{
    rect = {0, 0, target.width, target.height};
    if (!state.scissor_tl_set && !state.scissor_br_set) return 0;
    if (!state.scissor_tl_set || !state.scissor_br_set)
        return failed(error, size, "screen scissor requires both TL and BR registers");
    auto signed_field = [](uint32_t word, unsigned shift) {
        int32_t value = static_cast<int32_t>(word >> shift & 0xffff);
        return value >= 0x8000 ? value - 0x10000 : value;
    };
    int32_t left = signed_field(state.scissor_tl, 0), top = signed_field(state.scissor_tl, 16);
    int32_t right = signed_field(state.scissor_br, 0), bottom = signed_field(state.scissor_br, 16);
    if (left > 16383 || top > 16383 || right > 16384 || bottom > 16384)
        return failed(error, size, "screen scissor coordinate exceeds AMD's documented range");
    left = std::clamp(left, 0, static_cast<int32_t>(target.width));
    top = std::clamp(top, 0, static_cast<int32_t>(target.height));
    right = std::clamp(right, 0, static_cast<int32_t>(target.width));
    bottom = std::clamp(bottom, 0, static_cast<int32_t>(target.height));
    rect = {static_cast<NSUInteger>(left), static_cast<NSUInteger>(top),
        static_cast<NSUInteger>(std::max(0, right - left)), static_cast<NSUInteger>(std::max(0, bottom - top))};
    return 0;
}

extern "C" int rt_executor_open(const rt_executor_memory *memory, rt_executor **out, char *error, size_t error_size)
{
    if (!memory || !out || memory->fd < 0 || !memory->size || memory->size > SIZE_MAX ||
        memory->guest_base > UINT64_MAX - memory->size || memory->size % static_cast<uint64_t>(getpagesize()))
        return failed(error, error_size, "require a destination, a page-sized pool and a non-overflowing guest range");
    struct stat info{};
    if (fstat(memory->fd, &info) || info.st_size < 0 || static_cast<uint64_t>(info.st_size) < memory->size)
        return failed(error, error_size, "pool descriptor is smaller than the declared memory");
    @autoreleasepool {
      try {
        @try {
            if (@available(macOS 15.0, *)) {} else
                return failed(error, error_size, "Metal executor requires macOS 15 or later");
            auto next = std::make_unique<rt_executor>();
            next->device = MTLCreateSystemDefaultDevice();
            if (!next->device || !next->device.hasUnifiedMemory)
                return failed(error, error_size, "Metal executor requires a unified-memory device");
            if (memory->size > next->device.maxBufferLength)
                return failed(error, error_size, "pool exceeds the device's maximum buffer length");
            next->queue = [next->device newCommandQueue];
            if (!next->queue) return failed(error, error_size, "Metal command queue allocation failed");
            PoolMapping mapping{mmap(nullptr, static_cast<size_t>(memory->size), PROT_READ | PROT_WRITE,
                                     MAP_SHARED, memory->fd, 0), static_cast<size_t>(memory->size)};
            if (mapping.address == MAP_FAILED) return failed(error, error_size, "pool mapping failed");
            next->memory = [next->device newBufferWithBytesNoCopy:mapping.address length:static_cast<NSUInteger>(memory->size)
                options:MTLResourceStorageModeShared deallocator:^(void *pointer, NSUInteger length) { munmap(pointer, length); }];
            if (!next->memory) return failed(error, error_size, "Metal refused the pool mapping");
            next->view = mapping.address;
            mapping.address = MAP_FAILED;
            next->size = memory->size; next->guest_base = memory->guest_base;
            if (!next->memory.gpuAddress) return failed(error, error_size, "the device did not expose the pool's GPU address");
            next->address_offset = next->memory.gpuAddress - memory->guest_base;
            next->stop = [next->device newBufferWithLength:sizeof(uint32_t) options:MTLResourceStorageModeShared];
            if (!next->stop || !next->stop.gpuAddress) return failed(error, error_size, "Metal runtime stop-word allocation failed");
            *static_cast<volatile uint32_t *>(next->stop.contents) = 0;
            *out = next.release();
            return 0;
        } @catch (NSException *exception) { return failed(error, error_size, "%s", exception.reason.UTF8String); }
      } catch (const std::exception &exception) { return failed(error, error_size, "Metal allocation: %s", exception.what()); }
    }
}

static MetalTarget *target_for(rt_executor *x, const rt_color_target &color, char *error, size_t size)
{
    if (color.format != COLOR_8_8_8_8 || (color.number_type != NUMBER_UNORM && color.number_type != NUMBER_SRGB) ||
        color.component_swap > SWAP_ALT)
        return failed(error, size, "render target format %#x, number type %u, swap %u is not drawn yet",
                      color.format, color.number_type, color.component_swap), nullptr;
    if (color.samples != 1 || color.first_slice || color.last_slice || color.fast_clear || color.compression)
        return failed(error, size, "multisample, layered or compressed render targets are not drawn yet"), nullptr;
    if (!color.pitch || !color.height || color.pitch > MAX_DIMENSION || color.height > MAX_DIMENSION)
        return failed(error, size, "render target dimensions exceed the executor's 1..4096 profile"), nullptr;
    const MTLPixelFormat format = color.component_swap == SWAP_STD
        ? (color.number_type == NUMBER_SRGB ? MTLPixelFormatRGBA8Unorm_sRGB : MTLPixelFormatRGBA8Unorm)
        : (color.number_type == NUMBER_SRGB ? MTLPixelFormatBGRA8Unorm_sRGB : MTLPixelFormatBGRA8Unorm);
    for (auto &target : x->targets) if (target.address == color.address) {
        if (target.width != color.pitch || target.height != color.height || target.format != format)
            return failed(error, size, "a render target changed size or format; reallocation is not supported"), nullptr;
        return &target;
    }
    if (x->targets.size() == MAX_TARGETS) return failed(error, size, "render target cache is full"), nullptr;
    MTLTextureDescriptor *descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format
        width:color.pitch height:color.height mipmapped:NO];
    descriptor.storageMode = MTLStorageModeShared;
    descriptor.usage = MTLTextureUsageRenderTarget;
    descriptor.allowGPUOptimizedContents = NO;
    id<MTLTexture> texture = [x->device newTextureWithDescriptor:descriptor];
    if (!texture) return failed(error, size, "render target allocation failed"), nullptr;
    x->targets.push_back({color.address, color.pitch, color.height, format, texture, false});
    return &x->targets.back();
}

static id<MTLFunction> shader_for(rt_executor *x, bool vertex, uint64_t code, unsigned user, unsigned output,
                                 unsigned parameter_capacity, bool flip_y, uint32_t &user_read, char *error, size_t error_size)
{
    for (const auto &shader : x->shaders)
        if (shader.code == code && shader.vertex == vertex && shader.user == user && shader.output == output && shader.flip_y == flip_y) {
            if (shader.required_parameters > parameter_capacity)
                return failed(error, error_size, "%s allocation is smaller than the shader interface", vertex ? "vertex parameter" : "pixel interpolant"), nil;
            user_read = shader.user_read;
            return shader.function;
        }
    if (code < x->guest_base || code - x->guest_base >= x->size || code % 4)
        return failed(error, error_size, "shader code is outside the GPU-visible pool or unaligned"), nil;
    if (x->shaders.size() == MAX_SHADERS) return failed(error, error_size, "shader cache is full"), nil;
    const auto *words = reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(x->view) + code - x->guest_base);
    const size_t available = static_cast<size_t>((x->size - (code - x->guest_base)) / 4);
    uint32_t *spirv = nullptr; size_t count = 0; char reason[256] = {};
    int result;
    if (vertex) {
        /* Fetch callees stay unbound in this profile, so a call stops translation by name. */
        gcn_vertex_layout layout{};
        layout.user_sgprs = user; layout.output_control = output;
        layout.address_offset = x->address_offset; layout.error_address = x->stop.gpuAddress;
        layout.user_data_read = &user_read;
        result = gcn_translate_vertex(words, available, &layout, &spirv, &count, reason, sizeof reason);
    } else {
        /* Only perspective-center barycentrics occupy VGPR0/1. Image and sampler bindings stay empty,
           so image instructions stop translation before any GPU access. */
        gcn_pixel_layout layout{};
        layout.user_sgprs = user; layout.push_constant_offset = USER_BYTES;
        layout.address_offset = x->address_offset; layout.error_address = x->stop.gpuAddress;
        layout.user_data_read = &user_read;
        result = gcn_translate_pixel(words, available, &layout, &spirv, &count, reason, sizeof reason);
    }
    if (result) return failed(error, error_size, "%s shader translation: %s", vertex ? "vertex" : "pixel", reason), nil;
    std::unique_ptr<uint32_t, decltype(&std::free)> owned(spirv, &std::free);
    try {
        spirv_cross::CompilerMSL compiler(spirv, count);
        const auto &capabilities = compiler.get_declared_capabilities();
        if (std::find(capabilities.begin(), capabilities.end(), spv::CapabilityGroupNonUniformQuad) != capabilities.end())
            return failed(error, error_size, "whole-quad shader operations are outside the Metal draw profile"), nil;
        const auto resources = compiler.get_shader_resources();
        const auto &parameters = vertex ? resources.stage_outputs : resources.stage_inputs;
        unsigned required_parameters = 0;
        for (const auto &parameter : parameters) {
            if (compiler.has_decoration(parameter.id, spv::DecorationBuiltIn)) continue;
            const auto &type = compiler.get_type(parameter.type_id);
            if (!compiler.has_decoration(parameter.id, spv::DecorationLocation) || !type.array.empty() ||
                type.basetype != spirv_cross::SPIRType::Float || type.width != 32 || type.vecsize != 4 || type.columns != 1)
                return failed(error, error_size, "shader parameter interface is not a located float4"), nil;
            const unsigned location = compiler.get_decoration(parameter.id, spv::DecorationLocation);
            if (location >= PARAMETER_LIMIT) return failed(error, error_size, "shader parameter location exceeds the modeled range"), nil;
            required_parameters = std::max(required_parameters, location + 1);
        }
        if (required_parameters > parameter_capacity)
            return failed(error, error_size, "%s allocation is smaller than the shader interface", vertex ? "vertex parameter" : "pixel interpolant"), nil;
        if (!vertex) for (const auto &color : resources.stage_outputs)
            if (!compiler.has_decoration(color.id, spv::DecorationBuiltIn) &&
                (!compiler.has_decoration(color.id, spv::DecorationLocation) || compiler.get_decoration(color.id, spv::DecorationLocation) != 0))
                return failed(error, error_size, "pixel outputs beyond target 0 are not modeled"), nil;
        auto options = compiler.get_msl_options(); options.set_msl_version(2, 3);
        compiler.set_msl_options(options);
        auto common = compiler.get_common_options(); common.vertex.flip_vert_y = vertex && flip_y;
        compiler.set_common_options(common);
        const auto stage = vertex ? spv::ExecutionModelVertex : spv::ExecutionModelFragment;
        spirv_cross::MSLResourceBinding binding{};
        binding.stage = stage; binding.desc_set = spirv_cross::ResourceBindingPushConstantDescriptorSet;
        binding.binding = spirv_cross::ResourceBindingPushConstantBinding; binding.msl_buffer = 0;
        compiler.add_msl_resource_binding(binding);
        const auto entries = compiler.get_entry_points_and_stages();
        if (entries.size() != 1 || entries[0].execution_model != stage)
            return failed(error, error_size, "translated shader entry point does not match its stage"), nil;
        const std::string source = compiler.compile();
        const std::string entry = compiler.get_cleansed_entry_point_name(entries[0].name, stage);
        MTLCompileOptions *metal_options = [MTLCompileOptions new];
        metal_options.languageVersion = MTLLanguageVersion2_3;
        if (@available(macOS 15.0, *)) metal_options.mathMode = MTLMathModeSafe;
        else return failed(error, error_size, "safe MSL math requires macOS 15 or later"), nil;
        NSError *failure = nil;
        id<MTLLibrary> library = [x->device newLibraryWithSource:[NSString stringWithUTF8String:source.c_str()]
            options:metal_options error:&failure];
        if (!library) return failed(error, error_size, "MSL compilation: %s", failure.localizedDescription.UTF8String), nil;
        id<MTLFunction> function = [library newFunctionWithName:[NSString stringWithUTF8String:entry.c_str()]];
        if (!function) return failed(error, error_size, "compiled MSL entry point is missing"), nil;
        x->shaders.push_back({code, user, output, required_parameters, user_read, vertex, flip_y, function});
        return function;
    } catch (const std::exception &exception) { return failed(error, error_size, "SPIR-V to MSL: %s", exception.what()), nil; }
}

static id<MTLRenderPipelineState> pipeline_for(rt_executor *x, id<MTLFunction> vertex, id<MTLFunction> pixel,
                                             MTLPixelFormat format, bool write_color, char *error, size_t size)
{
    for (const auto &entry : x->pipelines)
        if (entry.vertex == vertex && entry.pixel == pixel && entry.format == format && entry.write_color == write_color)
            return entry.pipeline;
    if (x->pipelines.size() == MAX_PIPELINES) return failed(error, size, "draw pipeline cache is full"), nil;
    NSError *failure = nil;
    MTLRenderPipelineDescriptor *description = [MTLRenderPipelineDescriptor new];
    description.vertexFunction = vertex; description.fragmentFunction = pixel;
    description.colorAttachments[0].pixelFormat = format;
    description.colorAttachments[0].writeMask = write_color ? MTLColorWriteMaskAll : MTLColorWriteMaskNone;
    id<MTLRenderPipelineState> pipeline = [x->device newRenderPipelineStateWithDescriptor:description error:&failure];
    if (!pipeline) return failed(error, size, "Metal draw pipeline: %s", failure.localizedDescription.UTF8String), nil;
    x->pipelines.push_back({vertex, pixel, format, write_color, pipeline});
    return pipeline;
}

struct MetalIndices {
    MTLIndexType type;
    NSUInteger offset;
};

/* A physical zero in the backing array does not establish an incoming user SGPR's value. */
static int check_user_data(bool vertex, uint32_t required, uint32_t written, char *error, size_t size)
{
    const uint32_t missing = required & ~written;
    if (missing) return failed(error, size, "%s shader reads unset user data %#x", vertex ? "vertex" : "pixel", missing);
    return 0;
}

static int draw(rt_executor *x, const MetalState &state, uint32_t count, char *error, size_t size,
                 const MetalIndices *indices = nullptr)
{
    if (!state.color_enabled) return failed(error, size, "a draw has no color target in slot 0");
    for (unsigned slot = 1; slot < RT_COLOR_TARGETS; ++slot)
        if (state.other[slot]) return failed(error, size, "render targets beyond slot 0 are not drawn yet");
    if (!state.viewport_set || !state.vertex_set || !state.pixel_set)
        return failed(error, size, "a draw requires viewport, vertex and pixel shader state");
    if (!state.pixel.code)
        return failed(error, size, "a draw with no pixel shader (a depth-only pass): not drawn yet");
    if (shader_profile(state, error, size)) return -1;
    uint32_t zmin_bits, zmax_bits;
    std::memcpy(&zmin_bits, &state.zmin, sizeof zmin_bits); std::memcpy(&zmax_bits, &state.zmax, sizeof zmax_bits);
    if ((zmin_bits & 0x7fffffffu) || zmax_bits != 0x3f800000u)
        return failed(error, size, "viewport zmin/zmax must use the standard 0,1 clip bounds");
    for (unsigned axis = 0; axis < 3; ++axis)
        if (!std::isfinite(state.scale[axis]) || !std::isfinite(state.translate[axis]))
            return failed(error, size, "viewport contains a non-finite value");
    for (unsigned axis = 0; axis < 2; ++axis)
        if (std::fabs(state.scale[axis]) > static_cast<float>(MAX_DIMENSION) ||
            std::fabs(state.translate[axis]) > static_cast<float>(MAX_DIMENSION))
            return failed(error, size, "viewport exceeds the executor's coordinate range");
    if (state.scale[0] <= 0 || state.scale[1] == 0 || state.translate[2] < 0 ||
        state.translate[2] + state.scale[2] > 1 || state.scale[2] < 0)
        return failed(error, size, "viewport scale or depth range is not supported");
    MetalTarget *target = target_for(x, state.color, error, size);
    if (!target) return -1;
    MTLScissorRect scissor;
    if (screen_scissor(state, *target, scissor, error, size)) return -1;
    uint32_t vertex_read = 0, pixel_read = 0;
    id<MTLFunction> vertex = shader_for(x, true, state.vertex.code, state.vertex.user_sgprs,
                                      state.vertex.output_control, state.vertex.parameter_count, state.scale[1] > 0, vertex_read, error, size);
    id<MTLFunction> pixel = vertex ? shader_for(x, false, state.pixel.code, state.pixel.user_sgprs, 0,
                                             state.pixel.interpolated, false, pixel_read, error, size) : nil;
    if (!pixel) return -1;
    if (check_user_data(true, vertex_read, state.user_set[0], error, size) ||
        check_user_data(false, pixel_read, state.user_set[1], error, size)) return -1;
    const bool write_color = !state.target_mask_set || (state.target_mask & TARGET0_MASK) != 0;
    id<MTLRenderPipelineState> pipeline = pipeline_for(x, vertex, pixel, target->format, write_color, error, size);
    if (!pipeline) return -1;
    MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = target->texture;
    pass.colorAttachments[0].loadAction = target->initialized ? MTLLoadActionLoad : MTLLoadActionClear;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0);
    *static_cast<volatile uint32_t *>(x->stop.contents) = 0;
    id<MTLCommandBuffer> command = [x->queue commandBuffer];
    id<MTLRenderCommandEncoder> encoder = [command renderCommandEncoderWithDescriptor:pass];
    if (!command || !encoder) return failed(error, size, "Metal draw encoder allocation failed");
    [encoder setRenderPipelineState:pipeline]; [encoder setCullMode:MTLCullModeNone];
    const double sy = std::fabs(state.scale[1]);
    const MTLViewport viewport = {state.translate[0] - state.scale[0], state.translate[1] - sy,
        2 * state.scale[0], 2 * sy, state.translate[2], state.translate[2] + state.scale[2]};
    [encoder setViewport:viewport];
    [encoder setScissorRect:scissor];
    [encoder setVertexBytes:state.user length:sizeof state.user atIndex:0];
    [encoder setFragmentBytes:state.user length:sizeof state.user atIndex:0];
    [encoder useResource:x->memory usage:MTLResourceUsageRead | MTLResourceUsageWrite stages:MTLRenderStageVertex | MTLRenderStageFragment];
    [encoder useResource:x->stop usage:MTLResourceUsageWrite stages:MTLRenderStageVertex | MTLRenderStageFragment];
    if (indices)
        [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:count indexType:indices->type
            indexBuffer:x->memory indexBufferOffset:indices->offset];
    else [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:count];
    [encoder endEncoding];
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    if (!done) return failed(error, size, "Metal completion semaphore allocation failed");
    id<MTLBuffer> keep_pool = x->memory, keep_stop = x->stop;
    [command addCompletedHandler:^(id<MTLCommandBuffer> completed) {
        (void)completed;
        if (keep_pool.length && keep_stop.length) dispatch_semaphore_signal(done);
    }];
    [command commit];
    if (dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 30000ll * NSEC_PER_MSEC))) {
        x->failed = true; return failed(error, size, "Metal GPU completion timed out; destroy the executor");
    }
    if (command.status != MTLCommandBufferStatusCompleted || command.error) {
        x->failed = true; return failed(error, size, "Metal GPU draw failed: %s", command.error.localizedDescription.UTF8String);
    }
    target->initialized = true;
    const uint32_t stop = *static_cast<volatile uint32_t *>(x->stop.contents);
    *static_cast<volatile uint32_t *>(x->stop.contents) = 0;
    if (stop >> GCN_RUNTIME_STOP_KIND_SHIFT == GCN_RUNTIME_STOP_BUFFER_FORMAT)
        return failed(error, size, "shader runtime stop: %u-component buffer %s, DATA_FORMAT %u, NUM_FORMAT %u%s is unsupported",
            stop & 7, stop & 0x80 ? "store" : "load", stop >> 8 & 15, stop >> 4 & 7, stop & 8 ? ", nonidentity swizzle" : "");
    if (stop >> GCN_RUNTIME_STOP_KIND_SHIFT == GCN_RUNTIME_STOP_VCC_UNKNOWN)
        return failed(error, size, "shader runtime stop: VCC lane mask is unknown after a scalar half write");
    if (stop) return failed(error, size, "shader runtime stop with unknown code %#x", stop);
    return 0;
}

/* Raw _2 packets program their own DMA bound; logical offset calls use retained INDEX_SIZE. AMD's
   SI guide shifts INDEX_OFFSET by index width and adds INDEX_BASE; the register manual
   describes MAX_SIZE as the number of good fetches from that effective base. Zero filling
   beyond the bound is deliberately unsupported. No address is fetched when count is zero. */
static int draw_indexed(rt_executor *x, MetalState &state, uint32_t header, uint32_t maximum, uint64_t address,
                         uint32_t index_offset, bool use_index_base, bool has_packet_fields, uint32_t count, uint32_t initiator,
                         char *error, size_t size)
{
    constexpr uint32_t DRAW_INDEX_2_HEADER = 0xc0042700u, DRAW_INDEX_OFFSET_2_HEADER = 0xc0033500u;
    if (has_packet_fields) {
        if (header & PM4_PREDICATE) return failed(error, size, "predicated indexed draws are not supported");
        if (header != (use_index_base ? DRAW_INDEX_OFFSET_2_HEADER : DRAW_INDEX_2_HEADER))
            return failed(error, size, "unsupported indexed packet header");
        if (initiator != DRAW_INITIATOR_DMA)
            return failed(error, size, "draw initiator %#x: only DMA source and major mode 0 are drawn", initiator);
        if (count > maximum) return failed(error, size, "indexed draw beyond MAX_SIZE requires DMA zero filling, which is not supported");
    } else {
        // The logical offset API carries no packet fields; INDEX_SIZE bounds its buffer.
        if (!state.index_base_set) return failed(error, size, "legacy indexed draw requires INDEX_BASE");
        if (state.index_size_set && static_cast<uint64_t>(index_offset) + count > state.index_size)
            return failed(error, size, "legacy indexed draw exceeds INDEX_BUFFER_SIZE");
    }
    /* Public evidence does not settle the direct packet's effect on the CP's saved INDEX_BASE.
       Require a fresh explicit base before a later nonempty offset draw instead of guessing. */
    if (!use_index_base) state.index_base_current = false;
    if (!state.index_type_set) return failed(error, size, "indexed draw requires INDEX_TYPE");
    if (state.index_type != VGT_INDEX_16 && state.index_type != VGT_INDEX_32)
        return failed(error, size, "index type %#x: only 16/32-bit indices without byte swapping are drawn", state.index_type);
    const uint64_t width = state.index_type == VGT_INDEX_16 ? 2 : 4;
    MetalIndices indices{state.index_type == VGT_INDEX_16 ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32, 0};
    if (count) {
        if (use_index_base) {
            if (!state.index_base_set) return failed(error, size, "indexed offset draw requires INDEX_BASE");
            if (!state.index_base_current)
                return failed(error, size, "indexed offset draw after direct draw requires a fresh INDEX_BASE");
            const uint64_t delta = static_cast<uint64_t>(index_offset) * width;
            if (delta > UINT64_MAX - state.index_base) return failed(error, size, "index base plus offset overflows");
            address = state.index_base + delta;
        }
        if (address % width) return failed(error, size, "index address is not aligned to its element width");
        const uint64_t bytes = static_cast<uint64_t>(count) * width;
        if (address < x->guest_base || address - x->guest_base > x->size || bytes > x->size - (address - x->guest_base))
            return failed(error, size, "fetched index bytes are outside the GPU-visible pool");
        indices.offset = static_cast<NSUInteger>(address - x->guest_base);
    }
    if (check_state(state, error, size)) return -1;
    return draw(x, state, count, error, size, &indices);
}

/* Reset only GPU state: completed targets, compiled shaders and device failure remain. */
extern "C" void rt_executor_reset(rt_executor *x)
{
    if (x) x->state = MetalState{};
}

extern "C" int rt_executor_run(rt_executor *x, uint64_t buffer, uint32_t bytes, char *error, size_t error_size)
{
    if (!x || x->failed) return failed(error, error_size, "a usable executor is required");
    size_t count = 0;
    const rt_render_call *calls = rt_render_submitted(buffer, bytes, &count);
    return rt_executor_run_calls(x, calls, count, error, error_size);
}

/* The renderer copies calls before queuing them and may split them at a GPU step.
   Consume that copy directly, retaining the state set by earlier fragments. */
extern "C" int rt_executor_run_calls(rt_executor *x, const rt_render_call *calls, size_t count,
                                      char *error, size_t error_size)
{
    if (!x || x->failed) return failed(error, error_size, "a usable executor is required");
    @autoreleasepool {
        @try {
            try {
                MetalState &state = x->state;
                for (size_t i = 0; i < count; ++i) {
                    const rt_render_call &call = calls[i];
                    switch (call.kind) {
                    case RT_CALL_COLOR_TARGET:
                        if (call.u.color_target.slot == 0) {
                            state.color = call.u.color_target.target; state.color_enabled = call.u.color_target.enabled;
                        } else state.other[call.u.color_target.slot] = call.u.color_target.enabled;
                        break;
                    case RT_CALL_VIEWPORT:
                        if (call.u.viewport.index) return failed(error, error_size, "only viewport 0 is drawn yet");
                        std::memcpy(state.scale, call.u.viewport.scale, sizeof state.scale);
                        std::memcpy(state.translate, call.u.viewport.translate, sizeof state.translate); state.viewport_set = true;
                        state.zmin = call.u.viewport.zmin; state.zmax = call.u.viewport.zmax;
                        break;
                    case RT_CALL_VERTEX_SHADER: state.vertex = call.u.vertex_shader; state.vertex_set = true; break;
                    case RT_CALL_PIXEL_SHADER: state.pixel = call.u.pixel_shader; state.pixel_set = true; break;
                    case RT_CALL_COMPUTE_SHADER: return failed(error, error_size, "compute shaders are outside the Metal draw profile");
                    case RT_CALL_DISPATCH: return failed(error, error_size, "compute dispatches are outside the Metal draw profile");
                    case RT_CALL_USER_DATA: {
                        int stage = call.u.user_data.stage == RT_STAGE_VERTEX ? 0 : call.u.user_data.stage == RT_STAGE_PIXEL ? 1 : -1;
                        if (stage < 0) return failed(error, error_size, "only vertex and pixel user data are drawn yet");
                        std::memcpy(&state.user[stage][call.u.user_data.slot], call.u.user_data.words, call.u.user_data.count * sizeof(uint32_t));
                        for (unsigned word = 0; word < call.u.user_data.count; ++word)
                            state.user_set[stage] |= 1u << (call.u.user_data.slot + word);
                        break;
                    }
                    case RT_CALL_REGISTER:
                        if (set_register(state, call.u.reg.address, call.u.reg.value))
                            return failed(error, error_size, "register %#x: the executor does not interpret it yet", call.u.reg.address);
                        break;
                    case RT_CALL_INSTANCES: state.instances = call.u.instances.count; break;
                    case RT_CALL_INDEX_TYPE: state.index_type = call.u.index_type.type; state.index_type_set = true; break;
                    case RT_CALL_INDEX_BASE:
                        state.index_base = call.u.index_base.address; state.index_base_set = true; state.index_base_current = true; break;
                    case RT_CALL_INDEX_SIZE:
                        state.index_size = call.u.index_size.count; state.index_size_set = true; break;
                    case RT_CALL_DRAW_AUTO: {
                        if (check_state(state, error, error_size)) return -1;
                        if (draw(x, state, call.u.draw_auto.index_count, error, error_size)) return -1;
                        break;
                    }
                    case RT_CALL_DRAW_INDEXED: {
                        const auto &indexed = call.u.draw_indexed;
                        if (draw_indexed(x, state, indexed.header, indexed.max_size, indexed.address, 0, false, true,
                            indexed.index_count, indexed.initiator, error, error_size)) return -1;
                        break;
                    }
                    case RT_CALL_DRAW_INDEX_OFFSET: {
                        const auto &indexed = call.u.draw_index;
                        if (indexed.has_packet_fields > 1 || (!indexed.has_packet_fields &&
                            (indexed.header || indexed.max_size || indexed.initiator)))
                            return failed(error, error_size, "inconsistent indexed packet-field tag");
                        if (draw_indexed(x, state, indexed.header, indexed.max_size, 0, indexed.first, true,
                            indexed.has_packet_fields != 0, indexed.count, indexed.initiator, error, error_size)) return -1;
                        break;
                    }
                    default: return failed(error, error_size, "render call kind %u is outside the Metal draw profile", static_cast<unsigned>(call.kind));
                    }
                }
                return 0;
            } catch (const std::exception &exception) { return failed(error, error_size, "Metal allocation: %s", exception.what()); }
        } @catch (NSException *exception) { x->failed = true; return failed(error, error_size, "Metal exception: %s", exception.reason.UTF8String); }
    }
}

extern "C" int rt_executor_read_target(rt_executor *x, uint64_t address, uint32_t *bgra, uint32_t width, uint32_t height,
                                        char *error, size_t error_size)
{
    if (!x || x->failed || !bgra || !width || !height) return failed(error, error_size, "usable executor, destination and nonzero dimensions required");
    for (const auto &target : x->targets) if (target.address == address) {
        if (!target.initialized || width > target.width || height > target.height)
            return failed(error, error_size, "read size is past the completed render target");
        @try {
            [target.texture getBytes:bgra bytesPerRow:static_cast<NSUInteger>(width) * 4
                fromRegion:MTLRegionMake2D(0, 0, width, height) mipmapLevel:0];
            if (target.format == MTLPixelFormatRGBA8Unorm || target.format == MTLPixelFormatRGBA8Unorm_sRGB)
                for (size_t i = 0; i < static_cast<size_t>(width) * height; ++i) {
                    uint32_t pixel = bgra[i]; bgra[i] = (pixel & 0xff00ff00u) | (pixel & 0xffu) << 16 | (pixel >> 16 & 0xffu);
                }
            return 0;
        } @catch (NSException *exception) { x->failed = true; return failed(error, error_size, "Metal readback: %s", exception.reason.UTF8String); }
    }
    return failed(error, error_size, "no draw has used that render target");
}

extern "C" void rt_executor_close(rt_executor *x) { delete x; }
