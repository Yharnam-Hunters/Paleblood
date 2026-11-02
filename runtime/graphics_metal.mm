/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "runtime/graphics_metal.h"

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <dispatch/dispatch.h>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>

struct rt_metal_offscreen {
    id<MTLDevice> device = nil;
    id<MTLCommandQueue> queue = nil;
    id<MTLTexture> texture = nil;
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
            descriptor.usage = MTLTextureUsageRenderTarget;
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
            context->readable = true;
            return result(RT_METAL_OK, error, error_size, "");
        }
    } @catch (NSException *exception) {
        context->failed = true;
        return result(RT_METAL_GPU_FAILED, error, error_size, exception.reason.UTF8String);
    }
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
        return result(RT_METAL_NOT_READY, error, error_size, "clear must complete before readback");
    @try {
        @autoreleasepool {
            [context->texture getBytes:context->pixels.get() bytesPerRow:row
                            fromRegion:MTLRegionMake2D(0, 0, context->width, context->height) mipmapLevel:0];
        }
    } @catch (NSException *exception) {
        context->failed = true;
        return result(RT_METAL_GPU_FAILED, error, error_size, exception.reason.UTF8String);
    }
    for (size_t y = 0; y < context->height; y++)
        std::memcpy(static_cast<uint8_t *>(destination) + y * stride, context->pixels.get() + y * row, row);
    return result(RT_METAL_OK, error, error_size, "");
}

const char *rt_metal_offscreen_device_name(const rt_metal_offscreen *context)
{
    return context ? context->device_name : nullptr;
}

void rt_metal_offscreen_destroy(rt_metal_offscreen **context)
{
    if (context) { delete *context; *context = nullptr; }
}
