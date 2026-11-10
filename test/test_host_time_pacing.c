/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Deterministic clock/sleep boundaries with the production audio and video consumers. */
#include "runtime/host_time.h"
#include "runtime/syslib.h"
#include "runtime/process.h"
#include "runtime/display.h"
#include "runtime/renderer.h"
#include "runtime/equeue.h"
#include "runtime/video_out.h"
#include <errno.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, cases, failures;
#define CHECK(value) do { ++checks; if (!(value)) { ++failures; fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#value); } } while (0)
static struct timespec now, requests[32], expected_present;
static unsigned clock_calls, sleep_calls, interrupt_budget, presentations;
static unsigned fail_clock_on;
static int sleep_error;
static const char *expected_fatal, *expected_reason = "monotonic wait";
static jmp_buf fatal_return;
static int equal(struct timespec a, struct timespec b) { return a.tv_sec == b.tv_sec && a.tv_nsec == b.tv_nsec; }
static void advance(struct timespec delta)
{
    now.tv_sec += delta.tv_sec; now.tv_nsec += delta.tv_nsec;
    if (now.tv_nsec >= 1000000000) { ++now.tv_sec; now.tv_nsec -= 1000000000; }
}
int rt_test_clock_gettime(clockid_t clock, struct timespec *value)
{
    CHECK(clock == CLOCK_MONOTONIC); ++clock_calls;
    if (fail_clock_on && clock_calls == fail_clock_on) { errno = EIO; return -1; }
    *value = now; return 0;
}
int rt_test_nanosleep(const struct timespec *request, struct timespec *remaining)
{
    CHECK(remaining == NULL); CHECK(request->tv_sec >= 0 && request->tv_nsec >= 0 && request->tv_nsec < 1000000000);
    if (sleep_calls == 32) { fprintf(stderr,"unbounded sleep loop\n"); exit(3); }
    requests[sleep_calls++] = *request;
    if (sleep_error) { errno = sleep_error; return -1; }
    if (interrupt_budget) { --interrupt_budget; advance((struct timespec){0,1000000}); errno = EINTR; return -1; }
    advance(*request); return 0;
}
_Noreturn void rt_fatal(const char *function, const char *message)
{
    if (!expected_fatal) { fprintf(stderr,"unexpected fatal: %s: %s\n",function,message); exit(3); }
    CHECK(strcmp(function,expected_fatal) == 0); CHECK(strstr(message,expected_reason) != NULL); longjmp(fatal_return,1);
}
/* Display, renderer and event delivery are separate from these clock-boundary tests. */
void rt_display_attach(int port, uint32_t width, uint32_t height, int count)
{ CHECK(port > 0 && width == 8 && height == 8 && count == 1); }
uint32_t *rt_display_frame(int port, int buffer) { CHECK(port > 0 && buffer == 0); return NULL; }
void rt_display_present(int port, int buffer)
{ CHECK(port > 0 && buffer == 0); CHECK(equal(now,expected_present)); ++presentations; }
int rt_renderer_frame(uint64_t address, uint32_t *frame, uint32_t width, uint32_t height)
{ (void)address; (void)frame; (void)width; (void)height; CHECK(0); return -1; }
int rt_equeue_add(rt_equeue *queue, int16_t filter, uint64_t ident, void *data)
{ (void)queue; (void)filter; (void)ident; (void)data; CHECK(0); return -1; }
int rt_equeue_trigger(rt_equeue *queue, int16_t filter, uint64_t ident, int64_t data)
{ (void)queue; (void)filter; (void)ident; (void)data; CHECK(0); return -1; }
static void *function(const char *library, const char *symbol)
{
    const rt_syslib_entry *first = rt_syslib_begin(), *last = rt_syslib_end();
    if (first && last) for (const rt_syslib_entry *entry = first; entry < last; ++entry)
        if (!strcmp(entry->library,library) && !strcmp(entry->symbol,symbol)) return entry->fn;
    fprintf(stderr,"missing production registration: %s/%s\n",library,symbol); exit(3);
}
static void reset_clock(struct timespec value)
{
    now = value; clock_calls = sleep_calls = interrupt_budget = fail_clock_on = 0; sleep_error = 0;
}
static void helper_cases(void)
{
    reset_clock((struct timespec){7,900000000});
    const struct timespec deadline = {8,100000000};
    CHECK(rt_host_sleep_until_monotonic(&deadline) == 0); CHECK(sleep_calls == 1);
    CHECK(equal(requests[0],(struct timespec){0,200000000})); CHECK(equal(now,deadline)); ++cases;
    reset_clock((struct timespec){3,995000000}); interrupt_budget = 3;
    const struct timespec interrupted = {4,5000000};
    CHECK(rt_host_sleep_until_monotonic(&interrupted) == 0); CHECK(sleep_calls == 4);
    const long expected[] = {10000000,9000000,8000000,7000000};
    for (unsigned i = 0; i < 4; ++i) CHECK(equal(requests[i],(struct timespec){0,expected[i]}));
    CHECK(equal(now,interrupted)); ++cases;
    reset_clock((struct timespec){10,1});
    CHECK(rt_host_sleep_until_monotonic(&(struct timespec){10,0}) == 0); CHECK(sleep_calls == 0); ++cases;
    CHECK(rt_host_sleep_until_monotonic(&now) == 0); CHECK(sleep_calls == 0); ++cases;
    CHECK(rt_host_sleep_until_monotonic(NULL) == EINVAL); ++cases;
    CHECK(rt_host_sleep_until_monotonic(&(struct timespec){-1,0}) == EINVAL); ++cases;
    CHECK(rt_host_sleep_until_monotonic(&(struct timespec){0,-1}) == EINVAL); ++cases;
    CHECK(rt_host_sleep_until_monotonic(&(struct timespec){0,1000000000}) == EINVAL); ++cases;
    reset_clock((struct timespec){7,0}); fail_clock_on = 1;
    CHECK(rt_host_sleep_until_monotonic(&(struct timespec){8,0}) == EIO); CHECK(sleep_calls == 0); ++cases;
    reset_clock((struct timespec){7,0}); sleep_error = EAGAIN;
    CHECK(rt_host_sleep_until_monotonic(&(struct timespec){8,0}) == EAGAIN); CHECK(sleep_calls == 1); ++cases;
    reset_clock((struct timespec){(time_t)INT64_MAX - 1,900000000});
    CHECK(rt_host_sleep_until_monotonic(&(struct timespec){(time_t)INT64_MAX,100000000}) == 0);
    CHECK(equal(requests[0],(struct timespec){0,200000000})); ++cases;
}
static void audio_cases(void)
{
    int32_t (*initialize)(void) = function("libSceAudioOut","sceAudioOutInit");
    int32_t (*open_port)(int32_t,int32_t,int32_t,uint32_t,uint32_t,uint32_t) = function("libSceAudioOut","sceAudioOutOpen");
    int32_t (*output)(int32_t,const void *) = function("libSceAudioOut","sceAudioOutOutput");
    int32_t (*close_port)(int32_t) = function("libSceAudioOut","sceAudioOutClose");
    CHECK(initialize() == 0); const int32_t port = open_port(0xff,0,0,256,48000,4); CHECK(port > 0);
    float samples[512] = {0}; reset_clock((struct timespec){1,0});
    CHECK(output(port,samples) == 0); CHECK(sleep_calls == 0); ++cases;
    interrupt_budget = 3; CHECK(output(port,samples) == 0); CHECK(sleep_calls == 4);
    const long expected[] = {5333333,4333333,3333333,2333333};
    for (unsigned i = 0; i < 4; ++i) CHECK(equal(requests[i],(struct timespec){0,expected[i]}));
    CHECK(equal(now,(struct timespec){1,5333333})); ++cases;
    CHECK(output(port,NULL) == 0); CHECK(equal(now,(struct timespec){1,10666666})); ++cases;
    const unsigned before = sleep_calls; CHECK(output(port,NULL) == 0); CHECK(sleep_calls == before); ++cases;
    CHECK(output(port,samples) == 0); fail_clock_on = clock_calls + 2; expected_fatal = "sceAudioOutOutput";
    if (!setjmp(fatal_return)) { output(port,samples); CHECK(0); }
    expected_fatal = NULL; fail_clock_on = 0; ++cases;
    /* An error path released the port lock, and a later flush can still acquire it. */
    CHECK(output(port,NULL) == 0); CHECK(equal(now,(struct timespec){1,15999999})); ++cases;
    CHECK(output(port,samples) == 0); fail_clock_on = clock_calls + 1;
    expected_fatal = "sceAudioOutOutput"; expected_reason = "monotonic clock read";
    if (!setjmp(fatal_return)) { output(port,samples); CHECK(0); }
    fail_clock_on = 0; expected_reason = "monotonic wait"; ++cases;
    sleep_error = EAGAIN;
    if (!setjmp(fatal_return)) { output(port,samples); CHECK(0); }
    sleep_error = 0; expected_fatal = NULL; ++cases;
    CHECK(output(port,NULL) == 0); CHECK(equal(now,(struct timespec){1,21333332}));
    CHECK(close_port(port) == 0);
}
static void video_cases(void)
{
    int32_t (*open_port)(int32_t,int32_t,int32_t,const void *) = function("libSceVideoOut","sceVideoOutOpen");
    uint64_t (*attribute)(void *,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t) = function("libSceVideoOut","sceVideoOutSetBufferAttribute");
    int32_t (*register_buffers)(int32_t,int32_t,void *const *,int32_t,const rt_video_out_buffer_attribute *) = function("libSceVideoOut","sceVideoOutRegisterBuffers");
    int32_t (*flip)(int32_t,int32_t,int32_t,int64_t) = function("libSceVideoOut","sceVideoOutSubmitFlip");
    int32_t (*close_port)(int32_t) = function("libSceVideoOut","sceVideoOutClose");
    const int32_t port = open_port(0,0,0,NULL); CHECK(port > 0);
    rt_video_out_buffer_attribute attrs; CHECK(attribute(&attrs,0x80000000,0,0,8,8,8) == 8);
    uint32_t pixels[64] = {0}; void *buffers[] = {pixels}; CHECK(register_buffers(port,0,buffers,1,&attrs) == 0);
    reset_clock((struct timespec){1,16666625}); expected_present = (struct timespec){1,16666626};
    CHECK(flip(port,0,1,7) == 0); CHECK(sleep_calls == 1); CHECK(equal(requests[0],(struct timespec){0,1})); ++cases;
    reset_clock((struct timespec){1,16666626}); expected_present = (struct timespec){1,33333292}; interrupt_budget = 2;
    CHECK(flip(port,0,1,8) == 0); CHECK(sleep_calls == 3); CHECK(equal(now,expected_present)); ++cases;
    reset_clock((struct timespec){2,0}); expected_present = now;
    CHECK(flip(port,0,2,9) == 0); CHECK(sleep_calls == 0); ++cases;
    reset_clock((struct timespec){7,0}); fail_clock_on = 2; expected_fatal = "sceVideoOutSubmitFlip";
    const unsigned before = presentations;
    if (!setjmp(fatal_return)) { flip(port,0,1,10); CHECK(0); }
    CHECK(presentations == before); expected_fatal = NULL; fail_clock_on = 0; ++cases;
    expected_present = (struct timespec){7,16666386};
    CHECK(flip(port,0,1,11) == 0); CHECK(equal(now,expected_present)); ++cases;
    reset_clock((struct timespec){8,0}); fail_clock_on = 1;
    expected_fatal = "sceVideoOutSubmitFlip"; expected_reason = "monotonic clock read";
    if (!setjmp(fatal_return)) { flip(port,0,1,12); CHECK(0); }
    CHECK(presentations == before + 1); fail_clock_on = 0; expected_reason = "monotonic wait"; ++cases;
    sleep_error = EAGAIN;
    if (!setjmp(fatal_return)) { flip(port,0,1,13); CHECK(0); }
    CHECK(presentations == before + 1); sleep_error = 0; expected_fatal = NULL; ++cases;
    CHECK(close_port(port) == 0);
}
int main(void)
{
    helper_cases(); audio_cases(); video_cases();
    printf("host pacing: %u cases, %u checks, %u failures\n",cases,checks,failures); return failures ? 1 : 0;
}
