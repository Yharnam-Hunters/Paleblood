/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Owned four-channel storage permutations. Logical loads and physical stores have
   independent oracles; inverse mistakes cannot hide behind a load/store roundtrip.
   The store contract follows AMD's public driver/compiler path, not a console run. */
#include "runtime/render_executor.h"
#include "runtime/tiling.h"
#include "metal_permutation_programs.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

enum { POOL = 2 << 20, CODE = 0x1000, CODE_STEP = 0x100, OUTPUT = 0x10000,
       IMAGE = 0x20000, RAW = 0x60000, GUARD = 64, OUTPUT_BYTES = 256,
       CANARY = 0xa5, RAW_UNWRITTEN = 0x5a, WIDTH = 11, HEIGHT = 5, PITCH = 64, LANES = 7, FIRST_X = 2,
       PLANNED_SCENARIOS = 2249 };
static const uint64_t guest_base = UINT64_C(0x3000000000);
static const uint32_t unwritten = UINT32_C(0x7fc0dead);
static unsigned scenarios, checks, failures;
static char case_id[128] = "setup";
#define CHECK(value) do { ++checks; if (!(value)) { ++failures; if (failures <= 24) \
    fprintf(stderr,"%s:%d [%s]: %s\n",__FILE__,__LINE__,case_id,#value); } } while (0)

typedef struct { const char *name; unsigned data, number, components, bytes; } Format;
static const Format formats[] = {
    {"rgba8-unorm",10,0,4,4}, {"rgba8-uint",10,4,4,4}, {"rgba32-float",14,7,4,16}
};
static const Format narrow_formats[] = {
    {"r8-unorm",1,0,1,1}, {"rg8-unorm",3,0,2,2},
    {"r32-float",4,7,1,4}, {"rg32-float",11,7,2,8}
};
/* Literal tables, independent of descriptor decoding and translator code.
   forward[c] chooses a stored channel for logical c. raw[c] chooses a logical
   source for stored c. In particular 1230 writes (d,a,b,c), not (b,c,d,a). */
typedef struct { unsigned char forward[4], raw[4]; } Permutation;
static const Permutation permutations[24] = {
    {{0,1,2,3},{0,1,2,3}}, {{0,1,3,2},{0,1,3,2}},
    {{0,2,1,3},{0,2,1,3}}, {{0,2,3,1},{0,3,1,2}},
    {{0,3,1,2},{0,2,3,1}}, {{0,3,2,1},{0,3,2,1}},
    {{1,0,2,3},{1,0,2,3}}, {{1,0,3,2},{1,0,3,2}},
    {{1,2,0,3},{2,0,1,3}}, {{1,2,3,0},{3,0,1,2}},
    {{1,3,0,2},{2,0,3,1}}, {{1,3,2,0},{3,0,2,1}},
    {{2,0,1,3},{1,2,0,3}}, {{2,0,3,1},{1,3,0,2}},
    {{2,1,0,3},{2,1,0,3}}, {{2,1,3,0},{3,1,0,2}},
    {{2,3,0,1},{2,3,0,1}}, {{2,3,1,0},{3,2,0,1}},
    {{3,0,1,2},{1,2,3,0}}, {{3,0,2,1},{1,3,2,0}},
    {{3,1,0,2},{2,1,3,0}}, {{3,1,2,0},{3,1,2,0}},
    {{3,2,0,1},{2,3,1,0}}, {{3,2,1,0},{3,2,1,0}}
};

typedef struct { const uint32_t *words; size_t bytes; } Program;
#define PROGRAM(name) {name,sizeof name}
static const Program programs[] = {
    PROGRAM(permutation_load_1d), PROGRAM(permutation_store_1d),
    PROGRAM(permutation_rw_float_1d), PROGRAM(permutation_rw_uint_1d),
    PROGRAM(permutation_load_2d), PROGRAM(permutation_store_2d),
    PROGRAM(permutation_rw_float_2d), PROGRAM(permutation_rw_uint_2d),
    PROGRAM(permutation_load_1d_array), PROGRAM(permutation_store_1d_array),
    PROGRAM(permutation_rw_float_1d_array), PROGRAM(permutation_rw_uint_1d_array),
    PROGRAM(permutation_load_2d_array), PROGRAM(permutation_store_2d_array),
    PROGRAM(permutation_rw_float_2d_array), PROGRAM(permutation_rw_uint_2d_array),
    PROGRAM(permutation_raw_copy)
};
#undef PROGRAM
static const unsigned types[] = {RT_IMAGE_1D,RT_IMAGE_2D,RT_IMAGE_1D_ARRAY,RT_IMAGE_2D_ARRAY};
static const char *const dimensions[] = {"1d","2d","1d-array","2d-array"};
static const char *const operation_names[] = {"load","store","read-write"};
enum { LOAD, STORE, READ_WRITE, RAW_PROGRAM = 16 };
typedef struct { unsigned char *pool; rt_executor *executor; } World;
typedef struct { rt_tiling layout; size_t span; unsigned height, layers; } Geometry;

static uint64_t at(unsigned offset) { return guest_base + offset; }
static uint32_t bits(float value) { uint32_t word; memcpy(&word,&value,4); return word; }
static float as_float(uint32_t word) { float value; memcpy(&value,&word,4); return value; }
static uint32_t word_at(const unsigned char *p) { uint32_t word; memcpy(&word,p,4); return word; }
static void put_word(unsigned char *p, uint32_t word) { memcpy(p,&word,4); }
static void *allocate(size_t bytes) { void *p = malloc(bytes); if (!p) { perror("malloc"); exit(1); } return p; }

static World open_world(void)
{
    char path[] = "/tmp/paleblood-metal-permutations-XXXXXX", error[512] = "";
    int fd = mkstemp(path); if (fd < 0) { perror("mkstemp"); exit(1); }
    if (unlink(path) || ftruncate(fd,POOL)) { perror("pool"); close(fd); exit(1); }
    unsigned char *pool = mmap(NULL,POOL,PROT_READ | PROT_WRITE,MAP_SHARED,fd,0);
    if (pool == MAP_FAILED) { perror("mmap"); close(fd); exit(1); }
    memset(pool,CANARY,POOL);
    for (unsigned i = 0; i < sizeof programs / sizeof *programs; ++i) {
        CHECK(programs[i].bytes <= CODE_STEP);
        if (programs[i].bytes > CODE_STEP) exit(1);
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
static void begin_case(const char *family, const Format *format, unsigned dim, unsigned tile, unsigned item, unsigned op)
{
    snprintf(case_id,sizeof case_id,"%s/%s/%s/t%u/p%02u/%s",family,format->name,dimensions[dim],tile,item,operation_names[op]);
    ++scenarios;
}
static unsigned swizzle_for(const Permutation *permutation)
{
    unsigned value = 0;
    for (unsigned c = 0; c < 4; ++c) value |= (4u + permutation->forward[c]) << (3 * c);
    return value;
}
static void image_descriptor(uint32_t words[8], const Format *format, unsigned dim, unsigned tile, unsigned swizzle)
{
    const uint64_t address = at(IMAGE) >> 8;
    memset(words,0,32); words[0] = (uint32_t)address;
    words[1] = (uint32_t)(address >> 32) | format->data << 20 | format->number << 26;
    words[2] = WIDTH - 1 | (dim & 1 ? HEIGHT - 1 : 0) << 14;
    words[3] = swizzle | tile << 20 | types[dim] << 28;
    words[4] = (dim >= 2 ? 2u : 0u) | (PITCH - 1) << 13;
    words[5] = dim >= 2 ? 1u | 2u << 13 : 0;
}
static Geometry geometry_for(const Format *format, unsigned dim, unsigned tile)
{
    Geometry g = {.height=dim & 1 ? HEIGHT : 1,.layers=dim >= 2 ? 3u : 1u}; char error[256] = "";
    rt_tiling_level level[RT_TILING_MAX_LEVELS];
    if (rt_tiling_mip_chain(tile,format->bytes,1,0,PITCH,g.height,WIDTH,g.height,1,g.layers,level,error,sizeof error)) {
        fprintf(stderr,"geometry: %s\n",error); exit(1);
    }
    g.layout = level[0].layout;
    uint64_t span = rt_tiling_size(&g.layout,g.layers);
    CHECK(span > 0 && span <= RAW - IMAGE - 2 * GUARD && span <= POOL - RAW - GUARD);
    if (!span || span > RAW - IMAGE - 2 * GUARD || span > POOL - RAW - GUARD) exit(1);
    g.span = (size_t)span; return g;
}
static void buffer_descriptor(uint32_t words[4], unsigned offset, unsigned bytes)
{
    words[0] = (uint32_t)at(offset); words[1] = (uint32_t)(at(offset) >> 32); words[2] = bytes; words[3] = 0;
}
static void fill_image(World *world, const Format *format, const Geometry *g, unsigned seed)
{
    memset(world->pool + IMAGE - GUARD,CANARY,g->span + 2 * GUARD);
    for (unsigned layer = 0; layer < g->layers; ++layer)
        for (unsigned y = 0; y < g->height; ++y)
            for (unsigned x = 0; x < WIDTH; ++x) {
                size_t offset = (size_t)rt_tiling_offset(&g->layout,x,y,layer);
                CHECK(offset + format->bytes <= g->span);
                for (unsigned c = 0; c < format->components; ++c) {
                    if (format->bytes == format->components)
                        world->pool[IMAGE + offset + c] = (unsigned char)(17 + (seed + 11 * x + 23 * y + 31 * layer + 19 * c) % 89);
                    else put_word(world->pool + IMAGE + offset + 4 * c,bits(0.25f + (float)((seed + 3 * x + 5 * y + 7 * layer) % 13) / 8 + (float)c / 32));
                }
            }
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
static unsigned program_for(const Format *format, unsigned dim, unsigned op)
{
    return 4 * dim + (op == READ_WRITE ? (format->number == 4 ? 3u : 2u) : op);
}
static int execute(World *world, unsigned program, const uint32_t user[16], unsigned groups, const char *expected)
{
    const int raw = program == RAW_PROGRAM;
    rt_render_call calls[4] = {{0}};
    calls[0].kind = RT_CALL_COMPUTE_SHADER;
    /* The storage foundation requires XYZ entry IDs. Programs consume X only
       and explicitly assign any Y/layer coordinate they use. */
    calls[0].u.compute_shader = (rt_compute_shader){.code=at(CODE + program * CODE_STEP),.vgprs=12,.sgprs=24,
        .user_sgprs=raw ? 8u : 16u,.workgroup_id_enables=raw ? 1u : 0u,.thread_id_components=2,
        .threads={raw ? 64u : LANES,1,1}};
    calls[1].kind = calls[2].kind = RT_CALL_USER_DATA;
    calls[1].u.user_data.stage = calls[2].u.user_data.stage = RT_STAGE_COMPUTE;
    calls[1].u.user_data.count = calls[2].u.user_data.count = 8;
    calls[2].u.user_data.slot = 8;
    memcpy(calls[1].u.user_data.words,user,8 * sizeof *user);
    memcpy(calls[2].u.user_data.words,user + 8,8 * sizeof *user);
    unsigned dispatch = raw ? 2u : 3u;
    calls[dispatch].kind = RT_CALL_DISPATCH;
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
static void check_bytes(const unsigned char *actual, const unsigned char *expected, const unsigned char *quantized, size_t span)
{
    for (size_t i = 0; i < span; ++i) {
        if (quantized && quantized[i]) CHECK(abs((int)actual[i] - (int)expected[i]) <= 1);
        else CHECK(actual[i] == expected[i]);
    }
}
static void check_output(const World *world, const Format *format, const Permutation *permutation,
                         const Geometry *g, const unsigned char *before, unsigned dim, unsigned op)
{
    for (unsigned lane = 0; lane < LANES && op != STORE; ++lane) {
        const size_t offset = (size_t)rt_tiling_offset(&g->layout,FIRST_X + lane,dim & 1 ? 2 : 0,dim >= 2 ? 2 : 0);
        for (unsigned c = 0; c < 4; ++c) {
            const unsigned physical = permutation->forward[c];
            const uint32_t actual = word_at(world->pool + OUTPUT + 16 * lane + 4 * c);
            if (format->number == 0) {
                const float expected = (float)before[offset + physical] / 255;
                CHECK(isfinite(as_float(actual)) && fabsf(as_float(actual) - expected) <= 0.000001f);
            } else if (format->number == 4) CHECK(actual == before[offset + physical]);
            else CHECK(actual == word_at(before + offset + 4 * physical));
        }
    }
    const unsigned written = op == STORE ? 0 : LANES * 16;
    for (unsigned i = written; i < OUTPUT_BYTES; i += 4) CHECK(word_at(world->pool + OUTPUT + i) == unwritten);
    check_guards(world,OUTPUT,OUTPUT_BYTES);
}
static void store_values(const Format *format, uint32_t values[4])
{
    static const unsigned integers[4] = {19,67,131,223};
    static const float normalized[4] = {0.125f,0.25f,0.5f,0.75f};
    static const float floating[4] = {0.1875f,-0.375f,1.5f,-2.25f};
    for (unsigned c = 0; c < 4; ++c)
        values[c] = format->number == 4 ? integers[c] : bits(format->number == 0 ? normalized[c] : floating[c]);
}
static void expected_write(unsigned char *expected, unsigned char *quantized, const unsigned char *before,
                           const Format *format, const Permutation *permutation, const Geometry *g, unsigned dim, unsigned op,
                           const uint32_t store[4])
{
    for (unsigned lane = 0; lane < LANES; ++lane) {
        const size_t offset = (size_t)rt_tiling_offset(&g->layout,FIRST_X + lane,dim & 1 ? 2 : 0,dim >= 2 ? 2 : 0);
        if (op == STORE) {
            /* Literal inverse rows choose each physical channel's source. */
            for (unsigned physical = 0; physical < 4; ++physical) {
                const uint32_t value = store[permutation->raw[physical]];
                if (format->number == 0) {
                    expected[offset + physical] = (unsigned char)lroundf(as_float(value) * 255);
                    quantized[offset + physical] = 1;
                } else if (format->number == 4) expected[offset + physical] = (unsigned char)value;
                else put_word(expected + offset + 4 * physical,value);
            }
        } else if (op == READ_WRITE) {
            /* Only logical R increases, so its physical destination is explicit. */
            const unsigned physical = permutation->forward[0];
            if (format->number == 0) {
                expected[offset + physical] = (unsigned char)lroundf(((float)before[offset + physical] / 255 + 0.125f) * 255);
                quantized[offset + physical] = 1;
            } else if (format->number == 4) expected[offset + physical] = (unsigned char)(before[offset + physical] + 13);
            else put_word(expected + offset + 4 * physical,bits(as_float(word_at(before + offset + 4 * physical)) + 0.125f));
        }
    }
}
static void observe_raw(World *world, const Geometry *g, const unsigned char *expected, const unsigned char *quantized)
{
    CHECK(g->span % 256 == 0);
    if (g->span % 256) return;
    uint32_t user[16] = {0};
    buffer_descriptor(user,IMAGE,(unsigned)g->span); buffer_descriptor(user + 4,RAW,(unsigned)g->span);
    memset(world->pool + RAW - GUARD,CANARY,g->span + 2 * GUARD);
    memset(world->pool + RAW,RAW_UNWRITTEN,g->span);
    if (!execute(world,RAW_PROGRAM,user,(unsigned)(g->span / 256),NULL))
        check_bytes(world->pool + RAW,expected,quantized,g->span);
    check_guards(world,RAW,g->span);
}
static void positive(World *world, const Format *format, unsigned dim, unsigned tile, unsigned p, unsigned op, unsigned seed,
                     const char *family)
{
    if (family) begin_case(family,format,dim,tile,p,op);
    Geometry g = geometry_for(format,dim,tile); const Permutation *permutation = permutations + p;
    fill_image(world,format,&g,seed); prepare_output(world);
    unsigned char *before = allocate(g.span), *expected = allocate(g.span), *quantized = allocate(g.span);
    memcpy(before,world->pool + IMAGE,g.span); memcpy(expected,before,g.span); memset(quantized,0,g.span);
    uint32_t user[16] = {0}; image_descriptor(user + 8,format,dim,tile,swizzle_for(permutation));
    if (op == STORE) store_values(format,user); else buffer_descriptor(user,OUTPUT,OUTPUT_BYTES);
    user[4] = dim & 1 ? 2 : 0; user[5] = dim >= 2 ? 1 : 0; user[6] = FIRST_X;
    expected_write(expected,quantized,before,format,permutation,&g,dim,op,user);
    if (!execute(world,program_for(format,dim,op),user,1,NULL)) {
        check_output(world,format,permutation,&g,before,dim,op);
        check_bytes(world->pool + IMAGE,expected,quantized,g.span);
        if (op != LOAD) observe_raw(world,&g,expected,quantized);
    } else rt_executor_reset(world->executor);
    check_guards(world,IMAGE,g.span); check_guards(world,OUTPUT,OUTPUT_BYTES);
    free(before); free(expected); free(quantized);
}
static void matrix(void)
{
    const unsigned tiles[] = {31,13};
    for (unsigned f = 0; f < 3; ++f) for (unsigned dim = 0; dim < 4; ++dim)
        for (unsigned t = 0; t < 2; ++t) for (unsigned block = 0; block < 3; ++block) {
            /* Reset retains caches. A fresh executor bounds each world to 8
               permutations and 24 image-access pipelines plus raw observation. */
            World world = open_world();
            for (unsigned p = 8 * block; p < 8 * (block + 1); ++p)
                for (unsigned op = 0; op < 3; ++op) positive(&world,formats + f,dim,tiles[t],p,op,7 + 5 * p + op,"matrix");
            close_world(&world);
        }
}
static void live_descriptors(void)
{
    const unsigned sequence[] = {0,14,9,18,0};
    for (unsigned f = 0; f < 3; ++f) for (unsigned dim = 0; dim < 4; ++dim) {
        World world = open_world();
        for (unsigned step = 0; step < 5; ++step) {
            /* No reset: same code/address and descriptor identity changes,
               followed by a CPU-only refresh with the descriptor unchanged. */
            char family[40]; snprintf(family,sizeof family,"cache-step%u",step);
            positive(&world,formats + f,dim,31,sequence[step],LOAD,13 + 11 * step,family);
            snprintf(family,sizeof family,"cache-cpu-step%u",step);
            positive(&world,formats + f,dim,31,sequence[step],LOAD,71 + 17 * step,family);
            snprintf(family,sizeof family,"cache-step%u",step);
            positive(&world,formats + f,dim,31,sequence[step],STORE,37 + 3 * step,family);
            positive(&world,formats + f,dim,31,sequence[step],READ_WRITE,59 + 7 * step,family);
        }
        close_world(&world);
    }
}
static void refusal(World *world, const Format *format, unsigned swizzle, unsigned op, unsigned item, const char *family)
{
    if (family) { begin_case(family,format,1,31,item,op); rt_executor_reset(world->executor); }
    Geometry g = geometry_for(format,1,31); fill_image(world,format,&g,29); prepare_output(world);
    unsigned char *before = allocate(g.span); memcpy(before,world->pool + IMAGE,g.span);
    uint32_t user[16] = {0}; image_descriptor(user + 8,format,1,31,swizzle);
    if (op == STORE) store_values(format,user); else buffer_descriptor(user,OUTPUT,OUTPUT_BYTES);
    user[4] = 2; user[6] = FIRST_X;
    execute(world,program_for(format,1,op),user,1,"swizzle");
    check_bytes(world->pool + IMAGE,before,NULL,g.span); check_guards(world,IMAGE,g.span);
    for (unsigned i = 0; i < OUTPUT_BYTES; i += 4) CHECK(word_at(world->pool + OUTPUT + i) == unwritten);
    check_guards(world,OUTPUT,OUTPUT_BYTES); free(before);
}
static void descriptor_guards(void)
{
    const unsigned identity = swizzle_for(permutations);
    for (unsigned f = 0; f < 3; ++f) {
        World world = open_world(); unsigned item = 0;
        for (unsigned position = 0; position < 4; ++position) for (unsigned selector = 0; selector < 8; ++selector) {
            if (selector == 4 + position) continue;
            const unsigned swizzle = (identity & ~(7u << (3 * position))) | selector << (3 * position);
            for (unsigned op = 0; op < 3; ++op) refusal(&world,formats + f,swizzle,op,item,"invalid-selector");
            ++item;
        }
        close_world(&world);
    }
    /* These permutations change a present component. Existing freedom in unused
       selectors is not reclassified as an error by this regression fixture. */
    for (unsigned f = 0; f < 4; ++f) {
        World world = open_world(); const Format *format = narrow_formats + f;
        const unsigned selected[] = {format->components == 1 ? 14u : 6u,9};
        for (unsigned p = 0; p < 2; ++p) {
            for (unsigned op = 0; op < 2; ++op)
                refusal(&world,format,swizzle_for(permutations + selected[p]),op,p,"narrow-permutation");
            if (format->components == 1)
                refusal(&world,format,swizzle_for(permutations + selected[p]),READ_WRITE,p,"narrow-permutation");
        }
        close_world(&world);
    }
}
/* A raw all-zero selector word must not borrow an identity sentinel. Each case
   warms that exact operation's identity pipeline with observed data first, then
   submits raw0000 in the same executor without reset. Setup is named within the
   refusal case; it does not add a separate scenario. */
static void all_zero_selectors(void)
{
    for (unsigned f = 0; f < 3; ++f) for (unsigned op = 0; op < 3; ++op) {
        const Format *format = formats + f;
        begin_case("all-zero-selector",format,1,31,0,op);
        char refusal_id[sizeof case_id]; memcpy(refusal_id,case_id,sizeof case_id);
        World world = open_world();
        snprintf(case_id,sizeof case_id,"all-zero-selector/%s/2d/t31/p00/%s/identity-setup",format->name,operation_names[op]);
        positive(&world,format,1,31,0,op,43,NULL);
        memcpy(case_id,refusal_id,sizeof case_id);
        refusal(&world,format,0,op,0,NULL);
        close_world(&world);
    }
}
static void literal_tables(void)
{
    const unsigned char logical[4] = {19,67,131,223}, cycle_raw[4] = {223,19,67,131};
    for (unsigned p = 0; p < 24; ++p) {
        unsigned mask = 0;
        for (unsigned c = 0; c < 4; ++c) {
            CHECK(permutations[p].forward[c] < 4 && permutations[p].raw[c] < 4);
            mask |= 1u << permutations[p].forward[c];
            CHECK(permutations[p].raw[permutations[p].forward[c]] == c);
        }
        CHECK(mask == 15);
        for (unsigned other = 0; other < p; ++other) CHECK(memcmp(permutations[p].forward,permutations[other].forward,4) != 0);
    }
    for (unsigned c = 0; c < 4; ++c) CHECK(logical[permutations[9].raw[c]] == cycle_raw[c]);
}
int main(void)
{
    literal_tables(); matrix(); live_descriptors(); descriptor_guards(); all_zero_selectors();
    snprintf(case_id,sizeof case_id,"complete"); CHECK(scenarios == PLANNED_SCENARIOS);
    printf("metal permutations: %u scenarios, %u checks, %u failures\n",scenarios,checks,failures);
    return failures ? 1 : 0;
}
