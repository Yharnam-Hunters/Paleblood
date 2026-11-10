/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System call group: kernel, threads and memory. The system C library's calendar and formatted
   output calls that libSceFios2.elf imports from libSceLibcInternal: gmtime, gmtime_s, printf,
   vprintf and vsnprintf.

   gmtime(timer) and gmtime_s(timer, result) are ISO C 7.27.3.3 and C11 Annex K.3.8.2.3: the UTC
   broken-down time of a time_t (64-bit, as on FreeBSD amd64). gmtime answers a pointer to an
   object of the library's own (one per thread here, which ISO C allows: a later call may
   overwrite it); gmtime_s fills the caller's structure and answers its address. libSceFios2's calls
   (+0x2cd73, +0x2cdf4, +0x2e494) pass a time_t on the stack, and the gmtime_s callers test the
   answer for null, so it is the Annex K form, not Microsoft's (which answers an error number).

   The PS4's struct tm is ISO C's nine ints, 36 bytes, without FreeBSD's tm_gmtoff and tm_zone: the
   same module's time-zone helper (+0x2cd15) copies exactly 0x24 bytes into a local struct tm that
   mktime then normalizes in place, and its next local starts 0x28 bytes after it, so a FreeBSD
   struct tm (56 bytes) would not fit. The runtime writes those nine ints and nothing more.

   printf(format, ...), vprintf(format, va_list) and vsnprintf(buffer, size, format, va_list) are
   ISO C 7.21.6.3, 7.21.6.10 and 7.21.6.12. The module's 62 printf calls pass constant formats with
   %s conversions; its vsnprintf calls pass formats through from their callers. The guest calls
   with the System V convention the host uses, so its va_list is the host's and the host's
   functions format; printf and vprintf write to the host's standard output. A format the runtime
   cannot hand to the host unchanged stops by name: a wide character or string (%lc, %ls: the PS4's
   wchar_t is not established), %n (the host's fortified printf refuses it from writable memory,
   and no call is known to pass it, so it is left off the list below), and any conversion ISO C
   does not define.

   localtime(timer) and mktime(tm) are ISO C 7.27.3.4 and 7.27.2.3 in the console's time zone,
   which is the host's here (the same zone sceKernelGettimezone reports): localtime answers a
   per-thread object as gmtime does; mktime normalizes the nine ints in place and answers the time,
   or -1 when there is none. libSceFios2 calls localtime once and mktime from its time-zone helper.

   malloc(size) and free(pointer) (ISO C 7.22.3) come from the host's allocator, which aligns to 16
   bytes like the PS4's: libSceFios2 reaches them only through its default allocator (+0x3a5e0,
   +0x3a5f0). fflush(stream) (7.21.5.2): libSceFios2 flushes &_Stdout after its default log line
   (+0x2cad5), so _Stdout's address flushes the host's standard output and NULL flushes every host
   stream; another stream stops by name. __cxa_finalize(dso) (the Itanium C++ ABI's DSO
   destructor) runs the handlers __cxa_atexit registered for a module; libSceFios2 imports no
   __cxa_atexit or atexit, so there are none and it does nothing.

   Two data imports: _Stdout, libSceLibcInternal's standard output FILE, is an object of the size
   libc.elf's own _Stdout export has (456 bytes) that nothing reads through (libSceFios2 only
   passes its address to fflush); Need_sceLibcInternal is an int whose address libSceFios2 stores
   and never reads (the droogie bbhost runtime's analogous Need_sceLibc holds 1, used here too). */
#include "runtime/process.h"
#include "runtime/syslib.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    int32_t sec, min, hour, mday, mon, year, wday, yday, isdst;
} guest_tm;
_Static_assert(sizeof(guest_tm) == 36, "the PS4's struct tm is nine ints");

/* The broken-down UTC time of *timer in out; stops by name when there is none. */
static guest_tm *utc_time(const char *caller, const int64_t *timer, guest_tm *out)
{
    if (!timer || !out) rt_fatal(caller, "no time or no result: ISO C leaves it undefined");
    const time_t seconds = (time_t)*timer;
    struct tm t;
    if (!gmtime_r(&seconds, &t)) rt_fatal(caller, "a time whose year does not fit: errno is not modelled");
    *out = (guest_tm){t.tm_sec, t.tm_min, t.tm_hour, t.tm_mday, t.tm_mon, t.tm_year, t.tm_wday, t.tm_yday, t.tm_isdst};
    return out;
}

static _Thread_local guest_tm gmtime_result;
static guest_tm *libc_gmtime(const int64_t *timer) { return utc_time("gmtime", timer, &gmtime_result); }
static guest_tm *libc_gmtime_s(const int64_t *timer, guest_tm *result) { return utc_time("gmtime_s", timer, result); }

/* Stops by name unless every conversion in format is one ISO C defines and the host formats the
   same way: flags, an optional argument position (POSIX n$), width, precision and length, then one
   of diouxXeEfFgGaAcsp, or %%. */
