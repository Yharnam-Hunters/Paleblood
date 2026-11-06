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

RT_SYSLIB("libSceRtc", sceRtcGetCurrentClockLocalTime, get_current_clock_local_time);
RT_SYSLIB("libSceRtc", sceRtcGetTick, get_tick);
RT_SYSLIB("libSceRtc", sceRtcSetTick, set_tick);
RT_SYSLIB("libSceRtc", sceRtcGetTickResolution, get_tick_resolution);
RT_SYSLIB("libSceRtc", sceRtcGetDayOfWeek, get_day_of_week);
