/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Host input through SDL3 (runtime/input_host.h): the first SDL gamepad and the window's keyboard.

   SDL's events are pumped by the display window, on its own thread (display_vulkan.c); this file
   never pumps them from a guest thread. After each pump the window calls rt_input_host_pump, which
   reads the gamepad and the keyboard there and keeps the sample under a lock; the title's
   scePadReadState copies it from any thread. The state therefore changes once per presented frame.

   Vibration: the title's large motor goes to SDL's low-frequency rumble and its small motor to the
   high-frequency one (SDL3's SDL_RumbleGamepad names the low-frequency motor the left one, and the
   DualShock 4's left motor is its strong one: Linux, drivers/hid/hid-playstation.c, sends a force
   feedback effect's strong magnitude to motor_left). A speed byte becomes SDL's 16-bit intensity
   by repeating it (0xff -> 0xffff). A PS4 pad keeps vibrating until the title sets another speed;
   SDL's rumble lasts a given time, so a held speed is sent again before that time is up.

   Motion (the gamepad's accelerometer and gyroscope) is not forwarded: the units and axes the
   system reports them in are not established here (shadPS4 scales SDL's m/s^2 by 0.098 without a
   source), so the state keeps the pad level and still, as with --input none. */
#include "runtime/input_host.h"

#include <SDL3/SDL.h>
#include <pthread.h>
#include <stdatomic.h>

enum {
    RUMBLE_HOLD_MS = 0xffff,   /* the longest rumble SDL plays at once */
    RUMBLE_RESEND_MS = 30000,  /* sent again well before it ends */
    SPEED_TO_INTENSITY = 0x101 /* 0xff * 0x101 == 0xffff */
};

static int enabled, started;
static SDL_Gamepad *gamepad;
static rt_touch_tracker touches;
static pthread_mutex_t sample_lock = PTHREAD_MUTEX_INITIALIZER;
static rt_pad_sample latest = {.left_x = RT_PAD_STICK_CENTRE, .left_y = RT_PAD_STICK_CENTRE,
                               .right_x = RT_PAD_STICK_CENTRE, .right_y = RT_PAD_STICK_CENTRE};
static _Atomic uint16_t wanted_vibration; /* large motor in the low byte, small above, as rt_pad_vibration */
static uint16_t sent_vibration;
static uint64_t sent_at_ms;
static int vibration_sent;

static void sample_latest(rt_pad_sample *sample)
{
    pthread_mutex_lock(&sample_lock);
    *sample = latest;
    pthread_mutex_unlock(&sample_lock);
}

static void vibrate(uint8_t large_motor, uint8_t small_motor)
{
    atomic_store(&wanted_vibration, (uint16_t)(large_motor | small_motor << 8));
}

static const rt_input_backend sdl_backend = {sample_latest, vibrate};

int rt_input_host_configure(int on)
{
    enabled = on;
    rt_input_install(on ? &sdl_backend : NULL);
    return 0;
}

int rt_input_host_enabled(void) { return enabled; }

static const SDL_GamepadButton sdl_buttons[RT_HOST_BUTTONS] = {
    [RT_HOST_SOUTH] = SDL_GAMEPAD_BUTTON_SOUTH,
    [RT_HOST_EAST] = SDL_GAMEPAD_BUTTON_EAST,
    [RT_HOST_WEST] = SDL_GAMEPAD_BUTTON_WEST,
    [RT_HOST_NORTH] = SDL_GAMEPAD_BUTTON_NORTH,
    [RT_HOST_BACK] = SDL_GAMEPAD_BUTTON_BACK,
    [RT_HOST_START] = SDL_GAMEPAD_BUTTON_START,
    [RT_HOST_LEFT_STICK] = SDL_GAMEPAD_BUTTON_LEFT_STICK,
    [RT_HOST_RIGHT_STICK] = SDL_GAMEPAD_BUTTON_RIGHT_STICK,
    [RT_HOST_LEFT_SHOULDER] = SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,
    [RT_HOST_RIGHT_SHOULDER] = SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
    [RT_HOST_DPAD_UP] = SDL_GAMEPAD_BUTTON_DPAD_UP,
    [RT_HOST_DPAD_DOWN] = SDL_GAMEPAD_BUTTON_DPAD_DOWN,
    [RT_HOST_DPAD_LEFT] = SDL_GAMEPAD_BUTTON_DPAD_LEFT,
    [RT_HOST_DPAD_RIGHT] = SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
    [RT_HOST_TOUCHPAD] = SDL_GAMEPAD_BUTTON_TOUCHPAD,
};

static const SDL_GamepadAxis sdl_axes[RT_HOST_AXES] = {
    [RT_HOST_LEFT_X] = SDL_GAMEPAD_AXIS_LEFTX,
    [RT_HOST_LEFT_Y] = SDL_GAMEPAD_AXIS_LEFTY,
    [RT_HOST_RIGHT_X] = SDL_GAMEPAD_AXIS_RIGHTX,
    [RT_HOST_RIGHT_Y] = SDL_GAMEPAD_AXIS_RIGHTY,
    [RT_HOST_LEFT_TRIGGER] = SDL_GAMEPAD_AXIS_LEFT_TRIGGER,
    [RT_HOST_RIGHT_TRIGGER] = SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,
};

/* Keeps the first connected gamepad open; a removed one is closed and the next one taken. */
static void choose_gamepad(void)
{
    if (gamepad && SDL_GamepadConnected(gamepad)) return;
    if (gamepad) {
        SDL_CloseGamepad(gamepad);
        gamepad = NULL;
    }
    int count = 0;
    SDL_JoystickID *const ids = SDL_GetGamepads(&count);
    if (!ids) return;
    if (count > 0) {
        gamepad = SDL_OpenGamepad(ids[0]);
        vibration_sent = 0; /* a new gamepad gets the title's current speed */
    }
    SDL_free(ids);
}

static void read_gamepad(rt_host_gamepad *state)
{
    state->present = gamepad != NULL;
    if (!gamepad) return;
    for (int b = 0; b < RT_HOST_BUTTONS; b++) state->button[b] = SDL_GetGamepadButton(gamepad, sdl_buttons[b]);
    for (int a = 0; a < RT_HOST_AXES; a++) state->axis[a] = SDL_GetGamepadAxis(gamepad, sdl_axes[a]);
    if (SDL_GetNumGamepadTouchpads(gamepad) < 1) return; /* no touch pad: no fingers */
    const int fingers = SDL_GetNumGamepadTouchpadFingers(gamepad, 0);
    for (int f = 0; f < RT_PAD_MAX_TOUCHES && f < fingers; f++) {
        float pressure;
        if (!SDL_GetGamepadTouchpadFinger(gamepad, 0, f, &state->finger[f].down, &state->finger[f].x,
                                          &state->finger[f].y, &pressure))
            state->finger[f].down = false;
    }
}

static void apply_vibration(void)
{
    if (!gamepad) return;
    const uint16_t wanted = atomic_load(&wanted_vibration);
    const uint64_t now = SDL_GetTicks();
    const int held = wanted && now - sent_at_ms >= RUMBLE_RESEND_MS;
    if (vibration_sent && wanted == sent_vibration && !held) return;
    const uint16_t large = (uint16_t)((wanted & 0xff) * SPEED_TO_INTENSITY);
    const uint16_t small = (uint16_t)((wanted >> 8) * SPEED_TO_INTENSITY);
    SDL_RumbleGamepad(gamepad, large, small, RUMBLE_HOLD_MS); /* false: this gamepad has no rumble */
    sent_vibration = wanted, sent_at_ms = now, vibration_sent = 1;
}

void rt_input_host_pump(void)
{
    if (!enabled) return;
    if (!started) {
        /* Gamepads are optional: without the subsystem the keyboard still works. */
        started = SDL_InitSubSystem(SDL_INIT_GAMEPAD) ? 1 : -1;
    }
    if (started > 0) choose_gamepad();
    rt_host_gamepad state = {0};
    read_gamepad(&state);
    int key_count = 0;
    const bool *const keys = SDL_GetKeyboardState(&key_count);
    rt_pad_sample sample;
    rt_input_map(&state, keys, key_count, &touches, &sample);
    pthread_mutex_lock(&sample_lock);
    latest = sample;
    pthread_mutex_unlock(&sample_lock);
    apply_vibration();
}