static void check_format(const char *caller, const char *format)
{
    if (!format) rt_fatal(caller, "no format: ISO C leaves it undefined");
    for (const char *p = strchr(format, '%'); p; p = strchr(p, '%')) {
        p++;
        if (*p == '%') {
            p++;
            continue;
        }
        p += strspn(p, "0123456789$");    /* an argument position, or the start of the width */
        p += strspn(p, "-+ #0'");          /* flags */
        p += strspn(p, "0123456789*$");   /* width */
        if (*p == '.') p += 1 + strspn(p + 1, "0123456789*$");
        const char *length = p;
        p += strspn(p, "hljztL");
        const size_t length_size = (size_t)(p - length);
        const int wide = length_size == 1 && *length == 'l';
        if ((*p == 'c' || *p == 's') && wide) rt_fatal(caller, "a wide character or string: the PS4's wchar_t is not established");
        if (!*p || !strchr("diouxXeEfFgGaAcsp", *p) || length_size > 2)
            rt_fatal(caller, "a conversion the runtime does not hand to the host: %n, or one ISO C does not define");
        p++;
    }
}

static int libc_vsnprintf(char *buffer, size_t size, const char *format, va_list arguments)
{
    check_format("vsnprintf", format);
    if (!buffer && size) rt_fatal("vsnprintf", "no buffer for a non-zero size: ISO C leaves it undefined");
    return vsnprintf(buffer, size, format, arguments);
}

static int libc_vprintf(const char *format, va_list arguments)
{
    check_format("vprintf", format);
    return vprintf(format, arguments);
}

static int libc_printf(const char *format, ...)
{
    check_format("printf", format);
    va_list arguments;
    va_start(arguments, format);
    const int written = vprintf(format, arguments);
    va_end(arguments);
    return written;
}

RT_SYSLIB("libSceLibcInternal", gmtime, libc_gmtime);
RT_SYSLIB("libSceLibcInternal", gmtime_s, libc_gmtime_s);
RT_SYSLIB("libSceLibcInternal", printf, libc_printf);
RT_SYSLIB("libSceLibcInternal", vprintf, libc_vprintf);
RT_SYSLIB("libSceLibcInternal", vsnprintf, libc_vsnprintf);

static _Thread_local guest_tm localtime_result;

static guest_tm *libc_localtime(const int64_t *timer)
{
    if (!timer) rt_fatal("localtime", "no time: ISO C leaves it undefined");
    const time_t seconds = (time_t)*timer;
    struct tm t;
    if (!localtime_r(&seconds, &t)) rt_fatal("localtime", "a time whose year does not fit: errno is not modelled");
    localtime_result = (guest_tm){t.tm_sec, t.tm_min, t.tm_hour, t.tm_mday, t.tm_mon, t.tm_year, t.tm_wday, t.tm_yday, t.tm_isdst};
    return &localtime_result;
}

static int64_t libc_mktime(guest_tm *time_parts)
{
    if (!time_parts) rt_fatal("mktime", "no time: ISO C leaves it undefined");
    guest_tm *const g = time_parts;
    struct tm t = {.tm_sec = g->sec, .tm_min = g->min, .tm_hour = g->hour, .tm_mday = g->mday,
                   .tm_mon = g->mon, .tm_year = g->year, .tm_isdst = g->isdst};
    const time_t seconds = mktime(&t);
    if (seconds == (time_t)-1) return -1;
    *g = (guest_tm){t.tm_sec, t.tm_min, t.tm_hour, t.tm_mday, t.tm_mon, t.tm_year, t.tm_wday, t.tm_yday, t.tm_isdst};
    return (int64_t)seconds;
}

static void *libc_malloc(uint64_t size) { return malloc(size); }
static void libc_free(void *pointer) { free(pointer); }

enum { LIBC_FILE_BYTES = 456, LIBC_EOF = -1 };
static _Alignas(16) uint8_t libc_stdout[LIBC_FILE_BYTES];
static int32_t need_libc_internal[1] = {1}; /* an array, so its name is its address */

static int32_t libc_fflush(void *stream)
{
    if (stream && stream != (void *)libc_stdout) rt_fatal("fflush", "a stream other than _Stdout: not modelled");
    return fflush(stream ? stdout : NULL) ? LIBC_EOF : 0;
}

static void libc_cxa_finalize(void *dso) { (void)dso; }

RT_SYSLIB("libSceLibcInternal", localtime, libc_localtime);
RT_SYSLIB("libSceLibcInternal", mktime, libc_mktime);
RT_SYSLIB("libSceLibcInternal", malloc, libc_malloc);
RT_SYSLIB("libSceLibcInternal", free, libc_free);
RT_SYSLIB("libSceLibcInternal", fflush, libc_fflush);
RT_SYSLIB("libSceLibcInternal", __cxa_finalize, libc_cxa_finalize);
RT_SYSLIB("libSceLibcInternal", _Stdout, libc_stdout);
RT_SYSLIB("libSceLibcInternal", Need_sceLibcInternal, need_libc_internal);
