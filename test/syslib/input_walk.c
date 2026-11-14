/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Scripted walks (runtime/input_host.h, runtime/input/input_script.c): parsing, and the steering
   played against a synthetic world: a character that moves where the stick points, turned by a
   camera the walk is not told about, with walls, a drifting camera, and no position at all. */
#include "../syslib_test.h"

#include "runtime/input_host.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static rt_input_script script;
static rt_input_script_progress progress;
static char error[256];

/* ---- the synthetic world ---- */

enum { FRAMES_PER_SECOND = 30 };
static const double pi = 3.14159265358979323846;

typedef struct {
    float x, z;
    float camera_turn;       /* degrees: the character's heading minus the stick's angle */
    float camera_drift;      /* degrees per frame */
    float speed;             /* per frame at full deflection */
    int has_position;
    /* A wall along z = wall_z from wall_x0 to wall_x1 (none when wall_x0 == wall_x1). */
    float wall_z, wall_x0, wall_x1;
    int enclosed;            /* the character cannot move at all */
} world;

static world here;
static int arrived_lines, stuck_lines, gave_up_lines;

static int synthetic_position(float position[3])
{
    if (!here.has_position) return -1;
    position[0] = here.x, position[1] = 7.0f, position[2] = here.z;
    return 0;
}

static void count_line(const char *line)
{
    arrived_lines += strstr(line, "arrived") != NULL;
    stuck_lines += strstr(line, "stuck") != NULL;
    gave_up_lines += strstr(line, "gave up") != NULL;
}

/* Moves the character one frame along the stick, unless a wall is in the way. */
static void move(const rt_pad_sample *s)
{
    const double sx = (s->left_x - RT_PAD_STICK_CENTRE) / 127.0, sy = (RT_PAD_STICK_CENTRE - s->left_y) / 127.0;
    const double deflection = hypot(sx, sy);
    here.camera_turn += here.camera_drift;
    if (deflection < 0.5 || here.enclosed) return;
    const double heading = atan2(sx, sy) + here.camera_turn * pi / 180.0;
    const float nx = here.x + here.speed * (float)sin(heading), nz = here.z + here.speed * (float)cos(heading);
    const int crosses = (here.z - here.wall_z) * (nz - here.wall_z) <= 0.0f && here.z != nz;
    if (here.wall_x0 != here.wall_x1 && crosses && nx >= here.wall_x0 && nx <= here.wall_x1) return;
    here.x = nx, here.z = nz;
}

/* Plays the script from frame 0 for at most frames frames; the frame the last walk finished, or -1. */
static long run(long frames, uint32_t *buttons_seen)
{
    rt_input_script_progress_reset(&progress);
    arrived_lines = stuck_lines = gave_up_lines = 0;
    long finished = -1;
    for (long f = 0; f < frames; f++) {
        rt_pad_sample s;
        rt_input_script_play(&script, &progress, (double)f / FRAMES_PER_SECOND, (uint64_t)f, synthetic_position,
                             count_line, &s);
        if (buttons_seen) *buttons_seen |= s.buttons;
        move(&s);
        int last = -1;
        for (int e = 0; e < script.count; e++)
            if (script.events[e].kind == RT_SCRIPT_WALK) last = e;
        if (finished < 0 && last >= 0 && !isnan(progress.walks[last].finished_at)) finished = f;
    }
    return finished;
}

static world open_ground(float camera_turn)
{
    return (world){.x = 0.0f, .z = 0.0f, .camera_turn = camera_turn, .speed = 0.1f, .has_position = 1};
}

/* ---- tests ---- */

