/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "runtime/graphics_metal.h"

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <dispatch/dispatch.h>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>

struct rt_metal_offscreen {
    id<MTLDevice> device = nil;
    id<MTLCommandQueue> queue = nil;
    id<MTLTexture> texture = nil;
    id<MTLRenderPipelineState> presentation_pipeline = nil;
    std::unique_ptr<uint8_t[]> pixels;
    uint32_t width = 0, height = 0;
    bool readable = false, failed = false;
    char device_name[256] = {};
};

static rt_metal_status result(rt_metal_status status, char *error, size_t size, const char *message)
{
    if (error && size) std::snprintf(error, size, "%s", message ? message : "Metal operation failed");
    return status;
}

static rt_metal_status submit_and_wait(rt_metal_offscreen *context, id<MTLCommandBuffer> command,
                                       uint32_t timeout_ms, char *error, size_t error_size)
{
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    if (!done) {
        context->failed = true;
        return result(RT_METAL_ALLOCATION_FAILED, error, error_size, "completion semaphore allocation failed");
    }
    [command addCompletedHandler:^(id<MTLCommandBuffer> completed) {
        (void)completed;
        dispatch_semaphore_signal(done);
    }];
    [command commit];
    const dispatch_time_t until = dispatch_time(DISPATCH_TIME_NOW, static_cast<int64_t>(timeout_ms) * NSEC_PER_MSEC);
    if (dispatch_semaphore_wait(done, until) != 0) {
        context->failed = true;
        return result(RT_METAL_TIMED_OUT, error, error_size, "GPU completion deadline exceeded; context is no longer readable");
    }
    if (command.status != MTLCommandBufferStatusCompleted || command.error) {
        context->failed = true;
        return result(RT_METAL_GPU_FAILED, error, error_size,
                      command.error ? command.error.localizedDescription.UTF8String : "GPU command did not complete successfully");
    }
    return RT_METAL_OK;
}

rt_metal_status rt_metal_offscreen_create(uint32_t width, uint32_t height, rt_metal_offscreen **out,
                                         char *error, size_t error_size)
{
    if (!out || *out || !width || !height || width > 4096 || height > 4096)
        return result(RT_METAL_INVALID_ARGUMENT, error, error_size, "require an empty handle and dimensions 1..4096");
    std::unique_ptr<rt_metal_offscreen> next(new (std::nothrow) rt_metal_offscreen);
    if (!next) return result(RT_METAL_ALLOCATION_FAILED, error, error_size, "context allocation failed");
    @try {
        @autoreleasepool {
            next->device = MTLCreateSystemDefaultDevice();
            if (!next->device || !next->device.hasUnifiedMemory)
                return result(RT_METAL_UNAVAILABLE, error, error_size, "a unified-memory Metal device is required by this offscreen unit");
            next->queue = [next->device newCommandQueue];
            if (!next->queue)
                return result(RT_METAL_ALLOCATION_FAILED, error, error_size, "Metal command queue allocation failed");
            MTLTextureDescriptor *descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                              width:width height:height mipmapped:NO];
            descriptor.storageMode = MTLStorageModeShared;
            descriptor.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
            descriptor.allowGPUOptimizedContents = NO;
            next->texture = [next->device newTextureWithDescriptor:descriptor];
            next->pixels.reset(new (std::nothrow) uint8_t[static_cast<size_t>(width) * height * 4]);
            if (!next->texture || !next->pixels)
                return result(RT_METAL_ALLOCATION_FAILED, error, error_size, "RGBA8 texture/readback allocation failed");
            next->width = width; next->height = height;
            if (![next->device.name getCString:next->device_name maxLength:sizeof next->device_name encoding:NSUTF8StringEncoding])
                std::snprintf(next->device_name, sizeof next->device_name, "%s", "Metal device");
            *out = next.release();
            return result(RT_METAL_OK, error, error_size, "");
        }
    } @catch (NSException *exception) {
        return result(RT_METAL_GPU_FAILED, error, error_size, exception.reason.UTF8String);
    }
}

