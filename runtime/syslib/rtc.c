/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System call group: time. The real-time clock (libSceRtc): the local date and time, and ticks.

   Facts used:
   - The signatures: int32_t sceRtcGetCurrentClockLocalTime(date and time *), int32_t
     sceRtcGetTick(const date and time *, uint64_t *tick), int32_t sceRtcSetTick(date and time *,
     const uint64_t *tick), the reverse (the OpenOrbis homebrew SDK's Rtc.h,
     which names sceRtcGetTickResolution without a signature). The executable's one call to that
     one (0x010b70ea) takes no argument and uses the int32_t it answers as ticks per second: a
     negative answer is a failure, and it divides a difference of two ticks by it.
   - The date and time: year, month, day, hour, minute and second as 16-bit fields (Rtc.h), then
     the microsecond. Rtc.h gives that one 16 bits, which cannot hold 999999. The Vita homebrew
     SDK's SceDateTime, of the same library family (vitasdk, psp2common/types.h), gives it 32 bits
     at offset 12 for a 16-byte structure, and the executable copies the whole structure as 16
     bytes (0x01893013, 0x019693ee); so 32 bits at offset 12.
   - About 60 sites ask for the local time, and those reached so far pass it straight to
     sceRtcGetTick (0x026fb5c3, 0x0185b724). The executable holds no constant relating ticks to
     another epoch (neither the span from 0001 to 1970 in microseconds or seconds, nor its
     negation): it compares ticks, takes differences, and asks for the resolution. Its first
     sceRtcSetTick (0x024ee22d) turns a tick it was given back into a date and goes on only on 0,
     accepting years 1601 to 4095.

   So the tick is the runtime's own convention, the same in every call: microseconds since
   0001-01-01 00:00:00 in the proleptic Gregorian calendar, 1000000 per second. The local time is
   the host's, in the time zone of the person running the runtime, as the console gives the one
   its owner set. sceRtcGetDayOfWeek(year, month, day) (0x024ee2a9, its answer packed into 3 bits)
   answers 0 for Sunday to 6 for Saturday: the PS4Recomp project's libSceRtc module numbers the
   days so, and notes that a real PS4's ticks count from year 1, as these do (facts only; the PSP
   SDK documents 0 as Monday, an older library). A date and time out of range, a missing argument and every error value stop by
   name. */
#include "runtime/process.h"
#include "runtime/syslib.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

typedef struct {
    uint16_t year, month, day, hour, minute, second;
    uint32_t microsecond;
} rtc_date_time;
_Static_assert(sizeof(rtc_date_time) == 16, "the executable copies the date and time as 16 bytes");
_Static_assert(offsetof(rtc_date_time, microsecond) == 12, "the microsecond follows the six 16-bit fields");

enum { TICKS_PER_SECOND = 1000000, SECONDS_PER_DAY = 86400, MAX_YEAR = 9999 };

static int is_leap(int64_t year) { return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0; }

static int days_in_month(int64_t year, int month)
{
    static const int days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return month == 2 && is_leap(year) ? 29 : days[month - 1];
}

/* Days from 0001-01-01 to the given date, which is valid. */
static int64_t days_since_year_one(int64_t year, int month, int day)
{
    const int64_t before = year - 1;
    int64_t days = before * 365 + before / 4 - before / 100 + before / 400;
    for (int m = 1; m < month; m++) days += days_in_month(year, m);
    return days + day - 1;
}

static int32_t get_current_clock_local_time(rtc_date_time *out)
{
    static const char fn[] = "sceRtcGetCurrentClockLocalTime";
    if (!out) rt_fatal(fn, "no structure for the time: the error value is not established");
    struct timespec now;
    struct tm local;
    if (clock_gettime(CLOCK_REALTIME, &now) || !localtime_r(&now.tv_sec, &local))
        rt_fatal(fn, "the host gave no local time");
    *out = (rtc_date_time){
        .year = (uint16_t)(local.tm_year + 1900),
        .month = (uint16_t)(local.tm_mon + 1),
        .day = (uint16_t)local.tm_mday,
        .hour = (uint16_t)local.tm_hour,
        .minute = (uint16_t)local.tm_min,
        .second = (uint16_t)(local.tm_sec > 59 ? 59 : local.tm_sec), /* a leap second shows as :59 */
        .microsecond = (uint32_t)(now.tv_nsec / 1000),
    };
    return 0;
}

