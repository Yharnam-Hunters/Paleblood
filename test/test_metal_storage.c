/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Owned GFX7 compute images observed through raw guest bytes and buffer stores. */
#include "runtime/render_executor.h"
#include "runtime/render_calls.h"
#include "runtime/tiling.h"
#include "metal_storage_programs.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

enum { POOL = 8 << 20, CODE = 0x1000, OUTPUT = 0x10000, TABLE = 0x20000,
       IMAGE = 0x40000, SECOND = 0x400000, CANARY = 0xa5, WIDTH = 13, PITCH = 256 };
static const uint64_t guest_base = UINT64_C(0x3000000000);
static unsigned scenarios, checks, failures;
#define CHECK(value) do { ++checks; if (!(value)) { ++failures; if (failures < 25) fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#value); } } while (0)
typedef struct { unsigned data, number, components, bytes; } Format;
static const Format formats[] = {
    {1,0,1,1}, {3,0,2,2}, {4,7,1,4}, {4,4,1,4}, {4,5,1,4},
    {11,7,2,8}, {14,7,4,16}, {10,0,4,4}, {10,4,4,4}
};
typedef struct { unsigned char *pool; rt_executor *executor; unsigned declared; rt_shader_stage stage; } World;
static uint64_t at(unsigned offset) { return guest_base + offset; }
static uint32_t bits(float value) { uint32_t word; memcpy(&word,&value,4); return word; }
static float as_float(uint32_t word) { float value; memcpy(&value,&word,4); return value; }
static World open_world(void)
{
    char path[] = "/tmp/paleblood-metal-storage-XXXXXX", error[512] = "";
    int fd = mkstemp(path); if (fd < 0) exit(1);
    CHECK(unlink(path) == 0); if (ftruncate(fd,POOL)) exit(1);
    unsigned char *pool = mmap(NULL,POOL,PROT_READ | PROT_WRITE,MAP_SHARED,fd,0);
    if (pool == MAP_FAILED) exit(1);
    memset(pool,CANARY,POOL);
    rt_executor_memory memory = {fd,POOL,guest_base,NULL}; rt_executor *executor = NULL;
    if (rt_executor_open(&memory,&executor,error,sizeof error)) { fprintf(stderr,"open: %s\n",error); exit(1); }
    CHECK(close(fd) == 0); return (World){pool,executor,16,RT_STAGE_COMPUTE};
}
static void close_world(World *world)
{
    rt_executor_close(world->executor); CHECK(munmap(world->pool,POOL) == 0);
}
static void buffer_descriptor(uint32_t words[4], unsigned offset)
{
    words[0] = (uint32_t)at(offset); words[1] = (uint32_t)(at(offset) >> 32); words[2] = 256; words[3] = 0;
}
static void image_descriptor(uint32_t words[8], const Format *format, unsigned type, unsigned tile, unsigned offset)
{
    const int arrayed = type == RT_IMAGE_1D_ARRAY || type == RT_IMAGE_2D_ARRAY;
    const int one_d = type == RT_IMAGE_1D || type == RT_IMAGE_1D_ARRAY;
    const uint64_t address = at(offset) >> 8;
    memset(words,0,8 * sizeof *words); words[0] = (uint32_t)address;
    words[1] = (uint32_t)(address >> 32) | format->data << 20 | format->number << 26;
    words[2] = WIDTH - 1 | (one_d ? 0 : 4) << 14;
    words[3] = (4 | 5 << 3 | 6 << 6 | 7 << 9) | tile << 20 | type << 28;
    words[4] = (arrayed ? 2 : 0) | (PITCH - 1) << 13;
    words[5] = arrayed ? 1 | 2 << 13 : 0;
}
static rt_tiling layout_for(const uint32_t words[8], rt_image_descriptor *descriptor)
{
    rt_image_descriptor_decode(words,descriptor); rt_tiling_level level[RT_TILING_MAX_LEVELS]; char error[256] = "";
    const unsigned height = descriptor->type == RT_IMAGE_1D || descriptor->type == RT_IMAGE_1D_ARRAY ? 1 : descriptor->height;
    unsigned bytes = 0;
    for (unsigned i = 0; i < sizeof formats / sizeof *formats; ++i)
        if (formats[i].data == descriptor->data_format && formats[i].number == descriptor->num_format) bytes = formats[i].bytes;
    CHECK(bytes != 0);
    if (rt_tiling_mip_chain(descriptor->tiling_index,bytes,1,descriptor->pow2_pad,descriptor->pitch,height,descriptor->width,height,
                            1,descriptor->layers,level,error,sizeof error)) { fprintf(stderr,"layout: %s\n",error); exit(1); }
    return level[0].layout;
}
static void put_pixel(unsigned char *destination, const Format *format, unsigned seed)
{
    for (unsigned c = 0; c < format->components; ++c) {
        if (format->number == 0 || (format->number == 4 && format->data == 10)) destination[c] = (unsigned char)(seed + 37 * c);
        else {
            uint32_t word = format->number == 7 ? bits((float)((seed + 3 * c) % 23) / 32) :
                format->number == 5 ? (uint32_t)-(int32_t)(seed + c + 1) : 0x80000000u + seed + c;
            memcpy(destination + 4 * c,&word,4);
        }
    }
}
static void fill_image(World *world, const Format *format, const rt_image_descriptor *descriptor, const rt_tiling *layout, unsigned seed)
{
    const unsigned height = descriptor->type == RT_IMAGE_1D || descriptor->type == RT_IMAGE_1D_ARRAY ? 1 : descriptor->height;
    for (unsigned layer = 0; layer < descriptor->layers; ++layer)
        for (unsigned y = 0; y < height; ++y)
            for (unsigned x = 0; x < descriptor->width; ++x)
                put_pixel(world->pool + descriptor->address - guest_base + rt_tiling_offset(layout,x,y,layer),format,seed + x + 5 * y + 17 * layer);
}
static int execute(World *world, const uint32_t *program, size_t bytes, const uint32_t user[16], unsigned local,
                    uint32_t written, const char *expected)
{
    memcpy(world->pool + CODE,program,bytes); memset(world->pool + OUTPUT,CANARY,256); rt_executor_reset(world->executor);
    rt_render_call calls[20] = {{0}}; unsigned count = 0;
    calls[count].kind = RT_CALL_COMPUTE_SHADER;
    calls[count++].u.compute_shader = (rt_compute_shader){.code=at(CODE),.vgprs=16,.sgprs=40,.user_sgprs=world->declared,
        .threads={local,1,1},.thread_id_components=2};
    for (unsigned i = 0; i < 16; ++i) if (written & (1u << i)) {
        calls[count].kind = RT_CALL_USER_DATA; calls[count].u.user_data.stage = world->stage;
        calls[count].u.user_data.slot = i; calls[count].u.user_data.count = 1; calls[count++].u.user_data.words[0] = user[i];
    }
    calls[count].kind = RT_CALL_DISPATCH; calls[count].u.dispatch.groups[0] = calls[count].u.dispatch.groups[1] = calls[count].u.dispatch.groups[2] = 1; ++count;
    char error[512] = ""; const int result = rt_executor_run_calls(world->executor,calls,count,error,sizeof error);
    if (expected) { if (result != -1 || !strstr(error,expected)) fprintf(stderr,"expected %s, got %d: %s\n",expected,result,error); CHECK(result == -1 && strstr(error,expected)); }
    else { if (result) fprintf(stderr,"storage: %s\n",error); CHECK(result == 0); }
    ++scenarios; return result;
}
static void check_load(const World *world, const Format *format, const rt_image_descriptor *descriptor, const rt_tiling *layout,
                       unsigned x, unsigned y, unsigned layer, unsigned local)
{
    const uint32_t *actual = (const uint32_t *)(world->pool + OUTPUT);
    for (unsigned lane = 0; lane < local; ++lane) {
        const unsigned char *texel = world->pool + descriptor->address - guest_base + rt_tiling_offset(layout,x + lane,y,layer);
        for (unsigned c = 0; c < 4; ++c) {
            uint32_t expected = c == 3 ? (format->number == 4 || format->number == 5 ? 1 : bits(1)) : 0;
            if (c < format->components) {
                if (format->number == 0) expected = bits((float)texel[c] / 255);
                else if (format->number == 4 && format->data == 10) expected = texel[c];
                else memcpy(&expected,texel + c * 4,4);
            }
            if (format->number == 0) CHECK(fabsf(as_float(actual[4 * lane + c]) - as_float(expected)) <= 0.000001f);
            else CHECK(actual[4 * lane + c] == expected);
        }
    }
    for (unsigned i = local * 4; i < 64; ++i) CHECK(actual[i] == 0xa5a5a5a5u);
}
static void matrix(void)
{
    const unsigned types[] = {RT_IMAGE_1D,RT_IMAGE_2D,RT_IMAGE_1D_ARRAY,RT_IMAGE_2D_ARRAY};
    const unsigned tiles[] = {8,9,10,13,14,31};
    const struct { const uint32_t *load, *store; size_t load_bytes, store_bytes; } programs[] = {
#define PAIR(name) {storage_load_##name,storage_store_##name,sizeof storage_load_##name,sizeof storage_store_##name}
        PAIR(1d), PAIR(2d), PAIR(1d_array), PAIR(2d_array)
#undef PAIR
    };
    for (unsigned f = 0; f < sizeof formats / sizeof *formats; ++f) for (unsigned d = 0; d < 4; ++d) {
        World world = open_world(); const Format *format = formats + f;
        for (unsigned tile = 0; tile < sizeof tiles / sizeof *tiles; ++tile) {
            uint32_t user[16] = {0}; image_descriptor(user + 8,format,types[d],tiles[tile],IMAGE);
            rt_image_descriptor descriptor; rt_tiling layout = layout_for(user + 8,&descriptor);
            const size_t allocation = (size_t)rt_tiling_size(&layout,descriptor.layers);
            memset(world.pool + IMAGE - 1,CANARY,allocation + 2);
            fill_image(&world,format,&descriptor,&layout,19); buffer_descriptor(user,OUTPUT);
            user[4] = d == 0 || d == 2 ? 0 : 3; user[5] = d >= 2 ? 1 : 0; user[6] = 3;
            if (!execute(&world,programs[d].load,programs[d].load_bytes,user,4,0xffff,NULL))
                check_load(&world,format,&descriptor,&layout,3,user[4],d >= 2 ? 2 : 0,4);
            const size_t span = (size_t)rt_tiling_size(&layout,descriptor.layers);
            unsigned char *expected = malloc(span); if (!expected) exit(1); memcpy(expected,world.pool + IMAGE,span);
            for (unsigned c = 0; c < 4; ++c) user[c] = format->number == 0 ? bits(c & 1 ? 1 : 0) :
                format->number == 7 ? bits((float)(c + 1) / 4) : 0x80000001u + c;
            if (format->number == 4 && format->data == 10) for (unsigned c = 0; c < 4; ++c) user[c] = 13 + 41 * c;
            for (unsigned lane = 0; lane < 4; ++lane) {
                unsigned char *pixel = expected + rt_tiling_offset(&layout,3 + lane,user[4],d >= 2 ? 2 : 0);
                for (unsigned c = 0; c < format->components; ++c) {
                    if (format->number == 0) pixel[c] = (unsigned char)(as_float(user[c]) * 255);
                    else if (format->number == 4 && format->data == 10) pixel[c] = (unsigned char)user[c];
                    else memcpy(pixel + 4 * c,user + c,4);
                }
            }
            execute(&world,programs[d].store,programs[d].store_bytes,user,4,0xffff,NULL);
            CHECK(memcmp(world.pool + IMAGE,expected,span) == 0); CHECK(world.pool[IMAGE - 1] == CANARY && world.pool[IMAGE + span] == CANARY);
            free(expected);
            buffer_descriptor(user,OUTPUT);
            if (!execute(&world,programs[d].load,programs[d].load_bytes,user,4,0xffff,NULL))
                check_load(&world,format,&descriptor,&layout,3,user[4],d >= 2 ? 2 : 0,4);
        }
        close_world(&world);
    }
}
/* Same program bytes, changing descriptor formats/components and CPU texels on cache hits. */
static void live_descriptors(void)
{
    World world = open_world(); const unsigned sequence[] = {2,3,4,6,2,7,2};
    for (unsigned i = 0; i < sizeof sequence / sizeof *sequence; ++i) {
        const Format *format = formats + sequence[i]; uint32_t user[16] = {0};
        image_descriptor(user + 8,format,RT_IMAGE_2D,31,IMAGE); rt_image_descriptor d; rt_tiling layout = layout_for(user + 8,&d);
        fill_image(&world,format,&d,&layout,11 + i); buffer_descriptor(user,OUTPUT);
        if (!execute(&world,storage_load_2d,sizeof storage_load_2d,user,4,0xffff,NULL)) check_load(&world,format,&d,&layout,0,0,0,4);
        fill_image(&world,format,&d,&layout,47 + i);
        if (!execute(&world,storage_load_2d,sizeof storage_load_2d,user,4,0xffff,NULL)) check_load(&world,format,&d,&layout,0,0,0,4);
        for (unsigned c = 0; c < 4; ++c) user[c] = format->number == 0 || format->number == 7 ? bits((float)(c & 1)) : 17 + c;
        execute(&world,storage_store_2d,sizeof storage_store_2d,user,1,0xffff,NULL);
        for (unsigned c = 0; c < format->components; ++c) {
            if (format->number == 0) CHECK(world.pool[IMAGE + c] == (c & 1 ? 255 : 0));
            else if (format->number == 4 && format->data == 10) CHECK(world.pool[IMAGE + c] == 17 + c);
            else { uint32_t actual; memcpy(&actual,world.pool + IMAGE + 4 * c,4); CHECK(actual == user[c]); }
        }
    }
    close_world(&world);
}
static void descriptor_guards(void)
{
    World world = open_world(); const Format *format = formats + 2; uint32_t user[16] = {0}, saved[8];
    image_descriptor(saved,format,RT_IMAGE_2D,31,IMAGE); memcpy(user + 8,saved,sizeof saved);
    rt_image_descriptor d; rt_tiling layout = layout_for(saved,&d); fill_image(&world,format,&d,&layout,7); buffer_descriptor(user,OUTPUT);
    for (unsigned bit = 8; bit < 16; ++bit) execute(&world,storage_load_2d,sizeof storage_load_2d,user,1,0xffffu ^ (1u << bit),"compute descriptor reads unset");
    world.declared = 15; execute(&world,storage_load_2d,sizeof storage_load_2d,user,1,0xffff,"outside its declared user data"); world.declared = 16;
    world.stage = RT_STAGE_PIXEL; execute(&world,storage_load_2d,sizeof storage_load_2d,user,1,0xffff,"compute descriptor reads unset"); world.stage = RT_STAGE_COMPUTE;
    user[4] = (uint32_t)at(TABLE); user[5] = (uint32_t)(at(TABLE) >> 32); memcpy(world.pool + TABLE + 32,saved,sizeof saved);
    world.declared = 6;
    if (!execute(&world,storage_load_memory,sizeof storage_load_memory,user,4,0x3f,NULL)) check_load(&world,format,&d,&layout,0,0,0,4);
    user[5] |= 0xbeef0000u;
    if (!execute(&world,storage_load_memory,sizeof storage_load_memory,user,4,0x3f,NULL)) check_load(&world,format,&d,&layout,0,0,0,4);
    user[5] &= 0xffff;
    for (unsigned bit = 4; bit < 6; ++bit) execute(&world,storage_load_memory,sizeof storage_load_memory,user,1,0x3fu ^ (1u << bit),"compute descriptor reads unset");
    world.declared = 5; execute(&world,storage_load_memory,sizeof storage_load_memory,user,1,0x3f,"outside its declared user data"); world.declared = 6;
    const unsigned offsets[] = {POOL - 64, POOL - 60, TABLE + 1};
    for (unsigned i = 0; i < 3; ++i) {
        user[4] = (uint32_t)at(offsets[i]);
        if (!i) memcpy(world.pool + offsets[i] + 32,saved,sizeof saved);
        const int result = execute(&world,storage_load_memory,sizeof storage_load_memory,user,1,0x3f,i ? "descriptor is outside the GPU-visible pool or unaligned" : NULL);
        if (!i && !result) check_load(&world,format,&d,&layout,0,0,0,1);
    }
    world.declared = 16; user[4] = user[5] = user[6] = 0;
    for (unsigned variant = 0; variant < 9; ++variant) {
        memcpy(user + 8,saved,sizeof saved); const char *reason = "";
        if (variant == 0) { user[11] |= 1u << 16; reason = "require relative and physical mip zero"; }
        if (variant == 1) { user[11] = (user[11] & ~15u) | 1; reason = "swizzle must preserve"; }
        if (variant == 2) { user[12] = 11u << 13; reason = "dimensions, layers or pitch"; }
        if (variant == 3) { user[11] = (user[11] & 0x0fffffffu) | (uint32_t)RT_IMAGE_3D << 28; reason = "only 1D/2D compute"; }
        if (variant == 4) { user[9] = (user[9] & ~(63u << 20 | 15u << 26)) | 35u << 20; reason = "compressed and sRGB"; }
        if (variant == 5) { user[9] = (user[9] & ~(63u << 20 | 15u << 26)) | 10u << 20 | 9u << 26; reason = "compressed and sRGB"; }
        if (variant == 6) { user[11] = (user[11] & ~(31u << 20)) | 1u << 20; reason = "compute image layout"; }
        if (variant == 7) { image_descriptor(user + 8,format,RT_IMAGE_2D_ARRAY,31,IMAGE); user[13] = 3 | 2u << 13; reason = "array view exceeds"; }
        if (variant == 8) { image_descriptor(user + 8,format,RT_IMAGE_2D_ARRAY,31,IMAGE); user[13] = 1 | 3u << 13; reason = "array view exceeds"; }
        execute(&world,storage_load_2d,sizeof storage_load_2d,user,1,0xffff,reason);
    }
    memcpy(user + 8,saved,sizeof saved); user[11] = (user[11] & ~0xfffu) | 4 | 2u << 3 | 3u << 6;
    if (!execute(&world,storage_load_2d,sizeof storage_load_2d,user,1,0xffff,NULL)) check_load(&world,format,&d,&layout,0,0,0,1);
    close_world(&world);
}
static void publication(void)
{
    World world = open_world(); const Format *format = formats + 2; uint32_t user[16] = {0};
    image_descriptor(user + 8,format,RT_IMAGE_2D_ARRAY,14,IMAGE); user[11] |= 1u << 25;
    rt_image_descriptor d; rt_tiling layout = layout_for(user + 8,&d); CHECK(d.layers == 4);
    fill_image(&world,format,&d,&layout,3); const size_t span = (size_t)rt_tiling_size(&layout,d.layers);
    unsigned char *expected = malloc(span); if (!expected) exit(1); memcpy(expected,world.pool + IMAGE,span);
    const uint32_t half = bits(0.5f);
    /* A buffer write to an unexposed layer must survive the later image publication. */
    buffer_descriptor(user,IMAGE + (unsigned)rt_tiling_offset(&layout,0,0,0));
    memcpy(expected + rt_tiling_offset(&layout,0,0,0),&half,4);
    memcpy(expected + rt_tiling_offset(&layout,0,0,1),&half,4);
    /* The store uses an array view: select relative layer zero explicitly. */
    execute(&world,storage_array_store_and_buffer,sizeof storage_array_store_and_buffer,user,1,0xffff,NULL);
    CHECK(memcmp(world.pool + IMAGE,expected,span) == 0); free(expected);
    /* Read-only image access must not publish a stale texture over this buffer write. */
    image_descriptor(user + 8,format,RT_IMAGE_2D,31,IMAGE); layout = layout_for(user + 8,&d); fill_image(&world,format,&d,&layout,7);
    const size_t plain_span = (size_t)rt_tiling_size(&layout,1); expected = malloc(plain_span); if (!expected) exit(1); memcpy(expected,world.pool + IMAGE,plain_span);
    uint32_t values[4] = {0,0,0,0x3f800000}; memcpy(values,world.pool + IMAGE,4);
    buffer_descriptor(user,IMAGE + (unsigned)rt_tiling_offset(&layout,9,4,0)); memcpy(expected + rt_tiling_offset(&layout,9,4,0),values,sizeof values);
    execute(&world,storage_load_2d,sizeof storage_load_2d,user,1,0xffff,NULL);
    CHECK(memcmp(world.pool + IMAGE,expected,plain_span) == 0); free(expected);
    /* A later runtime-stop word does not undo completed texture writes. */
    const Format *rgba = formats + 6; image_descriptor(user + 8,rgba,RT_IMAGE_2D,31,IMAGE); layout = layout_for(user + 8,&d); fill_image(&world,rgba,&d,&layout,9);
    buffer_descriptor(user,OUTPUT); user[3] = (4 | 5u << 3 | 6u << 6 | 7u << 9) | 2u << 15 | 7u << 12;
    execute(&world,storage_store_then_stop,sizeof storage_store_then_stop,user,1,0xffff,"DATA_FORMAT 2");
    for (unsigned c = 0; c < 4; ++c) CHECK(((uint32_t *)(world.pool + IMAGE))[c] == half);
    close_world(&world);
}
static void read_write_and_alias(void)
{
    const unsigned selection[] = {0,2,3,4,6,7,8};
    for (unsigned f = 0; f < sizeof selection / sizeof *selection; ++f) {
        const Format *format = formats + selection[f]; World world = open_world(); uint32_t user[16] = {0};
        image_descriptor(user + 8,format,RT_IMAGE_2D,31,IMAGE); rt_image_descriptor d; rt_tiling layout = layout_for(user + 8,&d); fill_image(&world,format,&d,&layout,17);
        uint32_t initial = format->number == 7 ? bits(0.25f) : 0xffffffffu, expected = format->number == 7 ? bits(0.75f) : 0;
        if (format->number == 0) { world.pool[IMAGE] = 192; expected = 255; }
        else if (format->data == 10) { world.pool[IMAGE] = 100; expected = 101; }
        else memcpy(world.pool + IMAGE,&initial,4);
        const uint32_t *program = format->number == 4 || format->number == 5 ? storage_inplace_bits : storage_inplace;
        size_t bytes = program == storage_inplace_bits ? sizeof storage_inplace_bits : sizeof storage_inplace;
        const size_t span = (size_t)rt_tiling_size(&layout,1); unsigned char *whole = malloc(span); if (!whole) exit(1);
        memcpy(whole,world.pool + IMAGE,span);
        if (format->number == 0 || format->data == 10) whole[0] = (unsigned char)expected;
        else memcpy(whole,&expected,4);
        execute(&world,program,bytes,user,1,0xffff,NULL);
        CHECK(memcmp(world.pool + IMAGE,whole,span) == 0); free(whole);
        close_world(&world);
    }
    for (unsigned f = 1; f <= 5; f += 4) {
        World world = open_world(); uint32_t user[16] = {0}; image_descriptor(user + 8,formats + f,RT_IMAGE_2D,31,IMAGE);
        execute(&world,storage_inplace,sizeof storage_inplace,user,1,0xffff,"no native read_write"); close_world(&world);
    }
    World world = open_world(); uint32_t user[16] = {0}; image_descriptor(user,formats + 6,RT_IMAGE_2D,31,IMAGE); memcpy(user + 8,user,32);
    rt_image_descriptor d; rt_tiling layout = layout_for(user,&d); fill_image(&world,formats + 6,&d,&layout,4);
    unsigned char first[16]; memcpy(first,world.pool + IMAGE,16);
    execute(&world,storage_alias_copy,sizeof storage_alias_copy,user,1,0xffff,NULL); CHECK(memcmp(world.pool + IMAGE + 16,first,16) == 0);
    user[15] = 1; execute(&world,storage_alias_copy,sizeof storage_alias_copy,user,1,0xffff,"overlapping compute images");
    close_world(&world);
}
static void slots_and_bounds(void)
{
    World world = open_world(); uint32_t user[16] = {0}; user[0] = (uint32_t)at(TABLE); user[1] = (uint32_t)(at(TABLE) >> 32); buffer_descriptor(user + 4,OUTPUT);
    for (unsigned i = 0; i < 17; ++i) {
        uint32_t *words = (uint32_t *)(world.pool + TABLE + i * 32); image_descriptor(words,formats + 2,RT_IMAGE_2D,31,IMAGE + i * 0x10000);
        float value = (float)(i + 1) / 32; memcpy(world.pool + IMAGE + i * 0x10000,&value,4);
    }
    const struct {const uint32_t *code;size_t bytes;unsigned count;} loads[] = {
        {storage_8_images,sizeof storage_8_images,8}, {storage_9_images,sizeof storage_9_images,9},
        {storage_16_images,sizeof storage_16_images,16}};
    world.declared = 8;
    for (unsigned mode = 0; mode < sizeof loads / sizeof *loads; ++mode) {
        execute(&world,loads[mode].code,loads[mode].bytes,user,1,0xff,NULL);
        for (unsigned i = 0; i < 64; ++i)
            CHECK(((uint32_t *)(world.pool + OUTPUT))[i] == (i < loads[mode].count ? bits((float)(i + 1) / 32) : 0xa5a5a5a5u));
    }
    execute(&world,storage_17_images,sizeof storage_17_images,user,1,0xff,"more than 16 images");
    const struct {const uint32_t *code;size_t bytes;unsigned count;const char *error;} stores[] = {
        {storage_8_stores,sizeof storage_8_stores,8,NULL}, {storage_16_stores,sizeof storage_16_stores,16,NULL},
        {storage_17_stores,sizeof storage_17_stores,0,"more than 16 images"}};
    rt_image_descriptor first; rt_tiling first_layout = layout_for((const uint32_t *)(world.pool + TABLE),&first);
    const size_t image_span = (size_t)rt_tiling_size(&first_layout,1);
    unsigned char *previous = malloc(17 * image_span); CHECK(previous != NULL); if (!previous) exit(1);
    world.declared = 2;
    for (unsigned mode = 0; mode < sizeof stores / sizeof *stores; ++mode) {
        for (unsigned i = 0; i < 17; ++i) memcpy(previous + i * image_span,world.pool + IMAGE + i * 0x10000,image_span);
        execute(&world,stores[mode].code,stores[mode].bytes,user,1,3,stores[mode].error);
        for (unsigned i = 0; i < 17; ++i) {
            if (i < stores[mode].count) { const uint32_t value = bits((float)(i + 1) / 16); memcpy(previous + i * image_span,&value,4); }
            CHECK(memcmp(world.pool + IMAGE + i * 0x10000,previous + i * image_span,image_span) == 0);
        }
    }
    free(previous);
    world.declared = 16; memset(user,0,sizeof user); image_descriptor(user + 8,formats + 2,RT_IMAGE_2D,31,IMAGE);
    rt_image_descriptor d; rt_tiling layout = layout_for(user + 8,&d); const size_t span = (size_t)rt_tiling_size(&layout,1);
    unsigned char *before = malloc(span); if (!before) exit(1); memcpy(before,world.pool + IMAGE,span); user[6] = WIDTH;
    execute(&world,storage_store_2d,sizeof storage_store_2d,user,1,0xffff,NULL); CHECK(memcmp(world.pool + IMAGE,before,span) == 0); free(before);
    buffer_descriptor(user,OUTPUT); execute(&world,storage_load_2d,sizeof storage_load_2d,user,1,0xffff,"image access is outside");
    user[6] = 0; const unsigned offset = POOL - (unsigned)span;
    image_descriptor(user + 8,formats + 2,RT_IMAGE_2D,31,offset); layout = layout_for(user + 8,&d); fill_image(&world,formats + 2,&d,&layout,9);
    if (!execute(&world,storage_load_2d,sizeof storage_load_2d,user,1,0xffff,NULL)) check_load(&world,formats + 2,&d,&layout,0,0,0,1);
    user[8] += 1; execute(&world,storage_load_2d,sizeof storage_load_2d,user,1,0xffff,"compute image is outside");
    close_world(&world);
}

