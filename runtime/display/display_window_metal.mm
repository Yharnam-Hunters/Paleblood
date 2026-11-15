/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "runtime/display_window.h"
#include "runtime/window_boot.h"
#include "runtime/graphics_metal.h"
#include "runtime/process.h"
#include "window_mailbox.h"
#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <pthread.h>

/* Kept until process exit: a stopped guest/helper may still call the display boundary. */
static rt_window_mailbox *mailbox;

@interface PBRunWindow : NSObject <NSApplicationDelegate,NSWindowDelegate> {
@public
    rt_window_boot_fn boot;
    void *options;
    sigset_t previous_mask;
    pthread_t coordinator;
    std::atomic<bool> done;
    bool started;
    int result;
    NSWindow *window;
    CAMetalLayer *layer;
    NSTimer *timer;
    rt_metal_offscreen *context;
    uint32_t source_width,source_height;
}
- (void)tick;
- (void)fail:(const char *)reason;
@end

static void *coordinate(void *opaque)
{
    PBRunWindow *owner=(__bridge PBRunWindow *)opaque;
    @autoreleasepool {
        owner->result=owner->boot(owner->options,&owner->previous_mask);
        owner->done.store(true,std::memory_order_release);
    }
    return nullptr;
}

@implementation PBRunWindow
- (void)fail:(const char *)reason
{
    rt_window_mailbox_cancel(mailbox);
    rt_boot_window_request_stop("Metal window",reason);
}
- (void)applicationDidFinishLaunching:(NSNotification *)notification
{
    (void)notification;
    @try {
        timer=[NSTimer scheduledTimerWithTimeInterval:1.0/60.0 repeats:YES block:^(NSTimer *tick) {
            (void)tick;[self tick];
        }];
        if (!timer) {result=3;done.store(true,std::memory_order_release);}
        else {
            const int error=pthread_create(&coordinator,nullptr,coordinate,(__bridge void *)self);
            if (error) {result=3;done.store(true,std::memory_order_release);}
            else started=true;
        }
    } @catch (NSException *exception) {
        if (started) [self fail:exception.reason.UTF8String];
        else {result=3;done.store(true,std::memory_order_release);}
    }
    [self tick];
}
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender
{
    (void)sender;[self fail:"application close requested"];return NSTerminateCancel;
}
- (BOOL)windowShouldClose:(NSWindow *)sender
{
    (void)sender;[self fail:"window close requested"];return NO;
}
- (void)windowDidResize:(NSNotification *)notification
{
    (void)notification;
    layer.frame=window.contentView.bounds;
    layer.contentsScale=window.backingScaleFactor;
}
- (void)windowDidChangeBackingProperties:(NSNotification *)notification
{
    [self windowDidResize:notification];
}
- (void)pump
{
    if (done.load(std::memory_order_acquire)) {
        rt_window_mailbox_cancel(mailbox);
        [timer invalidate];timer=nil;
        window.delegate=nil;[window orderOut:nil];window=nil;layer=nil;
        rt_metal_offscreen_destroy(&context);
        [NSApp stop:nil];
        [NSApp postEvent:[NSEvent otherEventWithType:NSEventTypeApplicationDefined location:NSZeroPoint
            modifierFlags:0 timestamp:0 windowNumber:0 context:nil subtype:0 data1:0 data2:0] atStart:NO];
        return;
    }
    rt_window_frame frame{};
    if (!rt_window_mailbox_poll(mailbox,&frame)) return;
    /* Snapshot ownership is independent of mailbox and display locks, including exceptions. */
    std::unique_ptr<uint8_t,decltype(&std::free)> pixels(frame.rgba,&std::free);
    if (frame.opening) {
        if (!window) {
            window=[[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,frame.open_width,frame.open_height)
                styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskMiniaturizable|NSWindowStyleMaskResizable
                backing:NSBackingStoreBuffered defer:NO];
            if (!window) {[self fail:"cannot create AppKit window"];return;}
            window.title=@"Paleblood";window.releasedWhenClosed=NO;window.delegate=self;
            NSView *view=[[NSView alloc] initWithFrame:window.contentView.bounds];
            layer=[CAMetalLayer layer];
            if (!view || !layer) {[self fail:"cannot create AppKit Metal view/layer"];return;}
            layer.frame=view.bounds;
            layer.contentsScale=window.backingScaleFactor;layer.framebufferOnly=YES;view.layer=layer;view.wantsLayer=YES;
            window.contentView=view;[window center];[window makeKeyAndOrderFront:nil];[NSApp activateIgnoringOtherApps:YES];
        }
        (void)rt_window_mailbox_opened(mailbox); /* A busy publisher is retried on an ordinary tick. */
    }
    if (!pixels || !window) return;
    char error[512]="";
    if (!context || source_width!=frame.width || source_height!=frame.height) {
        rt_metal_offscreen_destroy(&context);
        rt_metal_status status=rt_metal_offscreen_create(frame.width,frame.height,&context,error,sizeof error);
        if (status!=RT_METAL_OK) {[self fail:error];return;}
        source_width=frame.width;source_height=frame.height;
        status=rt_metal_offscreen_configure_layer(context,(__bridge void *)layer,error,sizeof error);
        if (status!=RT_METAL_OK) {rt_metal_offscreen_destroy(&context);[self fail:error];return;}
    }
    rt_metal_status status=rt_metal_offscreen_upload(context,pixels.get(),(size_t)frame.width*4,
        (size_t)frame.width*frame.height*4,error,sizeof error);
    if (status==RT_METAL_OK)
        status=rt_metal_offscreen_present(context,(__bridge void *)layer,nullptr,1000,error,sizeof error);
    /* No drawable preserves this context. The latest future frame gets the next ordinary tick. */
    if (status!=RT_METAL_OK && status!=RT_METAL_NO_DRAWABLE) {
        rt_metal_offscreen_destroy(&context);[self fail:error];
    }
}
- (void)tick
{
    try {
        @try {[self pump];}
        @catch (NSException *exception) {rt_metal_offscreen_destroy(&context);[self fail:exception.reason.UTF8String];}
    } catch (const std::exception &exception) {rt_metal_offscreen_destroy(&context);[self fail:exception.what()];}
      catch (...) {rt_metal_offscreen_destroy(&context);[self fail:"unexpected native window exception"];}
}
@end

