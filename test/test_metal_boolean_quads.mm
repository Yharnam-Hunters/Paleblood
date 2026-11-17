/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Owned native MSL probes for Boolean quad permutations and quad-local WQM OR.
   No shader stream, input mask or expected result comes from a game artifact.
   This primitive fixture does not establish wave-wide control or draw rollback. */
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include "metal_fragment_wqm_source.hpp"
#include <array>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace {
constexpr unsigned side = 8, components = 4, truth_masks = 16;
constexpr uint32_t gathered_marker = 1, bypass_marker = 2;
constexpr uint32_t selected_bits = 0x59c36ae1u, selected_or = 0x9f284bd6u;
constexpr uint32_t bypass_bits = 0xb16b00b5u, bypass_or = 0x80604020u;
constexpr uint32_t or_only_bits = 0x726f6e6cu;
constexpr std::array<uint32_t, 4> fault_initial = {0x62d79ac4u, 0, 0x38af015bu, 0xc051e296u};
using Pixel = std::array<uint32_t, components>;
using Pixels = std::array<uint32_t, side * side * components>;
constexpr Pixel clear_pixel = {UINT32_MAX, UINT32_MAX, 0, UINT32_MAX};

enum class Caller : uint32_t { Whole, Selected, Divergent };
enum class Operation : uint32_t { Packed, Xor1, Xor2, Xor3, OrOnly };
struct Input { uint32_t seed, varying; Caller caller; Operation operation; };
static_assert(sizeof(Input) == 16);

struct Results {
    unsigned draws = 0, checks = 0, failures = 0, observed_pixels = 0, raster_covered = 0;
    unsigned expected_gathered = 0, expected_bypass = 0, expected_discarded = 0;
    unsigned observed_gathered = 0, observed_bypass = 0;
    unsigned expected_fault_draws = 0, observed_fault_draws = 0;
    void check(bool condition, const std::string &message)
    {
        ++checks;
        if (condition) return;
        ++failures;
        if (failures <= 30) std::fprintf(stderr, "%s\n", message.c_str());
    }
};

/* Each full varying draw assigns all16 truth masks to the16 physical quads.
   Tiny geometry retains one covered lane and three raster helper invocations. */
std::string source()
{
    return std::string(R"(
#include <metal_stdlib>
using namespace metal;
vertex float4 boolean_quad_vertex(uint vertex_id [[vertex_id]], constant uint &tiny [[buffer(0)]])
{
    const float2 full[3] = {float2(-1, -1), float2(3, -1), float2(-1, 3)};
    const float2 edge[3] = {float2(-1, 1), float2(-0.5625, 1), float2(-1, 0.5625)};
    return float4(tiny ? edge[vertex_id] : full[vertex_id], 0, 1);
}
)") + pb::metal_fragment_wqm_source("owned_boolean_quad") + R"(
struct Input { uint seed; uint varying; uint caller; uint operation; };
fragment uint4 boolean_quad_fragment(float4 position [[position]], uint lane [[thread_index_in_quadgroup]],
                                      constant Input &input [[buffer(0)]], device atomic_uint *fault [[buffer(1)]])
{
    uint x = uint(position.x), y = uint(position.y);
    uint physical_lane = (x & 1u) | ((y & 1u) << 1u);
    uint quad = (x >> 1u) | ((y >> 1u) << 2u);
    uint mask = input.varying != 0u ? (input.seed ^ quad) : input.seed;
    bool value = ((mask >> physical_lane) & 1u) != 0u;
    uint tag = lane | (mask << 4u) | (quad << 8u) | (input.operation << 12u) |
               (input.caller << 16u) | (input.seed << 20u);

    // This deliberately divergent primitive caller is outside an admitted SPIR-V profile.
    if (input.caller == 2u && (lane & 1u) != 0u)
        return uint4(0xb16b00b5u, 0x80604020u, 2u, tag);

    // Isolate each direction. With only even lanes, XOR2's requested peer is present,
    // but the all-four-presence contract still requires a fault and discard.
    if (input.operation >= 1u && input.operation <= 3u) {
        bool swapped;
        if (input.operation == 1u) swapped = owned_boolean_quad::swap(value, 1u, lane, fault);
        else if (input.operation == 2u) swapped = owned_boolean_quad::swap(value, 2u, lane, fault);
        else swapped = owned_boolean_quad::swap(value, 3u, lane, fault);
        return uint4(uint(swapped), uint(swapped), 1u, tag);
    }

    // Keep all three acquisitions unconditional and materialized before either OR or select.
    bool across_x = owned_boolean_quad::swap(value, 1u, lane, fault);
    bool across_y = owned_boolean_quad::swap(value, 2u, lane, fault);
    bool across_diagonal = owned_boolean_quad::swap(value, 3u, lane, fault);
    bool expanded = value || across_x || across_y || across_diagonal;
    uint bits = 0x726f6e6cu;
    if (input.operation == 0u)
        bits = uint(value) | (uint(across_x) << 1u) | (uint(across_y) << 2u) | (uint(across_diagonal) << 3u);
    // In OR-only draws there is no packed-bit consumer keeping lazy OR operands alive.
    bool ignored = input.caller == 1u && (physical_lane & 2u) != 0u;
    return uint4(ignored ? 0x59c36ae1u : bits, ignored ? 0x9f284bd6u : uint(expanded), 1u, tag);
}
)";
}