SYSLIB_TEST(input_walk_parse)
{
    CHECK(rt_input_script_parse("20:walk -195.6 220.3 within 1.5 every 4*90, +1:cross, +2.5s:walk 3 -4\n900f:walk 1 2",
                                &script, error, sizeof error) == 0);
    CHECK(script.count == 4 && rt_input_script_has_walk(&script));
    const rt_input_script_event *w = &script.events[0];
    CHECK(w->kind == RT_SCRIPT_WALK && w->clock == RT_SCRIPT_SECONDS && w->start == 20.0 && w->hold == 90.0);
    CHECK(w->target_x == -195.6f && w->target_z == 220.3f && w->within == 1.5f && w->aim_frames == 4);
    CHECK(w->after_walk == -1 && !w->chained);
    CHECK(script.events[1].kind == RT_SCRIPT_PRESS && script.events[1].chained && script.events[1].after_walk == 0);
    CHECK(script.events[1].start == 1.0 && script.events[1].buttons == RT_PAD_BUTTON_CROSS);
    const rt_input_script_event *w2 = &script.events[2];
    CHECK(w2->chained && w2->after_walk == 0 && w2->start == 2.5 && w2->target_x == 3.0f && w2->target_z == -4.0f);
    CHECK(w2->within == RT_WALK_DEFAULT_WITHIN && w2->aim_frames == RT_WALK_DEFAULT_AIM_FRAMES);
    CHECK(w2->hold == RT_INPUT_SCRIPT_DEFAULT_WALK_LIMIT_SECONDS);
    CHECK(script.events[3].clock == RT_SCRIPT_FRAMES && script.events[3].after_walk == 2);
    CHECK(script.events[3].hold == RT_INPUT_SCRIPT_DEFAULT_WALK_LIMIT_FRAMES);
    /* Timed presses alone have no walk; the plain sampler ignores walks and `+` events. */
    CHECK(rt_input_script_parse("1:cross", &script, error, sizeof error) == 0 && !rt_input_script_has_walk(&script));

    static const char *const bad[] = {
        "+1:cross",              /* a '+' time with no walk before it */
        "1:cross,+1f:circle",    /* '+' times are seconds */
        "5:walk",                /* no coordinates */
        "5:walk 1",
        "5:walk 1 z",
        "5:walk --1 2",
        "5:walk 1 2 within 0",
        "5:walk 1 2 within -1",
        "5:walk 1 2 every 0",
        "5:walk 1 2 every 1.5",
        "5:walk 1 2 every 20000",
        "5:walk 1 2 near 3",     /* an unknown option */
        "5:walk 1 2+cross",      /* a walk is the event's only control */
        "5:walk 1 2*0",
        "5:walk 1 2*3f",         /* a limit on the other clock */
        "5:walk 1e3 2",
        "5:walk 2000000 2",      /* out of range */
        "5:walker 1 2",
    };
    for (size_t b = 0; b < sizeof bad / sizeof bad[0]; b++) {
        error[0] = '\0';
        const int refused = rt_input_script_parse(bad[b], &script, error, sizeof error) == -1 && error[0];
        if (!refused) fprintf(stderr, "walk script accepted: \"%s\"\n", bad[b]);
        CHECK(refused);
    }
}

SYSLIB_TEST(input_walk_arrives_whatever_the_camera)
{
    /* The walk is not told the camera's turn; it learns it from the first moves. */
    static const float turns[] = {0.0f, 137.0f, -90.0f, 180.0f, 45.0f};
    for (size_t t = 0; t < sizeof turns / sizeof turns[0]; t++) {
        CHECK(rt_input_script_parse("0:walk 8 -6 within 0.5*60", &script, error, sizeof error) == 0);
        here = open_ground(turns[t]);
        const long finished = run(60 * FRAMES_PER_SECOND, NULL);
        if (finished < 0 || hypotf(here.x - 8.0f, here.z + 6.0f) > 1.0f)
            fprintf(stderr, "camera turn %.0f: finished %ld at (%.2f, %.2f)\n", turns[t], finished, here.x, here.z);
        /* 10 units at 0.1 per frame: well within 300 frames even with a wrong first aim. */
        CHECK(finished >= 0 && finished < 300);
        CHECK(arrived_lines == 1 && gave_up_lines == 0);
        /* After arriving the stick is at rest: the character stays (it may coast within one aim). */
        CHECK(hypotf(here.x - 8.0f, here.z + 6.0f) < 0.5f + 6 * here.speed);
    }
}

SYSLIB_TEST(input_walk_follows_a_turning_camera)
{
    /* A camera that keeps turning, as a follow camera does behind a walking character. */
    CHECK(rt_input_script_parse("0:walk -20 15*60", &script, error, sizeof error) == 0);
    here = open_ground(30.0f);
    here.camera_drift = 0.5f;
    CHECK(run(60 * FRAMES_PER_SECOND, NULL) >= 0 && arrived_lines == 1);
}

SYSLIB_TEST(input_walk_sidesteps_a_wall)
{
    /* A wall across the way from x -3 to 3, the target behind it: the walk gets stuck, sidesteps
       round the end and arrives. */
    CHECK(rt_input_script_parse("0:walk 0 10*120", &script, error, sizeof error) == 0);
    here = open_ground(0.0f);
    here.wall_z = 5.0f, here.wall_x0 = -3.0f, here.wall_x1 = 3.0f;
    const long finished = run(120 * FRAMES_PER_SECOND, NULL);
    if (finished < 0) fprintf(stderr, "wall: stuck at (%.2f, %.2f)\n", here.x, here.z);
    CHECK(finished >= 0 && stuck_lines >= 1 && arrived_lines == 1);
}