/* The last visible texel/layer is admitted at the native allocation limits. */
static void native_limits(void)
{
    World world = open_world(); uint32_t user[16] = {0}; buffer_descriptor(user,OUTPUT);
    image_descriptor(user + 8,formats + 2,RT_IMAGE_1D,31,IMAGE);
    user[10] = 16383; user[12] = 16383u << 13; user[6] = 16383;
    rt_image_descriptor d; rt_tiling layout = layout_for(user + 8,&d); const uint32_t value = bits(0.625f);
    memcpy(world.pool + IMAGE + rt_tiling_offset(&layout,16383,0,0),&value,4);
    if (!execute(&world,storage_load_1d,sizeof storage_load_1d,user,1,0xffff,NULL)) check_load(&world,formats + 2,&d,&layout,16383,0,0,1);
    image_descriptor(user + 8,formats + 2,RT_IMAGE_2D_ARRAY,31,IMAGE); user[10] = 0; user[12] = 2047; user[13] = 2047 | 2047u << 13;
    user[6] = 0; layout = layout_for(user + 8,&d); CHECK(d.layers == 2048);
    memcpy(world.pool + IMAGE + rt_tiling_offset(&layout,0,0,2047),&value,4);
    if (!execute(&world,storage_load_2d_array,sizeof storage_load_2d_array,user,1,0xffff,NULL)) check_load(&world,formats + 2,&d,&layout,0,0,2047,1);
    user[12] = 2048; user[13] = 2048 | 2048u << 13;
    execute(&world,storage_load_2d_array,sizeof storage_load_2d_array,user,1,0xffff,"dimensions, layers or pitch");
    close_world(&world);
}

int main(void)
{
    matrix(); live_descriptors(); descriptor_guards(); publication(); read_write_and_alias(); slots_and_bounds(); native_limits();
    printf("metal storage: %u scenarios, %u checks, %u failures\n",scenarios,checks,failures); return failures ? 1 : 0;
}
