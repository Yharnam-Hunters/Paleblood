/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "runtime/graphics_metal.h"
#include "metal_frame_pixels.h"
#import <Foundation/Foundation.h>
#include <array>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>
#include <sys/mman.h>
#include <unistd.h>

#if defined(__arm64__) || defined(__aarch64__)
static NSString *const compiled_architecture = @"arm64";
#elif defined(__x86_64__)
static NSString *const compiled_architecture = @"x86_64";
#else
static NSString *const compiled_architecture = @"unknown";
#endif

// Distinct channels and rows make a swizzle, dropped alpha or vertical flip observable.
static std::array<uint8_t, 4> pixel(size_t x, size_t y)
{
    return frame_pixels_rgba[y * frame_pattern_width + x];
}

int main(void)
{
    @autoreleasepool {
        NSMutableArray<NSString *> *failures = [NSMutableArray array];
        unsigned checks = 0, uploads = 0, clears = 0, pixels_checked = 0;
        NSString *case_id = @"upload/lifecycle";
        NSMutableArray<NSDictionary *> *operations = [NSMutableArray array];
        NSMutableArray<NSDictionary *> *data_records = [NSMutableArray array];
        const auto check = [&](bool good, const char *message) {
            checks++;
            if (!good) [failures addObject:[NSString stringWithFormat:@"%@: %s", case_id, message]];
        };
        char error[256] = {};
        rt_metal_offscreen *context = nullptr;
        const uint8_t marker[4] = {0, 255, 128, 0};
        check(rt_metal_offscreen_upload(nullptr, marker, 4, 4, nullptr, 0) == RT_METAL_INVALID_ARGUMENT,
              "null upload context rejected");
        const rt_metal_status created = rt_metal_offscreen_create(7, 5, &context, error, sizeof error);
        check(created == RT_METAL_OK && context, "real shared Metal texture created");
        NSString *device = context ? [NSString stringWithUTF8String:rt_metal_offscreen_device_name(context)] : @"unavailable";
        NSString *creation_error = [NSString stringWithUTF8String:error];
        if (context) {
            constexpr size_t width = 7, height = 5, row = width * 4;
            constexpr size_t source_stride = row + 3, source_needed = (height - 1) * source_stride + row;
            constexpr size_t stride = row + 13, guard = 9, needed = (height - 1) * stride + row;
            std::vector<uint8_t> output(guard + stride * height + guard, 0xa5);
            const auto untouched = output;
            const auto read_pattern = [&]() {
                output.assign(output.size(), 0xa5);
                const rt_metal_status read = rt_metal_offscreen_read(context, output.data() + guard, stride, needed, error, sizeof error);
                check(read == RT_METAL_OK, "uploaded texture read through Metal");
                if (read != RT_METAL_OK) return;
                bool pixels = true, padding = true;
                for (size_t y = 0; y < height; y++) {
                    for (size_t x = 0; x < width; x++) {
                        const auto expected = pixel(x, y);
                        pixels &= std::memcmp(output.data() + guard + y * stride + x * 4, expected.data(), 4) == 0;
                        pixels_checked++;
                    }
                    for (size_t x = row; x < stride; x++) padding &= output[guard + y * stride + x] == 0xa5;
                }
                for (size_t x = 0; x < guard; x++) padding &= output[x] == 0xa5 && output[output.size() - 1 - x] == 0xa5;
                check(pixels, "every uploaded channel and row matches the independent pixel pattern");
                check(padding, "readback padding and surrounding canaries preserved");
                [data_records addObject:@{@"id":case_id, @"read_status":@(read), @"pixels_match":@(pixels), @"padding_match":@(padding)}];
            };
            check(rt_metal_offscreen_upload(context, nullptr, source_stride, source_needed, error, sizeof error) == RT_METAL_INVALID_ARGUMENT,
                  "null source rejected before any upload");
            check(rt_metal_offscreen_upload(context, marker, row - 1, source_needed, error, sizeof error) == RT_METAL_INVALID_ARGUMENT,
                  "short stride rejected before reading source");
            check(rt_metal_offscreen_read(context, output.data() + guard, stride, needed, error, sizeof error) == RT_METAL_NOT_READY && output == untouched,
                  "invalid first upload leaves texture unreadable and destination unchanged");

            // The final source pixel abuts an inaccessible page; padding after it is absent.
            const long page_size = sysconf(_SC_PAGESIZE);
            check(page_size > 0 && static_cast<size_t>(page_size) >= source_needed, "page can contain the exact source extent");
            if (page_size > 0 && static_cast<size_t>(page_size) >= source_needed) {
                const size_t page = static_cast<size_t>(page_size);
                void *mapping = mmap(nullptr, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
                check(mapping != MAP_FAILED, "guarded source mapping allocated");
                if (mapping != MAP_FAILED) {
                    auto *source = static_cast<uint8_t *>(mapping) + page - source_needed;
                    std::memset(source, 0x6d, source_needed);
                    for (size_t y = 0; y < height; y++) {
                        for (size_t x = 0; x < width; x++) {
                            const auto value = pixel(x, y);
                            std::memcpy(source + y * source_stride + x * 4, value.data(), 4);
                        }
                    }
                    const std::vector<uint8_t> original(source, source + source_needed);
                    const bool protected_source = mprotect(static_cast<uint8_t *>(mapping) + page, page, PROT_NONE) == 0 &&
                                                  mprotect(mapping, page, PROT_READ) == 0;
                    check(protected_source, "source is read-only with no accessible trailing padding");
                    if (protected_source) {
                        case_id = @"upload/strided7x5/guarded";
                        const auto status = rt_metal_offscreen_upload(context, source, source_stride, source_needed, error, sizeof error);
                        [operations addObject:@{@"id":case_id, @"status":@(status)}];
                        check(status == RT_METAL_OK, "strided frame uploads without a prior GPU clear");
                        if (status == RT_METAL_OK) uploads++;
                        check(std::memcmp(source, original.data(), source_needed) == 0, "upload preserves every source byte including padding");
                    }
                    check(munmap(mapping, page * 2) == 0, "source mapping released immediately after upload returns");
                    read_pattern();
                }
            }

            // All rejected updates must retain the preceding successful frame.
            case_id = @"upload/invalid-after-success";
            check(rt_metal_offscreen_upload(context, marker, source_stride, source_needed - 1, error, sizeof error) == RT_METAL_INVALID_ARGUMENT,
                  "short capacity rejected before reading source");
            check(rt_metal_offscreen_upload(context, marker, SIZE_MAX, SIZE_MAX, error, sizeof error) == RT_METAL_INVALID_ARGUMENT,
                  "stride arithmetic overflow rejected before reading source");
            check(rt_metal_offscreen_upload(context, nullptr, source_stride, source_needed, error, sizeof error) == RT_METAL_INVALID_ARGUMENT,
                  "null source preserves the preceding frame");
            char tiny_error[5] = {'x', 'x', 'x', 'x', 'z'};
            check(rt_metal_offscreen_upload(context, marker, 0, 0, tiny_error, 4) == RT_METAL_INVALID_ARGUMENT &&
                  tiny_error[3] == '\0' && tiny_error[4] == 'z', "diagnostic buffer bounded and nul terminated");
            read_pattern();

            const auto cleared = rt_metal_offscreen_clear(context, marker, 5000, error, sizeof error);
            check(cleared == RT_METAL_OK, "GPU clear safely follows CPU upload");
            if (cleared == RT_METAL_OK) clears++;
            std::vector<uint8_t> tight(row * height);
            check(rt_metal_offscreen_read(context, tight.data(), row, tight.size(), error, sizeof error) == RT_METAL_OK,
                  "GPU overwrite read back after upload");
            bool clear_pixels = true;
            for (size_t i = 0; i < width * height; i++) clear_pixels &= std::memcmp(tight.data() + i * 4, marker, 4) == 0;
            check(clear_pixels, "GPU clear overwrites every uploaded pixel");
            for (size_t y = 0; y < height; y++) {
                for (size_t x = 0; x < width; x++) {
                    const auto value = pixel(x, y);
                    std::memcpy(tight.data() + y * row + x * 4, value.data(), 4);
                }
            }
            case_id = @"upload/tight7x5/after-clear";
            const auto uploaded = rt_metal_offscreen_upload(context, tight.data(), row, tight.size(), error, sizeof error);
            [operations addObject:@{@"id":case_id, @"status":@(uploaded)}];
            check(uploaded == RT_METAL_OK, "tight CPU upload safely follows completed GPU clear");
            if (uploaded == RT_METAL_OK) uploads++;
            tight.assign(tight.size(), 0);
            read_pattern();
            rt_metal_offscreen_destroy(&context);
            check(context == nullptr, "uploaded texture lifecycle destroyed cleanly");

            check(rt_metal_offscreen_create(1, 1, &context, error, sizeof error) == RT_METAL_OK && context,
                  "single-pixel upload context created");
            if (context) {
                case_id = @"upload/thin1x1/extreme-channels";
                const auto single = rt_metal_offscreen_upload(context, marker, SIZE_MAX, sizeof marker, nullptr, 0);
                [operations addObject:@{@"id":case_id, @"status":@(single)}];
                check(single == RT_METAL_OK, "single row requires only its pixels regardless of unused stride");
                if (single == RT_METAL_OK) uploads++;
                uint8_t actual[4] = {};
                check(rt_metal_offscreen_read(context, actual, sizeof actual, sizeof actual, nullptr, 0) == RT_METAL_OK &&
                      std::memcmp(actual, marker, sizeof actual) == 0, "zero and full-range RGBA bytes preserved");
                pixels_checked++;
                rt_metal_offscreen_destroy(&context);
            }
            struct ThinCase { const char *id; size_t width, height; };
            const ThinCase thin_cases[] = {{"upload/thin7x1/pattern",7,1}, {"upload/thin1x5/pattern",1,5}};
            for (const auto &item : thin_cases) {
                case_id = [NSString stringWithUTF8String:item.id];
                const size_t thin_row = item.width * 4, thin_stride = thin_row + 3;
                const size_t source_size = (item.height - 1) * thin_stride + thin_row;
                std::vector<uint8_t> source(source_size,0x6d);
                for (size_t y = 0; y < item.height; ++y) for (size_t x = 0; x < item.width; ++x) {
                    const auto value = pixel(x,y);
                    std::memcpy(source.data() + y * thin_stride + x * 4,value.data(),4);
                }
                const auto before = source;
                check(rt_metal_offscreen_create(static_cast<uint32_t>(item.width),static_cast<uint32_t>(item.height),
                                                &context,error,sizeof error) == RT_METAL_OK && context,
                      "thin frame creates an independent context");
                if (!context) continue;
                // A one-row caller stride is unused; its exact capacity is only the row.
                const size_t caller_stride = item.height == 1 ? SIZE_MAX : thin_stride;
                const auto uploaded_thin = rt_metal_offscreen_upload(context,source.data(),caller_stride,source_size,error,sizeof error);
                [operations addObject:@{@"id":case_id, @"status":@(uploaded_thin)}];
                check(uploaded_thin == RT_METAL_OK, "thin frame uploads with exact final-pixel capacity");
                if (uploaded_thin == RT_METAL_OK) uploads++;
                check(source == before, "upload leaves source pixels and padding untouched");
                std::fill(source.begin(),source.end(),0xcc);
                const size_t thin_output_stride = thin_row + 9, thin_guard = 11;
                const size_t output_size = (item.height - 1) * thin_output_stride + thin_row;
                std::vector<uint8_t> actual(thin_guard + thin_output_stride * item.height + thin_guard,0xa5);
                const auto read = rt_metal_offscreen_read(context,actual.data() + thin_guard,thin_output_stride,
                                                         output_size,error,sizeof error);
                check(read == RT_METAL_OK, "thin uploaded pixels survive immediate caller reuse");
                bool pixels = true, padding = true;
                for (size_t y = 0; y < item.height; ++y) {
                    for (size_t x = 0; x < item.width; ++x) {
                        const auto expected = frame_pixels_rgba[y * frame_pattern_width + x];
                        pixels &= std::memcmp(actual.data() + thin_guard + y * thin_output_stride + x * 4,expected.data(),4) == 0;
                        pixels_checked++;
                    }
                    for (size_t x = thin_row; x < thin_output_stride; ++x)
                        padding &= actual[thin_guard + y * thin_output_stride + x] == 0xa5;
                }
                for (size_t x = 0; x < thin_guard; ++x)
                    padding &= actual[x] == 0xa5 && actual[actual.size() - 1 - x] == 0xa5;
                check(pixels, "thin frames match the independent literal channel and row table");
                check(padding, "thin frame readback preserves padding and both guards");
                [data_records addObject:@{@"id":case_id, @"read_status":@(read), @"pixels_match":@(pixels), @"padding_match":@(padding)}];
                rt_metal_offscreen_destroy(&context);
                check(!context, "thin frame context released");
            }
        }
        NSDictionary *record = @{@"schema":@1, @"status":failures.count ? @"failed" : @"pass",
            @"device":device ?: @"unavailable", @"architecture":compiled_architecture,
            @"checks":@(checks), @"CPU_texture_uploads":@(uploads), @"GPU_completed_clear_submissions":@(clears),
            @"pixels_checked":@(pixels_checked), @"operations":operations, @"data_records":data_records, @"failures":failures, @"creation_error":creation_error ?: @"",
            @"window_or_guest_execution":@NO, @"hardware_timeout_device_loss_OOM_paths_forced":@NO};
        NSData *json = [NSJSONSerialization dataWithJSONObject:record options:NSJSONWritingPrettyPrinted | NSJSONWritingSortedKeys error:nullptr];
        std::fwrite(json.bytes, 1, json.length, stdout); std::putchar('\n');
        return failures.count ? 1 : 0;
    }
}
