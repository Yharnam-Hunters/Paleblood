/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The system C library's calendar and formatted output (runtime/syslib/libc_internal.c): gmtime,
   gmtime_s, printf, vprintf and vsnprintf from libSceLibcInternal. */
#include "../syslib_test.h"

#include <stdarg.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    int32_t sec, min, hour, mday, mon, year, wday, yday, isdst;
} guest_tm;
typedef guest_tm *(*f_gmtime)(const int64_t *);
typedef guest_tm *(*f_gmtime_s)(const int64_t *, guest_tm *);
typedef int (*f_vsnprintf)(char *, size_t, const char *, va_list);
typedef int (*f_vprintf)(const char *, va_list);
typedef int (*f_printf)(const char *, ...);

static f_gmtime gm;
static f_gmtime_s gm_s;
static f_vsnprintf v_snprintf;
static f_printf p_printf;
static f_vprintf v_printf;

static int print_through_vprintf(const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    const int n = v_printf(format, arguments);
    va_end(arguments);
    return n;
}

/* vsnprintf through the registered entry, called as the guest would with its own va_list. */
static int format_into(char *buffer, size_t size, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    const int n = v_snprintf(buffer, size, format, arguments);
    va_end(arguments);
    return n;
}

static void gmtime_null(void) { gm(NULL); }
static void gmtime_s_no_result(void) { const int64_t t = 0; gm_s(&t, NULL); }
static void gmtime_overflow(void) { const int64_t t = INT64_MAX; gm(&t); }
static void format_wide_string(void) { char b[8]; format_into(b, sizeof b, "%ls", L"x"); }
static void format_wide_char(void) { char b[8]; format_into(b, sizeof b, "%lc", 'x'); }
static void format_count(void) { char b[8]; int n; format_into(b, sizeof b, "ab%n", &n); }
static void format_unknown(void) { char b[8]; format_into(b, sizeof b, "%y"); }
static void format_long_length(void) { char b[8]; format_into(b, sizeof b, "%llld", 1ll); }
static void format_none(void) { char b[8]; format_into(b, sizeof b, NULL); }
static void format_no_buffer(void) { format_into(NULL, 4, "x"); }
static void printf_wide(void) { p_printf("%ls\n", L"x"); }
static void vprintf_wide(void) { print_through_vprintf("%ls\n", L"x"); }

SYSLIB_TEST(libc_internal)
{
    gm = (f_gmtime)fn("libSceLibcInternal", "gmtime");
    gm_s = (f_gmtime_s)fn("libSceLibcInternal", "gmtime_s");
    v_snprintf = (f_vsnprintf)fn("libSceLibcInternal", "vsnprintf");
    v_printf = (f_vprintf)fn("libSceLibcInternal", "vprintf");
    p_printf = (f_printf)fn("libSceLibcInternal", "printf");
    const char *names[] = {"gmtime", "gmtime_s", "vsnprintf", "vprintf", "printf"};
    for (unsigned i = 0; i < sizeof names / sizeof names[0]; i++) CHECK(is_implemented("libSceLibcInternal", names[i]));
    if (!gm || !gm_s || !v_snprintf || !v_printf || !p_printf) return;

    /* 2001-09-09 01:46:40 UTC, a Sunday, day 251 of the year. */
    const int64_t billion = 1000000000;
    guest_tm *t = gm(&billion);
    CHECK(t && t->year == 101 && t->mon == 8 && t->mday == 9 && t->hour == 1 && t->min == 46 && t->sec == 40);
    CHECK(t && t->wday == 0 && t->yday == 251 && t->isdst == 0);
    const int64_t epoch = 0;
    CHECK(gm(&epoch) == t && t->year == 70 && t->mday == 1); /* the same object, overwritten */
    /* gmtime_s writes the nine ints only: the bytes after them stay as they were. */
    struct { guest_tm tm; uint32_t after; } out;
    memset(&out, 0x5a, sizeof out);
    CHECK(gm_s(&billion, &out.tm) == &out.tm && out.tm.year == 101 && out.tm.yday == 251);
    CHECK(out.after == 0x5a5a5a5au);
    const int64_t before_epoch = -1;
    CHECK(gm_s(&before_epoch, &out.tm) == &out.tm && out.tm.year == 69 && out.tm.mon == 11 && out.tm.sec == 59);

    /* Formatting: the host's own, for the conversions ISO C defines. */
    char buffer[64];
    CHECK(format_into(buffer, sizeof buffer, "%s=%d %05.1f %x %%", "a", -3, 2.25, 255u) == 15);
    CHECK(!strcmp(buffer, "a=-3 002.2 ff %") || !strcmp(buffer, "a=-3 002.3 ff %"));
    CHECK(format_into(buffer, 4, "%lld|%zu", 123456ll, (size_t)7) == 8 && !strcmp(buffer, "123"));
    CHECK(format_into(buffer, sizeof buffer, "%2$s%1$s", "b", "a") == 2 && !strcmp(buffer, "ab"));
    CHECK(format_into(NULL, 0, "%*d", 5, 1) == 5);
    CHECK(p_printf("%s", "") == 0 && print_through_vprintf("%.0s", "hidden") == 0);

    CHECK(died_in_fatal(gmtime_null) && died_in_fatal(gmtime_s_no_result) && died_in_fatal(gmtime_overflow));
    CHECK(died_in_fatal(format_wide_string) && died_in_fatal(format_wide_char) && died_in_fatal(format_count));
    CHECK(died_in_fatal(format_unknown) && died_in_fatal(format_long_length) && died_in_fatal(format_none));
    CHECK(died_in_fatal(format_no_buffer) && died_in_fatal(printf_wide) && died_in_fatal(vprintf_wide));
}
