/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Controller input from the host (runtime/input/): what scePadReadState reports
   (runtime/syslib/pad.c). With pbboot's --input none, the default, no backend is installed and the
   pad is at rest. With --input host (a build with SDL3, and --display window), input_host_sdl.c
   reads the first SDL gamepad and the window's keyboard on the window's event thread and installs
   itself as the backend; the title's vibration goes to the gamepad's rumble.

   Three layers, so each can be tested on its own:
   - the backend: a source of samples and a sink for vibration (rt_input_backend), installed with
     rt_input_install; a test installs a fake one;
   - the mapping: a host gamepad's buttons and axes and the keyboard's keys, host-neutral, turned
     into a sample in the title's terms (rt_input_map), with the default keyboard layout;
   - the SDL glue (input_host_sdl.c): copies SDL's state into the host-neutral form. */
#ifndef RUNTIME_INPUT_HOST_H
#define RUNTIME_INPUT_HOST_H

#include <stdbool.h>
#include <stdint.h>

/* The button bits of the state's `buttons` word (the OpenOrbis homebrew SDK's _types/pad.h,
   enum OrbisPadButton; shadPS4's pad.h, OrbisPadButtonDataOffset, agrees). */
enum {
    RT_PAD_BUTTON_L3 = 0x0002,
    RT_PAD_BUTTON_R3 = 0x0004,
    RT_PAD_BUTTON_OPTIONS = 0x0008,
    RT_PAD_BUTTON_UP = 0x0010,
    RT_PAD_BUTTON_RIGHT = 0x0020,
    RT_PAD_BUTTON_DOWN = 0x0040,
    RT_PAD_BUTTON_LEFT = 0x0080,
    RT_PAD_BUTTON_L2 = 0x0100,
    RT_PAD_BUTTON_R2 = 0x0200,
    RT_PAD_BUTTON_L1 = 0x0400,
    RT_PAD_BUTTON_R1 = 0x0800,
    RT_PAD_BUTTON_TRIANGLE = 0x1000,
    RT_PAD_BUTTON_CIRCLE = 0x2000,
    RT_PAD_BUTTON_CROSS = 0x4000,
    RT_PAD_BUTTON_SQUARE = 0x8000,
    RT_PAD_BUTTON_TOUCH_PAD = 0x100000,
};

enum {
    RT_PAD_STICK_CENTRE = 0x80,
    RT_PAD_MAX_TOUCHES = 2,
    RT_PAD_TOUCH_WIDTH = 1920, /* the DualShock 4's touch pad (Linux, drivers/hid/hid-playstation.c) */
    RT_PAD_TOUCH_HEIGHT = 942,
};

typedef struct {
    uint16_t x, y; /* 0 .. width - 1, 0 .. height - 1; from the top left */
    uint8_t id;
} rt_pad_touch;

/* The controller's state in the title's terms. Sticks are 0..255, 0x80 centred, 0 to the left
   and to the top; triggers 0 (released) .. 255. */
typedef struct {
    uint32_t buttons; /* RT_PAD_BUTTON_* */
    uint8_t left_x, left_y, right_x, right_y;
    uint8_t l2, r2;
    uint8_t touch_count; /* the first touch_count entries of touches */
    rt_pad_touch touches[RT_PAD_MAX_TOUCHES];
} rt_pad_sample;

/* No button down, sticks centred, triggers released, no touch. */
void rt_pad_sample_at_rest(rt_pad_sample *sample);

/* A source of controller samples and a sink for the title's vibration. Both may be called from any
   guest thread. */
typedef struct {
    void (*sample)(rt_pad_sample *sample);
    void (*vibrate)(uint8_t large_motor, uint8_t small_motor);
} rt_input_backend;

/* Installs a backend; NULL removes it (the pad is at rest again). */
void rt_input_install(const rt_input_backend *backend);
/* The installed backend's sample, or the pad at rest: 1 when it came from a backend, 0 at rest. */
int rt_input_sample(rt_pad_sample *sample);
/* Passes the title's motor speeds to the installed backend, if any. */
void rt_input_vibrate(uint8_t large_motor, uint8_t small_motor);

