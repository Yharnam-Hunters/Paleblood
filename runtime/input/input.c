/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Controller input, host-neutral (runtime/input_host.h): the installed backend, and the mapping
   from a host gamepad and keyboard to the state the title reads. Compiled into every build; the
   SDL glue (input_host_sdl.c) only copies SDL's state in.

   Facts used:
   - Button bits: the OpenOrbis homebrew SDK's _types/pad.h (OrbisPadButton); shadPS4's
     src/core/libraries/pad/pad.h agrees.
   - Stick bytes are 0..255, 0x80 centred (the title counts a stick within 0x80 +/- the deadzone as
     centred, runtime/syslib/pad.c). The DualShock 4's report carries each stick axis as one byte,
     0 at the left and at the top (Linux, drivers/hid/hid-playstation.c, passes the bytes to
     ABS_X/ABS_Y unchanged, and evdev's convention puts up at the minimum). SDL's stick axes run
     -32768 (left, up) to 32767 (right, down) and its triggers 0 to 32767 (SDL3's
     SDL_GetGamepadAxis), so the byte is the axis' top eight bits after the offset; nothing is
     inverted.
   - Touch: the pad is 1920 x 942 (hid-playstation.c, DS4_TOUCHPAD_WIDTH and _HEIGHT), with up to
     two touches; SDL gives a finger's position as 0..1 of the pad (SDL_GetGamepadTouchpadFinger).
     A touch carries a 7-bit tracking id that changes for each new touch (hid-playstation.c reads
     it from the report's contact byte; shadPS4's src/input/controller.cpp counts it up for each
     new touch). The title reads only x and y (pad.c).

   The runtime's choices: the L2 and R2 bits are set while their trigger is off zero; the touch ids
   count 0..127 and wrap; touches are listed down fingers first, in finger order; the SDL gamepad's
   back button (Share on a DualShock 4, which reports no bit to a title) also clicks the touch pad,
   as shadPS4's src/sdl_window.cpp does. */
#include "runtime/input_host.h"

#include <stdatomic.h>
#include <stddef.h>
#include <string.h>

enum {
    AXIS_OFFSET = 32768,  /* SDL's stick axes are signed 16-bit */
    AXIS_TO_BYTE_SHIFT = 8,
    TRIGGER_MAX = 32767,  /* SDL's triggers are 0..32767 */
    BYTE_MAX = 0xff,
    TOUCH_ID_MASK = 0x7f, /* the touch id's seven bits */
};

static const rt_input_backend *_Atomic installed;

void rt_pad_sample_at_rest(rt_pad_sample *sample)
{
    memset(sample, 0, sizeof *sample);
    sample->left_x = sample->left_y = sample->right_x = sample->right_y = RT_PAD_STICK_CENTRE;
}

void rt_input_install(const rt_input_backend *backend) { atomic_store(&installed, backend); }

int rt_input_sample(rt_pad_sample *sample)
{
    const rt_input_backend *const backend = atomic_load(&installed);
    rt_pad_sample_at_rest(sample);
    if (!backend) return 0;
    backend->sample(sample);
    return 1;
}

void rt_input_vibrate(uint8_t large_motor, uint8_t small_motor)
{
    const rt_input_backend *const backend = atomic_load(&installed);
    if (backend && backend->vibrate) backend->vibrate(large_motor, small_motor);
}

/* USB HID usages, keyboard page 0x07 (USB HID Usage Tables, section 10). */
enum {
    HID_A = 0x04, HID_C = 0x06, HID_D = 0x07, HID_E = 0x08, HID_F = 0x09, HID_I = 0x0c, HID_J = 0x0d, HID_K = 0x0e,
    HID_L = 0x0f, HID_Q = 0x14, HID_R = 0x15, HID_S = 0x16, HID_T = 0x17, HID_W = 0x1a, HID_Z = 0x1d,
    HID_RETURN = 0x28, HID_ESCAPE = 0x29, HID_BACKSPACE = 0x2a, HID_TAB = 0x2b,
    HID_SPACE = 0x2c, HID_RIGHT = 0x4f, HID_LEFT = 0x50, HID_DOWN = 0x51, HID_UP = 0x52,
    HID_LEFT_SHIFT = 0xe1,
};
enum { STICK_MIN = 0x00, STICK_MAX = 0xff };

const rt_key_binding rt_default_keys[] = {
    {HID_W, 0, RT_KEY_STICK_LEFT_Y, STICK_MIN, "W: left stick up"},
    {HID_S, 0, RT_KEY_STICK_LEFT_Y, STICK_MAX, "S: left stick down"},
    {HID_A, 0, RT_KEY_STICK_LEFT_X, STICK_MIN, "A: left stick left"},
    {HID_D, 0, RT_KEY_STICK_LEFT_X, STICK_MAX, "D: left stick right"},
    {HID_UP, 0, RT_KEY_STICK_RIGHT_Y, STICK_MIN, "Up: right stick up"},
    {HID_DOWN, 0, RT_KEY_STICK_RIGHT_Y, STICK_MAX, "Down: right stick down"},
    {HID_LEFT, 0, RT_KEY_STICK_RIGHT_X, STICK_MIN, "Left: right stick left"},
    {HID_RIGHT, 0, RT_KEY_STICK_RIGHT_X, STICK_MAX, "Right: right stick right"},
    {HID_I, RT_PAD_BUTTON_UP, RT_KEY_STICK_NONE, 0, "I: D-pad up"},
    {HID_K, RT_PAD_BUTTON_DOWN, RT_KEY_STICK_NONE, 0, "K: D-pad down"},
    {HID_J, RT_PAD_BUTTON_LEFT, RT_KEY_STICK_NONE, 0, "J: D-pad left"},
    {HID_L, RT_PAD_BUTTON_RIGHT, RT_KEY_STICK_NONE, 0, "L: D-pad right"},
    {HID_RETURN, RT_PAD_BUTTON_CROSS, RT_KEY_STICK_NONE, 0, "Return: cross"},
    {HID_SPACE, RT_PAD_BUTTON_CROSS, RT_KEY_STICK_NONE, 0, "Space: cross"},
    {HID_BACKSPACE, RT_PAD_BUTTON_CIRCLE, RT_KEY_STICK_NONE, 0, "Backspace: circle"},
    {HID_R, RT_PAD_BUTTON_SQUARE, RT_KEY_STICK_NONE, 0, "R: square"},
    {HID_F, RT_PAD_BUTTON_TRIANGLE, RT_KEY_STICK_NONE, 0, "F: triangle"},
    {HID_Q, RT_PAD_BUTTON_L1, RT_KEY_STICK_NONE, 0, "Q: L1"},
    {HID_E, RT_PAD_BUTTON_R1, RT_KEY_STICK_NONE, 0, "E: R1"},
    {HID_Z, RT_PAD_BUTTON_L2, RT_KEY_STICK_NONE, 0, "Z: L2, fully pressed"},
    {HID_C, RT_PAD_BUTTON_R2, RT_KEY_STICK_NONE, 0, "C: R2, fully pressed"},
    {HID_LEFT_SHIFT, RT_PAD_BUTTON_L3, RT_KEY_STICK_NONE, 0, "left Shift: L3"},
    {HID_TAB, RT_PAD_BUTTON_R3, RT_KEY_STICK_NONE, 0, "Tab: R3"},
    {HID_ESCAPE, RT_PAD_BUTTON_OPTIONS, RT_KEY_STICK_NONE, 0, "Escape: options"},
    {HID_T, RT_PAD_BUTTON_TOUCH_PAD, RT_KEY_STICK_NONE, 0, "T: touch pad click"},
};
const int rt_default_key_count = (int)(sizeof rt_default_keys / sizeof rt_default_keys[0]);

/* The gamepad's buttons that are a bit of the state (the triggers and sticks are axes). */
static const uint32_t gamepad_buttons[RT_HOST_BUTTONS] = {
    [RT_HOST_SOUTH] = RT_PAD_BUTTON_CROSS,
    [RT_HOST_EAST] = RT_PAD_BUTTON_CIRCLE,
    [RT_HOST_WEST] = RT_PAD_BUTTON_SQUARE,
    [RT_HOST_NORTH] = RT_PAD_BUTTON_TRIANGLE,
    [RT_HOST_BACK] = RT_PAD_BUTTON_TOUCH_PAD,
    [RT_HOST_START] = RT_PAD_BUTTON_OPTIONS,
    [RT_HOST_LEFT_STICK] = RT_PAD_BUTTON_L3,
    [RT_HOST_RIGHT_STICK] = RT_PAD_BUTTON_R3,
    [RT_HOST_LEFT_SHOULDER] = RT_PAD_BUTTON_L1,
    [RT_HOST_RIGHT_SHOULDER] = RT_PAD_BUTTON_R1,
    [RT_HOST_DPAD_UP] = RT_PAD_BUTTON_UP,
    [RT_HOST_DPAD_DOWN] = RT_PAD_BUTTON_DOWN,
    [RT_HOST_DPAD_LEFT] = RT_PAD_BUTTON_LEFT,
    [RT_HOST_DPAD_RIGHT] = RT_PAD_BUTTON_RIGHT,
    [RT_HOST_TOUCHPAD] = RT_PAD_BUTTON_TOUCH_PAD,
};

static uint8_t stick_byte(int16_t axis) { return (uint8_t)((axis + AXIS_OFFSET) >> AXIS_TO_BYTE_SHIFT); }

static uint8_t trigger_byte(int16_t axis) { return axis <= 0 ? 0 : (uint8_t)(axis * BYTE_MAX / TRIGGER_MAX); }

/* A finger's position, 0..1 of the pad, as a touch coordinate 0..size - 1. */
static uint16_t touch_coordinate(float fraction, uint16_t size)
{
    if (!(fraction > 0.0f)) return 0; /* also NaN */
    const float scaled = fraction * (float)size;
    return scaled >= (float)(size - 1) ? (uint16_t)(size - 1) : (uint16_t)scaled;
}

static void map_gamepad(const rt_host_gamepad *gamepad, rt_touch_tracker *touches, rt_pad_sample *sample)
{
    for (int b = 0; b < RT_HOST_BUTTONS; b++)
        if (gamepad->button[b]) sample->buttons |= gamepad_buttons[b];
    sample->left_x = stick_byte(gamepad->axis[RT_HOST_LEFT_X]);
    sample->left_y = stick_byte(gamepad->axis[RT_HOST_LEFT_Y]);
    sample->right_x = stick_byte(gamepad->axis[RT_HOST_RIGHT_X]);
    sample->right_y = stick_byte(gamepad->axis[RT_HOST_RIGHT_Y]);
    sample->l2 = trigger_byte(gamepad->axis[RT_HOST_LEFT_TRIGGER]);
    sample->r2 = trigger_byte(gamepad->axis[RT_HOST_RIGHT_TRIGGER]);
    for (int f = 0; f < RT_PAD_MAX_TOUCHES; f++) {
        const bool down = gamepad->finger[f].down;
        if (down && !touches->down[f]) { /* a new touch: the next id */
            touches->id[f] = touches->next_id;
            touches->next_id = (uint8_t)((touches->next_id + 1) & TOUCH_ID_MASK);
        }
        touches->down[f] = down;
        if (!down) continue;
        rt_pad_touch *const touch = &sample->touches[sample->touch_count++];
        touch->x = touch_coordinate(gamepad->finger[f].x, RT_PAD_TOUCH_WIDTH);
        touch->y = touch_coordinate(gamepad->finger[f].y, RT_PAD_TOUCH_HEIGHT);
        touch->id = touches->id[f];
    }
}

/* A key-held stick direction: both of an axis' keys held cancel out, back to centre. */
static void apply_key_stick(uint8_t *axis, bool *held, bool *cancelled, uint8_t value)
{
    if (*held && *axis != value) *cancelled = true;
    *held = true;
    *axis = value;
}

static void map_keyboard(const bool *keys, int key_count, rt_pad_sample *sample)
{
    uint8_t *const stick_axis[] = {
        [RT_KEY_STICK_LEFT_X] = &sample->left_x,
        [RT_KEY_STICK_LEFT_Y] = &sample->left_y,
        [RT_KEY_STICK_RIGHT_X] = &sample->right_x,
        [RT_KEY_STICK_RIGHT_Y] = &sample->right_y,
    };
    enum { STICK_AXES = RT_KEY_STICK_RIGHT_Y + 1 };
    uint8_t key_value[STICK_AXES] = {0};
    bool held[STICK_AXES] = {false}, cancelled[STICK_AXES] = {false};
    for (int k = 0; k < rt_default_key_count; k++) {
        const rt_key_binding *const binding = &rt_default_keys[k];
        if (binding->usage >= key_count || !keys[binding->usage]) continue;
        if (binding->stick != RT_KEY_STICK_NONE)
            apply_key_stick(&key_value[binding->stick], &held[binding->stick], &cancelled[binding->stick], binding->value);
        else sample->buttons |= binding->button;
    }
    for (int s = RT_KEY_STICK_LEFT_X; s < STICK_AXES; s++)
        if (held[s]) *stick_axis[s] = cancelled[s] ? RT_PAD_STICK_CENTRE : key_value[s];
    if (sample->buttons & RT_PAD_BUTTON_L2) sample->l2 = BYTE_MAX; /* a key presses its trigger fully */
    if (sample->buttons & RT_PAD_BUTTON_R2) sample->r2 = BYTE_MAX;
}

void rt_input_map(const rt_host_gamepad *gamepad, const bool *keys, int key_count, rt_touch_tracker *touches,
                  rt_pad_sample *sample)
{
    rt_pad_sample_at_rest(sample);
    if (gamepad && gamepad->present) map_gamepad(gamepad, touches, sample);
    else memset(touches->down, 0, sizeof touches->down); /* the fingers went with the gamepad */
    if (keys) map_keyboard(keys, key_count, sample);
    if (sample->l2) sample->buttons |= RT_PAD_BUTTON_L2;
    if (sample->r2) sample->buttons |= RT_PAD_BUTTON_R2;
}
