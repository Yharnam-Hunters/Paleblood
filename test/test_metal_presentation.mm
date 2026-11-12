/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "runtime/graphics_metal.h"
#include "metal_frame_pixels.h"
#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>
#import <QuartzCore/CATransaction.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>
#include <thread>

struct PresentationWitness {
    std::atomic<unsigned> callbacks{0}, positive_times{0}, last_acquisition{0};
    std::atomic<double> last_time{0};
};

// Observe genuine drawables; neither the Metal device nor the drawable is replaced or mocked.
@interface PBObservedMetalLayer : CAMetalLayer {
@public
    std::shared_ptr<PresentationWitness> witness;
    unsigned acquisitions;
}
@end
@implementation PBObservedMetalLayer
- (id<CAMetalDrawable>)nextDrawable
{
    id<CAMetalDrawable> drawable = [super nextDrawable];
    const unsigned acquisition = ++acquisitions;
    if (drawable) {
        const auto signal = witness;
        [drawable addPresentedHandler:^(id<MTLDrawable> presented) {
            const double time = presented.presentedTime;
            signal->last_acquisition.store(acquisition);
            signal->last_time.store(time);
            if (time > 0) signal->positive_times.fetch_add(1);
            signal->callbacks.fetch_add(1);
        }];
    }
    return drawable;
}
@end

#if defined(__arm64__) || defined(__aarch64__)
static NSString *const compiled_architecture = @"arm64";
#else
static NSString *const compiled_architecture = @"x86_64";
#endif

static std::array<uint8_t, 4> rgba(size_t x, size_t y)
{
    return frame_pixels_rgba[y * frame_pattern_width + x];
}

static void pump_window_events()
{
    @autoreleasepool {
        NSEvent *event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                          untilDate:[NSDate dateWithTimeIntervalSinceNow:0.01]
                                             inMode:NSDefaultRunLoopMode dequeue:YES];
        if (event) [NSApp sendEvent:event];
        [NSApp updateWindows];
        [CATransaction flush];
    }
}

static bool wait_for_presentations(const std::shared_ptr<PresentationWitness> &signal, unsigned count)
{
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (signal->callbacks.load() < count && std::chrono::steady_clock::now() < until) {
        pump_window_events();
    }
    return signal->callbacks.load() == count;
}

