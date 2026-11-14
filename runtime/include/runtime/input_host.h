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
#include <stddef.h>
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

/* Scripted input (pbboot --input-script SPEC, runtime/input/input_script.c), for unattended boots:
   timed presses instead of a person at the controller. SPEC is the script itself, or @FILE to read
   it from a file. Events are separated by commas, semicolons or new lines; `#` starts a comment
   that runs to the end of the line. An event is

     WHEN:CONTROLS[*HOLD]

   WHEN is a time on one of two clocks: seconds since the process started (`45`, `45.5s`), or the
   number of frames video out's first port (handle 1) has presented (`700f`). Frames follow the
   title's own progress, so they suit a slow boot best; seconds need no display. CONTROLS are names
   joined by `+`: cross circle square triangle up down left right l1 r1 l2 r2 l3 r3 options touch
   (the touch pad click), and stick directions lstick-up lstick-down lstick-left lstick-right and
   rstick-up ... (full deflection). The controls are held from WHEN for HOLD, on the same clock
   (default 0.25 s or 4 frames), then released; events that overlap combine. For example, down at 45 s,
   then cross at 47 s:

     pbboot ... --display headless --input-script "45:down,47:cross"

   Walks (closed-loop driving): CONTROLS may instead be

     walk X Z [within R] [every N]

   which steers the left stick towards the ground position (X, Z) until the character is within R
   (default 1) of it, re-aiming every N presented frames (default 6) from where the game says the
   character is: the position source (rt_input_position_source), which pbboot takes from the game
   library. HOLD is the walk's time limit (default 60 s or 600 frames); a walk that reaches it
   gives up, so a route goes on. Walks run one after another: a walk starts at WHEN, or when the
   walk before it in the script has finished if that is later. WHEN may also be `+SECONDS`: that
   long after the walk before the event finished (arrived or gave up), for presses that belong to
   a place rather than a time, such as opening the door the walk leads to:

     20:walk -10 4.5 within 1.5*90, +1:cross, +3:walk -12 0

   Steering knows nothing of the camera: the stick is camera-relative, so the walk learns how the
   stick's directions turn into the character's from how far and which way it moved since the
   last aim, and keeps learning as the camera turns (a walk starts from what the walk before it
   learned). Headings are measured as atan2(dx, dz) on the
   source's X and Z, with the stick's angle clockwise from up; a source whose axes run the other
   way round swaps them. A character that moved less than RT_WALK_MOVED (in the source's units)
   in an aim is stuck: the walk sidesteps, turning 60, then 120, then 180 degrees from the
   target's direction, alternating sides, for RT_WALK_SIDESTEP_AIMS aims each, until the
   character gets RT_WALK_PROGRESS closer to the target than it has been. */
enum {
    RT_INPUT_SCRIPT_MAX_EVENTS = 256,
    RT_INPUT_SCRIPT_DEFAULT_HOLD_FRAMES = 4,
};
#define RT_INPUT_SCRIPT_DEFAULT_HOLD_SECONDS 0.25

enum {
    RT_INPUT_SCRIPT_DEFAULT_WALK_LIMIT_FRAMES = 600,
    RT_WALK_DEFAULT_AIM_FRAMES = 6,
    RT_WALK_SIDESTEP_AIMS = 3,
};
#define RT_INPUT_SCRIPT_DEFAULT_WALK_LIMIT_SECONDS 60.0
#define RT_WALK_DEFAULT_WITHIN 1.0f
#define RT_WALK_MOVED 0.2f     /* less than this in one aim: stuck */
#define RT_WALK_PROGRESS 1.0f  /* this much closer than the best so far: the sidesteps are over */

typedef enum { RT_SCRIPT_SECONDS, RT_SCRIPT_FRAMES } rt_script_clock;
typedef enum { RT_SCRIPT_PRESS, RT_SCRIPT_WALK } rt_script_kind;

typedef struct {
    rt_script_clock clock;
    rt_script_kind kind;
    double start, hold;     /* on the event's clock; a walk's hold is its time limit */
    int after_walk;         /* for a `+` start or a walk: the walk before it in the script, or -1 */
    uint8_t chained;        /* the start is `+SECONDS` after that walk finished */
    uint32_t buttons;       /* RT_PAD_BUTTON_* */
    uint8_t stick_set;      /* bit RT_KEY_STICK_* - 1 for each stick byte the event sets */
    uint8_t stick[4];       /* left x, left y, right x, right y, where stick_set says */
    float target_x, target_z, within; /* a walk's target and arrival distance */
    int aim_frames;                   /* a walk re-aims every so many presented frames */
} rt_input_script_event;

typedef struct {
    int count;
    rt_input_script_event events[RT_INPUT_SCRIPT_MAX_EVENTS];
} rt_input_script;

/* Where the game's character stands, in the game's world units: 0 and position filled in, or
   nonzero when there is no character to steer (a loading screen, a menu). Called on the thread
   that reads the pad. */
typedef int (*rt_input_position_source)(float position[3]);

/* How a walk is going. finished_at and started_at are NAN until it finished or started. */
typedef struct {
    double started_at;      /* on the walk's clock */
    double finished_at;     /* seconds */
    uint64_t aim_frame;     /* the presented frame of the last aim */
    float aim_x, aim_z;     /* where the character was at the last aim */
    float best_distance;    /* the closest the character has been to the target */
    float stick_angle;      /* degrees clockwise from up */
    float camera_turn;      /* the character's heading minus the stick's angle, as last seen */
    int steering;           /* the stick is pushed (the last aim had a position) */
    int sidestep, sidestep_aims_left;
    int aims;
} rt_walk_progress;

typedef struct {
    rt_walk_progress walks[RT_INPUT_SCRIPT_MAX_EVENTS]; /* by event index; walks only */
} rt_input_script_progress;

/* Parses a script's text (not @FILE). 0, or -1 with a message in error naming the event. */
int rt_input_script_parse(const char *text, rt_input_script *script, char *error, size_t error_size);
/* Whether the script has a walk, which needs a position source. */
int rt_input_script_has_walk(const rt_input_script *script);
/* The sample at a moment, given on both clocks: the pad at rest plus every timed event that is
   active (walks and `+` events need rt_input_script_play). */
void rt_input_script_sample(const rt_input_script *script, double seconds, uint64_t frames, rt_pad_sample *sample);
/* Before playing a script from its start. */
void rt_input_script_progress_reset(rt_input_script_progress *progress);
/* The sample at a moment with walks: steers the running walk from source (NULL: none, so walks
   only wait for their limit), updates progress, and passes report one line for each thing worth a
   log line (a walk starting, sidestepping, arriving or giving up, and where the character is every
   tenth aim); report may be NULL. Timed events act on top of the walk, so a scripted stick press
   wins over the walk's. */
void rt_input_script_play(const rt_input_script *script, rt_input_script_progress *progress, double seconds,
                          uint64_t frames, rt_input_position_source source, void (*report)(const char *line),
                          rt_pad_sample *sample);
/* pbboot's --input-script: parses SPEC (reading @FILE) and installs a backend that plays it on the
   process time and video out's presented frames, writing a line to stderr when each event starts.
   0, or -1 with a message in error. */
int rt_input_script_configure(const char *spec, char *error, size_t error_size);
/* The position source walks steer from (pbboot passes the game library's bbgame_player_position),
   and whether the installed script needs one. */
void rt_input_script_set_position_source(rt_input_position_source source);
int rt_input_script_needs_position(void);

#endif