static int32_t get_tick(const rtc_date_time *time, uint64_t *tick)
{
    static const char fn[] = "sceRtcGetTick";
    if (!time || !tick) rt_fatal(fn, "no date and time, or no variable for the tick: the error value is not established");
    if (time->year < 1 || time->year > MAX_YEAR || time->month < 1 || time->month > 12 || time->day < 1 ||
        time->day > days_in_month(time->year, time->month) || time->hour > 23 || time->minute > 59 ||
        time->second > 59 || time->microsecond >= TICKS_PER_SECOND)
        rt_fatal(fn, "a date and time out of range: the error value is not established");
    const int64_t seconds = days_since_year_one(time->year, time->month, time->day) * SECONDS_PER_DAY +
                            time->hour * 3600 + time->minute * 60 + time->second;
    *tick = (uint64_t)seconds * TICKS_PER_SECOND + time->microsecond;
    return 0;
}

static int32_t set_tick(rtc_date_time *time, const uint64_t *tick)
{
    static const char fn[] = "sceRtcSetTick";
    if (!time || !tick) rt_fatal(fn, "no structure for the date and time, or no tick: the error value is not established");
    const uint64_t seconds = *tick / TICKS_PER_SECOND;
    int64_t days = (int64_t)(seconds / SECONDS_PER_DAY);
    const uint32_t second_of_day = (uint32_t)(seconds % SECONDS_PER_DAY);
    int64_t year = 1;
    for (int64_t span = 400 * 365 + 97; days >= span; days -= span) year += 400; /* whole 400-year cycles */
    while (days >= 365 + is_leap(year)) days -= 365 + is_leap(year++);
    if (year > MAX_YEAR) rt_fatal(fn, "a tick past the year 9999: the error value is not established");
    int month = 1;
    while (days >= days_in_month(year, month)) days -= days_in_month(year, month++);
    *time = (rtc_date_time){
        .year = (uint16_t)year,
        .month = (uint16_t)month,
        .day = (uint16_t)(days + 1),
        .hour = (uint16_t)(second_of_day / 3600),
        .minute = (uint16_t)(second_of_day / 60 % 60),
        .second = (uint16_t)(second_of_day % 60),
        .microsecond = (uint32_t)(*tick % TICKS_PER_SECOND),
    };
    return 0;
}

static int32_t get_tick_resolution(void) { return TICKS_PER_SECOND; }

enum { MONDAY = 1, DAYS_PER_WEEK = 7 };

/* 0 Sunday to 6 Saturday. 0001-01-01 was a Monday in the proleptic Gregorian calendar. */
static int32_t get_day_of_week(int32_t year, int32_t month, int32_t day)
{
    if (year < 1 || year > MAX_YEAR || month < 1 || month > 12 || day < 1 || day > days_in_month(year, month))
        rt_fatal("sceRtcGetDayOfWeek", "a date out of range: the answer is not established");
    return (int32_t)((days_since_year_one(year, month, day) + MONDAY) % DAYS_PER_WEEK);
}

