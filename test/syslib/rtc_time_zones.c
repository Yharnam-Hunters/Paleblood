/* SPDX-License-Identifier: GPL-2.0-or-later */
/* libSceRtc (runtime/syslib/rtc.c): Unix time to a date, UTC and local ticks in the host's time
   zone, the RFC 2822 local time and the network tick. The time zone is set through TZ with POSIX
   rules, so no time zone database is needed. */
#include "../syslib_test.h"

#include "runtime/process.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    uint16_t year, month, day, hour, minute, second;
    uint32_t microsecond;
} date_time;
typedef int32_t (*f_set_time_t)(date_time *, int64_t);
typedef int32_t (*f_convert)(const uint64_t *, uint64_t *);
typedef int32_t (*f_network_tick)(uint64_t *);
typedef int32_t (*f_format)(char *, const uint64_t *);

#define EPOCH_TICK UINT64_C(62135596800000000) /* 1970-01-01 00:00:00 */
#define SECOND UINT64_C(1000000)
#define HOUR (3600 * SECOND)

static f_set_time_t set_time_t;
static f_convert to_local, to_utc;
static f_format format;

/* 2026-11-02 14:05:09.250000 UTC */
static const uint64_t november_tick = EPOCH_TICK + UINT64_C(1793628309) * SECOND + 250000;

static void set_time_t_negative(void) { date_time t; set_time_t(&t, -1); }
static void set_time_t_past_32_bits(void) { date_time t; set_time_t(&t, INT64_C(0x100000000)); }
static void set_time_t_no_structure(void) { set_time_t(NULL, 0); }
static void to_local_no_answer(void) { to_local(&november_tick, NULL); }
static void to_utc_no_tick(void) { uint64_t out; to_utc(NULL, &out); }
static void to_local_no_tick(void) { uint64_t out; to_local(NULL, &out); }
static void to_utc_no_answer(void) { to_utc(&november_tick, NULL); }
static void to_local_before_year_one(void) { const uint64_t first = 0; uint64_t out; to_local(&first, &out); }
static f_network_tick network_tick;
static void network_tick_no_variable(void) { network_tick(NULL); }
static void format_no_buffer(void) { format(NULL, &november_tick); }
static void to_local_past_9999(void)
{
    const uint64_t last = EPOCH_TICK + UINT64_C(253402300799) * SECOND; /* 9999-12-31 23:59:59 UTC */
    uint64_t out;
    to_local(&last, &out); /* UTC+1 makes it the year 10000 */
}

/* 2026-03-29 02:30 does not happen in Central European time; 2026-10-25 02:30 happens twice. */
static void to_utc_skipped_hour(void)
{
    const uint64_t wall = EPOCH_TICK + UINT64_C(1774751400) * SECOND; /* 2026-03-29 02:30 as if UTC */
    uint64_t out;
    to_utc(&wall, &out);
}
static void to_utc_repeated_hour(void)
{
    const uint64_t wall = EPOCH_TICK + UINT64_C(1792895400) * SECOND; /* 2026-10-25 02:30 as if UTC */
    uint64_t out;
    to_utc(&wall, &out);
}

