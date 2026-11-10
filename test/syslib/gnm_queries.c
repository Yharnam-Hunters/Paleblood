/* SPDX-License-Identifier: GPL-2.0-or-later */
/* GNM queries and debug calls (runtime/syslib/gnm.c): compute queue unmapping and priority maps,
   submissions allowed, the event type, the geometry ring sizes, the mip statistics report and the
   retail answers of the capture tools. */
#include "../syslib_test.h"

#include <stdint.h>
#include <string.h>

typedef int32_t (*void_fn)(void);
typedef int32_t (*map_fn)(uint32_t, uint32_t, void *, uint32_t, volatile uint32_t *);
typedef int32_t (*map_priority_fn)(uint32_t, uint32_t, void *, uint32_t, volatile uint32_t *, uint32_t);
typedef int32_t (*id_fn)(uint32_t);
typedef void (*ding_fn)(uint32_t, uint32_t);
typedef int32_t (*event_fn)(const void *);
typedef int32_t (*rings_fn)(uint32_t, uint32_t);
typedef int32_t (*mip_fn)(void *, uint32_t, uint8_t, uint8_t, uint32_t);

static id_fn unmap;
static ding_fn ding;
static event_fn event_type;
static uint32_t ring[64];
static volatile uint32_t read_pointer;
static int32_t unmapped_id;

static void unmap_unknown(void) { unmap(unmapped_id); }
static void unmap_zero(void) { unmap(0); }
static void unmap_past_end(void) { unmap(60); }
static void ding_unmapped(void) { ding((uint32_t)unmapped_id, 0); }
static void event_null(void) { event_type(NULL); }

SYSLIB_TEST(gnm_queries)
{
    const map_fn map = (map_fn)fn("libSceGnmDriver", "sceGnmMapComputeQueue");
    const map_priority_fn map_priority = (map_priority_fn)fn("libSceGnmDriver", "sceGnmMapComputeQueueWithPriority");
    unmap = (id_fn)fn("libSceGnmDriver", "sceGnmUnmapComputeQueue");
    ding = (ding_fn)fn("libSceGnmDriver", "sceGnmDingDong");
    event_type = (event_fn)fn("libSceGnmDriver", "sceGnmGetEqEventType");
    const void_fn allowed = (void_fn)fn("libSceGnmDriver", "sceGnmAreSubmitsAllowed");
    void (*const flush)(void) = (void (*)(void))fn("libSceGnmDriver", "sceGnmFlushGarlic");
    const rings_fn rings = (rings_fn)fn("libSceGnmDriver", "sceGnmSetGsRingSizes");
    const mip_fn mip_setup = (mip_fn)fn("libSceGnmDriver", "sceGnmSetupMipStatsReport");
    const void_fn mip_request = (void_fn)fn("libSceGnmDriver", "sceGnmRequestMipStatsReportAndReset");
    const void_fn mip_disable = (void_fn)fn("libSceGnmDriver", "sceGnmDisableMipStatsReport");
    const void_fn razor_loaded = (void_fn)fn("libSceGnmDriver", "sceRazorIsLoaded");
    const void_fn capture_in_progress = (void_fn)fn("libSceGnmDriver", "sceGnmDriverCaptureInProgress");
    const void_fn trigger = (void_fn)fn("libSceGnmDriver", "sceGnmDriverTriggerCapture");
    if (!map || !map_priority || !unmap || !ding || !event_type || !allowed || !flush || !rings || !mip_setup || !mip_request ||
        !mip_disable || !razor_loaded || !capture_in_progress || !trigger)
        return;

    /* Unmap frees the id for the next map; the priority changes nothing. */
    const int32_t a = map(7, 0, ring, 64, &read_pointer), b = map_priority(7, 1, ring, 64, &read_pointer, 3);
    CHECK(a >= 1 && b == a + 1);
    CHECK(unmap((uint32_t)a) == 0);
    unmapped_id = a;
    CHECK(died_in_fatal(unmap_unknown) && died_in_fatal(ding_unmapped) && died_in_fatal(unmap_zero) && died_in_fatal(unmap_past_end));
    CHECK(map(7, 0, ring, 64, &read_pointer) == a); /* the same pipe and queue again, in the freed slot */
    ding((uint32_t)b, 0);
    /* Freed slots are taken first, and only then new ones. */
    CHECK(unmap((uint32_t)a) == 0 && unmap((uint32_t)b) == 0);
    CHECK(map(7, 2, ring, 64, &read_pointer) == a && map(7, 3, ring, 64, &read_pointer) == b);
    CHECK(map(7, 4, ring, 64, &read_pointer) == b + 1);
    for (int32_t id = a; id <= b + 1; id++) CHECK(unmap((uint32_t)id) == 0);

    CHECK(allowed() == 1);
    flush();
    uint8_t event[32] = {0};
    const int64_t data = 0x40;
    memcpy(event + 16, &data, sizeof data);
    CHECK(event_type(event) == 0x40);
    CHECK(died_in_fatal(event_null));

    CHECK(rings(0x400000, 0x800000) == 0 && rings(0x500000, 0x400000) == 0);
    CHECK(rings(0x300000, 0x400000) == 0x08000000 && rings(0x400000, 0x900000) == 0x08000000);
    CHECK(rings(0x480000, 0x400000) == 0x08000000 && rings(0x400000, 0x480000) == 0x08000000);

    static _Alignas(64) uint8_t report[0x1040 + 64];
    memset(report, 0xee, sizeof report);
    CHECK(mip_setup(report, 0x1040, 1, 1, 0) == 0);
    uint32_t header;
    memcpy(&header, report, 4);
    CHECK(header == 0x40 && report[4] == 0 && report[0x103f] == 0 && report[0x1040] == 0xee);
    CHECK(mip_setup(NULL, 0x1040, 1, 1, 0) == (int32_t)0x80000001u && mip_setup(report + 1, 0x1040, 1, 1, 0) == (int32_t)0x80000001u);
    CHECK(mip_setup(report, 0x1000, 1, 1, 0) == (int32_t)0x80000002u && mip_setup(report, 0x1080, 1, 1, 0) == (int32_t)0x80000002u);
    CHECK(mip_setup(report, 0x840, 1, 1, 0) == (int32_t)0x80000002u);
    CHECK(mip_request() == 0 && mip_disable() == 0);

    CHECK(razor_loaded() == 0 && capture_in_progress() == 0 && trigger() == (int32_t)0x80d15001u);
    const char *const captures[] = {"sceRazorCaptureImmediate", "sceRazorCaptureSinceLastFlip", "sceRazorCaptureCommandBuffersOnlyImmediate",
                                    "sceRazorCaptureCommandBuffersOnlySinceLastFlip"};
    for (int i = 0; i < 4; i++) {
        const void_fn capture = (void_fn)fn("libSceGnmDriver", captures[i]);
        CHECK(capture && capture() == (int32_t)0x80d1500fu);
    }
    CHECK(is_implemented("libSceGnmDriver", "sceGnmUnmapComputeQueue"));
}