rt_metal_status rt_metal_offscreen_clear(rt_metal_offscreen *context, const uint8_t rgba[4],
                                        uint32_t timeout_ms, char *error, size_t error_size)
{
    if (!context || !rgba || !timeout_ms || timeout_ms > 30000)
        return result(RT_METAL_INVALID_ARGUMENT, error, error_size, "invalid clear context/color/deadline");
    if (context->failed)
        return result(RT_METAL_GPU_FAILED, error, error_size, "destroy the failed context before reuse");
    @try {
        @autoreleasepool {
            MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
            pass.colorAttachments[0].texture = context->texture;
            pass.colorAttachments[0].loadAction = MTLLoadActionClear;
            pass.colorAttachments[0].storeAction = MTLStoreActionStore;
            pass.colorAttachments[0].clearColor = MTLClearColorMake(rgba[0] / 255.0, rgba[1] / 255.0,
                                                                 rgba[2] / 255.0, rgba[3] / 255.0);
            id<MTLCommandBuffer> command = [context->queue commandBuffer];
            if (!command || !command.retainedReferences) {
                context->failed = true;
                return result(RT_METAL_ALLOCATION_FAILED, error, error_size, "retaining command buffer allocation failed");
            }
            id<MTLRenderCommandEncoder> encoder = [command renderCommandEncoderWithDescriptor:pass];
            if (!encoder) {
                context->failed = true;
                return result(RT_METAL_GPU_FAILED, error, error_size, "clear render encoder creation failed");
            }
            [encoder endEncoding];
            const auto status = submit_and_wait(context, command, timeout_ms, error, error_size);
            if (status != RT_METAL_OK) return status;
            context->readable = true;
            return result(RT_METAL_OK, error, error_size, "");
        }
    } @catch (NSException *exception) {
        context->failed = true;
        return result(RT_METAL_GPU_FAILED, error, error_size, exception.reason.UTF8String);
    }
}

rt_metal_status rt_metal_offscreen_upload(rt_metal_offscreen *context, const void *source,
                                         size_t stride, size_t capacity, char *error, size_t error_size)
{
    if (!context || !source)
        return result(RT_METAL_INVALID_ARGUMENT, error, error_size, "upload requires a context and source");
    const size_t row = static_cast<size_t>(context->width) * 4, lines = context->height - 1;
    if (stride < row || (lines && stride > (SIZE_MAX - row) / lines) || capacity < stride * lines + row)
        return result(RT_METAL_INVALID_ARGUMENT, error, error_size, "upload stride/capacity overflow or insufficient storage");
    if (context->failed)
        return result(RT_METAL_GPU_FAILED, error, error_size, "destroy the failed context before upload");
    // Pack arbitrary caller padding into tightly spaced rows accepted by the RGBA8 texture.
    // The preceding serialized clear already waited for its GPU commands to complete.
    for (size_t y = 0; y < context->height; y++)
        std::memcpy(context->pixels.get() + y * row, static_cast<const uint8_t *>(source) + y * stride, row);
    @try {
        @autoreleasepool {
            [context->texture replaceRegion:MTLRegionMake2D(0, 0, context->width, context->height)
                               mipmapLevel:0 withBytes:context->pixels.get() bytesPerRow:(context->height == 1 ? 0 : row)];
        }
    } @catch (NSException *exception) {
        context->failed = true;
        return result(RT_METAL_GPU_FAILED, error, error_size, exception.reason.UTF8String);
    }
    context->readable = true;
    return result(RT_METAL_OK, error, error_size, "");
}

rt_metal_status rt_metal_offscreen_read(rt_metal_offscreen *context, void *destination,
                                       size_t stride, size_t capacity, char *error, size_t error_size)
{
    if (!context || !destination)
        return result(RT_METAL_INVALID_ARGUMENT, error, error_size, "read requires a context and destination");
    const size_t row = static_cast<size_t>(context->width) * 4, lines = context->height - 1;
    if (stride < row || (lines && stride > (SIZE_MAX - row) / lines) || capacity < stride * lines + row)
        return result(RT_METAL_INVALID_ARGUMENT, error, error_size, "readback stride/capacity overflow or insufficient storage");
    if (context->failed)
        return result(RT_METAL_GPU_FAILED, error, error_size, "failed context has no readable result");
    if (!context->readable)
        return result(RT_METAL_NOT_READY, error, error_size, "clear or upload must complete before readback");
    // Packed rows can go straight to caller memory. Stage padded rows so only
    // their pixel bytes are copied and the caller's padding stays untouched.
    void *readback = stride == row ? destination : context->pixels.get();
    @try {
        @autoreleasepool {
            [context->texture getBytes:readback bytesPerRow:row
                            fromRegion:MTLRegionMake2D(0, 0, context->width, context->height) mipmapLevel:0];
        }
    } @catch (NSException *exception) {
        context->failed = true;
        return result(RT_METAL_GPU_FAILED, error, error_size, exception.reason.UTF8String);
    }
    if (stride != row) {
        for (size_t y = 0; y < context->height; y++)
            std::memcpy(static_cast<uint8_t *>(destination) + y * stride, context->pixels.get() + y * row, row);
    }
    return result(RT_METAL_OK, error, error_size, "");
}