/* Ticks and Unix time, UTC and local time.

   - sceRtcSetTime_t(date and time *, time_t) (0x010b70a6: a count of seconds parsed from text,
     then sceRtcGetTick on the result, going on only on 0): the date and time that many seconds
     after 1970-01-01 00:00:00 UTC. A tick counts from year 1, so 1970 is tick 62135596800000000
     (PS4Recomp's libSceRtc and shadPS4's rtc.cpp, facts only). Both say the conversion depends on
     the SDK the title was built with: before SDK 3.00 only the low 32 bits of the count are used,
     from 3.00 a negative count is an error. Counts from 0 to 0xffffffff mean the same either way;
     others stop by name.
   - sceRtcConvertUtcToLocalTime(const tick *utc, tick *local) and sceRtcConvertLocalTimeToUtc(const
     tick *local, tick *utc) (11 and 2 sites, such as 0x01969648 and 0x019696ae: the tick passed
     through a local copy, 0 is success, anything else is treated as no time): the host's time zone,
     as for the local time above, including its daylight saving rule at that instant. A local time
     the clock skips or shows twice (at a daylight saving change) stops by name, since the PS4's
     choice there is not established.
   - sceRtcFormatRFC2822LocalTime(char *text, const tick *utc) (0x023e5ca9, with no tick, meaning
     now; 0 is success): RFC 2822 section 3.3's date and time, "Sun, 02 Nov 2026 14:05:09 +0100",
     31 characters and a NUL, the day of the month with two digits as both PS4Recomp and shadPS4
     write it, in the host's local time with its offset from UTC.
   - sceRtcGetCurrentNetworkTick(tick *) (0x01086f50 and 0x010b70cf, 0 is success; the first
     treats a failure as no time): the current UTC tick, from the host's clock, as both PS4Recomp and
     shadPS4 answer. The host's wall clock stands for the network clock.
   A missing argument and a tick past the year 9999 stop by name. */
enum { UNIX_EPOCH_SECONDS = 62135596800 }; /* 0001-01-01 to 1970-01-01, in seconds */
#define SECONDS_TO_YEAR_10000 (INT64_C(315537897600)) /* 0001-01-01 to 10000-01-01 */

static int64_t unix_seconds_of(uint64_t tick) { return (int64_t)(tick / TICKS_PER_SECOND) - UNIX_EPOCH_SECONDS; }

/* The tick of a Unix time (seconds) and a microsecond; stops past the year 9999 or before year 1. */
static uint64_t tick_of(const char *caller, int64_t unix_seconds, uint32_t microsecond)
{
    const int64_t since_year_one = unix_seconds + UNIX_EPOCH_SECONDS;
    if (since_year_one < 0 || since_year_one >= SECONDS_TO_YEAR_10000)
        rt_fatal(caller, "a time before year 1 or past the year 9999: the error value is not established");
    return (uint64_t)since_year_one * TICKS_PER_SECOND + microsecond;
}

static int32_t set_time_t(rtc_date_time *time, int64_t seconds)
{
    static const char fn[] = "sceRtcSetTime_t";
    if (!time) rt_fatal(fn, "no structure for the date and time: the error value is not established");
    if (seconds < 0 || seconds > UINT32_MAX)
        rt_fatal(fn, "a count outside 0 to 0xffffffff: its answer depends on the title's SDK version");
    const uint64_t tick = tick_of(fn, seconds, 0);
    return set_tick(time, &tick);
}

/* The host's offset from UTC, in seconds, at a Unix time. */
static int64_t local_offset(const char *caller, int64_t unix_seconds)
{
    const time_t t = (time_t)unix_seconds;
    struct tm local;
    if (!localtime_r(&t, &local)) rt_fatal(caller, "the host gave no local time");
    return local.tm_gmtoff;
}

static int32_t convert_utc_to_local_time(const uint64_t *utc, uint64_t *local)
{
    static const char fn[] = "sceRtcConvertUtcToLocalTime";
    if (!utc || !local) rt_fatal(fn, "no tick, or no variable for the answer: the error value is not established");
    const int64_t seconds = unix_seconds_of(*utc);
    *local = tick_of(fn, seconds + local_offset(fn, seconds), (uint32_t)(*utc % TICKS_PER_SECOND));
    return 0;
}

