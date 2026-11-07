/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Scripted input (runtime/input_host.h, runtime/input/input_script.c): parsing, timing on both
   clocks, refusals, and the installed backend read through rt_input_sample. */
#include "../syslib_test.h"

#include "runtime/input_host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static rt_input_script script;
static char error[256];

static uint32_t buttons_at(double seconds, uint64_t frames)
{
    rt_pad_sample s;
    rt_input_script_sample(&script, seconds, frames, &s);
    return s.buttons;
}

SYSLIB_TEST(input_script_parse_and_timing)
{
    CHECK(rt_input_script_parse("45:down, 47.5s:cross*1 ;700f:circle+square\n 800f : r2 * 10f # a comment\n", &script,
                                error, sizeof error) == 0);
    CHECK(script.count == 4);
    CHECK(script.events[0].clock == RT_SCRIPT_SECONDS && script.events[0].start == 45.0 && script.events[0].hold == 0.25);
    CHECK(script.events[1].start == 47.5 && script.events[1].hold == 1.0);
    CHECK(script.events[2].clock == RT_SCRIPT_FRAMES && script.events[2].start == 700.0 && script.events[2].hold == 4.0);
    CHECK(script.events[3].hold == 10.0);
    /* Seconds: held from the start for the hold, then released. */
    CHECK(buttons_at(44.99, 0) == 0);
    CHECK(buttons_at(45.0, 0) == RT_PAD_BUTTON_DOWN);
    CHECK(buttons_at(45.24, 0) == RT_PAD_BUTTON_DOWN);
    CHECK(buttons_at(45.25, 0) == 0);
    CHECK(buttons_at(48.4, 0) == RT_PAD_BUTTON_CROSS && buttons_at(48.5, 0) == 0);
    /* Frames: their own clock, whatever the seconds say. */
    CHECK(buttons_at(0, 699) == 0);
    CHECK(buttons_at(0, 700) == (RT_PAD_BUTTON_CIRCLE | RT_PAD_BUTTON_SQUARE));
    CHECK(buttons_at(0, 703) == (RT_PAD_BUTTON_CIRCLE | RT_PAD_BUTTON_SQUARE) && buttons_at(0, 704) == 0);
    /* Overlapping events combine; a scripted trigger is fully pressed. */
    rt_pad_sample s;
    rt_input_script_sample(&script, 45.1, 805, &s);
    CHECK(s.buttons == (RT_PAD_BUTTON_DOWN | RT_PAD_BUTTON_R2) && s.r2 == 0xff && s.l2 == 0);
    CHECK(s.left_x == 0x80 && s.left_y == 0x80 && s.touch_count == 0);
}

SYSLIB_TEST(input_script_sticks)
{
    CHECK(rt_input_script_parse("1:lstick-up+rstick-right,1:lstick-left", &script, error, sizeof error) == 0);
    rt_pad_sample s;
    rt_input_script_sample(&script, 1.1, 0, &s);
    CHECK(s.left_y == 0x00 && s.left_x == 0x00 && s.right_x == 0xff && s.right_y == 0x80 && s.buttons == 0);
    rt_input_script_sample(&script, 2.0, 0, &s);
    CHECK(s.left_y == 0x80 && s.left_x == 0x80 && s.right_x == 0x80);
    /* One event pushing a stick both ways is refused. */
    CHECK(rt_input_script_parse("1:lstick-up+lstick-down", &script, error, sizeof error) == -1);
}

SYSLIB_TEST(input_script_refusals)
{
    static const char *const bad[] = {
        "",            /* no events */
        "# only a comment\n",
        "cross",       /* no time */
        "-1:cross",    /* negative */
        "inf:cross",
        "nan:cross",
        "0x10:cross",  /* not decimal */
        "5 cross",     /* no colon */
        "5:",          /* no controls */
        "5:jump",      /* an unknown control */
        "5:cross+",
        "5:cross*0",   /* no hold */
        "5:cross*2f",  /* a hold on the other clock */
        "5f:cross*2s",
        "5:cross extra",
    };
    for (size_t b = 0; b < sizeof bad / sizeof bad[0]; b++) {
        error[0] = '\0';
        const int refused = rt_input_script_parse(bad[b], &script, error, sizeof error) == -1 && error[0];
        if (!refused) fprintf(stderr, "input script accepted: \"%s\"\n", bad[b]);
        CHECK(refused);
    }
    /* The message names the event. */
    CHECK(rt_input_script_parse("1:cross,2:jump", &script, error, sizeof error) == -1 && strstr(error, "event 2"));
    /* At most RT_INPUT_SCRIPT_MAX_EVENTS events. */
    char *many = malloc((RT_INPUT_SCRIPT_MAX_EVENTS + 1) * 8 + 1);
    many[0] = '\0';
    for (int e = 0; e <= RT_INPUT_SCRIPT_MAX_EVENTS; e++) strcat(many, "1:cross,");
    CHECK(rt_input_script_parse(many, &script, error, sizeof error) == -1);
    many[strlen(many) - 8] = '\0';
    CHECK(rt_input_script_parse(many, &script, error, sizeof error) == 0 && script.count == RT_INPUT_SCRIPT_MAX_EVENTS);
    free(many);
}

SYSLIB_TEST(input_script_backend)
{
    /* From a file: cross held from the process' start for an hour, triangle long after. */
    char path[] = "/tmp/pb-input-script-XXXXXX";
    const int fd = mkstemp(path);
    CHECK(fd >= 0);
    static const char text[] = "# test script\n0:cross*3600\n100000:triangle\n";
    CHECK(write(fd, text, sizeof text - 1) == (ssize_t)(sizeof text - 1));
    close(fd);
    char spec[64];
    snprintf(spec, sizeof spec, "@%s", path);
    CHECK(rt_input_script_configure(spec, error, sizeof error) == 0);
    unlink(path);
    rt_pad_sample s;
    CHECK(rt_input_sample(&s) == 1 && s.buttons == RT_PAD_BUTTON_CROSS);
    rt_input_install(NULL);
    CHECK(rt_input_sample(&s) == 0 && s.buttons == 0);
    /* A missing file and a bad inline script are refused and install nothing. */
    CHECK(rt_input_script_configure("@/nonexistent/pb-input-script", error, sizeof error) == -1);
    CHECK(rt_input_script_configure("0:nothing", error, sizeof error) == -1);
    CHECK(rt_input_sample(&s) == 0);
}