std::runtime_error metal_error(const char *operation, NSError *error)
{
    return std::runtime_error(std::string(operation) + ": " +
        (error ? error.localizedDescription.UTF8String : "no Metal object returned"));
}

struct Gpu {
    id<MTLDevice> device;
    id<MTLCommandQueue> queue;
    id<MTLBuffer> fault;
    id<MTLTexture> target;
    id<MTLRenderPipelineState> pipeline;

    Gpu()
    {
        device = MTLCreateSystemDefaultDevice();
        if (!device || !device.hasUnifiedMemory)
            throw std::runtime_error("a unified-memory Metal device is required");
        MTLCompileOptions *options = [MTLCompileOptions new];
        options.languageVersion = MTLLanguageVersion2_3;
        if (@available(macOS 15.0, *)) options.mathMode = MTLMathModeSafe;
        else throw std::runtime_error("safe MSL math requires macOS 15 or later");
        const std::string text = source();
        NSError *error = nil;
        id<MTLLibrary> library = [device newLibraryWithSource:[NSString stringWithUTF8String:text.c_str()]
            options:options error:&error];
        if (!library) throw metal_error("MSL compilation", error);
        MTLRenderPipelineDescriptor *descriptor = [MTLRenderPipelineDescriptor new];
        descriptor.vertexFunction = [library newFunctionWithName:@"boolean_quad_vertex"];
        descriptor.fragmentFunction = [library newFunctionWithName:@"boolean_quad_fragment"];
        descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA32Uint;
        if (!descriptor.vertexFunction || !descriptor.fragmentFunction)
            throw std::runtime_error("owned Metal entry point missing");
        pipeline = [device newRenderPipelineStateWithDescriptor:descriptor error:&error];
        if (!pipeline) throw metal_error("render pipeline", error);
        queue = [device newCommandQueue];
        fault = [device newBufferWithLength:sizeof(fault_initial) options:MTLResourceStorageModeShared];
        MTLTextureDescriptor *texture = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA32Uint
            width:side height:side mipmapped:NO];
        texture.storageMode = MTLStorageModeShared;
        texture.usage = MTLTextureUsageRenderTarget;
        target = [device newTextureWithDescriptor:texture];
        if (!queue || !fault || !target) throw std::runtime_error("Metal fixture allocation failed");
        std::printf("device=%s MSL=2.3 math=safe target=RGBA32Uint\n", device.name.UTF8String);
    }

    Pixels draw(const Input &input, bool tiny)
    {
        auto *fault_words = static_cast<uint32_t *>(fault.contents);
        for (unsigned word = 0; word < fault_initial.size(); ++word) fault_words[word] = fault_initial[word];
        MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = target;
        pass.colorAttachments[0].loadAction = MTLLoadActionClear;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        pass.colorAttachments[0].clearColor = MTLClearColorMake(double(UINT32_MAX), double(UINT32_MAX), 0, double(UINT32_MAX));
        id<MTLCommandBuffer> command = [queue commandBuffer];
        id<MTLRenderCommandEncoder> encoder = [command renderCommandEncoderWithDescriptor:pass];
        if (!command || !encoder) throw std::runtime_error("Metal render encoder missing");
        [encoder setRenderPipelineState:pipeline];
        [encoder setViewport:MTLViewport{0, 0, side, side, 0, 1}];
        const uint32_t edge = tiny;
        [encoder setVertexBytes:&edge length:sizeof(edge) atIndex:0];
        [encoder setFragmentBytes:&input length:sizeof(input) atIndex:0];
        [encoder setFragmentBuffer:fault offset:sizeof(uint32_t) atIndex:1];
        [encoder useResource:fault usage:MTLResourceUsageRead | MTLResourceUsageWrite stages:MTLRenderStageFragment];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        [encoder endEncoding];
        [command commit];
        [command waitUntilCompleted];
        if (command.status != MTLCommandBufferStatusCompleted) throw metal_error("GPU command", command.error);
        Pixels pixels;
        [target getBytes:pixels.data() bytesPerRow:side * components * sizeof(uint32_t)
            fromRegion:MTLRegionMake2D(0, 0, side, side) mipmapLevel:0];
        return pixels;
    }
};

