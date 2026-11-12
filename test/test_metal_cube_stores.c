/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Owned witnesses for a deliberately bounded host-project cube-store policy:
   one six-face cube, zero-based prefix view, tile13, RGBA8 UNORM, WRITE only.
   DA0/DA1 use the same existing host translation; this is not hardware proof.
   No runtime tiling helper supplies either addresses or expected stored bytes. */
#include "runtime/render_executor.h"
#include "metal_cube_programs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

enum { POOL = 2 << 20, CODE = 0x1000, CODE_STEP = 0x100, OUTPUT = 0x10000,
       IMAGE = 0x20000, RAW = 0x80000, GUARD = 64, OUTPUT_BYTES = 256,
       CANARY = 0xa5, OUTPUT_UNWRITTEN = 0x6d, RAW_UNWRITTEN = 0x5a,
       PLANNED_SCENARIOS = 1173 };
static const uint64_t guest_base = UINT64_C(0x3000000000);
static unsigned scenarios, setups, raw_observations, checks, failures;
static char case_id[192] = "setup";
#define CHECK(value) do { ++checks; if (!(value)) { ++failures; if (failures <= 24) \
    fprintf(stderr,"%s:%d [%s]: %s\n",__FILE__,__LINE__,case_id,#value); } } while (0)

typedef struct { unsigned char select[4], raw[4]; } Permutation;
/* Independently literal inverse-store bytes for logical (.125,.3125,.5625,.8125).
   In particular p09=(1,2,3,0) stores physical(207,32,80,143), not a forward map. */
static const Permutation permutations[] = {
    {{0,1,2,3},{32,80,143,207}}, {{0,1,3,2},{32,80,207,143}},
    {{0,2,1,3},{32,143,80,207}}, {{0,2,3,1},{32,207,80,143}},
    {{0,3,1,2},{32,143,207,80}}, {{0,3,2,1},{32,207,143,80}},
    {{1,0,2,3},{80,32,143,207}}, {{1,0,3,2},{80,32,207,143}},
    {{1,2,0,3},{143,32,80,207}}, {{1,2,3,0},{207,32,80,143}},
    {{1,3,0,2},{143,32,207,80}}, {{1,3,2,0},{207,32,143,80}},
    {{2,0,1,3},{80,143,32,207}}, {{2,0,3,1},{80,207,32,143}},
    {{2,1,0,3},{143,80,32,207}}, {{2,1,3,0},{207,80,32,143}},
    {{2,3,0,1},{143,207,32,80}}, {{2,3,1,0},{207,143,32,80}},
    {{3,0,1,2},{80,143,207,32}}, {{3,0,2,1},{80,207,143,32}},
    {{3,1,0,2},{143,80,207,32}}, {{3,1,2,0},{207,80,143,32}},
    {{3,2,0,1},{143,207,80,32}}, {{3,2,1,0},{207,143,80,32}},
};
/* Thin microtile element indices, literal row-major (y,x). Public AMD thin order
   is x0,y0,x1,y1,x2,y2. Existing owned test_tiling.c also pins (3,5)->39,
   (8,0)->256B, (0,8)->512B at pitch16; this fixture does not call its helper. */
static const unsigned char thin[64] = {
     0, 1, 4, 5,16,17,20,21,  2, 3, 6, 7,18,19,22,23,
     8, 9,12,13,24,25,28,29, 10,11,14,15,26,27,30,31,
    32,33,36,37,48,49,52,53, 34,35,38,39,50,51,54,55,
    40,41,44,45,56,57,60,61, 42,43,46,47,58,59,62,63,
};
typedef struct { const uint32_t *words; size_t bytes; } Program;
#define PROGRAM(name) {name,sizeof name}
static const Program programs[] = {
    PROGRAM(cube_store_da0), PROGRAM(cube_store_da1),
    PROGRAM(cube_load_da0), PROGRAM(cube_load_da1),
    PROGRAM(cube_rw_da0), PROGRAM(cube_rw_da1), PROGRAM(cube_raw_copy),
    PROGRAM(cube_overlap_da0), PROGRAM(cube_overlap_da1)
};
#undef PROGRAM
enum { STORE0, STORE1, LOAD0, LOAD1, RW0, RW1, RAW_PROGRAM, OVERLAP0, OVERLAP1 };
typedef struct { unsigned char *pool; rt_executor *executor; } World;
typedef struct { unsigned side, pitch, padded_height, image; size_t slice, span; } Geometry;
typedef struct { int rc; char error[512]; } Result;
static const char type_reason[] = "compute image type 11 is outside 1D/2D arrays and the selected 3D volume profile";
static const char access_reason[] = "compute cube storage requires write-only access";
static const char mip_reason[] = "compute storage images require relative and physical mip zero";
static const char format_reason[] = "compressed and sRGB compute storage images are not modeled";
static const char selector_reason[] = "compute image swizzle must preserve its stored components or permute all four";
static const char pool_reason[] = "compute image is outside the GPU-visible pool";

static uint64_t at(unsigned offset) { return guest_base + offset; }
static uint32_t bits(float value) { uint32_t word; memcpy(&word,&value,4); return word; }
static void *allocate(size_t bytes) { void *p = malloc(bytes); if (!p) { perror("malloc"); exit(1); } return p; }
static World open_world(void)
{
    char path[] = "/tmp/paleblood-metal-cube-stores-XXXXXX", error[512] = "";
    int fd = mkstemp(path); if (fd < 0) { perror("mkstemp"); exit(1); }
    if (unlink(path) || ftruncate(fd,POOL)) { perror("pool"); close(fd); exit(1); }
    unsigned char *pool = mmap(NULL,POOL,PROT_READ | PROT_WRITE,MAP_SHARED,fd,0);
    if (pool == MAP_FAILED) { perror("mmap"); close(fd); exit(1); }
    memset(pool,CANARY,POOL);
    for (unsigned i = 0; i < sizeof programs / sizeof *programs; ++i) {
        CHECK(programs[i].bytes <= CODE_STEP); if (programs[i].bytes > CODE_STEP) exit(1);
        memcpy(pool + CODE + i * CODE_STEP,programs[i].words,programs[i].bytes);
    }
    rt_executor_memory memory = {fd,POOL,guest_base,NULL,NULL,NULL}; rt_executor *executor = NULL;
    if (rt_executor_open(&memory,&executor,error,sizeof error)) {
        fprintf(stderr,"open: %s\n",error); close(fd); munmap(pool,POOL); exit(1);
    }
    CHECK(close(fd) == 0); return (World){pool,executor};
}
static void close_world(World *world)
{
    rt_executor_close(world->executor); CHECK(munmap(world->pool,POOL) == 0);
}
static Geometry geometry(unsigned side, unsigned pitch, unsigned image)
{
    Geometry g = {side,pitch,(side + 7) / 8 * 8,image,0,0};
    CHECK(side && side <= 17 && pitch >= side && pitch % 8 == 0);
    g.slice = (size_t)pitch * g.padded_height * 4; g.span = 6 * g.slice;
    CHECK(g.span && g.span % 256 == 0 && g.span <= 32768 && image >= GUARD && image < POOL);
    return g;
}
static size_t offset(const Geometry *g, unsigned x, unsigned y, unsigned face)
{
    const size_t tile = (size_t)(y / 8) * (g->pitch / 8) + x / 8;
    return face * g->slice + tile * 256 + 4 * thin[8 * (y % 8) + x % 8];
}
static unsigned swizzle(unsigned p)
{
    unsigned value = 0;
    for (unsigned c = 0; c < 4; ++c) value |= (4u + permutations[p].select[c]) << (3 * c);
    return value;
}
static void descriptor(uint32_t words[8], const Geometry *g, unsigned last, unsigned selectors)
{
    const uint64_t address = at(g->image) >> 8; memset(words,0,32);
    words[0] = (uint32_t)address; words[1] = (uint32_t)(address >> 32) | 10u << 20;
    words[2] = (g->side - 1) | (g->side - 1) << 14;
    words[3] = selectors | 13u << 20 | (uint32_t)RT_IMAGE_CUBE << 28;
    words[4] = (g->pitch - 1) << 13; words[5] = last << 13;
}
static void buffer_descriptor(uint32_t words[4], unsigned address, unsigned bytes)
{
    words[0] = (uint32_t)at(address); words[1] = (uint32_t)(at(address) >> 32); words[2] = bytes; words[3] = 0;
}
static void values(uint32_t user[16])
{
    user[0] = bits(0.125f); user[1] = bits(0.3125f); user[2] = bits(0.5625f); user[3] = bits(0.8125f);
}
static void initialize(World *world, const Geometry *g, unsigned seed)
{
    const size_t available = g->span < POOL - g->image ? g->span : POOL - g->image;
    memset(world->pool + g->image - GUARD,CANARY,GUARD);
    /* Every initialized byte is below24; each selected UNORM result is at least31,
       so changed-byte witnesses cannot accidentally equal their prior contents. */
    for (size_t i = 0; i < available; ++i) world->pool[g->image + i] = (unsigned char)(1 + (13 * i + 7 * seed) % 23);
    if (g->image + g->span + GUARD <= POOL) memset(world->pool + g->image + g->span,CANARY,GUARD);
    memset(world->pool + OUTPUT - GUARD,CANARY,OUTPUT_BYTES + 2 * GUARD);
    memset(world->pool + OUTPUT,OUTPUT_UNWRITTEN,OUTPUT_BYTES);
    memset(world->pool + RAW - GUARD,CANARY,g->span + 2 * GUARD);
    memset(world->pool + RAW,RAW_UNWRITTEN,g->span);
}
static void guards(const World *world, unsigned address, size_t span)
{
    for (unsigned i = 0; i < GUARD; ++i) {
        CHECK(world->pool[address - GUARD + i] == CANARY);
        if (address + span + GUARD <= POOL) CHECK(world->pool[address + span + i] == CANARY);
    }
}
static void output_untouched(const World *world)
{
    for (unsigned i = 0; i < OUTPUT_BYTES; ++i) CHECK(world->pool[OUTPUT + i] == OUTPUT_UNWRITTEN);
    guards(world,OUTPUT,OUTPUT_BYTES);
}
static size_t changed(const unsigned char *actual, const unsigned char *before, size_t span)
{
    size_t count = 0; for (size_t i = 0; i < span; ++i) count += actual[i] != before[i]; return count;
}
static size_t output_changed(const World *world)
{
    size_t count = 0; for (unsigned i = 0; i < OUTPUT_BYTES; ++i) count += world->pool[OUTPUT + i] != OUTPUT_UNWRITTEN; return count;
}
static Result execute(World *world, unsigned program, const uint32_t user[16], unsigned lanes, unsigned groups, const char *expected)
{
    const int raw = program == RAW_PROGRAM; rt_render_call calls[4] = {{0}}; Result result = {0,{0}};
    calls[0].kind = RT_CALL_COMPUTE_SHADER;
    calls[0].u.compute_shader = (rt_compute_shader){.code=at(CODE + program * CODE_STEP),.vgprs=12,.sgprs=24,
        .user_sgprs=raw ? 8u : 16u,.workgroup_id_enables=raw ? 1u : 0u,.thread_id_components=2,
        .threads={raw ? 64u : lanes,1,1}};
    calls[1].kind = RT_CALL_USER_DATA; calls[1].u.user_data.stage = RT_STAGE_COMPUTE; calls[1].u.user_data.count = 8;
    memcpy(calls[1].u.user_data.words,user,8 * sizeof *user);
    if (!raw) {
        calls[2].kind = RT_CALL_USER_DATA; calls[2].u.user_data.stage = RT_STAGE_COMPUTE;
        calls[2].u.user_data.slot = 8; calls[2].u.user_data.count = 8;
        memcpy(calls[2].u.user_data.words,user + 8,8 * sizeof *user);
    }
    const unsigned dispatch = raw ? 2u : 3u; calls[dispatch].kind = RT_CALL_DISPATCH;
    calls[dispatch].u.dispatch.groups[0] = groups;
    calls[dispatch].u.dispatch.groups[1] = calls[dispatch].u.dispatch.groups[2] = 1;
    result.rc = rt_executor_run_calls(world->executor,calls,dispatch + 1,result.error,sizeof result.error);
    if (expected) {
        if (result.rc != -1 || !strstr(result.error,expected))
            fprintf(stderr,"[%s] expected %s, got %d: %s\n",case_id,expected,result.rc,result.error);
        CHECK(result.rc == -1 && strstr(result.error,expected));
    } else {
        if (result.rc) fprintf(stderr,"[%s] execute got%d: %s\n",case_id,result.rc,result.error);
        CHECK(result.rc == 0);
    }
    return result;
}
static void compare(const unsigned char *actual, const unsigned char *expected, const unsigned char *written, size_t span)
{
    for (size_t i = 0; i < span; ++i) {
        if (written && written[i]) CHECK(abs((int)actual[i] - (int)expected[i]) <= 1);
        else CHECK(actual[i] == expected[i]);
    }
}
static void raw_observer(World *world, const Geometry *g, const unsigned char *expected, const unsigned char *written)
{
    char saved[sizeof case_id]; memcpy(saved,case_id,sizeof saved);
    snprintf(case_id,sizeof case_id,"%.170s/raw",saved); ++raw_observations;
    uint32_t user[16] = {0}; buffer_descriptor(user,g->image,(unsigned)g->span); buffer_descriptor(user + 4,RAW,(unsigned)g->span);
    const Result result = execute(world,RAW_PROGRAM,user,64,(unsigned)(g->span / 256),NULL);
    if (!result.rc) compare(world->pool + RAW,expected,written,g->span);
    guards(world,RAW,g->span); output_untouched(world); memcpy(case_id,saved,sizeof case_id);
}
static void success(World *world, const char *name, const Geometry *g, unsigned last, unsigned p, unsigned da,
                    uint32_t x, uint32_t y, uint32_t face, unsigned lanes, unsigned seed, unsigned valid_lanes, int setup)
{
    snprintf(case_id,sizeof case_id,"%s",name); if (setup) ++setups; else ++scenarios;
    const unsigned failures_before = failures;
    CHECK(g->image + g->span <= POOL && last < 6 && p < 24 && da < 2 && lanes && lanes <= 7);
    initialize(world,g,seed);
    unsigned char *before = allocate(g->span), *expected = allocate(g->span), *written = allocate(g->span);
    memcpy(before,world->pool + g->image,g->span); memcpy(expected,before,g->span); memset(written,0,g->span);
    unsigned expected_lanes = 0;
    for (unsigned lane = 0; lane < lanes; ++lane) {
        const uint32_t coordinate_x = x + lane;
        if (coordinate_x >= g->side || y >= g->side || face > last || face >= 6) continue;
        ++expected_lanes; const size_t address = offset(g,coordinate_x,y,face); CHECK(address + 4 <= g->span);
        for (unsigned c = 0; c < 4; ++c) { expected[address + c] = permutations[p].raw[c]; written[address + c] = 1; }
    }
    CHECK(expected_lanes == valid_lanes);
    uint32_t user[16] = {0}; values(user); user[4] = y; user[5] = face; user[6] = x;
    descriptor(user + 8,g,last,swizzle(p));
    const Result result = execute(world,da ? STORE1 : STORE0,user,lanes,1,NULL);
    const size_t image_changes = changed(world->pool + g->image,before,g->span), output_changes = output_changed(world);
    CHECK(image_changes == 4u * valid_lanes); CHECK(output_changes == 0);
    compare(world->pool + g->image,expected,written,g->span); guards(world,g->image,g->span); output_untouched(world);
    if (!result.rc) raw_observer(world,g,expected,written);
    printf("cube_case kind=%s id=%s rc=%d image_changed_bytes=%zu output_changed_bytes=%zu expected_written_lanes=%u failures_delta=%u\n",
        setup ? "setup" : valid_lanes ? "write" : "suppressed",case_id,result.rc,image_changes,output_changes,valid_lanes,failures - failures_before);
    free(before); free(expected); free(written);
}
static void matrix(void)
{
    static const unsigned sides[] = {1,3,8,9,17};
    for (unsigned da = 0; da < 2; ++da) for (unsigned p = 0; p < 24; ++p) {
        /* At most21 raw descriptor identities and four shader pipelines per world;
           reset would retain caches, so matrix worlds are actually closed. */
        World world = open_world();
        for (unsigned last = 0; last < 6; ++last) for (unsigned face = 0; face <= last; ++face) {
            const unsigned side = sides[(p + last + face + da) % 5];
            const unsigned pitch = (side + 7) / 8 * 8 + 8 * ((p + face + da) % 3);
            const Geometry g = geometry(side,pitch,IMAGE); const unsigned lanes = side < 7 ? side : 7;
            char name[192]; snprintf(name,sizeof name,"matrix/da%u/p%02u/last%u/face%u/s%u/pitch%u",da,p,last,face,side,pitch);
            success(&world,name,&g,last,p,da,side - lanes,(3 * p + face + last + da) % side,face,lanes,
                    11 + 29 * p + 7 * last + face + da,lanes,0);
        }
        close_world(&world);
    }
}
static void suppressed(void)
{
    static const char *const labels[] = {"next-face","face6","face-max","x-end","y-end","x-max","y-max","mixed-x-edge"};
    const Geometry g = geometry(9,24,IMAGE);
    for (unsigned da = 0; da < 2; ++da) {
        World world = open_world();
        for (unsigned last = 0; last < 6; ++last) for (unsigned item = 0; item < 8; ++item) {
            uint32_t x = 2, y = 4, face = last; unsigned lanes = 1, valid = 0;
            if (item == 0) face = last + 1;
            if (item == 1) face = 6;
            if (item == 2) face = UINT32_MAX;
            if (item == 3) x = 9;
            if (item == 4) y = 9;
            if (item == 5) x = UINT32_MAX;
            if (item == 6) y = UINT32_MAX;
            if (item == 7) { x = 8; lanes = 2; valid = 1; }
            char name[192]; snprintf(name,sizeof name,"bounds/da%u/last%u/%s",da,last,labels[item]);
            /* OOB stores are suppressed successfully. A mixed edge still writes
               its one in-range lane, so dropping the dispatch is distinguishable. */
            success(&world,name,&g,last,9,da,x,y,face,lanes,101 + 11 * item + last,valid,0);
        }
        close_world(&world);
    }
}
static void cache_transitions(void)
{
    static const struct { unsigned last,p,face; } sequence[] = {
        {5,0,5},{0,0,0},{4,14,4},{2,9,2},{5,18,5},{5,0,1},{0,0,0}
    };
    const Geometry g = geometry(17,32,IMAGE);
    for (unsigned da = 0; da < 2; ++da) {
        World world = open_world();
        for (unsigned step = 0; step < sizeof sequence / sizeof *sequence; ++step) for (unsigned refresh = 0; refresh < 2; ++refresh) {
            char name[192]; snprintf(name,sizeof name,"cache/da%u/step%u/cpu%u",da,step,refresh);
            /* Same code/address, no reset, changed CPU bytes between every call. */
            success(&world,name,&g,sequence[step].last,sequence[step].p,da,10,16,sequence[step].face,7,
                    211 + 31 * step + 67 * refresh,7,0);
        }
        close_world(&world);
    }
}
static void full_span_success(void)
{
    Geometry g = geometry(9,16,IMAGE); g.image = POOL - (unsigned)g.span;
    World world = open_world();
    for (unsigned da = 0; da < 2; ++da) for (unsigned item = 0; item < 2; ++item) {
        const unsigned last = item ? 5u : 0u; char name[192]; snprintf(name,sizeof name,"pool-exact/da%u/last%u",da,last);
        /* Exact pool-end fits have a leading canary and the actual mapping end,
           rather than pretending an accessible trailing guard exists. */
        success(&world,name,&g,last,9,da,2,8,last,7,311 + da + item,7,0);
    }
    close_world(&world);
}
enum { BAD_READ, BAD_RW, BAD_MULTI, BAD_BASE, BAD_BASE_GT_LAST, BAD_LAST6, BAD_LAST_MAX,
       BAD_NONSQUARE, BAD_POW2, BAD_PITCH, BAD_TILE31, BAD_TILE9, BAD_TILE14, BAD_TILE19,
       BAD_R8, BAD_UINT, BAD_SRGB, BAD_COMPRESSED, BAD_LAST_MIP, BAD_BASE_MIP,
       BAD_ZERO, BAD_REPEAT0, BAD_REPEAT1, BAD_REPEAT2, BAD_REPEAT3, BAD_ONE, BAD_RESERVED2, BAD_RESERVED3, BAD_UNKNOWN };
typedef struct { const char *label; unsigned mutation,da; const char *reason; } Bad;
/* The generic unsupported-type prefix deliberately also covers Cube descriptors
   outside the selected profile; these are distinct from successful OOB stores. */
static const Bad bad[] = {
    {"read-da0",BAD_READ,0,access_reason}, {"read-da1",BAD_READ,1,access_reason},
    {"rw-da0",BAD_RW,0,access_reason}, {"rw-da1",BAD_RW,1,access_reason},
    {"multi-cube",BAD_MULTI,1,type_reason}, {"nonzero-base",BAD_BASE,1,type_reason},
    {"base-after-last",BAD_BASE_GT_LAST,1,type_reason}, {"last6",BAD_LAST6,1,type_reason},
    {"last8191",BAD_LAST_MAX,1,type_reason}, {"non-square",BAD_NONSQUARE,1,type_reason},
    {"pow2-pad",BAD_POW2,1,type_reason}, {"unaligned-pitch15",BAD_PITCH,1,type_reason},
    {"tile31",BAD_TILE31,1,type_reason}, {"tile9",BAD_TILE9,1,type_reason},
    {"tile14",BAD_TILE14,1,type_reason}, {"tile19",BAD_TILE19,1,type_reason},
    {"format-r8",BAD_R8,1,type_reason}, {"format-uint",BAD_UINT,1,type_reason},
    {"srgb",BAD_SRGB,1,format_reason}, {"compressed",BAD_COMPRESSED,1,format_reason},
    {"last-mip1",BAD_LAST_MIP,1,mip_reason}, {"base-last-mip1",BAD_BASE_MIP,1,mip_reason},
    {"all-zero-selectors",BAD_ZERO,1,selector_reason},
    {"repeat0",BAD_REPEAT0,1,selector_reason}, {"repeat1",BAD_REPEAT1,1,selector_reason},
    {"repeat2",BAD_REPEAT2,1,selector_reason}, {"repeat3",BAD_REPEAT3,1,selector_reason},
    {"constant-one",BAD_ONE,1,selector_reason}, {"reserved2",BAD_RESERVED2,1,selector_reason},
    {"reserved3",BAD_RESERVED3,1,selector_reason},
    {"unknown-format-pair",BAD_UNKNOWN,1,"image DATA_FORMAT 1, NUM_FORMAT 9 is not modeled"},
};
static void refusal(World *world, const char *name, const Geometry *g, uint32_t words[8], unsigned program, const char *reason)
{
    snprintf(case_id,sizeof case_id,"%s",name); ++scenarios; const unsigned failures_before = failures;
    initialize(world,g,419 + scenarios); uint32_t user[16] = {0};
    if (program == LOAD0 || program == LOAD1 || program == RW0 || program == RW1) buffer_descriptor(user,OUTPUT,OUTPUT_BYTES);
    else values(user);
    user[4] = 4; user[5] = 0; user[6] = 2; memcpy(user + 8,words,32);
    unsigned char *before = allocate(POOL); memcpy(before,world->pool,POOL);
    const Result result = execute(world,program,user,3,1,reason);
    const size_t available = g->span < POOL - g->image ? g->span : POOL - g->image;
    const size_t pool_changes = changed(world->pool,before,POOL);
    const size_t image_changes = changed(world->pool + g->image,before + g->image,available);
    const size_t output_changes = changed(world->pool + OUTPUT,before + OUTPUT,OUTPUT_BYTES);
    CHECK(pool_changes == 0); CHECK(memcmp(world->pool,before,POOL) == 0);
    printf("cube_case kind=refusal id=%s rc=%d image_changed_bytes=%zu output_changed_bytes=%zu pool_changed_bytes=%zu failures_delta=%u\n",
        case_id,result.rc,image_changes,output_changes,pool_changes,failures - failures_before);
    free(before);
}
static void refusals(void)
{
    const Geometry g = geometry(9,16,IMAGE); World world = open_world();
    for (unsigned item = 0; item < sizeof bad / sizeof *bad; ++item) {
        const Bad *b = bad + item; uint32_t words[8]; descriptor(words,&g,5,swizzle(0));
        unsigned program = b->da ? STORE1 : STORE0;
        if (b->mutation == BAD_READ) program = b->da ? LOAD1 : LOAD0;
        else if (b->mutation == BAD_RW) program = b->da ? RW1 : RW0;
        else if (b->mutation == BAD_MULTI) words[4] |= 1;
        else if (b->mutation == BAD_BASE) words[5] |= 1;
        else if (b->mutation == BAD_BASE_GT_LAST) words[5] = 1;
        else if (b->mutation == BAD_LAST6) words[5] = 6u << 13;
        else if (b->mutation == BAD_LAST_MAX) words[5] = 8191u << 13;
        else if (b->mutation == BAD_NONSQUARE) words[2] = (g.side - 1) | 7u << 14;
        else if (b->mutation == BAD_POW2) words[3] |= 1u << 25;
        else if (b->mutation == BAD_PITCH) words[4] = 14u << 13;
        else if (b->mutation >= BAD_TILE31 && b->mutation <= BAD_TILE19) {
            static const unsigned tiles[] = {31,9,14,19}; words[3] = (words[3] & ~(31u << 20)) | tiles[b->mutation - BAD_TILE31] << 20;
        } else if (b->mutation == BAD_R8) words[1] = (words[1] & ~(63u << 20)) | 1u << 20;
        else if (b->mutation == BAD_UINT) words[1] |= 4u << 26;
        else if (b->mutation == BAD_SRGB) words[1] |= 9u << 26;
        else if (b->mutation == BAD_COMPRESSED) words[1] = (words[1] & ~(63u << 20)) | 35u << 20;
        else if (b->mutation == BAD_LAST_MIP) words[3] |= 1u << 16;
        else if (b->mutation == BAD_BASE_MIP) words[3] |= 1u << 12 | 1u << 16;
        else if (b->mutation == BAD_ZERO) words[3] &= ~4095u;
        else if (b->mutation >= BAD_REPEAT0 && b->mutation <= BAD_REPEAT3) {
            const unsigned c = b->mutation - BAD_REPEAT0;
            words[3] = (words[3] & ~(7u << (3 * c))) | (4u + (c + 1) % 4) << (3 * c);
        } else if (b->mutation >= BAD_ONE && b->mutation <= BAD_RESERVED3)
            words[3] = (words[3] & ~7u) | (1u + b->mutation - BAD_ONE);
        else if (b->mutation == BAD_UNKNOWN) words[1] = (words[1] & ~(63u << 20)) | 1u << 20 | 9u << 26;
        if (b->mutation >= BAD_ZERO && b->mutation <= BAD_REPEAT3) {
            char setup[192]; snprintf(setup,sizeof setup,"refusal-setup/%s",b->label);
            success(&world,setup,&g,5,0,1,2,4,0,3,503 + item,3,1);
        }
        char name[192]; snprintf(name,sizeof name,"refusal/%s",b->label);
        refusal(&world,name,&g,words,program,b->reason);
    }
    for (unsigned da = 0; da < 2; ++da) for (unsigned item = 0; item < 2; ++item) {
        Geometry edge = g;
        /* prefix0 first-face fits exactly, or prefix5 misses only its last256B:
           both still require the complete physical six-face allocation. */
        edge.image = item ? POOL - (unsigned)edge.span + 256 : POOL - (unsigned)edge.slice;
        uint32_t words[8]; descriptor(words,&edge,item ? 5u : 0u,swizzle(9));
        char name[192]; snprintf(name,sizeof name,"refusal/full-six-face-span/da%u/%s",da,item ? "last256-missing" : "first-face-only");
        refusal(&world,name,&edge,words,da ? STORE1 : STORE0,pool_reason);
    }
    close_world(&world);
}
/* Both native allocations fit: A's one-face view and B's one-face view do
   not visibly intersect, but B begins at A's physical face5. The complete
   six-face footprints overlap and must be refused before either store runs. */
static void overlap_refusals(void)
{
    const Geometry a = geometry(1,8,IMAGE); Geometry b = a;
    b.image += 5 * (unsigned)a.slice;
    Geometry footprint = a; footprint.span = 11 * a.slice;
    CHECK(a.slice == 256 && a.span == 1536 && b.image == IMAGE + 1280);
    CHECK(b.image + b.span == a.image + footprint.span && a.image + footprint.span + GUARD <= POOL);
    World world = open_world();
    for (unsigned da = 0; da < 2; ++da) {
        snprintf(case_id,sizeof case_id,"refusal/overlapping-cube-prefix/da%u",da); ++scenarios;
        const unsigned failures_before = failures;
        /* initialize() uses only this union's byte span, not its shape, for the
           snapshot fill and guards; neither descriptor uses the union as a cube. */
        initialize(&world,&footprint,733 + da);
        uint32_t user[16]; descriptor(user,&a,0,swizzle(0)); descriptor(user + 8,&b,0,swizzle(0));
        unsigned char *before = allocate(POOL); memcpy(before,world.pool,POOL);
        const Result result = execute(&world,da ? OVERLAP1 : OVERLAP0,user,1,1,
                                      "overlapping compute images require the same native view");
        const size_t pool_changes = changed(world.pool,before,POOL);
        const size_t image_changes = changed(world.pool + a.image,before + a.image,footprint.span);
        const size_t output_changes = changed(world.pool + OUTPUT,before + OUTPUT,OUTPUT_BYTES);
        const size_t first_changes = changed(world.pool + a.image,before + a.image,4);
        const size_t shared_changes = changed(world.pool + b.image,before + b.image,4);
        CHECK(pool_changes == 0); CHECK(memcmp(world.pool,before,POOL) == 0);
        CHECK(first_changes == 0 && shared_changes == 0 && output_changes == 0);
        printf("cube_case kind=refusal id=%s rc=%d image_changed_bytes=%zu output_changed_bytes=%zu pool_changed_bytes=%zu "
               "a_face0_changed_bytes=%zu shared_a_face5_b_face0_changed_bytes=%zu failures_delta=%u\n",
            case_id,result.rc,image_changes,output_changes,pool_changes,first_changes,shared_changes,failures - failures_before);
        free(before);
    }
    close_world(&world);
}

static void literal_oracles(void)
{
    static const unsigned char logical[] = {32,80,143,207}, cycle[] = {207,32,80,143};
    CHECK(sizeof permutations / sizeof *permutations == 24);
    for (unsigned p = 0; p < 24; ++p) {
        unsigned mask = 0;
        for (unsigned c = 0; c < 4; ++c) {
            CHECK(permutations[p].select[c] < 4); mask |= 1u << permutations[p].select[c];
            CHECK(permutations[p].raw[permutations[p].select[c]] == logical[c]);
        }
        CHECK(mask == 15);
        for (unsigned q = 0; q < p; ++q) CHECK(memcmp(permutations[p].select,permutations[q].select,4) != 0);
    }
    CHECK(memcmp(permutations[9].raw,cycle,4) == 0);
    unsigned seen[64] = {0}; for (unsigned i = 0; i < 64; ++i) { CHECK(thin[i] < 64); ++seen[thin[i]]; }
    for (unsigned i = 0; i < 64; ++i) CHECK(seen[i] == 1);
    const Geometry g = geometry(9,16,IMAGE);
    CHECK(offset(&g,1,0,0) == 4 && offset(&g,0,1,0) == 8);
    CHECK(offset(&g,2,0,0) == 16 && offset(&g,0,2,0) == 32);
    CHECK(offset(&g,4,0,0) == 64 && offset(&g,0,4,0) == 128);
    CHECK(offset(&g,3,5,0) == 156 && offset(&g,8,0,0) == 256);
    CHECK(offset(&g,0,8,0) == 512 && offset(&g,8,8,0) == 768);
    CHECK(g.slice == 1024 && g.span == 6144 && offset(&g,0,0,5) == 5120);
}
int main(void)
{
    literal_oracles(); matrix(); suppressed(); cache_transitions(); full_span_success(); refusals(); overlap_refusals();
    snprintf(case_id,sizeof case_id,"complete"); CHECK(scenarios == PLANNED_SCENARIOS); CHECK(setups == 5);
    printf("metal cube stores host policy: %u scenarios, %u named setups, %u raw observations, %u checks, %u failures\n",
        scenarios,setups,raw_observations,checks,failures);
    return failures ? 1 : 0;
}
