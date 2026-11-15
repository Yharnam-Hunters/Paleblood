/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Observe completed NSApplication event dispatch, preserving AppKit's original implementation.
   The actual adapter creates its real view, first responder and focus delegates. */
#include "runtime/graphics_metal.h"
#include "runtime/input_host.h"
#import <AppKit/AppKit.h>
#import <objc/runtime.h>
#include <Carbon/Carbon.h>
#include <IOKit/hidsystem/IOLLEvent.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sched.h>
#include "macos_input/bindings.h"
#include "../runtime/display/display_window_metal.mm"

static std::atomic<unsigned> delivered{0},close_requests{0};
static std::atomic<unsigned> checks{0},failures{0};
static unsigned events;
/* These are used only on AppKit's main thread. A release acknowledgment follows sendEvent. */
static NSEvent *owned_event;
static unsigned owned_sequence;
static void (*native_send_event)(id,SEL,NSEvent *);
static void observed_send_event(id receiver,SEL selector,NSEvent *event)
{
    const bool keyboard=event.type==NSEventTypeKeyDown || event.type==NSEventTypeKeyUp || event.type==NSEventTypeFlagsChanged;
    /* AppKit can rewrap a posted NSEvent. One owned event is in flight, with timestamp0;
       real system keys have their own uptime. Correlate immutable fields, not object identity. */
    const bool owned=owned_event && keyboard && event.type==owned_event.type &&
        event.keyCode==owned_event.keyCode && event.windowNumber==owned_event.windowNumber &&
        event.timestamp==owned_event.timestamp;
    const unsigned sequence=owned_sequence;
    native_send_event(receiver,selector,event);
    if (owned) {owned_event=nil;delivered.store(sequence,std::memory_order_release);}
}
extern "C" void rt_boot_window_request_stop(const char *function,const char *reason)
{
    if (!NSThread.isMainThread || !function || !reason || !std::strstr(reason,"close")) ++failures;
    close_requests.fetch_add(1);
}
extern "C" void rt_fatal(const char *function,const char *reason)
{
    std::fprintf(stderr,"owned input window fatal: %s: %s\n",function,reason);std::_Exit(2);
}
static void check(const char *name,bool matches)
{
    ++checks;if (!matches) {++failures;std::fprintf(stderr,"FAIL AppKit keyboard check=%s\n",name);}
}
static bool wait_for(const std::atomic<unsigned> &value,unsigned target)
{
    const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while (value.load(std::memory_order_acquire)<target) {
        if (std::chrono::steady_clock::now()>=until) return false;sched_yield();
    }
    return true;
}
static void expect_sample(const rt_pad_sample &wanted)
{
    rt_pad_sample actual;
    const int present=rt_input_sample(&actual);
    const bool matches=!std::memcmp(&actual,&wanted,sizeof actual);
    std::printf("AppKit input event=%u backend=%d buttons=0x%08x left_y=%u expected_buttons=0x%08x expected_left_y=%u match=%u\n",
        events,present,actual.buttons,actual.left_y,wanted.buttons,wanted.left_y,matches?1u:0u);
    std::fflush(stdout);
    check("backend installed",present==1);
    check("complete guest sample",matches);
}
static void expect(uint32_t buttons,uint8_t left_y=RT_PAD_STICK_CENTRE)
{
    rt_pad_sample wanted;rt_pad_sample_at_rest(&wanted);wanted.buttons=buttons;wanted.left_y=left_y;
    expect_sample(wanted);
}
static bool post(NSEventType type,unsigned short code,NSEventModifierFlags flags=0,BOOL repeat=NO,
                 NSString *characters=@"x")
{
    const unsigned sequence=++events;
    dispatch_async(dispatch_get_main_queue(),^{
        NSWindow *window=NSApp.keyWindow;
        owned_event=[NSEvent keyEventWithType:type location:NSZeroPoint modifierFlags:flags timestamp:0
            windowNumber:window.windowNumber context:nil characters:characters charactersIgnoringModifiers:characters
            isARepeat:repeat keyCode:code];
        owned_sequence=sequence;
        [NSApp postEvent:owned_event atStart:NO];
    });
    const bool completed=wait_for(delivered,sequence) && delivered.load(std::memory_order_acquire)==sequence;
    check("real window event delivered",completed);return completed;
}
static bool ready(void)
{
    const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    do {
        __block bool active=false;
        dispatch_sync(dispatch_get_main_queue(),^{
            NSWindow *window=NSApp.keyWindow;
            active=NSApp.isActive && window && window.firstResponder==window.contentView;
        });
        if (active) return true;
        if (std::chrono::steady_clock::now()>=until) return false;sched_yield();
    } while (true);
}
static bool inactive(void)
{
    const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    for (;;) {
        __block bool active=true;
        dispatch_sync(dispatch_get_main_queue(),^{active=NSApp.isActive;});
        if (!active) return true;
        if (std::chrono::steady_clock::now()>=until) return false;
        sched_yield();
    }
}
static int owned_boot(void *unused,const sigset_t *previous)
{
    (void)unused;
    if (NSThread.isMainThread || pthread_sigmask(SIG_SETMASK,previous,nullptr) || rt_display_window_open(320,160)) return 2;
    check("actual key window first responder",ready());if (failures.load()) return 1;
    expect(0);
    for (const Binding &binding:bindings) {
        rt_pad_sample wanted;rt_pad_sample_at_rest(&wanted);wanted.buttons=binding.button;
        switch (binding.axis) {
        case 1:wanted.left_x=binding.value;break;case 2:wanted.left_y=binding.value;break;
        case 3:wanted.right_x=binding.value;break;case 4:wanted.right_y=binding.value;break;
        }
        if (binding.button==RT_PAD_BUTTON_L2) wanted.l2=255;
        if (binding.button==RT_PAD_BUTTON_R2) wanted.r2=255;
        NSString *characters=[[NSString stringWithUTF8String:binding.name] lowercaseString];
        NSEventModifierFlags flags=0;unichar arrow=0;
        switch (binding.code) {
        case kVK_Return:characters=@"\r";break;case kVK_Space:characters=@" ";break;
        case kVK_Tab:characters=@"\t";break;case kVK_Escape:characters=@"\x1b";break;
        case kVK_Delete:characters=@"\x7f";break;
        case kVK_UpArrow:arrow=NSUpArrowFunctionKey;break;
        case kVK_DownArrow:arrow=NSDownArrowFunctionKey;break;
        case kVK_LeftArrow:arrow=NSLeftArrowFunctionKey;break;
        case kVK_RightArrow:arrow=NSRightArrowFunctionKey;break;
        }
        if (arrow) {characters=[NSString stringWithCharacters:&arrow length:1];flags=NSEventModifierFlagNumericPad;}
        if (!post(NSEventTypeKeyDown,binding.code,flags,NO,characters)) return 1;
        expect_sample(wanted);
        if (!post(NSEventTypeKeyUp,binding.code,flags,NO,characters)) return 1;
        expect(0);
    }
    if (!post(NSEventTypeKeyDown,kVK_Return)) return 1;expect(RT_PAD_BUTTON_CROSS);
    if (!post(NSEventTypeKeyDown,kVK_Return,0,YES)) return 1;expect(RT_PAD_BUTTON_CROSS);
    if (!post(NSEventTypeKeyDown,kVK_Space)) return 1;expect(RT_PAD_BUTTON_CROSS);
    if (!post(NSEventTypeKeyUp,kVK_Return)) return 1;expect(RT_PAD_BUTTON_CROSS);
    if (!post(NSEventTypeKeyUp,kVK_Space)) return 1;expect(0);
    // Characters disagree deliberately: the canonical mapping uses physical key positions.
    if (!post(NSEventTypeKeyDown,kVK_ANSI_W,0,NO,@"q")) return 1;expect(0,0);
    if (!post(NSEventTypeKeyDown,kVK_ANSI_S)) return 1;expect(0);
    if (!post(NSEventTypeKeyUp,kVK_ANSI_S)) return 1;expect(0,0);
    if (!post(NSEventTypeKeyUp,kVK_ANSI_W)) return 1;expect(0);
    if (!post(NSEventTypeKeyDown,kVK_ANSI_K)) return 1;expect(RT_PAD_BUTTON_DOWN);
    if (!post(NSEventTypeKeyUp,kVK_ANSI_K)) return 1;expect(0);
    if (!post(NSEventTypeFlagsChanged,kVK_Shift,NX_SHIFTMASK|NX_DEVICELSHIFTKEYMASK)) return 1;expect(RT_PAD_BUTTON_L3);
    if (!post(NSEventTypeFlagsChanged,kVK_RightShift,NX_SHIFTMASK|NX_DEVICELSHIFTKEYMASK|NX_DEVICERSHIFTKEYMASK)) return 1;expect(RT_PAD_BUTTON_L3);
    if (!post(NSEventTypeFlagsChanged,kVK_Shift,NX_SHIFTMASK|NX_DEVICERSHIFTKEYMASK)) return 1;expect(0);
    if (!post(NSEventTypeFlagsChanged,kVK_RightShift,0)) return 1;expect(0);
    if (!post(NSEventTypeKeyDown,kVK_ANSI_W)) return 1;expect(0,0);
    if (!post(NSEventTypeFlagsChanged,kVK_Command,NX_COMMANDMASK|NX_DEVICELCMDKEYMASK)) return 1;expect(0);
    // Not a registered menu key equivalent; it must traverse sendEvent without moving the pad.
    if (!post(NSEventTypeKeyDown,kVK_ANSI_K,NX_COMMANDMASK|NX_DEVICELCMDKEYMASK,NO,@"k")) return 1;expect(0);
    if (!post(NSEventTypeKeyUp,kVK_ANSI_K,NX_COMMANDMASK|NX_DEVICELCMDKEYMASK,NO,@"k")) return 1;expect(0);
    if (!post(NSEventTypeFlagsChanged,kVK_Command,0)) return 1;expect(0);
    if (!post(NSEventTypeKeyDown,kVK_ANSI_W)) return 1;expect(0,0);
    __block NSWindow *window=nil;
    dispatch_sync(dispatch_get_main_queue(),^{window=NSApp.keyWindow;[window orderOut:nil];});
    expect(0);
    dispatch_sync(dispatch_get_main_queue(),^{[window makeKeyAndOrderFront:nil];});
    check("focus restored to actual view",ready());expect(0);
    if (!post(NSEventTypeKeyDown,kVK_Return)) return 1;expect(RT_PAD_BUTTON_CROSS);
    dispatch_sync(dispatch_get_main_queue(),^{
        [NSApp deactivate];
        // Let the actual application callback clear controls. No direct backend calls here.
    });
    // Window resignation precedes application deactivation; await the latter before reactivation.
    check("application deactivation completed",inactive());
    const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    for (;;) {
        rt_pad_sample current;rt_input_sample(&current);
        if (!current.buttons) break;
        if (std::chrono::steady_clock::now()>=until) {check("application focus releases controls",false);break;}
        sched_yield();
    }
    expect(0);
    dispatch_sync(dispatch_get_main_queue(),^{
        [NSApp activateIgnoringOtherApps:YES];[window makeKeyAndOrderFront:nil];
    });
    check("reactivated real key view",ready());
    if (!post(NSEventTypeKeyDown,kVK_Return)) return 1;expect(RT_PAD_BUTTON_CROSS);
    dispatch_async(dispatch_get_main_queue(),^{[window performClose:nil];});
    check("real close",wait_for(close_requests,1));
    expect(0); // Production close releases keys before the coordinator returns.
    return failures.load()?1:0;
}
int main(void)
{
    if (rt_input_host_configure(1)) return 2;
    Method method=class_getInstanceMethod([NSApplication class],@selector(sendEvent:));
    if (!method) return 2;
    native_send_event=reinterpret_cast<void (*)(id,SEL,NSEvent *)>(method_getImplementation(method));
    method_setImplementation(method,reinterpret_cast<IMP>(observed_send_event));
    const int result=rt_display_window_run(owned_boot,nullptr);
    method_setImplementation(method,reinterpret_cast<IMP>(native_send_event));
    rt_input_host_configure(0);
    std::printf("AppKit keyboard: events=%u delivered=%u checks=%u close_requests=%u failures=%u rc=%d\n",
        events,delivered.load(),checks.load(),close_requests.load(),failures.load(),result);
    return result || failures.load() || events!=71 || delivered.load()!=71 || close_requests.load()!=1 ?1:0;
}