SYSLIB_TEST(input_walk_gives_up_and_the_route_goes_on)
{
    /* Enclosed: the walk gives up at its 10 s limit; the press after it still comes, 1 s later,
       and so does the next walk. */
    CHECK(rt_input_script_parse("0:walk 5 5*10, +1:cross*0.5, +2:walk 0 3*5", &script, error, sizeof error) == 0);
    here = open_ground(0.0f);
    here.enclosed = 1;
    uint32_t buttons = 0;
    run(20 * FRAMES_PER_SECOND, &buttons);
    CHECK(gave_up_lines == 2 && arrived_lines == 0 && stuck_lines >= 1);
    CHECK(progress.walks[0].finished_at >= 10.0 && progress.walks[0].finished_at < 10.1);
    CHECK(buttons == RT_PAD_BUTTON_CROSS);
    CHECK(progress.walks[2].started_at >= 12.0 && progress.walks[2].started_at < 12.1);
    CHECK(progress.walks[2].finished_at >= 17.0 && progress.walks[2].finished_at < 17.1);
}

SYSLIB_TEST(input_walk_keeps_the_camera_between_walks)
{
    /* The second walk starts from the camera turn the first one learned, so its first aim already
       points the right way: its first move closes the distance. */
    CHECK(rt_input_script_parse("0:walk 0 6 within 0.5, +0:walk -6 6 within 0.5", &script, error, sizeof error) == 0);
    here = open_ground(-120.0f);
    rt_input_script_progress_reset(&progress);
    rt_pad_sample s;
    long f = 0;
    for (; f < 600 && isnan(progress.walks[1].started_at); f++) {
        rt_input_script_play(&script, &progress, (double)f / FRAMES_PER_SECOND, (uint64_t)f, synthetic_position, NULL, &s);
        move(&s);
    }
    CHECK(!isnan(progress.walks[1].started_at));
    CHECK(fabsf(progress.walks[1].camera_turn - progress.walks[0].camera_turn) < 1e-6f);
    CHECK(fabsf(progress.walks[1].camera_turn + 120.0f) < 5.0f);
    const float before = hypotf(here.x + 6.0f, here.z - 6.0f);
    for (long end = f + RT_WALK_DEFAULT_AIM_FRAMES; f < end; f++) {
        rt_input_script_play(&script, &progress, (double)f / FRAMES_PER_SECOND, (uint64_t)f, synthetic_position, NULL, &s);
        move(&s);
    }
    CHECK(hypotf(here.x + 6.0f, here.z - 6.0f) < before - 0.5f);
}

SYSLIB_TEST(input_walk_waits_without_a_position)
{
    /* No position (a loading screen): the stick stays at rest until the limit. */
    CHECK(rt_input_script_parse("0:walk 5 5*3", &script, error, sizeof error) == 0);
    here = open_ground(0.0f);
    here.has_position = 0;
    run(4 * FRAMES_PER_SECOND, NULL);
    CHECK(here.x == 0.0f && here.z == 0.0f && gave_up_lines == 1);
    /* No source at all behaves the same. */
    rt_input_script_progress_reset(&progress);
    rt_pad_sample s;
    rt_input_script_play(&script, &progress, 1.0, 30, NULL, NULL, &s);
    CHECK(s.left_x == RT_PAD_STICK_CENTRE && s.left_y == RT_PAD_STICK_CENTRE);
}

SYSLIB_TEST(input_walk_sequence_and_timing)
{
    /* The second walk's time comes while the first is still going: it waits for the first. A
       frame-clock press plays alongside on its own clock and wins over the walk's stick. */
    CHECK(rt_input_script_parse("1:walk 0 5 within 0.3, 2:walk 5 5 within 0.3, 40f:lstick-left*10f", &script, error,
                                sizeof error) == 0);
    here = open_ground(0.0f);
    rt_input_script_progress_reset(&progress);
    rt_pad_sample s;
    rt_input_script_play(&script, &progress, 0.5, 15, synthetic_position, NULL, &s);
    CHECK(isnan(progress.walks[0].started_at) && s.left_x == RT_PAD_STICK_CENTRE && s.left_y == RT_PAD_STICK_CENTRE);
    rt_input_script_play(&script, &progress, 40.0 / FRAMES_PER_SECOND, 40, synthetic_position, NULL, &s);
    CHECK(s.left_x == 0x00); /* the press's stick */
    CHECK(run(30 * FRAMES_PER_SECOND, NULL) >= 0 && arrived_lines == 2);
    CHECK(progress.walks[1].started_at >= progress.walks[0].finished_at); /* both on the seconds clock */
    /* Re-aims come every 6 frames by default: the stick only changes on an aim frame. */
    rt_input_script_progress_reset(&progress);
    here = open_ground(0.0f);
    uint8_t last_x = 0, last_y = 0;
    int changes_off_aim = 0;
    for (long f = 30; f < 120; f++) {
        rt_input_script_play(&script, &progress, (double)f / FRAMES_PER_SECOND, (uint64_t)f, synthetic_position, NULL, &s);
        if (f > 30 && (s.left_x != last_x || s.left_y != last_y) && f % RT_WALK_DEFAULT_AIM_FRAMES != 0 &&
            !(f >= 40 && f <= 50))
            changes_off_aim++;
        last_x = s.left_x, last_y = s.left_y;
        move(&s);
    }
    CHECK(changes_off_aim == 0);
}