static int run_tests()
{
    @autoreleasepool {
        NSMutableArray<NSString *> *failures = [NSMutableArray array];
        NSMutableArray<NSDictionary *> *frame_records = [NSMutableArray array];
        NSMutableArray<NSDictionary *> *pixel_records = [NSMutableArray array];
        NSMutableArray<NSDictionary *> *rejections = [NSMutableArray array];
        NSString *case_id = @"presentation/lifecycle";
        NSString *last_presentation_id = @"none";
        unsigned checks = 0, completed = 0, pixels_checked = 0, startup_frames = 0, verified_presentations = 0;
        const auto check = [&](bool good, const char *message) {
            checks++;
            if (!good) [failures addObject:[NSString stringWithFormat:@"%@: %s", case_id, message]];
        };
        check(NSApp.isRunning, "presentation fixture executes inside the AppKit application run loop");
        NSWindow *window = [[NSWindow alloc] initWithContentRect:NSMakeRect(160, 160, 280, 200)
                                                      styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskResizable backing:NSBackingStoreBuffered defer:NO];
        window.title = @"Paleblood Metal presentation test";
        window.releasedWhenClosed = NO;
        NSView *view = window.contentView;
        PBObservedMetalLayer *layer = [PBObservedMetalLayer layer];
        const auto signal = std::make_shared<PresentationWitness>();
        layer->witness = signal;
        layer.opaque = YES;
        view.wantsLayer = YES;
        layer.frame = view.bounds;
        layer.contentsScale = window.backingScaleFactor;
        [view.layer addSublayer:layer];
        [window orderFrontRegardless];
        [window displayIfNeeded];
        bool window_visible_before_drawable = false, window_resize_geometry_checked = false;

        constexpr size_t width = 7, height = 5, row = width * 4, stride = row + 9, guard = 11;
        constexpr size_t needed = (height - 1) * stride + row;
        std::vector<uint8_t> source(row * height), output(guard + stride * height + guard, 0xa5);
        const auto untouched = output;
        for (size_t y = 0; y < height; y++) {
            for (size_t x = 0; x < width; x++) {
                const auto value = rgba(x, y);
                std::memcpy(source.data() + y * row + x * 4, value.data(), 4);
            }
        }
        rt_metal_bgra_readback capture{output.data() + guard, stride, needed};
        char error[256] = {};
        rt_metal_offscreen *context = nullptr;
        check(rt_metal_offscreen_create(width, height, &context, error, sizeof error) == RT_METAL_OK && context,
              "real presentation source device/queue/texture created");
        NSString *device = context ? [NSString stringWithUTF8String:rt_metal_offscreen_device_name(context)] : @"unavailable";
        if (context) {
            check(rt_metal_offscreen_configure_layer(context, nullptr, error, sizeof error) == RT_METAL_INVALID_ARGUMENT,
                  "null layer rejected");
            NSObject *wrong = [NSObject new];
            check(rt_metal_offscreen_configure_layer(context, (__bridge void *)wrong, error, sizeof error) == RT_METAL_INVALID_ARGUMENT,
                  "valid non-layer object rejected");
            check(rt_metal_offscreen_configure_layer(context, (__bridge void *)layer, error, sizeof error) == RT_METAL_OK &&
                  layer.device && layer.pixelFormat == MTLPixelFormatBGRA8Unorm && layer.drawableSize.width == width &&
                  layer.drawableSize.height == height && layer.allowsNextDrawableTimeout && !layer.presentsWithTransaction,
                  "borrowed layer configured for exact BGRA frame extent");
            check(rt_metal_offscreen_present(context, (__bridge void *)layer, nullptr, 5000, error, sizeof error) == RT_METAL_NOT_READY && layer->acquisitions == 0,
                  "uninitialized source rejected before drawable acquisition");
            check(rt_metal_offscreen_upload(context, source.data(), row, source.size(), error, sizeof error) == RT_METAL_OK,
                  "patterned source uploaded");
            auto previous_submission = std::chrono::steady_clock::now() - std::chrono::milliseconds(100);
            const auto present = [&](const char *name, const rt_metal_bgra_readback *destination, bool startup = false) {
                case_id = [NSString stringWithUTF8String:name];
                last_presentation_id = case_id;
                // This fixture owns its display cadence. Service AppKit at a fixed 10Hz rate,
                // independently of callback results; do not submit a CPU-speed burst at startup.
                const auto next_frame = previous_submission + std::chrono::milliseconds(100);
                while (std::chrono::steady_clock::now() < next_frame) pump_window_events();
                previous_submission = std::chrono::steady_clock::now();
                const unsigned positive_before = signal->positive_times.load();
                const auto status = rt_metal_offscreen_present(context, (__bridge void *)layer, destination, 5000, error, sizeof error);
                check(status == RT_METAL_OK, "render and optional pre-present capture completed on GPU");
                if (status != RT_METAL_OK) [failures addObject:[NSString stringWithFormat:@"present status %d: %s", status, error]];
                else completed++;
                const unsigned acquisition = layer->acquisitions;
                const bool callback = wait_for_presentations(signal, completed);
                check(callback, "actual drawable reports completion of its presentation scheduling");
                [frame_records addObject:@{@"id":case_id, @"status":@(status), @"acquisition":@(acquisition), @"callback_acquisition":@(signal->last_acquisition.load()),
                    @"submission":@(completed), @"startup":@(startup),
                    @"GPU_completed":@(status == RT_METAL_OK), @"callback":@(callback),
                    @"presented_time":@(signal->last_time.load())}];
                if (startup) {
                    startup_frames++;
                } else {
                    const bool displayed = status == RT_METAL_OK && callback && signal->positive_times.load() == positive_before + 1 && signal->last_time.load() > 0 && signal->last_acquisition.load() == acquisition;
                    check(displayed, "verified frame has its own positive real drawable presentation timestamp");
                    if (displayed) verified_presentations++;
                }
            };
            check(layer.framebufferOnly, "normal layer defaults to framebuffer-only drawables");
            check(rt_metal_offscreen_present(context, (__bridge void *)layer, &capture, 5000, error, sizeof error) == RT_METAL_INVALID_ARGUMENT && output == untouched && layer->acquisitions == 0,
                  "capture rejects framebuffer-only layer before acquisition/output mutation");
            // Service AppKit events after configuration changed the layer's device/extent.
            // CATransaction completion describes animations, not window exposure.
            const auto exposure_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            do {
                pump_window_events();
                window_visible_before_drawable = window.isVisible && (window.occlusionState & NSWindowOcclusionStateVisible);
            } while (!window_visible_before_drawable && std::chrono::steady_clock::now() < exposure_deadline);
            check(window_visible_before_drawable, "configured layer tree belongs to a visible window before acquisition");
            // A newly exposed/reconfigured surface may skip its first scheduled frame. Record
            // this explicit startup frame; it does not satisfy any verified-display assertion.
            present("present/startup-initial",nullptr,true);
            present("present/framebuffer-only-pattern",nullptr);
            layer.framebufferOnly = NO;

            const auto reject = [&](const char *name, rt_metal_bgra_readback invalid, uint32_t timeout) {
                case_id = [NSString stringWithUTF8String:name];
                const unsigned before = layer->acquisitions;
                const auto status = rt_metal_offscreen_present(context, (__bridge void *)layer, &invalid, timeout, error, sizeof error);
                size_t changed = 0;
                for (size_t i = 0; i < output.size(); ++i) changed += output[i] != untouched[i];
                [rejections addObject:@{@"id":case_id, @"status":@(status), @"acquisitions_before":@(before),
                    @"acquisitions_after":@(layer->acquisitions), @"capture_changed_bytes":@(changed)}];
                check(status == RT_METAL_INVALID_ARGUMENT && output == untouched && layer->acquisitions == before,
                      "invalid capture/deadline rejected before drawable acquisition/output mutation");
            };
            reject("capture/null-data",{nullptr,stride,needed},5000);
            reject("capture/short-stride",{capture.data,row-1,needed},5000);
            reject("capture/short-capacity",{capture.data,stride,needed-1},5000);
            reject("capture/overflow-stride",{capture.data,SIZE_MAX,SIZE_MAX},5000);
            reject("present/zero-deadline",capture,0);
            reject("present/excess-deadline",capture,30001);
            layer.drawableSize = CGSizeMake(width + 1, height);
            reject("layer/drawable-size",capture,5000);
            layer.drawableSize = CGSizeMake(width, height);
            layer.pixelFormat = MTLPixelFormatBGRA8Unorm_sRGB;
            reject("layer/srgb-format",capture,5000);
            layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
            layer.allowsNextDrawableTimeout = NO;
            reject("layer/disabled-acquisition-timeout",capture,5000);
            layer.allowsNextDrawableTimeout = YES;
            layer.presentsWithTransaction = YES;
            reject("layer/transaction-presentation",capture,5000);
            layer.presentsWithTransaction = NO;
            layer.device = nil;
            reject("layer/missing-device",capture,5000);
            check(rt_metal_offscreen_configure_layer(context, (__bridge void *)layer, error, sizeof error) == RT_METAL_OK,
                  "caller may explicitly restore the layer profile");
            rt_metal_status thread_status = RT_METAL_OK;
            std::thread worker([&] {
                thread_status = rt_metal_offscreen_present(context, (__bridge void *)layer, nullptr, 5000, nullptr, 0);
            });
            worker.join();
            case_id = @"present/off-main";
            check(thread_status == RT_METAL_INVALID_ARGUMENT, "presentation requires the main thread");
            std::thread configure_worker([&] {
                thread_status = rt_metal_offscreen_configure_layer(context, (__bridge void *)layer, nullptr, 0);
            });
            configure_worker.join();
            case_id = @"configure/off-main";
            check(thread_status == RT_METAL_INVALID_ARGUMENT, "configuration requires the main thread");
            // The error cases deliberately reset device/format, recreating the drawable pool.
            // Perform the second fixed setup frame after the final profile restoration.
            pump_window_events();
            present("present/startup-after-profile-restore",nullptr,true);

            const auto check_pixels = [&](bool pattern) {
                bool pixels = true, padding = true;
                for (size_t y = 0; y < height; y++) {
                    for (size_t x = 0; x < width; x++) {
                        const auto expected = pattern ? frame_pixels_bgra[y * frame_pattern_width + x] :
                                                        std::array<uint8_t, 4>{149,71,23,255};
                        pixels &= std::memcmp(output.data() + guard + y * stride + x * 4, expected.data(), 4) == 0;
                        pixels_checked++;
                    }
                    for (size_t x = row; x < stride; x++) padding &= output[guard + y * stride + x] == 0xa5;
                }
                for (size_t x = 0; x < guard; x++) padding &= output[x] == 0xa5 && output[output.size() - 1 - x] == 0xa5;
                check(pixels, "actual drawable BGRA pixels preserve every source channel and row");
                check(padding, "capture exact extent, row padding and canaries preserved");
                [pixel_records addObject:@{@"id":last_presentation_id, @"pixels_match":@(pixels), @"padding_match":@(padding)}];
            };
            present("present/captured-pattern",&capture);
            check_pixels(true);
            // Layer bounds can resize while the exact drawable/source extent remains fixed.
            case_id = @"window/resize-bounds";
            [window setContentSize:NSMakeSize(350,230)];
            layer.frame = view.bounds;
            layer.contentsScale = window.backingScaleFactor;
            pump_window_events();
            window_resize_geometry_checked = NSEqualRects(layer.frame,view.bounds) && layer.drawableSize.width == width && layer.drawableSize.height == height;
            check(window_resize_geometry_checked, "window resize changes layer bounds without changing source drawable extent");
            std::fill(output.begin(),output.end(),0xa5);
            present("present/resized-window-pattern",&capture);
            check_pixels(true);
            const uint8_t color[4] = {23, 71, 149, 255};
            check(rt_metal_offscreen_clear(context, color, 5000, error, sizeof error) == RT_METAL_OK,
                  "GPU clear updates the source after presentation");
            std::fill(output.begin(), output.end(), 0xa5);
            present("present/captured-clear",&capture);
            check_pixels(false);
            rt_metal_offscreen_destroy(&context);
            check(!context && layer.device && layer.superlayer == view.layer, "destroy releases context without detaching caller-owned layer/view");
            struct ThinCase { const char *setup_id, *frame_id; size_t width, height; };
            const ThinCase thin_cases[] = {
                {"present/startup-thin1x1","present/captured-thin1x1",1,1},
                {"present/startup-thin7x1","present/captured-thin7x1",7,1},
                {"present/startup-thin1x5","present/captured-thin1x5",1,5}
            };
            for (const auto &item : thin_cases) {
                case_id = [NSString stringWithUTF8String:item.frame_id];
                check(rt_metal_offscreen_create(static_cast<uint32_t>(item.width),static_cast<uint32_t>(item.height),
                                                &context,error,sizeof error) == RT_METAL_OK && context,
                      "thin drawable gets an independent context");
                if (!context) continue;
                const size_t thin_row = item.width * 4, thin_stride = thin_row + 9, thin_guard = 11;
                const size_t thin_needed = (item.height - 1) * thin_stride + thin_row;
                std::vector<uint8_t> thin_source(thin_row * item.height);
                for (size_t y = 0; y < item.height; ++y) for (size_t x = 0; x < item.width; ++x) {
                    const auto value = frame_pixels_rgba[y * frame_pattern_width + x];
                    std::memcpy(thin_source.data() + y * thin_row + x * 4,value.data(),4);
                }
                check(rt_metal_offscreen_upload(context,thin_source.data(),item.height == 1 ? SIZE_MAX : thin_row,
                                                thin_source.size(),error,sizeof error) == RT_METAL_OK,
                      "thin literal frame uploads before layer configuration");
                std::fill(thin_source.begin(),thin_source.end(),0xcc);
                check(rt_metal_offscreen_configure_layer(context,(__bridge void *)layer,error,sizeof error) == RT_METAL_OK,
                      "thin drawable layer configured to its exact source extent");
                pump_window_events();
                present(item.setup_id,nullptr,true);
                std::vector<uint8_t> thin_output(thin_guard + thin_stride * item.height + thin_guard,0xa5);
                const rt_metal_bgra_readback thin_capture{thin_output.data() + thin_guard,thin_stride,thin_needed};
                present(item.frame_id,&thin_capture);
                bool pixels = true, padding = true;
                for (size_t y = 0; y < item.height; ++y) {
                    for (size_t x = 0; x < item.width; ++x) {
                        const auto expected = frame_pixels_bgra[y * frame_pattern_width + x];
                        pixels &= std::memcmp(thin_output.data() + thin_guard + y * thin_stride + x * 4,expected.data(),4) == 0;
                        pixels_checked++;
                    }
                    for (size_t x = thin_row; x < thin_stride; ++x)
                        padding &= thin_output[thin_guard + y * thin_stride + x] == 0xa5;
                }
                for (size_t x = 0; x < thin_guard; ++x)
                    padding &= thin_output[x] == 0xa5 && thin_output[thin_output.size() - 1 - x] == 0xa5;
                check(pixels, "thin drawable pixels match literal BGRA channels and rows");
                check(padding, "thin capture preserves exact capacity, padding and guards");
                [pixel_records addObject:@{@"id":last_presentation_id, @"pixels_match":@(pixels), @"padding_match":@(padding)}];
                rt_metal_offscreen_destroy(&context);
                check(!context && layer.superlayer == view.layer, "thin context destruction preserves caller layer attachment");
            }

        }
        case_id = @"window/close";
        [window orderOut:nil];
        [window close];
        check(!window.isVisible, "caller closes its owned window after releasing all source contexts");
        NSDictionary *record = @{@"schema":@1, @"status":failures.count ? @"failed" : @"pass", @"architecture":compiled_architecture,
            @"device":device ?: @"unavailable", @"window_visible_before_first_drawable":@(window_visible_before_drawable), @"checks":@(checks), @"GPU_completed_presentation_submissions":@(completed), @"startup_submissions":@(startup_frames), @"fixture_submission_rate_hz":@10,
            @"verified_positive_presentations":@(verified_presentations), @"frames":frame_records,
            @"pixel_records":pixel_records, @"rejections":rejections,
            @"window_resize_geometry_checked":@(window_resize_geometry_checked), @"no_drawable_timeout_device_loss_OOM_forced":@NO,
            @"drawable_acquisitions":@(layer->acquisitions), @"presented_callbacks":@(signal->callbacks.load()),
            @"positive_presented_times":@(signal->positive_times.load()), @"last_presented_time":@(signal->last_time.load()),
            @"actual_drawable_pixels_checked_before_present":@(pixels_checked), @"failures":failures,
            @"physical_scanout_or_screenshot_verified":@NO, @"guest_execution":@NO,
            @"hardware_timeout_device_loss_OOM_paths_forced":@NO};
        NSData *json = [NSJSONSerialization dataWithJSONObject:record options:NSJSONWritingPrettyPrinted | NSJSONWritingSortedKeys error:nullptr];
        std::fwrite(json.bytes, 1, json.length, stdout); std::putchar('\n');
        return failures.count ? 1 : 0;
    }
}

int main(void)
{
    @autoreleasepool {
        NSApplication *application = NSApplication.sharedApplication;
        [application setActivationPolicy:NSApplicationActivationPolicyAccessory];
        __block int status = 1;
        dispatch_async(dispatch_get_main_queue(), ^{
            status = run_tests();
            [NSApp stop:nil];
            NSEvent *wake = [NSEvent otherEventWithType:NSEventTypeApplicationDefined location:NSZeroPoint
                                         modifierFlags:0 timestamp:0 windowNumber:0 context:nil
                                               subtype:0 data1:0 data2:0];
            [NSApp postEvent:wake atStart:NO];
        });
        [application run];
        return status;
    }
}