static int32_t convert_local_time_to_utc(const uint64_t *local, uint64_t *utc)
{
    static const char fn[] = "sceRtcConvertLocalTimeToUtc";
    if (!local || !utc) rt_fatal(fn, "no tick, or no variable for the answer: the error value is not established");
    const int64_t wall = unix_seconds_of(*local); /* the local wall clock, counted as if it were UTC */
    /* The UTC instant is the wall clock less the offset in force then. Try the offsets in force a
       day either side (a time zone changes its offset at most once a day): the answer is the one
       instant whose own offset gives back this wall clock. */
    int64_t answer = 0;
    int answers = 0;
    const int64_t offsets[2] = {local_offset(fn, wall - SECONDS_PER_DAY), local_offset(fn, wall + SECONDS_PER_DAY)};
    for (int i = 0; i < 2; i++) {
        const int64_t candidate = wall - offsets[i];
        if (i == 1 && offsets[1] == offsets[0]) break;
        if (candidate + local_offset(fn, candidate) == wall) answer = candidate, answers++;
    }
    if (answers != 1) rt_fatal(fn, "a local time the clock skips or shows twice: the PS4's answer is not established");
    *utc = tick_of(fn, answer, (uint32_t)(*local % TICKS_PER_SECOND));
    return 0;
}

static uint64_t current_utc_tick(const char *caller)
{
    struct timespec now;
    if (clock_gettime(CLOCK_REALTIME, &now)) rt_fatal(caller, "the host gave no time");
    return tick_of(caller, now.tv_sec, (uint32_t)(now.tv_nsec / 1000));
}

static int32_t get_current_network_tick(uint64_t *tick)
{
    static const char fn[] = "sceRtcGetCurrentNetworkTick";
    if (!tick) rt_fatal(fn, "no variable for the tick: the error value is not established");
    *tick = current_utc_tick(fn);
    return 0;
}

enum { RFC2822_BYTES = 32 }; /* "Sun, 02 Nov 2026 14:05:09 +0100" and a NUL */

static int32_t format_rfc2822_local_time(char *text, const uint64_t *utc)
{
    static const char fn[] = "sceRtcFormatRFC2822LocalTime";
    static const char days[7][4] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char months[12][4] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                       "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    if (!text) rt_fatal(fn, "no buffer for the text: the error value is not established");
    const uint64_t at = utc ? *utc : current_utc_tick(fn);
    const int64_t offset = local_offset(fn, unix_seconds_of(at));
    const uint64_t local_tick = tick_of(fn, unix_seconds_of(at) + offset, 0);
    rtc_date_time local;
    set_tick(&local, &local_tick);
    const int64_t minutes = (offset < 0 ? -offset : offset) / 60;
    char line[64]; /* room for any field values; a valid date fills RFC2822_BYTES */
    snprintf(line, sizeof line, "%s, %02u %s %04u %02u:%02u:%02u %c%02u%02u",
             days[get_day_of_week(local.year, local.month, local.day)], local.day, months[local.month - 1],
             local.year, local.hour, local.minute, local.second, offset < 0 ? '-' : '+',
             (unsigned)(minutes / 60), (unsigned)(minutes % 60));
    memcpy(text, line, RFC2822_BYTES);
    return 0;
}

RT_SYSLIB("libSceRtc", sceRtcGetCurrentClockLocalTime, get_current_clock_local_time);
RT_SYSLIB("libSceRtc", sceRtcGetTick, get_tick);
RT_SYSLIB("libSceRtc", sceRtcSetTick, set_tick);
RT_SYSLIB("libSceRtc", sceRtcGetTickResolution, get_tick_resolution);
RT_SYSLIB("libSceRtc", sceRtcGetDayOfWeek, get_day_of_week);
RT_SYSLIB("libSceRtc", sceRtcSetTime_t, set_time_t);
RT_SYSLIB("libSceRtc", sceRtcConvertUtcToLocalTime, convert_utc_to_local_time);
RT_SYSLIB("libSceRtc", sceRtcConvertLocalTimeToUtc, convert_local_time_to_utc);
RT_SYSLIB("libSceRtc", sceRtcGetCurrentNetworkTick, get_current_network_tick);
RT_SYSLIB("libSceRtc", sceRtcFormatRFC2822LocalTime, format_rfc2822_local_time);
