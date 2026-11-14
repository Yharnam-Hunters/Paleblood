/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Scripted input (runtime/input_host.h, pbboot --input-script): timed presses and walks for
   unattended boots, played through the same backend interface as host input. The script format,
   its two clocks and the walk's steering are the runtime's own (input_host.h); the button bits are
   the ones input_host.h cites. */
#include "runtime/display.h"
#include "runtime/input_host.h"
#include "runtime/process.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    SCRIPT_TEXT_MAX = 1 << 16, /* a script file's size limit */
    NAME_MAX_LENGTH = 32,
    EVENT_TEXT_MAX = 256,
    FIRST_VIDEO_PORT = 1,      /* video out hands out handles from 1 */
    MICROSECONDS_PER_SECOND = 1000000,
    STICK_MIN = 0x00,
    STICK_MAX = 0xff,
    STICK_REACH = 0x7f,        /* from the centre to full deflection */
    WALK_AIM_FRAMES_MAX = 10000,
    WALK_REPORT_EVERY = 10,    /* aims between the walk's position lines */
    WALK_SIDESTEP_DEGREES = 60,
    WALK_SIDESTEP_LEVELS = 3,  /* 60, 120, 180 degrees, then again */
    REPORT_TEXT_MAX = 200,
};
#define WALK_COORDINATE_MAX 1e6

static const double degrees_per_radian = 57.29577951308232;

typedef struct {
    const char *name;
    uint32_t button; /* or 0 for a stick direction */
    uint8_t stick;   /* RT_KEY_STICK_* */
    uint8_t value;
} control;

static const control controls[] = {
    {"cross", RT_PAD_BUTTON_CROSS, RT_KEY_STICK_NONE, 0},
    {"circle", RT_PAD_BUTTON_CIRCLE, RT_KEY_STICK_NONE, 0},
    {"square", RT_PAD_BUTTON_SQUARE, RT_KEY_STICK_NONE, 0},
    {"triangle", RT_PAD_BUTTON_TRIANGLE, RT_KEY_STICK_NONE, 0},
    {"up", RT_PAD_BUTTON_UP, RT_KEY_STICK_NONE, 0},
    {"down", RT_PAD_BUTTON_DOWN, RT_KEY_STICK_NONE, 0},
    {"left", RT_PAD_BUTTON_LEFT, RT_KEY_STICK_NONE, 0},
    {"right", RT_PAD_BUTTON_RIGHT, RT_KEY_STICK_NONE, 0},
    {"l1", RT_PAD_BUTTON_L1, RT_KEY_STICK_NONE, 0},
    {"r1", RT_PAD_BUTTON_R1, RT_KEY_STICK_NONE, 0},
    {"l2", RT_PAD_BUTTON_L2, RT_KEY_STICK_NONE, 0},
    {"r2", RT_PAD_BUTTON_R2, RT_KEY_STICK_NONE, 0},
    {"l3", RT_PAD_BUTTON_L3, RT_KEY_STICK_NONE, 0},
    {"r3", RT_PAD_BUTTON_R3, RT_KEY_STICK_NONE, 0},
    {"options", RT_PAD_BUTTON_OPTIONS, RT_KEY_STICK_NONE, 0},
    {"touch", RT_PAD_BUTTON_TOUCH_PAD, RT_KEY_STICK_NONE, 0},
    {"lstick-up", 0, RT_KEY_STICK_LEFT_Y, STICK_MIN},
    {"lstick-down", 0, RT_KEY_STICK_LEFT_Y, STICK_MAX},
    {"lstick-left", 0, RT_KEY_STICK_LEFT_X, STICK_MIN},
    {"lstick-right", 0, RT_KEY_STICK_LEFT_X, STICK_MAX},
    {"rstick-up", 0, RT_KEY_STICK_RIGHT_Y, STICK_MIN},
    {"rstick-down", 0, RT_KEY_STICK_RIGHT_Y, STICK_MAX},
    {"rstick-left", 0, RT_KEY_STICK_RIGHT_X, STICK_MIN},
    {"rstick-right", 0, RT_KEY_STICK_RIGHT_X, STICK_MAX},
};