/* A host gamepad, host-neutral: the SDL glue copies SDL_Gamepad's state into it. The buttons are
   named by position, as SDL names them (south is the PlayStation pad's cross). */
enum {
    RT_HOST_SOUTH,
    RT_HOST_EAST,
    RT_HOST_WEST,
    RT_HOST_NORTH,
    RT_HOST_BACK,
    RT_HOST_START,
    RT_HOST_LEFT_STICK,
    RT_HOST_RIGHT_STICK,
    RT_HOST_LEFT_SHOULDER,
    RT_HOST_RIGHT_SHOULDER,
    RT_HOST_DPAD_UP,
    RT_HOST_DPAD_DOWN,
    RT_HOST_DPAD_LEFT,
    RT_HOST_DPAD_RIGHT,
    RT_HOST_TOUCHPAD,
    RT_HOST_BUTTONS
};
enum {
    RT_HOST_LEFT_X,
    RT_HOST_LEFT_Y,
    RT_HOST_RIGHT_X,
    RT_HOST_RIGHT_Y,
    RT_HOST_LEFT_TRIGGER,
    RT_HOST_RIGHT_TRIGGER,
    RT_HOST_AXES
};

typedef struct {
    bool present;
    bool button[RT_HOST_BUTTONS];
    /* Sticks -32768 (left, up) .. 32767 (right, down); triggers 0 .. 32767 (SDL_GetGamepadAxis). */
    int16_t axis[RT_HOST_AXES];
    struct {
        bool down;
        float x, y; /* 0..1 from the top left (SDL_GetGamepadTouchpadFinger) */
    } finger[RT_PAD_MAX_TOUCHES];
} rt_host_gamepad;

/* What rt_input_map remembers between samples: which fingers were down, and their touch ids. */
typedef struct {
    bool down[RT_PAD_MAX_TOUCHES];
    uint8_t id[RT_PAD_MAX_TOUCHES];
    uint8_t next_id;
} rt_touch_tracker;

/* The keyboard's state is indexed by USB HID usage (keyboard page 0x07), which is what SDL's
   scancodes are (SDL_scancode.h). The default layout (docs/runtime-syscalls.md, "The
   controller"):

     left stick  W A S D          right stick  arrow keys      D-pad  I J K L
     cross       Return, Space    circle       Backspace       square R    triangle F
     L1 Q   R1 E   L2 Z   R2 C    L3 left Shift   R3 Tab       options Escape   touch pad T */
typedef struct {
    uint16_t usage;
    uint32_t button; /* RT_PAD_BUTTON_*, or 0 for a stick direction */
    uint8_t stick;   /* for a stick direction: which of the four stick bytes, RT_KEY_STICK_* */
    uint8_t value;   /* and the byte it sets: 0x00 or 0xff */
    const char *name;
} rt_key_binding;
enum { RT_KEY_STICK_NONE, RT_KEY_STICK_LEFT_X, RT_KEY_STICK_LEFT_Y, RT_KEY_STICK_RIGHT_X, RT_KEY_STICK_RIGHT_Y };
extern const rt_key_binding rt_default_keys[];
extern const int rt_default_key_count;

/* The sample a host gamepad (gamepad->present false: none) and the keyboard (keys indexed by USB
   HID usage, key_count entries; keys may be NULL) give together: buttons from either, a stick
   direction a key holds overriding the gamepad's stick, triggers the larger of the two. */
void rt_input_map(const rt_host_gamepad *gamepad, const bool *keys, int key_count, rt_touch_tracker *touches,
                  rt_pad_sample *sample);

/* Host input: turns it on or off before the boot. 0, or -1 when this build has no host input
   (SDL3 was not found). */
int rt_input_host_configure(int enabled);
int rt_input_host_enabled(void);
/* Called by the display window on its event thread after it handled the window's events
   (display_vulkan.c): reads the gamepad and the keyboard into the backend's sample, and applies the
   title's vibration to the gamepad's rumble. Nothing when host input is off. */
void rt_input_host_pump(void);

#endif