rt_metal_status rt_metal_offscreen_configure_layer(rt_metal_offscreen *context, void *native_layer,
                                                  char *error, size_t error_size)
{
    if (!context || !native_layer || !NSThread.isMainThread)
        return result(RT_METAL_INVALID_ARGUMENT, error, error_size, "layer configuration requires a context, CAMetalLayer and main thread");
    if (context->failed)
        return result(RT_METAL_GPU_FAILED, error, error_size, "destroy the failed context before configuration");
    @try {
        @autoreleasepool {
            id object = (__bridge id)native_layer;
            if (![object isKindOfClass:CAMetalLayer.class])
                return result(RT_METAL_INVALID_ARGUMENT, error, error_size, "native layer must be a CAMetalLayer");
            CAMetalLayer *layer = object;
            layer.device = context->device;
            layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
            layer.drawableSize = CGSizeMake(context->width, context->height);
            layer.allowsNextDrawableTimeout = YES;
            layer.presentsWithTransaction = NO;
            return result(RT_METAL_OK, error, error_size, "");
        }
    } @catch (NSException *exception) {
        context->failed = true;
        return result(RT_METAL_GPU_FAILED, error, error_size, exception.reason.UTF8String);
    }
}

rt_metal_status rt_metal_offscreen_present(rt_metal_offscreen *context, void *native_layer,
                                          const rt_metal_bgra_readback *capture, uint32_t timeout_ms,
                                          char *error, size_t error_size)
{
    if (!context || !native_layer || !NSThread.isMainThread || !timeout_ms || timeout_ms > 30000)
        return result(RT_METAL_INVALID_ARGUMENT, error, error_size, "invalid presentation context/layer/thread/deadline");
    const rt_metal_bgra_readback output = capture ? *capture : rt_metal_bgra_readback{};
    const size_t row = static_cast<size_t>(context->width) * 4, lines = context->height - 1;
    if (capture && (!output.data || output.stride < row ||
                    (lines && output.stride > (SIZE_MAX - row) / lines) ||
                    output.capacity < output.stride * lines + row))
        return result(RT_METAL_INVALID_ARGUMENT, error, error_size, "invalid BGRA capture buffer/stride/capacity");
    if (context->failed)
        return result(RT_METAL_GPU_FAILED, error, error_size, "destroy the failed context before presentation");
    if (!context->readable)
        return result(RT_METAL_NOT_READY, error, error_size, "complete an upload or clear before presentation");
    @try {
        @autoreleasepool {
            id object = (__bridge id)native_layer;
            if (![object isKindOfClass:CAMetalLayer.class])
                return result(RT_METAL_INVALID_ARGUMENT, error, error_size, "native layer must be a CAMetalLayer");
            CAMetalLayer *layer = object;
            if (layer.device != context->device || layer.pixelFormat != MTLPixelFormatBGRA8Unorm ||
                layer.drawableSize.width != context->width || layer.drawableSize.height != context->height ||
                !layer.allowsNextDrawableTimeout || layer.presentsWithTransaction || (capture && layer.framebufferOnly))
                return result(RT_METAL_INVALID_ARGUMENT, error, error_size, "layer profile changed or capture requires framebufferOnly=NO");
            if (!context->presentation_pipeline) {
                static const char shader[] = R"MSL(
                    #include <metal_stdlib>
                    using namespace metal;
                    struct FramePosition { float4 position [[position]]; };
                    vertex FramePosition frame_vertex(uint index [[vertex_id]]) {
                        const float2 corners[3] = {float2(-1, 1), float2(3, 1), float2(-1, -3)};
                        return FramePosition{float4(corners[index], 0, 1)};
                    }
                    fragment float4 frame_pixel(float4 position [[position]],
                                                 texture2d<float, access::read> source [[texture(0)]]) {
                        return source.read(uint2(position.xy));
                    }
                )MSL";
                NSError *failure = nil;
                MTLCompileOptions *options = [MTLCompileOptions new];
                options.languageVersion = MTLLanguageVersion2_0;
                id<MTLLibrary> library = [context->device newLibraryWithSource:[NSString stringWithUTF8String:shader]
                                                                      options:options error:&failure];
                if (!library) {
                    context->failed = true;
                    return result(RT_METAL_GPU_FAILED, error, error_size,
                                  failure ? failure.localizedDescription.UTF8String : "presentation shader compilation failed");
                }
                MTLRenderPipelineDescriptor *descriptor = [MTLRenderPipelineDescriptor new];
                descriptor.vertexFunction = [library newFunctionWithName:@"frame_vertex"];
                descriptor.fragmentFunction = [library newFunctionWithName:@"frame_pixel"];
                descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
                context->presentation_pipeline = [context->device newRenderPipelineStateWithDescriptor:descriptor error:&failure];
                if (!context->presentation_pipeline) {
                    context->failed = true;
                    return result(RT_METAL_GPU_FAILED, error, error_size,
                                  failure ? failure.localizedDescription.UTF8String : "presentation pipeline allocation failed");
                }
            }
            // Allocate capture storage before acquiring a scarce drawable. The dimensions are
            // bounded at creation; 256-byte rows satisfy the uncompressed texture blit alignment.
            const size_t capture_row = (row + 255) & ~size_t(255);
            id<MTLBuffer> readback = capture ? [context->device newBufferWithLength:capture_row * context->height
                                                                          options:MTLResourceStorageModeShared] : nil;
            if (capture && !readback) {
                context->failed = true;
                return result(RT_METAL_ALLOCATION_FAILED, error, error_size, "drawable readback buffer allocation failed");
            }
            id<CAMetalDrawable> drawable = [layer nextDrawable];
            if (!drawable)
                return result(RT_METAL_NO_DRAWABLE, error, error_size, "layer has no available drawable; context remains usable");
            id<MTLTexture> target = drawable.texture;
            if (target.device != context->device || target.pixelFormat != MTLPixelFormatBGRA8Unorm ||
                target.width != context->width || target.height != context->height || target.sampleCount != 1)
                return result(RT_METAL_INVALID_ARGUMENT, error, error_size, "acquired drawable does not match the configured frame");
            id<MTLCommandBuffer> command = [context->queue commandBuffer];
            if (!command || !command.retainedReferences) {
                context->failed = true;
                return result(RT_METAL_ALLOCATION_FAILED, error, error_size, "retaining presentation command buffer allocation failed");
            }
            MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
            pass.colorAttachments[0].texture = target;
            pass.colorAttachments[0].loadAction = MTLLoadActionDontCare;
            pass.colorAttachments[0].storeAction = MTLStoreActionStore;
            id<MTLRenderCommandEncoder> encoder = [command renderCommandEncoderWithDescriptor:pass];
            if (!encoder) {
                context->failed = true;
                return result(RT_METAL_GPU_FAILED, error, error_size, "presentation render encoder creation failed");
            }
            [encoder setRenderPipelineState:context->presentation_pipeline];
            [encoder setViewport:MTLViewport{0, 0, static_cast<double>(context->width), static_cast<double>(context->height), 0, 1}];
            [encoder setFragmentTexture:context->texture atIndex:0];
            [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
            [encoder endEncoding];
            if (capture) {
                id<MTLBlitCommandEncoder> blit = [command blitCommandEncoder];
                if (!blit) {
                    context->failed = true;
                    return result(RT_METAL_GPU_FAILED, error, error_size, "drawable capture blit encoder creation failed");
                }
                // Drawable contents are undefined after presentation. Copy before scheduling it.
                [blit copyFromTexture:target sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
                          sourceSize:MTLSizeMake(context->width, context->height, 1) toBuffer:readback
                   destinationOffset:0 destinationBytesPerRow:capture_row destinationBytesPerImage:capture_row * context->height];
                [blit endEncoding];
            }
            [command presentDrawable:drawable];
            [command addCompletedHandler:^(id<MTLCommandBuffer> completed) {
                // Strong block captures outlive this function after a GPU wait timeout.
                __attribute__((objc_precise_lifetime)) id keep_drawable = drawable;
                __attribute__((objc_precise_lifetime)) CAMetalLayer *keep_layer = layer;
                (void)completed; (void)keep_drawable; (void)keep_layer;
            }];
            const auto status = submit_and_wait(context, command, timeout_ms, error, error_size);
            if (status != RT_METAL_OK) return status;
            if (capture) {
                const auto *bytes = static_cast<const uint8_t *>(readback.contents);
                for (size_t y = 0; y < context->height; y++)
                    std::memcpy(static_cast<uint8_t *>(output.data) + y * output.stride, bytes + y * capture_row, row);
            }
            return result(RT_METAL_OK, error, error_size, "");
        }
    } @catch (NSException *exception) {
        context->failed = true;
        return result(RT_METAL_GPU_FAILED, error, error_size, exception.reason.UTF8String);
    }
}

const char *rt_metal_offscreen_device_name(const rt_metal_offscreen *context)
{
    return context ? context->device_name : nullptr;
}

void rt_metal_offscreen_destroy(rt_metal_offscreen **context)
{
    if (context) { delete *context; *context = nullptr; }
}