static int is_separator(char c) { return c == ',' || c == ';' || c == '\n' || c == '\r'; }

static const char *skip_blanks(const char *p)
{
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

/* A non-negative decimal number at *p; advances *p past it. */
static int parse_number(const char **p, double *value)
{
    char *end;
    errno = 0;
    const double v = strtod(*p, &end);
    if (end == *p || errno || !(v >= 0.0) || v > 1e9) return -1; /* also NaN and "inf" */
    for (const char *c = *p; c < end; c++)
        if (!isdigit((unsigned char)*c) && *c != '.') return -1; /* plain decimals: no sign, hex or exponent */
    *value = v;
    *p = end;
    return 0;
}

/* A decimal number with an optional leading minus sign (a walk's coordinates). */
static int parse_signed(const char **p, double *value)
{
    const int negative = **p == '-';
    const char *q = *p + negative;
    double magnitude;
    if (parse_number(&q, &magnitude) || magnitude > WALK_COORDINATE_MAX) return -1;
    *value = negative ? -magnitude : magnitude;
    *p = q;
    return 0;
}

/* The word at p is `word`, followed by a blank or the end. */
static int at_word(const char *p, const char *word)
{
    const size_t length = strlen(word);
    return !strncmp(p, word, length) && (p[length] == ' ' || p[length] == '\t' || !p[length]);
}

/* A walk's controls after the word `walk`: X Z [within R] [every N]. */
static int parse_walk(const char **pp, rt_input_script_event *event, const char **why)
{
    const char *p = skip_blanks(*pp);
    double x, z;
    if (parse_signed(&p, &x)) return *why = "want a walk's X after 'walk'", -1;
    p = skip_blanks(p);
    if (parse_signed(&p, &z)) return *why = "want a walk's Z after its X", -1;
    event->kind = RT_SCRIPT_WALK;
    event->target_x = (float)x, event->target_z = (float)z;
    event->within = RT_WALK_DEFAULT_WITHIN;
    event->aim_frames = RT_WALK_DEFAULT_AIM_FRAMES;
    for (p = skip_blanks(p); at_word(p, "within") || at_word(p, "every"); p = skip_blanks(p)) {
        const int within = at_word(p, "within");
        p = skip_blanks(p + (within ? strlen("within") : strlen("every")));
        double value;
        if (parse_number(&p, &value) || value <= 0.0)
            return *why = within ? "want a distance above zero after 'within'" : "want frames after 'every'", -1;
        if (within) event->within = (float)value;
        else {
            if (value != floor(value) || value > WALK_AIM_FRAMES_MAX) return *why = "want whole frames after 'every'", -1;
            event->aim_frames = (int)value;
        }
    }
    *pp = p;
    return 0;
}

static int set_control(const char *name, size_t length, rt_input_script_event *event)
{
    for (size_t c = 0; c < sizeof controls / sizeof controls[0]; c++) {
        const control *const k = &controls[c];
        if (strlen(k->name) != length || strncmp(k->name, name, length)) continue;
        if (k->button) event->buttons |= k->button;
        else {
            const uint8_t bit = (uint8_t)(1u << (k->stick - 1));
            if ((event->stick_set & bit) && event->stick[k->stick - 1] != k->value) return -1; /* both ways */
            event->stick_set |= bit;
            event->stick[k->stick - 1] = k->value;
        }
        return 0;
    }
    return -1;
}

/* One event, text without blanks around it and NUL-terminated. */
static int parse_event(const char *p, rt_input_script_event *event, const char **why)
{
    *event = (rt_input_script_event){0};
    event->after_walk = -1;
    if (*p == '+') event->chained = 1, p++;
    if (parse_number(&p, &event->start)) return *why = "want a time: SECONDS, SECONDSs, FRAMESf or +SECONDS", -1;
    event->clock = RT_SCRIPT_SECONDS;
    if (*p == 'f' && !event->chained) event->clock = RT_SCRIPT_FRAMES, p++;
    else if (*p == 's') p++;
    p = skip_blanks(p);
    if (*p != ':') return *why = event->chained ? "want ':' after a '+' time (seconds only)" : "want ':' after the time", -1;
    p = skip_blanks(p + 1);
    if (at_word(p, "walk")) {
        p += strlen("walk");
        if (parse_walk(&p, event, why)) return -1;
    }
    else
        for (;;) {
            const char *name = p;
            while ((isalnum((unsigned char)*p) || *p == '-')) p++;
            const size_t length = (size_t)(p - name);
            if (!length || length > NAME_MAX_LENGTH || set_control(name, length, event))
                return *why = "want controls such as cross or down+cross, or a walk (input_host.h lists them)", -1;
            p = skip_blanks(p);
            if (*p != '+') break;
            p = skip_blanks(p + 1);
        }
    const int walk = event->kind == RT_SCRIPT_WALK;
    if (*p == '*') {
        p = skip_blanks(p + 1);
        if (parse_number(&p, &event->hold) || event->hold <= 0.0)
            return *why = walk ? "want a time limit above zero after '*', on the walk's clock"
                               : "want a hold above zero after '*', on the event's clock", -1;
        if (event->clock == RT_SCRIPT_SECONDS && *p == 's') p++;
        else if (event->clock == RT_SCRIPT_FRAMES && *p == 'f') p++;
        p = skip_blanks(p);
    }
    else if (walk)
        event->hold = event->clock == RT_SCRIPT_FRAMES ? RT_INPUT_SCRIPT_DEFAULT_WALK_LIMIT_FRAMES
                                                       : RT_INPUT_SCRIPT_DEFAULT_WALK_LIMIT_SECONDS;
    else event->hold = event->clock == RT_SCRIPT_FRAMES ? RT_INPUT_SCRIPT_DEFAULT_HOLD_FRAMES
                                                        : RT_INPUT_SCRIPT_DEFAULT_HOLD_SECONDS;
    if (*p) return *why = "unexpected text after the event", -1;
    return 0;
}

int rt_input_script_parse(const char *text, rt_input_script *script, char *error, size_t error_size)
{
    script->count = 0;
    const char *p = text;
    int event_number = 0;
    int last_walk = -1;
    while (*p) {
        const char *start = skip_blanks(p);
        const char *end = start;
        while (*end && !is_separator(*end) && *end != '#') end++;
        const char *next = end;
        if (*next == '#')
            while (*next && *next != '\n') next++;
        if (*next) next++;
        const char *trimmed = end;
        while (trimmed > start && (trimmed[-1] == ' ' || trimmed[-1] == '\t')) trimmed--;
        p = next;
        if (trimmed == start) continue; /* an empty entry or a comment line */
        event_number++;
        if (script->count == RT_INPUT_SCRIPT_MAX_EVENTS) {
            snprintf(error, error_size, "more than %d events", RT_INPUT_SCRIPT_MAX_EVENTS);
            return -1;
        }
        const char *why = NULL;
        char event_text[EVENT_TEXT_MAX];
        const size_t length = (size_t)(trimmed - start);
        if (length >= sizeof event_text) why = "longer than an event can be";
        else {
            memcpy(event_text, start, length);
            event_text[length] = '\0';
            rt_input_script_event *const event = &script->events[script->count];
            if (!parse_event(event_text, event, &why)) {
                if (event->chained || event->kind == RT_SCRIPT_WALK) event->after_walk = last_walk;
                if (event->chained && last_walk < 0) why = "a '+' time counts from a walk: put one before it";
                if (event->kind == RT_SCRIPT_WALK) last_walk = script->count;
            }
        }
        if (why) {
            snprintf(error, error_size, "event %d, \"%.*s\": %s", event_number, (int)(trimmed - start), start, why);
            return -1;
        }
        script->count++;
    }
    if (!script->count) {
        snprintf(error, error_size, "no events");
        return -1;
    }
    return 0;
}

int rt_input_script_has_walk(const rt_input_script *script)
{
    for (int e = 0; e < script->count; e++)
        if (script->events[e].kind == RT_SCRIPT_WALK) return 1;
    return 0;
}

static double clock_now(const rt_input_script_event *event, double seconds, uint64_t frames)
{
    return event->clock == RT_SCRIPT_FRAMES ? (double)frames : seconds;
}

/* A timed event (not chained) is active from its start for its hold. */
static int event_active(const rt_input_script_event *event, double seconds, uint64_t frames)
{
    const double now = clock_now(event, seconds, frames);
    return !event->chained && event->kind == RT_SCRIPT_PRESS && now >= event->start && now < event->start + event->hold;
}

/* A `+` event is active from its delay after its walk finished, for its hold. */
static int chained_active(const rt_input_script_event *event, const rt_input_script_progress *progress, double seconds)
{
    if (!event->chained || event->kind != RT_SCRIPT_PRESS) return 0;
    const double finished = progress->walks[event->after_walk].finished_at;
    return !isnan(finished) && seconds >= finished + event->start && seconds < finished + event->start + event->hold;
}

static void press(const rt_input_script_event *event, rt_pad_sample *sample)
{
    uint8_t *const stick[] = {&sample->left_x, &sample->left_y, &sample->right_x, &sample->right_y};
    sample->buttons |= event->buttons;
    for (int s = 0; s < 4; s++)
        if (event->stick_set & (1u << s)) *stick[s] = event->stick[s];
}

static void triggers_follow_buttons(rt_pad_sample *sample)
{
    if (sample->buttons & RT_PAD_BUTTON_L2) sample->l2 = STICK_MAX; /* a scripted trigger is fully pressed */
    if (sample->buttons & RT_PAD_BUTTON_R2) sample->r2 = STICK_MAX;
}

void rt_input_script_sample(const rt_input_script *script, double seconds, uint64_t frames, rt_pad_sample *sample)
{
    rt_pad_sample_at_rest(sample);
    for (int e = 0; e < script->count; e++)
        if (event_active(&script->events[e], seconds, frames)) press(&script->events[e], sample);
    triggers_follow_buttons(sample);
}

void rt_input_script_progress_reset(rt_input_script_progress *progress)
{
    for (int e = 0; e < RT_INPUT_SCRIPT_MAX_EVENTS; e++)
        progress->walks[e] = (rt_walk_progress){.started_at = NAN, .finished_at = NAN};
}

/* ---- walks ---- */

/* An angle in degrees, wrapped into [-180, 180). */
static float wrap_degrees(float angle)
{
    angle = fmodf(angle + 180.0f, 360.0f);
    return (angle < 0.0f ? angle + 360.0f : angle) - 180.0f;
}

/* The heading of a move on the ground: degrees, atan2(dx, dz) (input_host.h). */
static float heading(float dx, float dz) { return (float)(atan2(dx, dz) * degrees_per_radian); }

/* The left stick at full deflection towards angle (degrees clockwise from up). */
static void push_left_stick(float angle, rt_pad_sample *sample)
{
    const double radians = angle / degrees_per_radian;
    sample->left_x = (uint8_t)lrint(RT_PAD_STICK_CENTRE + STICK_REACH * sin(radians));
    sample->left_y = (uint8_t)lrint(RT_PAD_STICK_CENTRE - STICK_REACH * cos(radians));
}

/* How far a sidestep of this level turns from the target's direction: 60, -60, 120, -120, 180,
   -180, then 60 again. */
static float sidestep_turn(int level)
{
    const int step = (level - 1) / 2 % WALK_SIDESTEP_LEVELS + 1;
    return (float)(level % 2 ? WALK_SIDESTEP_DEGREES * step : -WALK_SIDESTEP_DEGREES * step);
}

static void say(void (*report)(const char *), const char *format, ...) __attribute__((format(printf, 2, 3)));
static void say(void (*report)(const char *), const char *format, ...)
{
    if (!report) return;
    char line[REPORT_TEXT_MAX];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(line, sizeof line, format, arguments);
    va_end(arguments);
    report(line);
}

static void finish_walk(rt_walk_progress *walk, double seconds)
{
    walk->finished_at = seconds;
    walk->steering = 0;
}

/* One aim of a running walk: learn the camera's turn from the last move, notice being stuck, and
   point the stick at the target (or a sidestep from it). */
static void aim(const rt_input_script_event *event, int number, rt_walk_progress *walk, double seconds,
                uint64_t frames, rt_input_position_source source, void (*report)(const char *))
{
    float position[3];
    walk->aim_frame = frames;
    if (!source || source(position)) {
        walk->steering = 0; /* nothing to steer: wait with the stick at rest */
        return;
    }
    const float x = position[0], z = position[2];
    if (walk->steering) {
        const float dx = x - walk->aim_x, dz = z - walk->aim_z;
        if (hypotf(dx, dz) >= RT_WALK_MOVED) {
            walk->camera_turn = wrap_degrees(heading(dx, dz) - walk->stick_angle);
            if (walk->sidestep_aims_left > 0) walk->sidestep_aims_left--;
        }
        else {
            walk->sidestep++;
            walk->sidestep_aims_left = RT_WALK_SIDESTEP_AIMS;
            say(report, "walk %d: stuck at (%.2f, %.2f), sidestep %+.0f degrees", number, x, z,
                sidestep_turn(walk->sidestep));
        }
    }
    const float distance = hypotf(event->target_x - x, event->target_z - z);
    if (distance <= event->within) {
        finish_walk(walk, seconds);
        say(report, "walk %d: arrived at (%.2f, %.2f), %.2f from the target, at %.2f s, frame %llu", number, x, z,
            distance, seconds, (unsigned long long)frames);
        return;
    }
    if (!walk->aims || distance < walk->best_distance - RT_WALK_PROGRESS) {
        walk->best_distance = distance;
        walk->sidestep = 0, walk->sidestep_aims_left = 0;
    }
    const float turn = walk->sidestep_aims_left > 0 ? sidestep_turn(walk->sidestep) : 0.0f;
    walk->stick_angle = wrap_degrees(heading(event->target_x - x, event->target_z - z) - walk->camera_turn + turn);
    walk->aim_x = x, walk->aim_z = z;
    walk->steering = 1;
    if (++walk->aims % WALK_REPORT_EVERY == 0)
        say(report, "walk %d: at (%.2f, %.2f), %.2f from the target, frame %llu", number, x, z, distance,
            (unsigned long long)frames);
}

/* Starts, steers and ends one walk; the left stick while it steers. */
static void play_walk(const rt_input_script *script, int e, rt_input_script_progress *progress, double seconds,
                      uint64_t frames, rt_input_position_source source, void (*report)(const char *),
                      rt_pad_sample *sample)
{
    const rt_input_script_event *const event = &script->events[e];
    rt_walk_progress *const walk = &progress->walks[e];
    if (!isnan(walk->finished_at)) return;
    const double now = clock_now(event, seconds, frames);
    if (isnan(walk->started_at)) {
        const double previous = event->after_walk < 0 ? 0.0 : progress->walks[event->after_walk].finished_at;
        if (isnan(previous)) return; /* the walk before it is still going */
        if (event->chained ? seconds < previous + event->start : now < event->start) return;
        walk->started_at = now;
        say(report, "walk %d: towards (%.2f, %.2f) at %.2f s, frame %llu", e + 1, event->target_x, event->target_z,
            seconds, (unsigned long long)frames);
        aim(event, e + 1, walk, seconds, frames, source, report);
    }
    else if (now >= walk->started_at + event->hold) {
        finish_walk(walk, seconds);
        say(report, "walk %d: gave up at its limit, %.2f s, frame %llu (best %.2f from the target)", e + 1, seconds,
            (unsigned long long)frames, walk->aims ? walk->best_distance : NAN);
        return;
    }
    else if (frames >= walk->aim_frame + (uint64_t)event->aim_frames)
        aim(event, e + 1, walk, seconds, frames, source, report);
    if (walk->steering) push_left_stick(walk->stick_angle, sample);
}

void rt_input_script_play(const rt_input_script *script, rt_input_script_progress *progress, double seconds,
                          uint64_t frames, rt_input_position_source source, void (*report)(const char *line),
                          rt_pad_sample *sample)
{
    rt_pad_sample_at_rest(sample);
    for (int e = 0; e < script->count; e++)
        if (script->events[e].kind == RT_SCRIPT_WALK) play_walk(script, e, progress, seconds, frames, source, report, sample);
    for (int e = 0; e < script->count; e++) {
        const rt_input_script_event *const event = &script->events[e];
        if (event_active(event, seconds, frames) || chained_active(event, progress, seconds)) press(event, sample);
    }
    triggers_follow_buttons(sample);
}

/* The installed script, how its walks are going, and which of its events were announced. The
   pad may be read on any guest thread; the lock keeps the walks' progress whole. */
static rt_input_script played;
static rt_input_script_progress played_progress;
static rt_input_position_source position_source;
static pthread_mutex_t played_lock = PTHREAD_MUTEX_INITIALIZER;
static atomic_uchar announced[RT_INPUT_SCRIPT_MAX_EVENTS];

static void announce(double seconds, uint64_t frames)
{
    for (int e = 0; e < played.count; e++) {
        const rt_input_script_event *const event = &played.events[e];
        if (!(event_active(event, seconds, frames) || chained_active(event, &played_progress, seconds)) ||
            atomic_exchange(&announced[e], 1))
            continue;
        fprintf(stderr, "pbboot: input script: event %d starts (%.2f s, frame %llu)\n", e + 1, seconds,
                (unsigned long long)frames);
    }
}

static void report_line(const char *line) { fprintf(stderr, "pbboot: input script: %s\n", line); }

static void script_sample(rt_pad_sample *sample)
{
    const double seconds = (double)rt_process_time() / MICROSECONDS_PER_SECOND;
    const uint64_t frames = rt_display_presented(FIRST_VIDEO_PORT);
    pthread_mutex_lock(&played_lock);
    rt_input_script_play(&played, &played_progress, seconds, frames, position_source, report_line, sample);
    announce(seconds, frames);
    pthread_mutex_unlock(&played_lock);
}

void rt_input_script_set_position_source(rt_input_position_source source)
{
    pthread_mutex_lock(&played_lock);
    position_source = source;
    pthread_mutex_unlock(&played_lock);
}

int rt_input_script_needs_position(void) { return rt_input_script_has_walk(&played); }

static const rt_input_backend script_backend = {script_sample, NULL};

static char *read_script_file(const char *path, char *error, size_t error_size)
{
    FILE *const f = fopen(path, "rb");
    if (!f) {
        snprintf(error, error_size, "%s: %s", path, strerror(errno));
        return NULL;
    }
    char *const text = malloc(SCRIPT_TEXT_MAX + 1);
    const size_t n = text ? fread(text, 1, SCRIPT_TEXT_MAX + 1, f) : 0;
    const int failed = !text || ferror(f) || n > SCRIPT_TEXT_MAX || memchr(text, '\0', n);
    fclose(f);
    if (failed) {
        snprintf(error, error_size, "%s: not a script file of at most %d bytes", path, SCRIPT_TEXT_MAX);
        free(text);
        return NULL;
    }
    text[n] = '\0';
    return text;
}

int rt_input_script_configure(const char *spec, char *error, size_t error_size)
{
    char *const file_text = spec[0] == '@' ? read_script_file(spec + 1, error, error_size) : NULL;
    if (spec[0] == '@' && !file_text) return -1;
    const int parsed = rt_input_script_parse(file_text ? file_text : spec, &played, error, error_size);
    free(file_text);
    if (parsed) return -1;
    for (int e = 0; e < RT_INPUT_SCRIPT_MAX_EVENTS; e++) atomic_store(&announced[e], 0);
    rt_input_script_progress_reset(&played_progress);
    rt_input_install(&script_backend);
    return 0;
}
