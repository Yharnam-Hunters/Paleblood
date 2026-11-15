/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Native keyboard input: AppKit virtual keys -> USB HID usages -> rt_input_map.
   Carbon's Events.h names physical US-key positions; NSEvent characters are deliberately
   not interpreted. IOLLEvent.h's device-specific left-Shift flag distinguishes both Shift
   keys held together. Command chords stay with AppKit and release game controls.

   Guest readers may unwind on ALRM/USR1. Mask both through unlocking so recovery cannot
   leave the sample mutex held. AppKit only updates this state on its own event thread. */
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#include "input_host_macos.h"
#include "runtime/process.h"
#include <Carbon/Carbon.h>
#include <IOKit/hid/IOHIDUsageTables.h>
#include <IOKit/hidsystem/IOLLEvent.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>

enum { KEY_USAGES = kHIDUsage_KeyboardLeftShift + 1 };
static pthread_mutex_t input_lock = PTHREAD_MUTEX_INITIALIZER;
static int enabled, focused, command;
static bool keys[KEY_USAGES];
static rt_touch_tracker touches;
static rt_pad_sample latest = {.left_x = RT_PAD_STICK_CENTRE, .left_y = RT_PAD_STICK_CENTRE,
                               .right_x = RT_PAD_STICK_CENTRE, .right_y = RT_PAD_STICK_CENTRE};

static void lock_state(sigset_t *previous)
{
    sigset_t owned;
    if (sigemptyset(&owned) || sigaddset(&owned, SIGALRM) || sigaddset(&owned, SIGUSR1) ||
        pthread_sigmask(SIG_BLOCK, &owned, previous))
        rt_fatal("host keyboard", "cannot mask recovery signals before taking the input lock");
    if (pthread_mutex_lock(&input_lock)) {
        (void)pthread_sigmask(SIG_SETMASK, previous, NULL);
        rt_fatal("host keyboard", "cannot take the input lock");
    }
}
static void unlock_state(const sigset_t *previous)
{
    const int unlocked = pthread_mutex_unlock(&input_lock);
    const int restored = pthread_sigmask(SIG_SETMASK, previous, NULL);
    if (unlocked || restored) rt_fatal("host keyboard", "cannot release the input lock and restore signals");
}
static void release_keys(void)
{
    memset(keys, 0, sizeof keys);
    rt_pad_sample_at_rest(&latest);
}
static void sample_latest(rt_pad_sample *sample)
{
    sigset_t previous; lock_state(&previous);
    *sample = latest;
    unlock_state(&previous);
}
static const rt_input_backend keyboard_backend = {sample_latest, NULL};

int rt_input_host_configure(int on)
{
    sigset_t previous; lock_state(&previous);
    enabled = on != 0; focused = command = 0;
    release_keys();
    rt_input_install(enabled ? &keyboard_backend : NULL);
    unlock_state(&previous);
    return 0;
}
int rt_input_host_enabled(void)
{
    sigset_t previous; lock_state(&previous);
    const int result = enabled;
    unlock_state(&previous);
    return result;
}
void rt_input_host_macos_focus(int on)
{
    sigset_t previous; lock_state(&previous);
    focused = on != 0;
    if (!focused) { command = 0; release_keys(); }
    unlock_state(&previous);
}
void rt_input_host_macos_modifiers(uint64_t flags)
{
    sigset_t previous; lock_state(&previous);
    if (enabled && focused) {
        command = (flags & NX_COMMANDMASK) != 0;
        if (command) release_keys();
        else keys[kHIDUsage_KeyboardLeftShift] = (flags & NX_DEVICELSHIFTKEYMASK) != 0;
    }
    unlock_state(&previous);
}
static uint16_t key_usage(uint16_t key_code)
{
    switch (key_code) {
    case kVK_ANSI_W: return kHIDUsage_KeyboardW;
    case kVK_ANSI_S: return kHIDUsage_KeyboardS;
    case kVK_ANSI_A: return kHIDUsage_KeyboardA;
    case kVK_ANSI_D: return kHIDUsage_KeyboardD;
    case kVK_UpArrow: return kHIDUsage_KeyboardUpArrow;
    case kVK_DownArrow: return kHIDUsage_KeyboardDownArrow;
    case kVK_LeftArrow: return kHIDUsage_KeyboardLeftArrow;
    case kVK_RightArrow: return kHIDUsage_KeyboardRightArrow;
    case kVK_ANSI_I: return kHIDUsage_KeyboardI;
    case kVK_ANSI_K: return kHIDUsage_KeyboardK;
    case kVK_ANSI_J: return kHIDUsage_KeyboardJ;
    case kVK_ANSI_L: return kHIDUsage_KeyboardL;
    case kVK_Return: return kHIDUsage_KeyboardReturnOrEnter;
    case kVK_Space: return kHIDUsage_KeyboardSpacebar;
    case kVK_Delete: return kHIDUsage_KeyboardDeleteOrBackspace;
    case kVK_ANSI_R: return kHIDUsage_KeyboardR;
    case kVK_ANSI_F: return kHIDUsage_KeyboardF;
    case kVK_ANSI_Q: return kHIDUsage_KeyboardQ;
    case kVK_ANSI_E: return kHIDUsage_KeyboardE;
    case kVK_ANSI_Z: return kHIDUsage_KeyboardZ;
    case kVK_ANSI_C: return kHIDUsage_KeyboardC;
    case kVK_Tab: return kHIDUsage_KeyboardTab;
    case kVK_Escape: return kHIDUsage_KeyboardEscape;
    case kVK_ANSI_T: return kHIDUsage_KeyboardT;
    default: return 0;
    }
}
int rt_input_host_macos_key(uint16_t key_code, int down)
{
    const uint16_t usage = key_usage(key_code);
    sigset_t previous; lock_state(&previous);
    const int handled = enabled && focused && !command && usage != 0;
    if (handled) keys[usage] = down != 0;
    unlock_state(&previous);
    return handled;
}
void rt_input_host_pump(void)
{
    sigset_t previous; lock_state(&previous);
    if (enabled) rt_input_map(NULL, keys, KEY_USAGES, &touches, &latest);
    unlock_state(&previous);
}
