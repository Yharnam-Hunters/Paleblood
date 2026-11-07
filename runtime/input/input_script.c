/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Scripted input (runtime/input_host.h, pbboot --input-script): timed presses for unattended
   boots, played through the same backend interface as host input. The script format and its two
   clocks are the runtime's own (input_host.h); the button bits are the ones input_host.h cites. */
#include "runtime/display.h"
#include "runtime/input_host.h"
#include "runtime/process.h"

#include <ctype.h>
#include <errno.h>
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
};

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
    if (parse_number(&p, &event->start)) return *why = "want a time: SECONDS, SECONDSs or FRAMESf", -1;
    event->clock = RT_SCRIPT_SECONDS;
    if (*p == 'f') event->clock = RT_SCRIPT_FRAMES, p++;
    else if (*p == 's') p++;
    p = skip_blanks(p);
    if (*p != ':') return *why = "want ':' after the time", -1;
    p = skip_blanks(p + 1);
    for (;;) {
        const char *name = p;
        while ((isalnum((unsigned char)*p) || *p == '-')) p++;
        const size_t length = (size_t)(p - name);
        if (!length || length > NAME_MAX_LENGTH || set_control(name, length, event))
            return *why = "want controls such as cross or down+cross (input_host.h lists them)", -1;
        p = skip_blanks(p);
        if (*p != '+') break;
        p = skip_blanks(p + 1);
    }
    if (*p == '*') {
        p = skip_blanks(p + 1);
        if (parse_number(&p, &event->hold) || event->hold <= 0.0)
            return *why = "want a hold above zero after '*', on the event's clock", -1;
        if (event->clock == RT_SCRIPT_SECONDS && *p == 's') p++;
        else if (event->clock == RT_SCRIPT_FRAMES && *p == 'f') p++;
        p = skip_blanks(p);
    }
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
            parse_event(event_text, &script->events[script->count], &why);
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

static int event_active(const rt_input_script_event *event, double seconds, uint64_t frames)
{
    const double now = event->clock == RT_SCRIPT_FRAMES ? (double)frames : seconds;
    return now >= event->start && now < event->start + event->hold;
}

void rt_input_script_sample(const rt_input_script *script, double seconds, uint64_t frames, rt_pad_sample *sample)
{
    rt_pad_sample_at_rest(sample);
    uint8_t *const stick[] = {&sample->left_x, &sample->left_y, &sample->right_x, &sample->right_y};
    for (int e = 0; e < script->count; e++) {
        const rt_input_script_event *const event = &script->events[e];
        if (!event_active(event, seconds, frames)) continue;
        sample->buttons |= event->buttons;
        for (int s = 0; s < 4; s++)
            if (event->stick_set & (1u << s)) *stick[s] = event->stick[s];
    }
    if (sample->buttons & RT_PAD_BUTTON_L2) sample->l2 = STICK_MAX; /* a scripted trigger is fully pressed */
    if (sample->buttons & RT_PAD_BUTTON_R2) sample->r2 = STICK_MAX;
}

/* The installed script, and which of its events were announced. */
static rt_input_script played;
static atomic_uchar announced[RT_INPUT_SCRIPT_MAX_EVENTS];

static void announce(double seconds, uint64_t frames)
{
    for (int e = 0; e < played.count; e++) {
        if (!event_active(&played.events[e], seconds, frames) || atomic_exchange(&announced[e], 1)) continue;
        fprintf(stderr, "pbboot: input script: event %d starts (%.2f s, frame %llu)\n", e + 1, seconds,
                (unsigned long long)frames);
    }
}

static void script_sample(rt_pad_sample *sample)
{
    const double seconds = (double)rt_process_time() / MICROSECONDS_PER_SECOND;
    const uint64_t frames = rt_display_presented(FIRST_VIDEO_PORT);
    announce(seconds, frames);
    rt_input_script_sample(&played, seconds, frames, sample);
}

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
    rt_input_install(&script_backend);
    return 0;
}