/* Which function a child stopped in (rt_fatal_observer), so each stop is checked by its name. */
static const char *expected_function;
static void observe_stop(const char *function, const char *reason)
{
    (void)reason;
    _exit(!strcmp(function, expected_function) ? 0 : 1);
}
static int stops_in(void (*call)(void), const char *function)
{
    const pid_t child = fork();
    if (child == 0) {
        expected_function = function;
        rt_fatal_observer = observe_stop;
        call();
        _exit(2); /* the call returned, so it did not stop */
    }
    int status = 0;
    if (waitpid(child, &status, 0) < 0) return 0;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static void use_time_zone(const char *tz)
{
    setenv("TZ", tz, 1);
    tzset();
}

SYSLIB_TEST(rtc_time_zones)
{
    set_time_t = (f_set_time_t)fn("libSceRtc", "sceRtcSetTime_t");
    to_local = (f_convert)fn("libSceRtc", "sceRtcConvertUtcToLocalTime");
    to_utc = (f_convert)fn("libSceRtc", "sceRtcConvertLocalTimeToUtc");
    network_tick = (f_network_tick)fn("libSceRtc", "sceRtcGetCurrentNetworkTick");
    format = (f_format)fn("libSceRtc", "sceRtcFormatRFC2822LocalTime");
    const char *saved = getenv("TZ");
    char saved_tz[256] = "";
    if (saved) strncpy(saved_tz, saved, sizeof saved_tz - 1);
    if (set_time_t && to_local && to_utc && network_tick && format) {
        /* Unix time to a date */
        date_time t;
        memset(&t, 0xff, sizeof t);
        CHECK(set_time_t(&t, 0) == 0 && t.year == 1970 && t.month == 1 && t.day == 1 && t.hour == 0 &&
              t.minute == 0 && t.second == 0 && t.microsecond == 0);
        CHECK(set_time_t(&t, 1793628309) == 0 && t.year == 2026 && t.month == 11 && t.day == 2 && t.hour == 14 &&
              t.minute == 5 && t.second == 9 && t.microsecond == 0);
        CHECK(set_time_t(&t, INT64_C(0xffffffff)) == 0 && t.year == 2106 && t.month == 2 && t.day == 7 &&
              t.hour == 6 && t.minute == 28 && t.second == 15);
        CHECK(stops_in(set_time_t_negative, "sceRtcSetTime_t"));
        CHECK(stops_in(set_time_t_past_32_bits, "sceRtcSetTime_t"));
        CHECK(stops_in(set_time_t_no_structure, "sceRtcSetTime_t"));

        /* Central European time: UTC+1, UTC+2 in summer */
        use_time_zone("CET-1CEST,M3.5.0,M10.5.0/3");
        uint64_t local = 0, utc = 0;
        CHECK(to_local(&november_tick, &local) == 0 && local == november_tick + HOUR);
        CHECK(to_utc(&local, &utc) == 0 && utc == november_tick);
        const uint64_t july = november_tick - UINT64_C(120) * 86400 * SECOND; /* 2026-07-05, summer time */
        CHECK(to_local(&july, &local) == 0 && local == july + 2 * HOUR);
        CHECK(to_utc(&local, &utc) == 0 && utc == july);
        local = november_tick; /* the input is read before the answer is written */
        CHECK(to_local(&local, &local) == 0 && local == november_tick + HOUR);
        char text[40];
        memset(text, 'x', sizeof text);
        CHECK(format(text, &november_tick) == 0 && !strcmp(text, "Mon, 02 Nov 2026 15:05:09 +0100") && text[32] == 'x');
        CHECK(format(text, &july) == 0 && !strcmp(text, "Sun, 05 Jul 2026 16:05:09 +0200"));
        CHECK(stops_in(to_utc_skipped_hour, "sceRtcConvertLocalTimeToUtc"));
        CHECK(stops_in(to_utc_repeated_hour, "sceRtcConvertLocalTimeToUtc"));
        CHECK(stops_in(to_local_past_9999, "sceRtcConvertUtcToLocalTime"));

        /* West of UTC: a negative offset with minutes, and a day change */
        use_time_zone("NST3:30");
        CHECK(to_local(&november_tick, &local) == 0 && local == november_tick - 3 * HOUR - 30 * 60 * SECOND);
        CHECK(to_utc(&local, &utc) == 0 && utc == november_tick);
        const uint64_t just_after_midnight = EPOCH_TICK + UINT64_C(1793578000) * SECOND; /* 2026-11-02 00:06:40 UTC */
        CHECK(format(text, &just_after_midnight) == 0 && !strcmp(text, "Sun, 01 Nov 2026 20:36:40 -0330"));
        const uint64_t before = EPOCH_TICK + (uint64_t)time(NULL) * SECOND; /* UTC, whatever the zone */
        uint64_t now = 0;
        CHECK(network_tick(&now) == 0 && now >= before && now < before + 5 * SECOND);
        use_time_zone("UTC0");
        CHECK(format(text, &november_tick) == 0 && !strcmp(text, "Mon, 02 Nov 2026 14:05:09 +0000"));

        /* No tick: now. The network tick is the current UTC tick. */
        char year[16];
        const time_t now_seconds = time(NULL);
        struct tm now_utc;
        gmtime_r(&now_seconds, &now_utc);
        snprintf(year, sizeof year, " %04d ", now_utc.tm_year + 1900);
        CHECK(format(text, NULL) == 0 && strlen(text) == 31 && !strcmp(text + 25, " +0000") && !strncmp(text + 11, year, 6));

        CHECK(stops_in(to_local_no_answer, "sceRtcConvertUtcToLocalTime"));
        CHECK(stops_in(to_utc_no_tick, "sceRtcConvertLocalTimeToUtc"));
        CHECK(stops_in(to_local_no_tick, "sceRtcConvertUtcToLocalTime"));
        CHECK(stops_in(to_utc_no_answer, "sceRtcConvertLocalTimeToUtc"));
        CHECK(stops_in(network_tick_no_variable, "sceRtcGetCurrentNetworkTick"));
        use_time_zone("NST3:30");
        CHECK(stops_in(to_local_before_year_one, "sceRtcConvertUtcToLocalTime")); /* UTC-3:30 at year 1 */
        CHECK(stops_in(format_no_buffer, "sceRtcFormatRFC2822LocalTime"));
    }
    if (saved) use_time_zone(saved_tz);
    else {
        unsetenv("TZ");
        tzset();
    }
    CHECK(is_implemented("libSceRtc", "sceRtcSetTime_t") && is_implemented("libSceRtc", "sceRtcConvertUtcToLocalTime") &&
          is_implemented("libSceRtc", "sceRtcConvertLocalTimeToUtc") &&
          is_implemented("libSceRtc", "sceRtcGetCurrentNetworkTick") &&
          is_implemented("libSceRtc", "sceRtcFormatRFC2822LocalTime"));
}