int rt_display_window_available(void) {return 1;}
int rt_display_window_open(uint32_t width,uint32_t height)
{
    return rt_window_mailbox_open(mailbox,width,height);
}
void rt_display_window_show(const uint32_t *pixels,uint32_t width,uint32_t height)
{
    if (rt_window_mailbox_publish(mailbox,pixels,width,height))
        rt_fatal("rt_display_window_show","Metal window frame rejected or cancelled");
}
int rt_display_window_run(rt_window_boot_fn run,void *options)
{
    if (!run || !pthread_main_np() || mailbox) return 3;
    sigset_t owned,previous;
    if (sigemptyset(&owned) || sigaddset(&owned,SIGALRM) || sigaddset(&owned,SIGUSR1) ||
        pthread_sigmask(SIG_BLOCK,&owned,&previous)) return 3;
    if (sigismember(&previous,SIGALRM) || sigismember(&previous,SIGUSR1)) {
        (void)pthread_sigmask(SIG_SETMASK,&previous,nullptr);
        std::fputs("pbboot: window requires initially unblocked SIGALRM and SIGUSR1\n",stderr);return 3;
    }
    /* All AppKit/Metal-created threads now inherit the blocked owned signals. */
    @autoreleasepool {
        mailbox=rt_window_mailbox_create();
        if (!mailbox) {(void)pthread_sigmask(SIG_SETMASK,&previous,nullptr);return 3;}
        PBRunWindow *owner=[PBRunWindow new];
        if (!owner) {rt_window_mailbox_destroy(mailbox);mailbox=nullptr;(void)pthread_sigmask(SIG_SETMASK,&previous,nullptr);return 3;}
        owner->boot=run;owner->options=options;owner->previous_mask=previous;
        owner->done.store(false);owner->result=3;
        NSApplication *application=nil;
        @try {
            application=[NSApplication sharedApplication];application.delegate=owner;
            [application setActivationPolicy:NSApplicationActivationPolicyRegular];
        } @catch (NSException *exception) {
            (void)exception;rt_window_mailbox_destroy(mailbox);mailbox=nullptr;
            (void)pthread_sigmask(SIG_SETMASK,&previous,nullptr);return 3;
        }
        if (!application) {rt_window_mailbox_destroy(mailbox);mailbox=nullptr;(void)pthread_sigmask(SIG_SETMASK,&previous,nullptr);return 3;}
        do {
            @try {[application run];}
            @catch (NSException *exception) {
                if (owner->started) [owner fail:exception.reason.UTF8String];
                else {owner->result=3;owner->done.store(true,std::memory_order_release);}
            }
            if (!owner->done.load(std::memory_order_acquire))
                [owner fail:"application event loop stopped before boot completion"];
        } while (!owner->done.load(std::memory_order_acquire));
        /* Options stay alive and the GUI keeps pumping until completion; never join an open waiter. */
        if (owner->started) pthread_join(owner->coordinator,nullptr);
        [owner->timer invalidate];owner->timer=nil;application.delegate=nil;
        const int result=owner->result;
        (void)pthread_sigmask(SIG_SETMASK,&previous,nullptr);return result;
    }
}