uint32_t truth_bits(uint32_t mask, unsigned lane)
{
    uint32_t packed = 0;
    for (unsigned direction = 0; direction < 4; ++direction)
        packed |= ((mask >> (lane ^ direction)) & 1u) << direction;
    return packed;
}

void inspect(Results &results, Gpu &gpu, const Pixels &pixels, const Input &input, bool tiny)
{
    const bool divergent = input.caller == Caller::Divergent;
    const std::string label = "seed=" + std::to_string(input.seed) + " varying=" + std::to_string(input.varying) +
        " caller=" + std::to_string(uint32_t(input.caller)) + " operation=" + std::to_string(uint32_t(input.operation)) +
        " tiny=" + std::to_string(tiny);
    const auto *fault = static_cast<const uint32_t *>(gpu.fault.contents);
    results.expected_fault_draws += divergent;
    results.observed_fault_draws += fault[1] != 0;
    for (unsigned word = 0; word < fault_initial.size(); ++word)
        results.check(fault[word] == (word == 1 ? uint32_t(divergent) : fault_initial[word]),
                       label + ": fault/canary word " + std::to_string(word));
    for (unsigned y = 0; y < side; ++y) for (unsigned x = 0; x < side; ++x) {
        const bool covered = !tiny || (x == 0 && y == 0);
        const unsigned lane = (x & 1u) | ((y & 1u) << 1u), quad = (x >> 1u) | ((y >> 1u) << 2u);
        const uint32_t mask = input.varying ? input.seed ^ quad : input.seed;
        const uint32_t tag = lane | (mask << 4u) | (quad << 8u) | (uint32_t(input.operation) << 12u) |
            (uint32_t(input.caller) << 16u) | (input.seed << 20u);
        Pixel expected = clear_pixel;
        if (covered) {
            ++results.raster_covered;
            if (divergent && (lane & 1u)) {
                expected = {bypass_bits, bypass_or, bypass_marker, tag};
                ++results.expected_bypass;
            } else if (divergent) {
                // A discard is observable only as absent color output. Earlier writes
                // and other invocations' bypass output are deliberately retained.
                ++results.expected_discarded;
            } else {
                uint32_t bits = truth_bits(mask, lane), expanded = mask != 0;
                if (input.operation == Operation::OrOnly) bits = or_only_bits;
                else if (input.operation != Operation::Packed)
                    bits = expanded = (mask >> (lane ^ uint32_t(input.operation))) & 1u;
                if (input.caller == Caller::Selected && (lane & 2u)) {
                    bits = selected_bits;
                    expanded = selected_or;
                }
                expected = {bits, expanded, gathered_marker, tag};
                ++results.expected_gathered;
            }
        }
        const uint32_t *actual = pixels.data() + components * (y * side + x);
        results.observed_gathered += actual[2] == gathered_marker;
        results.observed_bypass += actual[2] == bypass_marker;
        bool matches = true;
        for (unsigned channel = 0; channel < components; ++channel) matches &= actual[channel] == expected[channel];
        ++results.observed_pixels;
        if (!matches && results.failures < 30)
            std::fprintf(stderr, "%s pixel(%u,%u) %08x,%08x,%08x,%08x expected %08x,%08x,%08x,%08x\n",
                label.c_str(), x, y, actual[0], actual[1], actual[2], actual[3], expected[0], expected[1], expected[2], expected[3]);
        results.check(matches, label + ": exact pixel words differ");
    }
}

