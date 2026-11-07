/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System call group: PlayGo (libScePlayGo), the system that lets a title start before all of its
   package is installed. A system module: the runtime provides it (game/modules.csv, action
   "runtime"). On this runtime the whole title is always present, so PlayGo answers that every
   chunk is installed.

   No public source gives these calls' declarations (the OpenOrbis toolchain has no PlayGo header).
   The shapes come from the executable's own calls:
   - scePlayGoInitialize(params): at 0x023dd67a and 0x023de849, after loading the module, with a
     16-byte block { 2 MiB buffer the title allocated (aligned to 16), its size 0x200000, 0 }.
     Neither call reads the result.
   - scePlayGoOpen(int32_t *handle, NULL) at 0x023deae4: a 32-bit handle is written back; 0 counts
     as opened, but the title goes on either way.
   - scePlayGoGetChunkId(handle, uint16_t ids[], 100, uint32_t *count) at 0x023deb0f: up to 100
     chunk IDs (16-bit); the result is not read. A count of 0 skips the next call.
   - scePlayGoGetLocus(handle, const uint16_t ids[], count, int8_t loci[]) at 0x023de8d6 and
     0x023deb30: one signed byte per chunk; a non-zero result counts as failure. The title then
     treats locus 0 as a chunk not there yet, 2 as one still arriving (it polls again), and every
     other value as ready.

   What the title's own package declares: its sce_sys/playgo-manifest.xml (in the person's own
   copy, served as /app0) gives chunk_count, 3 for this title; its sce_sys/playgo-chunk.dat agrees.
   The manifest lists no IDs, so the chunks are taken as 0 to chunk_count - 1.

   So: Initialize checks its block and answers 0; Open gives handle 1 (the runtime's choice: the
   title only passes it back); GetChunkId reports the package's chunks; GetLocus answers 3 for each,
   a ready value (the title cannot tell the ready values apart). Not established, and stopping by
   name: Open with parameters, an unknown handle, a chunk list longer than the space given, an
   unknown chunk ID, a missing manifest, and every error value.

   scePlayGoSetInstallSpeed(handle, int32_t speed) is called through a function pointer
   (0x023dd891), with speed 1 (trickle) or 2 (full); 0 is suspended. An unknown handle answers
   0x80B20009, a call before initialize 0x80B20005, another speed 0x80B20004 (shadPS4's
   libScePlayGo, playgo.cpp and playgo_types.h). Everything is installed already, so the speed is
   only recorded. */
#include "runtime/process.h"
#include "runtime/syslib.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { PLAYGO_HANDLE = 1, LOCUS_READY = 3, MAX_CHUNKS = 0x10000, MANIFEST_BYTES = 64 * 1024 };

typedef struct {
    void *buffer;
    uint32_t size;
    uint32_t reserved;
} init_params;
_Static_assert(sizeof(init_params) == 16, "the title passes a 16-byte block");

static int initialized, opened;
static long chunk_count = -1; /* from the package's manifest, read once */

/* chunk_count="N" in the title's sce_sys/playgo-manifest.xml. */
static long manifest_chunk_count(const char *caller)
{
    if (chunk_count >= 0) return chunk_count;
    if (!rt_process.app0_dir) rt_fatal(caller, "no /app0 directory given (tools/boot.py --app0)");
    char path[4096];
    snprintf(path, sizeof path, "%s/sce_sys/playgo-manifest.xml", rt_process.app0_dir);
    FILE *f = fopen(path, "rb");
    if (!f) rt_fatal(caller, "the title's sce_sys/playgo-manifest.xml cannot be read");
    static char text[MANIFEST_BYTES];
    const size_t n = fread(text, 1, sizeof text - 1, f);
    fclose(f);
    text[n] = 0;
    const char *at = strstr(text, "chunk_count=\"");
    char *end = NULL;
    const long count = at ? strtol(at + strlen("chunk_count=\""), &end, 10) : -1;
    if (!at || !end || *end != '"' || count < 1 || count > MAX_CHUNKS)
        rt_fatal(caller, "the title's playgo-manifest.xml gives no usable chunk_count");
    return chunk_count = count;
}

static int32_t initialize(const init_params *params)
{
    static const char fn[] = "scePlayGoInitialize";
    if (!params || !params->buffer || !params->size) rt_fatal(fn, "no work buffer: the error value is not established");
    initialized = 1; /* a second call answers 0 too: the PS4's answer is not established, and the title reads neither */
    return 0;
}

static int32_t open_playgo(int32_t *handle, const void *params)
{
    static const char fn[] = "scePlayGoOpen";
    if (!initialized) rt_fatal(fn, "called before scePlayGoInitialize: the error value is not established");
    if (!handle) rt_fatal(fn, "no variable for the handle: the error value is not established");
    if (params) rt_fatal(fn, "open parameters: what they hold is not established");
    *handle = PLAYGO_HANDLE;
    opened = 1;
    return 0;
}

static void check_handle(const char *fn, int32_t handle)
{
    if (!opened || handle != PLAYGO_HANDLE) rt_fatal(fn, "a handle scePlayGoOpen did not give: the error value is not established");
}

static int32_t get_chunk_id(int32_t handle, uint16_t *ids, uint32_t capacity, uint32_t *count)
{
    static const char fn[] = "scePlayGoGetChunkId";
    check_handle(fn, handle);
    if (!ids || !count) rt_fatal(fn, "no array or count: the error value is not established");
    const long chunks = manifest_chunk_count(fn);
    if ((long)capacity < chunks) rt_fatal(fn, "more chunks than the space given: what the system then answers is not established");
    for (long i = 0; i < chunks; i++) ids[i] = (uint16_t)i;
    *count = (uint32_t)chunks;
    return 0;
}

static int32_t get_locus(int32_t handle, const uint16_t *ids, uint32_t count, int8_t *loci)
{
    static const char fn[] = "scePlayGoGetLocus";
    check_handle(fn, handle);
    if (!ids || !loci) rt_fatal(fn, "no chunk list or answer array: the error value is not established");
    const long chunks = manifest_chunk_count(fn);
    for (uint32_t i = 0; i < count; i++) {
        if (ids[i] >= chunks) rt_fatal(fn, "a chunk ID the package does not have: the error value is not established");
        loci[i] = LOCUS_READY;
    }
    return 0;
}

#define PLAYGO_ERROR_INVALID_ARGUMENT ((int32_t)0x80B20004u)
#define PLAYGO_ERROR_NOT_INITIALIZED ((int32_t)0x80B20005u)
#define PLAYGO_ERROR_BAD_HANDLE ((int32_t)0x80B20009u)
enum { SPEED_SUSPENDED = 0, SPEED_FULL = 2 };

static int32_t set_install_speed(int32_t handle, int32_t speed)
{
    if (!opened || handle != PLAYGO_HANDLE) return PLAYGO_ERROR_BAD_HANDLE;
    if (!initialized) return PLAYGO_ERROR_NOT_INITIALIZED;
    if (speed < SPEED_SUSPENDED || speed > SPEED_FULL) return PLAYGO_ERROR_INVALID_ARGUMENT;
    return 0; /* accepted: everything is installed, so there is nothing to pace */
}

RT_SYSLIB("libScePlayGo", scePlayGoInitialize, initialize);
RT_SYSLIB("libScePlayGo", scePlayGoOpen, open_playgo);
RT_SYSLIB("libScePlayGo", scePlayGoGetChunkId, get_chunk_id);
RT_SYSLIB("libScePlayGo", scePlayGoGetLocus, get_locus);
RT_SYSLIB("libScePlayGo", scePlayGoSetInstallSpeed, set_install_speed);
