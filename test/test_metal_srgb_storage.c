/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Owned witnesses for the upstream write-only RGBA8 sRGB-to-UNORM compatibility
   policy. This is not a GFX7 sRGB-store legality test: native writes deliberately
   use UNORM without gamma encoding. Every observed image byte is canonical data. */
#include "runtime/render_executor.h"
#include "runtime/tiling.h"
#include "metal_permutation_programs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

enum { POOL = 2 << 20, CODE = 0x1000, CODE_STEP = 0x100, OUTPUT = 0x10000,
       IMAGE = 0x20000, RAW = 0x60000, GUARD = 64, OUTPUT_BYTES = 256,
       CANARY = 0xa5, RAW_UNWRITTEN = 0x5a, WIDTH = 11, HEIGHT = 5,
       PITCH = 64, LANES = 7, FIRST_X = 2, PLANNED_SCENARIOS = 357 };
static const uint64_t guest_base = UINT64_C(0x3000000000);
static const uint32_t unwritten = UINT32_C(0x7fc0dead);
static unsigned scenarios, checks, failures;
static char case_id[192] = "setup";
#define CHECK(value) do { ++checks; if (!(value)) { ++failures; if (failures <= 24) \
    fprintf(stderr,"%s:%d [%s]: %s\n",__FILE__,__LINE__,case_id,#value); } } while (0)

typedef struct { unsigned char select[4], raw[4]; } Permutation;
/* Literal forward selectors and physical stored bytes. The logical constants are
   (.125,.3125,.5625,.8125), giving raw UNORM(32,80,143,207). Their unequal alpha
   and nonendpoint RGB distinguish raw packing from sRGB gamma encoding. */
static const Permutation permutations[] = {
    {{0,1,2,3},{32,80,143,207}},
    {{0,1,3,2},{32,80,207,143}},
    {{0,2,1,3},{32,143,80,207}},
    {{0,2,3,1},{32,207,80,143}},
    {{0,3,1,2},{32,143,207,80}},
    {{0,3,2,1},{32,207,143,80}},
    {{1,0,2,3},{80,32,143,207}},
    {{1,0,3,2},{80,32,207,143}},
    {{1,2,0,3},{143,32,80,207}},
    {{1,2,3,0},{207,32,80,143}},
    {{1,3,0,2},{143,32,207,80}},
    {{1,3,2,0},{207,32,143,80}},
    {{2,0,1,3},{80,143,32,207}},
    {{2,0,3,1},{80,207,32,143}},
    {{2,1,0,3},{143,80,32,207}},
    {{2,1,3,0},{207,80,32,143}},
    {{2,3,0,1},{143,207,32,80}},
    {{2,3,1,0},{207,143,32,80}},
    {{3,0,1,2},{80,143,207,32}},
    {{3,0,2,1},{80,207,143,32}},
    {{3,1,0,2},{143,80,207,32}},
    {{3,1,2,0},{207,80,143,32}},
    {{3,2,0,1},{143,207,80,32}},
    {{3,2,1,0},{207,143,80,32}},
};
typedef struct { const uint32_t *words; size_t bytes; } Program;
#define PROGRAM(name) {name,sizeof name}
static const Program programs[] = {
    PROGRAM(permutation_load_1d),
    PROGRAM(permutation_store_1d),
    PROGRAM(permutation_rw_float_1d),
    PROGRAM(permutation_rw_uint_1d),
    PROGRAM(permutation_load_2d),
    PROGRAM(permutation_store_2d),
    PROGRAM(permutation_rw_float_2d),
    PROGRAM(permutation_rw_uint_2d),
    PROGRAM(permutation_load_1d_array),
    PROGRAM(permutation_store_1d_array),
    PROGRAM(permutation_rw_float_1d_array),
    PROGRAM(permutation_rw_uint_1d_array),
    PROGRAM(permutation_load_2d_array),
    PROGRAM(permutation_store_2d_array),
    PROGRAM(permutation_rw_float_2d_array),
    PROGRAM(permutation_rw_uint_2d_array),
    PROGRAM(permutation_raw_copy)
};
#undef PROGRAM
static const unsigned types[] = {RT_IMAGE_1D,RT_IMAGE_2D,RT_IMAGE_1D_ARRAY,RT_IMAGE_2D_ARRAY};
static const char *const dimensions[] = {"1d","2d","1d-array","2d-array"};
static const char *const accesses[] = {"load","store","read-write"};
enum { LOAD, STORE, READ_WRITE, RAW_PROGRAM = 16 };
typedef struct { unsigned char *pool; rt_executor *executor; } World;
typedef struct { rt_tiling layout; size_t span; unsigned height, layers; } Geometry;

static uint64_t at(unsigned offset) { return guest_base + offset; }
static uint32_t bits(float value) { uint32_t word; memcpy(&word,&value,4); return word; }
static uint32_t word_at(const unsigned char *p) { uint32_t word; memcpy(&word,p,4); return word; }
static void put_word(unsigned char *p, uint32_t word) { memcpy(p,&word,4); }
static void *allocate(size_t bytes) { void *p = malloc(bytes); if (!p) { perror("malloc"); exit(1); } return p; }
static World open_world(void)
{
    char path[] = "/tmp/paleblood-metal-srgb-storage-XXXXXX", error[512] = "";
    int fd = mkstemp(path); if (fd < 0) { perror("mkstemp"); exit(1); }
    if (unlink(path) || ftruncate(fd,POOL)) { perror("pool"); close(fd); exit(1); }
    unsigned char *pool = mmap(NULL,POOL,PROT_READ | PROT_WRITE,MAP_SHARED,fd,0);
    if (pool == MAP_FAILED) { perror("mmap"); close(fd); exit(1); }
    memset(pool,CANARY,POOL);
    for (unsigned i = 0; i < sizeof programs / sizeof *programs; ++i) {
        CHECK(programs[i].bytes <= CODE_STEP); if (programs[i].bytes > CODE_STEP) exit(1);
        memcpy(pool + CODE + i * CODE_STEP,programs[i].words,programs[i].bytes);
    }
    rt_executor_memory memory = {fd,POOL,guest_base,NULL}; rt_executor *executor = NULL;
    if (rt_executor_open(&memory,&executor,error,sizeof error)) {
        fprintf(stderr,"open: %s\n",error); close(fd); munmap(pool,POOL); exit(1);
    }
    CHECK(close(fd) == 0); return (World){pool,executor};
}
static void close_world(World *world)
{
    rt_executor_close(world->executor); CHECK(munmap(world->pool,POOL) == 0);
}
static Geometry geometry(unsigned dim, unsigned tile, unsigned bytes, unsigned block, unsigned layers)
{
    Geometry g = {.height=dim & 1 ? HEIGHT : 1,.layers=layers}; char error[256] = "";
    rt_tiling_level level[RT_TILING_MAX_LEVELS];
    if (rt_tiling_mip_chain(tile,bytes,block,0,PITCH,g.height,WIDTH,g.height,1,layers,level,error,sizeof error)) {
        fprintf(stderr,"geometry: %s\n",error); exit(1);
    }
    g.layout = level[0].layout; uint64_t span = rt_tiling_size(&g.layout,layers);
    CHECK(span > 0 && span <= RAW - IMAGE - 2 * GUARD && span <= POOL - RAW - GUARD);
    if (!span || span > RAW - IMAGE - 2 * GUARD || span > POOL - RAW - GUARD) exit(1);
    g.span = (size_t)span; return g;
}
static unsigned swizzle(unsigned p)
{
    unsigned value = 0;
    for (unsigned c = 0; c < 4; ++c) value |= (4u + permutations[p].select[c]) << (3 * c);
    return value;
}
static void descriptor(uint32_t words[8], unsigned dim, unsigned tile, unsigned data, unsigned number, unsigned selectors)
{
    const uint64_t address = at(IMAGE) >> 8; memset(words,0,32); words[0] = (uint32_t)address;
    words[1] = (uint32_t)(address >> 32) | data << 20 | number << 26;
    words[2] = WIDTH - 1 | (dim & 1 ? HEIGHT - 1 : 0) << 14;
    words[3] = selectors | tile << 20 | types[dim] << 28;
    words[4] = (dim >= 2 ? 2u : 0u) | (PITCH - 1) << 13;
    words[5] = dim >= 2 ? 1u | 2u << 13 : 0;
}
static void buffer_descriptor(uint32_t words[4], unsigned offset, unsigned bytes)
{
    words[0] = (uint32_t)at(offset); words[1] = (uint32_t)(at(offset) >> 32); words[2] = bytes; words[3] = 0;
}
static void values(uint32_t user[16])
{
    user[0] = bits(0.125f); user[1] = bits(0.3125f); user[2] = bits(0.5625f); user[3] = bits(0.8125f);
}
static void prepare_output(World *world)
{
    memset(world->pool + OUTPUT - GUARD,CANARY,OUTPUT_BYTES + 2 * GUARD);
    for (unsigned i = 0; i < OUTPUT_BYTES; i += 4) put_word(world->pool + OUTPUT + i,unwritten);
}
static void check_guards(const World *world, unsigned offset, size_t span)
{
    for (unsigned i = 0; i < GUARD; ++i) {
        CHECK(world->pool[offset - GUARD + i] == CANARY);
        CHECK(world->pool[offset + span + i] == CANARY);
    }
}
static void check_output(const World *world)
{
    for (unsigned i = 0; i < OUTPUT_BYTES; i += 4) CHECK(word_at(world->pool + OUTPUT + i) == unwritten);
    check_guards(world,OUTPUT,OUTPUT_BYTES);
}
static int execute(World *world, unsigned program, const uint32_t user[16], unsigned groups, const char *expected)
{
    const int raw = program == RAW_PROGRAM; rt_render_call calls[4] = {{0}};
    calls[0].kind = RT_CALL_COMPUTE_SHADER;
    /* XYZ admission is required by the storage foundation. Owned programs use X
       and explicitly assign their Y/layer coordinates. */
    calls[0].u.compute_shader = (rt_compute_shader){.code=at(CODE + program * CODE_STEP),.vgprs=12,.sgprs=24,
        .user_sgprs=raw ? 8u : 16u,.workgroup_id_enables=raw ? 1u : 0u,.thread_id_components=2,
        .threads={raw ? 64u : LANES,1,1}};
    calls[1].kind = calls[2].kind = RT_CALL_USER_DATA;
    calls[1].u.user_data.stage = calls[2].u.user_data.stage = RT_STAGE_COMPUTE;
    calls[1].u.user_data.count = calls[2].u.user_data.count = 8; calls[2].u.user_data.slot = 8;
    memcpy(calls[1].u.user_data.words,user,8 * sizeof *user);
    memcpy(calls[2].u.user_data.words,user + 8,8 * sizeof *user);
    unsigned dispatch = raw ? 2u : 3u; calls[dispatch].kind = RT_CALL_DISPATCH;
    calls[dispatch].u.dispatch.groups[0] = groups;
    calls[dispatch].u.dispatch.groups[1] = calls[dispatch].u.dispatch.groups[2] = 1;
    char error[512] = "";
    const int result = rt_executor_run_calls(world->executor,calls,dispatch + 1,error,sizeof error);
    if (expected) {
        if (result != -1 || !strstr(error,expected)) fprintf(stderr,"[%s] expected %s, got %d: %s\n",case_id,expected,result,error);
        CHECK(result == -1 && strstr(error,expected));
    } else {
        if (result) fprintf(stderr,"[%s] execute: %s\n",case_id,error);
        CHECK(result == 0);
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
    snprintf(case_id,sizeof case_id,"%.160s/raw-observer",saved);
    CHECK(g->span % 256 == 0);
    if (g->span % 256 == 0) {
        uint32_t user[16] = {0}; buffer_descriptor(user,IMAGE,(unsigned)g->span); buffer_descriptor(user + 4,RAW,(unsigned)g->span);
        memset(world->pool + RAW - GUARD,CANARY,g->span + 2 * GUARD); memset(world->pool + RAW,RAW_UNWRITTEN,g->span);
        if (!execute(world,RAW_PROGRAM,user,(unsigned)(g->span / 256),NULL)) compare(world->pool + RAW,expected,written,g->span);
        check_guards(world,RAW,g->span);
    }
    memcpy(case_id,saved,sizeof case_id);
}
static void positive(World *world, unsigned dim, unsigned tile, unsigned p, unsigned number, unsigned seed, const char *family)
{
    snprintf(case_id,sizeof case_id,"%s/%s/t%u/n%u/p%02u",family,dimensions[dim],tile,number,p); ++scenarios;
    Geometry g = geometry(dim,tile,4,1,dim >= 2 ? 3 : 1);
    memset(world->pool + IMAGE - GUARD,CANARY,g.span + 2 * GUARD);
    /* CPU changes in untouched texels must survive upload and publication too. */
    for (unsigned layer = 0; layer < g.layers; ++layer) for (unsigned y = 0; y < g.height; ++y)
        for (unsigned x = 0; x < WIDTH; ++x) for (unsigned c = 0; c < 4; ++c)
            world->pool[IMAGE + rt_tiling_offset(&g.layout,x,y,layer) + c] =
                (unsigned char)(17 + (seed + 11 * x + 23 * y + 31 * layer + 19 * c) % 89);
    prepare_output(world);
    unsigned char *expected = allocate(g.span), *written = allocate(g.span);
    memcpy(expected,world->pool + IMAGE,g.span); memset(written,0,g.span);
    for (unsigned lane = 0; lane < LANES; ++lane) {
        size_t offset = (size_t)rt_tiling_offset(&g.layout,FIRST_X + lane,dim & 1 ? 2 : 0,dim >= 2 ? 2 : 0);
        CHECK(offset + 4 <= g.span);
        for (unsigned c = 0; c < 4; ++c) { expected[offset + c] = permutations[p].raw[c]; written[offset + c] = 1; }
    }
    uint32_t user[16] = {0}; values(user); descriptor(user + 8,dim,tile,10,number,swizzle(p));
    user[4] = dim & 1 ? 2 : 0; user[5] = dim >= 2 ? 1 : 0; user[6] = FIRST_X;
    if (!execute(world,4 * dim + STORE,user,1,NULL)) {
        compare(world->pool + IMAGE,expected,written,g.span); check_output(world);
        raw_observer(world,&g,expected,written);
    } else rt_executor_reset(world->executor);
    check_guards(world,IMAGE,g.span); check_output(world); free(expected); free(written);
}
static void matrix(void)
{
    const unsigned tiles[] = {31,13};
    for (unsigned dim = 0; dim < 4; ++dim) for (unsigned t = 0; t < 2; ++t)
        for (unsigned block = 0; block < 3; ++block) {
            /* Reset retains caches: close/open bounds each matrix world to eight
               permutation variants, plus its raw-buffer observer. */
            World world = open_world();
            for (unsigned p = 8 * block; p < 8 * (block + 1); ++p) positive(&world,dim,tiles[t],p,9,7 + p,"matrix");
            close_world(&world);
        }
}
static void cache_transitions(void)
{
    const unsigned tiles[] = {31,13};
    const struct {unsigned number,p;} sequence[] = {{0,0},{9,0},{9,14},{9,9},{0,18},{9,0},{0,0},{9,9}};
    for (unsigned dim = 0; dim < 4; ++dim) for (unsigned t = 0; t < 2; ++t) {
        World world = open_world();
        for (unsigned step = 0; step < sizeof sequence / sizeof *sequence; ++step) for (unsigned refresh = 0; refresh < 2; ++refresh) {
            char family[40]; snprintf(family,sizeof family,"cache-step%u-cpu%u",step,refresh);
            positive(&world,dim,tiles[t],sequence[step].p,sequence[step].number,17 + 31 * step + 53 * refresh,family);
        }
        close_world(&world);
    }
}
static void refusal(World *world, const char *family, unsigned item, unsigned dim, unsigned tile, unsigned op,
                    unsigned data, unsigned bytes, unsigned block, unsigned selectors, unsigned mip, int volume,
                    const char *reason)
{
    snprintf(case_id,sizeof case_id,"%s/%s/t%u/%s/c%02u",family,dimensions[dim],tile,accesses[op],item); ++scenarios;
    rt_executor_reset(world->executor);
    Geometry g = geometry(dim,tile,bytes,block,volume ? 3u : dim >= 2 ? 3u : 1u);
    memset(world->pool + IMAGE - GUARD,CANARY,g.span + 2 * GUARD);
    unsigned char *before = allocate(g.span); memcpy(before,world->pool + IMAGE,g.span); prepare_output(world);
    uint32_t user[16] = {0}; descriptor(user + 8,dim,tile,data,9,selectors);
    user[11] |= mip;
    /* A volume descriptor is only a host-admission refusal, not a volume-layout oracle. */
    if (volume) { user[11] = (user[11] & ~(15u << 28)) | (uint32_t)RT_IMAGE_3D << 28; user[12] |= 2; user[13] = 0; }
    if (op == STORE) values(user); else buffer_descriptor(user,OUTPUT,OUTPUT_BYTES);
    user[4] = dim & 1 ? 2 : 0; user[5] = dim >= 2 ? 1 : 0; user[6] = FIRST_X;
    execute(world,4 * dim + (op == READ_WRITE ? 2u : op),user,1,reason);
    compare(world->pool + IMAGE,before,NULL,g.span); check_guards(world,IMAGE,g.span); check_output(world); free(before);
}
static void refusals(void)
{
    static const char format_reason[] = "compressed and sRGB compute storage images are not modeled";
    static const char selector_reason[] = "compute image swizzle must preserve its stored components or permute all four";
    static const char mip_reason[] = "compute storage images require relative and physical mip zero";
    World world = open_world();
    for (unsigned dim = 0; dim < 4; ++dim) for (unsigned op = 0; op < 3; op += 2)
        refusal(&world,"read-access",0,dim,31,op,10,4,1,swizzle(9),0,0,format_reason);
    const unsigned compressed[] = {35,36,37,41};
    for (unsigned f = 0; f < 4; ++f) for (unsigned op = 0; op < 3; ++op)
        refusal(&world,"compressed",compressed[f],1,31,op,compressed[f],f == 0 ? 8u : 16u,4,swizzle(0),0,0,format_reason);
    for (unsigned op = 0; op < 3; ++op)
        refusal(&world,"volume",0,1,13,op,10,4,1,swizzle(0),0,1,format_reason);
    refusal(&world,"all-zero-selector",0,1,31,STORE,10,4,1,0,0,0,selector_reason);
    for (unsigned c = 0; c < 4; ++c) {
        unsigned selectors = (swizzle(0) & ~(7u << (3 * c))) | (4u + (c + 1) % 4) << (3 * c);
        refusal(&world,"repeat-selector",c,1,31,STORE,10,4,1,selectors,0,0,selector_reason);
    }
    for (unsigned dim = 0; dim < 4; ++dim) for (unsigned mip = 0; mip < 2; ++mip)
        refusal(&world,"mips",mip,dim,31,STORE,10,4,1,swizzle(0),mip ? (1u << 12 | 1u << 16) : 1u << 16,0,mip_reason);
    refusal(&world,"outside-format-pair",0,1,31,STORE,1,1,1,swizzle(0),0,0,"image DATA_FORMAT 1, NUM_FORMAT 9 is not modeled");
    close_world(&world);
}
static void literal_oracle(void)
{
    CHECK(sizeof permutations / sizeof *permutations == 24);
    static const unsigned char logical[4] = {32,80,143,207}, cycle[4] = {207,32,80,143};
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
}
int main(void)
{
    literal_oracle(); matrix(); cache_transitions(); refusals();
    snprintf(case_id,sizeof case_id,"complete"); CHECK(scenarios == PLANNED_SCENARIOS);
    printf("metal sRGB storage compatibility: %u scenarios, %u checks, %u failures\n",scenarios,checks,failures);
    return failures ? 1 : 0;
}