void run(Results &results)
{
    Gpu gpu;
    for (bool tiny : {false, true}) for (uint32_t varying : {0u, 1u})
        for (uint32_t seed = 0; seed < truth_masks; ++seed) {
            // Full varying draws already contain all16 masks. Two seeds also show
            // that changing uniforms changes the physical quad-to-mask assignment.
            if (!tiny && seed != 0 && seed != truth_masks - 1) continue;
            for (Caller caller : {Caller::Whole, Caller::Selected, Caller::Divergent}) {
                const Input input{seed, varying, caller, Operation::Packed};
                const auto pixels = gpu.draw(input, tiny);
                ++results.draws;
                inspect(results, gpu, pixels, input, tiny);
            }
            for (Operation operation : {Operation::Xor1, Operation::Xor2, Operation::Xor3, Operation::OrOnly}) {
                const Input input{seed, varying, Caller::Divergent, operation};
                const auto pixels = gpu.draw(input, tiny);
                ++results.draws;
                inspect(results, gpu, pixels, input, tiny);
            }
        }
    results.check(results.draws == 252, "matrix draw count");
    results.check(results.observed_pixels == 16128, "matrix observed pixel count");
    results.check(results.raster_covered == 2016, "matrix raster-covered pixel count");
    results.check(results.expected_gathered == 576 && results.observed_gathered == 576, "matrix gathered pixel count");
    results.check(results.expected_bypass == 640 && results.observed_bypass == 640, "matrix bypass pixel count");
    results.check(results.expected_discarded == 800, "matrix discarded pixel count");
    results.check(results.expected_fault_draws == 180 && results.observed_fault_draws == 180, "matrix fault draw count");
}
}

int main()
{
    Results results;
    @autoreleasepool {
        try { run(results); }
        catch (const std::exception &error) { results.check(false, std::string("fixture exception: ") + error.what()); }
    }
    std::printf("SUMMARY draws=%u observed_pixels=%u raster_covered=%u expected_gathered=%u observed_gathered=%u "
        "expected_bypass=%u observed_bypass=%u expected_discarded=%u expected_fault_draws=%u observed_fault_draws=%u "
        "checks=%u failures=%u\n", results.draws, results.observed_pixels, results.raster_covered,
        results.expected_gathered, results.observed_gathered, results.expected_bypass, results.observed_bypass,
        results.expected_discarded, results.expected_fault_draws, results.observed_fault_draws, results.checks, results.failures);
    return results.failures ? 1 : 0;
}
