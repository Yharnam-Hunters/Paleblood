/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "runtime/graphics_metal.h"
#import <Foundation/Foundation.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <vector>

#if defined(__arm64__) || defined(__aarch64__)
static NSString *const compiled_architecture = @"arm64";
#elif defined(__x86_64__)
static NSString *const compiled_architecture = @"x86_64";
#else
static NSString *const compiled_architecture = @"unknown";
#endif

int main(void)
{
    @autoreleasepool {
        NSMutableArray<NSString *> *failures = [NSMutableArray array];
        unsigned checks = 0, submissions = 0, pixels_checked = 0;
        const auto check = [&](bool good, const char *message) {
            checks++;
            if (!good) [failures addObject:[NSString stringWithUTF8String:message]];
        };
        char error[256] = {};
        rt_metal_offscreen *context = nullptr;
        check(rt_metal_offscreen_create(0, 5, &context, error, sizeof error) == RT_METAL_INVALID_ARGUMENT && !context, "zero width rejected without publishing a handle");
        check(rt_metal_offscreen_create(7, 4097, &context, error, sizeof error) == RT_METAL_INVALID_ARGUMENT && !context, "allocation bound rejected");
        check(rt_metal_offscreen_create(7, 5, nullptr, error, sizeof error) == RT_METAL_INVALID_ARGUMENT, "null handle output rejected");
        const rt_metal_status created = rt_metal_offscreen_create(7, 5, &context, error, sizeof error);
        check(created == RT_METAL_OK && context, "real Metal device/queue/RGBA8 texture created");
        NSString *device = context ? [NSString stringWithUTF8String:rt_metal_offscreen_device_name(context)] : @"unavailable";
        if (!device) device = @"unreadable device name";
        NSString *creation_error = [NSString stringWithUTF8String:error];
        if (context) {
            rt_metal_offscreen *saved = context;
            check(rt_metal_offscreen_create(7, 5, &context, error, sizeof error) == RT_METAL_INVALID_ARGUMENT && context == saved, "occupied output handle not replaced");
            constexpr size_t width = 7, height = 5, row = width * 4, stride = row + 12, guard = 7;
            constexpr size_t needed = (height - 1) * stride + row;
            std::vector<uint8_t> output(guard + stride * height + guard, 0xa5);
            const auto poison = output;
            check(rt_metal_offscreen_read(context, output.data() + guard, stride, needed, error, sizeof error) == RT_METAL_NOT_READY && output == poison, "no uninitialized texture read or destination write");
            check(rt_metal_offscreen_clear(context, nullptr, 5000, error, sizeof error) == RT_METAL_INVALID_ARGUMENT, "null clear color rejected");
            const uint8_t red[4] = {255, 0, 0, 255};
            check(rt_metal_offscreen_clear(context, red, 0, error, sizeof error) == RT_METAL_INVALID_ARGUMENT, "zero deadline rejected");
            check(rt_metal_offscreen_clear(context, red, 30001, error, sizeof error) == RT_METAL_INVALID_ARGUMENT, "unbounded deadline rejected");
            const std::array<std::array<uint8_t, 4>, 4> colors = {{{255,0,0,255}, {0,255,0,0}, {0,0,255,255}, {17,93,201,127}}};
            for (const auto &color : colors) {
                const rt_metal_status status = rt_metal_offscreen_clear(context, color.data(), 5000, error, sizeof error);
                check(status == RT_METAL_OK, "GPU render-pass clear/store completed successfully");
                if (status != RT_METAL_OK) {
                    [failures addObject:[NSString stringWithFormat:@"clear status %d: %s", status, error]];
                    break;
                }
                submissions++;
                output.assign(output.size(), 0xa5);
                check(rt_metal_offscreen_read(context, output.data() + guard, row - 1, needed, error, sizeof error) == RT_METAL_INVALID_ARGUMENT && output == poison, "short stride leaves all bytes untouched");
                check(rt_metal_offscreen_read(context, output.data() + guard, stride, needed - 1, error, sizeof error) == RT_METAL_INVALID_ARGUMENT && output == poison, "short capacity leaves all bytes untouched");
                check(rt_metal_offscreen_read(context, output.data() + guard, SIZE_MAX, SIZE_MAX, error, sizeof error) == RT_METAL_INVALID_ARGUMENT && output == poison, "stride overflow leaves all bytes untouched");
                check(rt_metal_offscreen_read(context, nullptr, stride, needed, error, sizeof error) == RT_METAL_INVALID_ARGUMENT && output == poison, "null destination rejected");
                const rt_metal_status read = rt_metal_offscreen_read(context, output.data() + guard, stride, needed, error, sizeof error);
                check(read == RT_METAL_OK, "completed GPU texture read back to CPU");
                if (read != RT_METAL_OK) {
                    [failures addObject:[NSString stringWithFormat:@"read status %d: %s", read, error]];
                    break;
                }
                bool pixels = true, padding = true;
                for (size_t y = 0; y < height; y++) {
                    for (size_t x = 0; x < width; x++) {
                        pixels &= std::memcmp(output.data() + guard + y * stride + x * 4, color.data(), 4) == 0;
                        pixels_checked++;
                    }
                    for (size_t x = row; x < stride; x++) padding &= output[guard + y * stride + x] == 0xa5;
                }
                for (size_t x = 0; x < guard; x++) padding &= output[x] == 0xa5 && output[output.size() - 1 - x] == 0xa5;
                check(pixels, "every RGBA byte equals the requested non-sRGB clear value");
                check(padding, "row padding, final padding and surrounding canaries unchanged");
            }
            rt_metal_offscreen_destroy(&context);
            check(!context, "destroy clears owned handle");
            rt_metal_offscreen_destroy(&context);
            rt_metal_offscreen_destroy(nullptr);
            check(rt_metal_offscreen_clear(context, red, 5000, nullptr, 0) == RT_METAL_INVALID_ARGUMENT, "destroyed context cannot be used");
            check(rt_metal_offscreen_device_name(nullptr) == nullptr, "null metadata lookup is safe");
            check(rt_metal_offscreen_create(1, 1, &context, error, sizeof error) == RT_METAL_OK, "second context lifecycle allocates independently");
            rt_metal_offscreen_destroy(&context);
            check(!context, "unsubmitted second context releases cleanly");
        }
        NSDictionary *record = @{@"schema":@1, @"status":failures.count ? @"failed" : @"pass", @"device":device,
            @"architecture":compiled_architecture, @"format":@"RGBA8Unorm", @"texture_width":@7, @"texture_height":@5,
            @"checks":@(checks), @"GPU_completed_clear_submissions":@(submissions), @"pixels_checked":@(pixels_checked),
            @"all_four_channels_checked":@YES, @"failures":failures, @"creation_error":creation_error ?: @"",
            @"window_or_guest_execution":@NO, @"hardware_timeout_device_loss_OOM_paths_forced":@NO};
        NSData *json = [NSJSONSerialization dataWithJSONObject:record options:NSJSONWritingPrettyPrinted | NSJSONWritingSortedKeys error:nullptr];
        std::fwrite(json.bytes, 1, json.length, stdout); std::putchar('\n');
        return failures.count ? 1 : 0;
    }
}
