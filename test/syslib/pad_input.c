/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Controller input (runtime/input_host.h, runtime/input/input.c, runtime/syslib/pad.c): the
   mapping from a host gamepad and keyboard to the title's state, and scePadReadState and
   scePadSetVibration through a fake backend; no host device is needed. Mutant checks run the same
   checks on deliberately broken mappings (sticks' axes swapped, motors swapped) and expect them to
   fail, so the checks are known to catch those mistakes. */
#include "../syslib_test.h"

#include "runtime/input_host.h"
#include "runtime/pad.h"

#include <stdint.h>
#include <string.h>

enum { KEYS = 512 };
enum { OFFSET_TOUCH_COUNT = 0x34, OFFSET_TOUCHES = 0x3c, OFFSET_CONNECTED = 0x4c, OFFSET_TIMESTAMP = 0x50 };

typedef void (*map_fn)(const rt_host_gamepad *, const bool *, int, rt_touch_tracker *, rt_pad_sample *);

/* Mutants: the real mapping, then a mistake made on purpose. */
static void map_swapped_left_stick(const rt_host_gamepad *g, const bool *k, int n, rt_touch_tracker *t, rt_pad_sample *s)
{
    rt_input_map(g, k, n, t, s);
    const uint8_t x = s->left_x;
    s->left_x = s->left_y, s->left_y = x;
}

static void map_swapped_right_stick(const rt_host_gamepad *g, const bool *k, int n, rt_touch_tracker *t, rt_pad_sample *s)
{
    rt_input_map(g, k, n, t, s);
    const uint8_t x = s->right_x;
    s->right_x = s->right_y, s->right_y = x;
}

/* The sticks: each axis on its own, at both ends and centred, from the gamepad and the keys.
   Returns the number of wrong bytes. */
static int stick_mistakes(map_fn map)
{
    int wrong = 0;
    rt_touch_tracker touches = {0};
    rt_pad_sample s;
    static const struct { int axis; int16_t value; int field; uint8_t want; } cases[] = {
        {RT_HOST_LEFT_X, -32768, 0, 0x00}, {RT_HOST_LEFT_X, 32767, 0, 0xff}, {RT_HOST_LEFT_X, 0, 0, 0x80},
        {RT_HOST_LEFT_Y, -32768, 1, 0x00}, {RT_HOST_LEFT_Y, 32767, 1, 0xff}, {RT_HOST_LEFT_Y, 256, 1, 0x81},
        {RT_HOST_RIGHT_X, -32768, 2, 0x00}, {RT_HOST_RIGHT_X, 32767, 2, 0xff},
        {RT_HOST_RIGHT_Y, -32768, 3, 0x00}, {RT_HOST_RIGHT_Y, 32767, 3, 0xff},
    };
    for (size_t c = 0; c < sizeof cases / sizeof cases[0]; c++) {
        rt_host_gamepad g = {.present = true};
        g.axis[cases[c].axis] = cases[c].value;
        map(&g, NULL, 0, &touches, &s);
        const uint8_t got[4] = {s.left_x, s.left_y, s.right_x, s.right_y};
        for (int f = 0; f < 4; f++) wrong += got[f] != (f == cases[c].field ? cases[c].want : 0x80);
    }
    /* W: left stick up; D: left stick right; Up: right stick up; Right: right stick right. */
    static const struct { uint16_t usage; int field; uint8_t want; } keyed[] = {
        {0x1a, 1, 0x00}, {0x07, 0, 0xff}, {0x52, 3, 0x00}, {0x4f, 2, 0xff},
    };
    for (size_t c = 0; c < sizeof keyed / sizeof keyed[0]; c++) {
        bool keys[KEYS] = {false};
        keys[keyed[c].usage] = true;
        map(NULL, keys, KEYS, &touches, &s);
        const uint8_t got[4] = {s.left_x, s.left_y, s.right_x, s.right_y};
        for (int f = 0; f < 4; f++) wrong += got[f] != (f == keyed[c].field ? keyed[c].want : 0x80);
    }
    return wrong;
}

SYSLIB_TEST(pad_input_sticks)
{
    CHECK(stick_mistakes(rt_input_map) == 0);
    CHECK(stick_mistakes(map_swapped_left_stick) > 0);  /* mutant: caught */
    CHECK(stick_mistakes(map_swapped_right_stick) > 0); /* mutant: caught */
}

SYSLIB_TEST(pad_input_buttons)
{
    static const struct { int button; uint32_t bit; } gamepad[] = {
        {RT_HOST_SOUTH, 0x4000},          {RT_HOST_EAST, 0x2000},          {RT_HOST_WEST, 0x8000},
        {RT_HOST_NORTH, 0x1000},          {RT_HOST_START, 0x0008},         {RT_HOST_LEFT_STICK, 0x0002},
        {RT_HOST_RIGHT_STICK, 0x0004},    {RT_HOST_LEFT_SHOULDER, 0x0400}, {RT_HOST_RIGHT_SHOULDER, 0x0800},
        {RT_HOST_DPAD_UP, 0x0010},        {RT_HOST_DPAD_RIGHT, 0x0020},    {RT_HOST_DPAD_DOWN, 0x0040},
        {RT_HOST_DPAD_LEFT, 0x0080},      {RT_HOST_TOUCHPAD, 0x100000},    {RT_HOST_BACK, 0x100000},
    };
    rt_touch_tracker touches = {0};
    rt_pad_sample s;
    for (size_t c = 0; c < sizeof gamepad / sizeof gamepad[0]; c++) {
        rt_host_gamepad g = {.present = true};
        g.button[gamepad[c].button] = true;
        rt_input_map(&g, NULL, 0, &touches, &s);
        CHECK(s.buttons == gamepad[c].bit);
        CHECK(s.left_x == 0x80 && s.right_y == 0x80 && s.l2 == 0 && s.r2 == 0 && s.touch_count == 0);
    }
    /* Triggers: analog, and the L2/R2 bits while off zero. */
    rt_host_gamepad g = {.present = true};
    g.axis[RT_HOST_LEFT_TRIGGER] = 32767;
    rt_input_map(&g, NULL, 0, &touches, &s);
    CHECK(s.l2 == 0xff && s.r2 == 0 && s.buttons == 0x0100);
    g.axis[RT_HOST_LEFT_TRIGGER] = 0;
    g.axis[RT_HOST_RIGHT_TRIGGER] = 16384;
    rt_input_map(&g, NULL, 0, &touches, &s);
    CHECK(s.l2 == 0 && s.r2 == 127 && s.buttons == 0x0200);

    /* The keyboard's default layout: every pad button has a key, no key is bound twice. */
    uint32_t bound = 0;
    for (int k = 0; k < rt_default_key_count; k++) {
        bound |= rt_default_keys[k].button;
        for (int j = 0; j < k; j++) CHECK(rt_default_keys[j].usage != rt_default_keys[k].usage);
    }
    CHECK(bound == 0x10fffe);
    static const struct { uint16_t usage; uint32_t bit; } keyed[] = {
        {0x28, 0x4000}, {0x2c, 0x4000}, {0x2a, 0x2000}, {0x15, 0x8000}, {0x09, 0x1000}, {0x29, 0x0008},
        {0x14, 0x0400}, {0x08, 0x0800}, {0xe1, 0x0002}, {0x2b, 0x0004}, {0x0c, 0x0010}, {0x0e, 0x0040},
        {0x0d, 0x0080}, {0x0f, 0x0020}, {0x17, 0x100000},
    };
    for (size_t c = 0; c < sizeof keyed / sizeof keyed[0]; c++) {
        bool keys[KEYS] = {false};
        keys[keyed[c].usage] = true;
        rt_input_map(NULL, keys, KEYS, &touches, &s);
        CHECK(s.buttons == keyed[c].bit && s.l2 == 0 && s.r2 == 0);
    }
    bool keys[KEYS] = {false};
    keys[0x1d] = keys[0x06] = true; /* Z, C: L2 and R2 fully pressed */
    rt_input_map(NULL, keys, KEYS, &touches, &s);
    CHECK(s.buttons == 0x0300 && s.l2 == 0xff && s.r2 == 0xff);
    /* Both keys of an axis cancel out; a held key overrides the gamepad's stick; buttons combine. */
    memset(keys, 0, sizeof keys);
    keys[0x1a] = keys[0x16] = true; /* W and S */
    keys[0x04] = true;              /* A */
    keys[0x28] = true;              /* Return: cross */
    rt_host_gamepad both = {.present = true};
    both.axis[RT_HOST_LEFT_X] = 32767;
    both.axis[RT_HOST_LEFT_Y] = 32767;
    both.button[RT_HOST_EAST] = true;
    rt_input_map(&both, keys, KEYS, &touches, &s);
    CHECK(s.left_y == 0x80 && s.left_x == 0x00 && s.buttons == (0x4000 | 0x2000));
    /* A key past the keyboard's reported size is not read. */
    rt_input_map(NULL, keys, 0x04, &touches, &s);
    CHECK(s.buttons == 0 && s.left_x == 0x80);
}

SYSLIB_TEST(pad_input_touch)
{
    rt_touch_tracker touches = {0};
    rt_pad_sample s;
    rt_host_gamepad g = {.present = true};
    g.finger[0].down = true, g.finger[0].x = 0.5f, g.finger[0].y = 0.5f;
    rt_input_map(&g, NULL, 0, &touches, &s);
    CHECK(s.touch_count == 1 && s.touches[0].x == 960 && s.touches[0].y == 471 && s.touches[0].id == 0);
    rt_input_map(&g, NULL, 0, &touches, &s); /* still down: the same touch */
    CHECK(s.touch_count == 1 && s.touches[0].id == 0);
    g.finger[1].down = true, g.finger[1].x = 1.0f, g.finger[1].y = 0.0f;
    rt_input_map(&g, NULL, 0, &touches, &s);
    CHECK(s.touch_count == 2 && s.touches[1].x == 1919 && s.touches[1].y == 0 && s.touches[1].id == 1);
    g.finger[0].down = false; /* the first finger lifts: the second is listed first */
    rt_input_map(&g, NULL, 0, &touches, &s);
    CHECK(s.touch_count == 1 && s.touches[0].x == 1919 && s.touches[0].id == 1);
    g.finger[0].down = true, g.finger[0].x = 0.0f, g.finger[0].y = 1.0f; /* a new touch, a new id */
    rt_input_map(&g, NULL, 0, &touches, &s);
    CHECK(s.touch_count == 2 && s.touches[0].id == 2 && s.touches[0].y == 941 && s.touches[1].id == 1);
    touches.next_id = 0x7f;
    g.finger[0].down = g.finger[1].down = false;
    rt_input_map(&g, NULL, 0, &touches, &s);
    CHECK(s.touch_count == 0);
    g.finger[0].down = g.finger[1].down = true;
    rt_input_map(&g, NULL, 0, &touches, &s);
    CHECK(s.touches[0].id == 0x7f && s.touches[1].id == 0); /* seven bits, wrapping */
}

/* A fake backend: hands out a set sample and records the motor speeds. */
static rt_pad_sample fake_sample;
static int fake_large = -1, fake_small = -1;
static void fake_read(rt_pad_sample *s) { *s = fake_sample; }
static void fake_vibrate(uint8_t large, uint8_t small) { fake_large = large, fake_small = small; }
static void fake_vibrate_swapped(uint8_t large, uint8_t small) { fake_vibrate(small, large); } /* mutant */

typedef int32_t (*f_pad_open)(int32_t, int32_t, int32_t, const void *);
typedef int32_t (*f_pad_handle)(int32_t);
typedef int32_t (*f_pad_block)(int32_t, void *);
typedef int32_t (*f_pad_vibrate)(int32_t, const uint8_t *);

/* Whether scePadSetVibration reached the backend as large motor, small motor. */
static int vibration_forwarded(f_pad_vibrate vibrate, int32_t handle)
{
    const uint8_t speeds[2] = {0xc0, 0x40};
    fake_large = fake_small = -1;
    return vibrate(handle, speeds) == 0 && fake_large == 0xc0 && fake_small == 0x40 && rt_pad_vibration() == 0x40c0;
}

SYSLIB_TEST(pad_input_read_state)
{
    const f_pad_open pad_open = (f_pad_open)fn("libScePad", "scePadOpen");
    const f_pad_handle pad_close = (f_pad_handle)fn("libScePad", "scePadClose");
    const f_pad_block pad_read = (f_pad_block)fn("libScePad", "scePadReadState");
    const f_pad_vibrate pad_vibrate = (f_pad_vibrate)fn("libScePad", "scePadSetVibration");
    CHECK(pad_open && pad_close && pad_read && pad_vibrate);
    if (!(pad_open && pad_close && pad_read && pad_vibrate)) return;
    const int32_t handle = pad_open(1, 0, 0, NULL); /* main's tests initialized the library */
    CHECK(handle >= 0);
    rt_pad_sample_at_rest(&fake_sample);
    fake_sample.buttons = 0x4000 | 0x0008;
    fake_sample.left_x = 0x10, fake_sample.left_y = 0x20, fake_sample.right_x = 0x30, fake_sample.right_y = 0x40;
    fake_sample.l2 = 0x50, fake_sample.r2 = 0x60;
    fake_sample.touch_count = 2;
    fake_sample.touches[0] = (rt_pad_touch){.x = 1000, .y = 500, .id = 7};
    fake_sample.touches[1] = (rt_pad_touch){.x = 3, .y = 941, .id = 8};
    const rt_input_backend fake = {fake_read, fake_vibrate};
    rt_input_install(&fake);
    unsigned char state[128];
    memset(state, 0xee, sizeof state);
    CHECK(pad_read(handle, state) == 0);
    uint32_t buttons;
    uint16_t x0, y0, x1, y1;
    uint64_t timestamp;
    float w;
    memcpy(&buttons, state, 4);
    memcpy(&w, state + 24, 4);
    memcpy(&x0, state + OFFSET_TOUCHES, 2);
    memcpy(&y0, state + OFFSET_TOUCHES + 2, 2);
    memcpy(&x1, state + OFFSET_TOUCHES + 8, 2);
    memcpy(&y1, state + OFFSET_TOUCHES + 10, 2);
    memcpy(&timestamp, state + OFFSET_TIMESTAMP, 8);
    CHECK(buttons == 0x4008);
    CHECK(state[4] == 0x10 && state[5] == 0x20 && state[6] == 0x30 && state[7] == 0x40); /* left x, y, right x, y */
    CHECK(state[8] == 0x50 && state[9] == 0x60 && w == 1.0f);
    CHECK(state[OFFSET_TOUCH_COUNT] == 2 && x0 == 1000 && y0 == 500 && state[OFFSET_TOUCHES + 4] == 7);
    CHECK(x1 == 3 && y1 == 941 && state[OFFSET_TOUCHES + 12] == 8);
    CHECK(state[OFFSET_CONNECTED] == 1 && timestamp > 0 && state[120] == 0xee);

    CHECK(vibration_forwarded(pad_vibrate, handle));
    const rt_input_backend swapped = {fake_read, fake_vibrate_swapped};
    rt_input_install(&swapped);
    CHECK(!vibration_forwarded(pad_vibrate, handle)); /* mutant: caught */

    rt_input_install(NULL); /* no backend: at rest again, no timestamp */
    CHECK(pad_read(handle, state) == 0);
    memcpy(&buttons, state, 4);
    memcpy(&timestamp, state + OFFSET_TIMESTAMP, 8);
    CHECK(buttons == 0 && state[4] == 0x80 && state[7] == 0x80 && state[8] == 0 && state[OFFSET_TOUCH_COUNT] == 0);
    CHECK(timestamp == 0 && state[OFFSET_CONNECTED] == 1);
    const uint8_t stop[2] = {0, 0};
    fake_large = -1;
    CHECK(pad_vibrate(handle, stop) == 0 && fake_large == -1 && rt_pad_vibration() == 0);
    CHECK(pad_close(handle) == 0);
    /* Builds without SDL3 refuse host input. */
    CHECK(rt_input_host_configure(1) == -1 && !rt_input_host_enabled() && rt_input_host_configure(0) == 0);
}
