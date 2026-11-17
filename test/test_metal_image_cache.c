/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Owned small textures exceed the native cache, then revisit live backing bytes.
   Raw probes and whole-pool canaries also observe a hit/miss/writable-alias call. */
#include "runtime/render_executor.h"
#include "runtime/tiling.h"
#include "gcn_programs.h"
#include "metal_image_programs.h"
#include "metal_storage_programs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

enum { POOL = 1 << 20, VS = 0x1000, PS = 0x2000, CS = 0x4000,
       POSITIONS = 0x10000, COLORS = 0x11000, OUTPUT = 0x20000, TABLE = 0x21000,
       OUTPUT_DESCRIPTOR = 0x22000, IMAGES = 0x40000, IMAGE_STEP = 256,
       TARGET = 0xe0000, IMAGE_COUNT = 70, WARM_COUNT = 64,
       IMAGE_WIDTH = 2, IMAGE_PITCH = 8, CANARY = 0xa5, OUTPUT_BYTES = 64,
       CALL_LIMIT = 16, IMAGE_RED_ZERO_ZERO_ONE = 4 | 1 << 9 };
static const uint64_t guest_base = UINT64_C(0x3000000000);
/* First-party LLVM GFX7 instructions from storage_16_images and storage_alias_copy:
   descriptors at pointer s[0:1] + dword0/8/16 bind A(read), B(read), A(write).
   The first two red texels go to s[4:7]; the last binding writes A's second texel.
   Reads and writes use distinct texels, so no same-texel ordering is assumed. */
static const uint32_t hit_miss_alias[] = {
    0x7e000280, 0x7e020280, /* v_mov_b32 v0,0; v_mov_b32 v1,0 */
    0xc0c40100, 0xbf8c007f, /* s_load_dwordx8 s[8:15],s[0:1],0; s_waitcnt lgkmcnt(0) */
    0xf0000100, 0x00020400, 0xbf8c0f70, /* image_load v4,v[0:1],s[8:15] dmask:1; wait */
    0x7e060280, 0xe0701000, 0x80010403, /* v3=0; buffer_store_dword v4,v3,s[4:7],0 offen */
    0xc0c40108, 0xbf8c007f, /* descriptor B at dword8 */
    0xf0000100, 0x00020400, 0xbf8c0f70,
    0x7e060284, 0xe0701000, 0x80010403, /* v3=4; second raw result */
    0xc0c40110, 0xbf8c007f, /* descriptor alias A at dword16 */
    0x7e000281, 0x7e0802f0, /* v0=1; v4=0.5 */
    0xf0200100, 0x00020400, /* image_store v4,v[0:1],s[8:15] dmask:1 */
    0xbf810000
};
static unsigned checks, failures, scenarios, sampled, storage, aliases;
#define CHECK(c) do { ++checks; if (!(c)) { ++failures; if (failures < 20) \
    fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#c); } } while (0)
typedef struct { rt_render_call call[CALL_LIMIT]; size_t count; } Calls;
typedef struct { unsigned char *pool, *expected; rt_executor *executor; } World;
static uint64_t at(unsigned offset) { return guest_base + offset; }
static uint32_t bits(float value) { uint32_t out; memcpy(&out,&value,4); return out; }
static unsigned image_offset(unsigned image) { return IMAGES + image * IMAGE_STEP; }
static rt_render_call *append(Calls *c, rt_render_call_kind kind)
{
    if (c->count == CALL_LIMIT) { fputs("owned call inventory overflow\n",stderr); exit(1); }
    rt_render_call *out = &c->call[c->count++]; memset(out,0,sizeof *out); out->kind = kind; return out;
}
static void buffer(uint32_t out[4], unsigned offset, unsigned records, unsigned stride)
{
    out[0]=(uint32_t)at(offset); out[1]=(uint32_t)(at(offset)>>32) | stride<<16;
    out[2]=records; out[3]=0;
}
static void image(uint32_t out[8], unsigned index)
{
    const uint64_t address = at(image_offset(index)) >> 8;
    memset(out,0,8*sizeof *out); out[0]=(uint32_t)address;
    out[1]=(uint32_t)(address>>32) | 4u<<20 | 7u<<26; /* R32 FLOAT */
    out[2]=IMAGE_WIDTH-1;
    out[3]=IMAGE_RED_ZERO_ZERO_ONE | 31u<<20 | (uint32_t)RT_IMAGE_2D<<28;
    out[4]=(IMAGE_PITCH-1)<<13;
}
static void set_texel(World *w, unsigned index, unsigned x, float value)
{
    const uint32_t raw = bits(value); memcpy(w->pool + image_offset(index) + 4*x,&raw,4);
}
static World open_world(void)
{
    char path[]="/tmp/paleblood-metal-image-cache-XXXXXX", error[512]="";
    int fd=mkstemp(path); if (fd<0) exit(1);
    CHECK(unlink(path)==0); if (ftruncate(fd,POOL)) { close(fd); exit(1); }
    unsigned char *pool=mmap(NULL,POOL,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    if (pool==MAP_FAILED) { close(fd); exit(1); }
    unsigned char *expected=malloc(POOL); if (!expected) exit(1);
    memset(pool,CANARY,POOL);
    memcpy(pool+VS,vertex_position_color,sizeof vertex_position_color);
    const float positions[3][4]={{-1,-1,0,1},{3,-1,0,1},{-1,3,0,1}}, colors[3][4]={{0}};
    memcpy(pool+POSITIONS,positions,sizeof positions); memcpy(pool+COLORS,colors,sizeof colors);
    buffer((uint32_t *)(pool+OUTPUT_DESCRIPTOR),OUTPUT,OUTPUT_BYTES,0);
    for (unsigned i=0;i<IMAGE_COUNT;++i) {
        const float values[2]={1.0f+(float)i/4,30.0f+(float)i/4};
        memcpy(pool+image_offset(i),values,sizeof values);
    }
    rt_executor *executor=NULL; const rt_executor_memory memory={.fd=fd,.size=POOL,.guest_base=guest_base};
    if (rt_executor_open(&memory,&executor,error,sizeof error)) { fprintf(stderr,"open: %s\n",error); exit(1); }
    CHECK(close(fd)==0); return (World){pool,expected,executor};
}
static void close_world(World *w)
{
    rt_executor_close(w->executor); CHECK(munmap(w->pool,POOL)==0); free(w->expected);
}
static void user(Calls *c, rt_shader_stage stage, const uint32_t words[16], unsigned count)
{
    for (unsigned first=0;first<count;first+=RT_USER_DATA_MAX_WORDS) {
        rt_render_call *out=append(c,RT_CALL_USER_DATA); out->u.user_data.stage=stage; out->u.user_data.slot=first;
        out->u.user_data.count=count-first < RT_USER_DATA_MAX_WORDS ? count-first : RT_USER_DATA_MAX_WORDS;
        memcpy(out->u.user_data.words,words+first,out->u.user_data.count*4);
    }
}
static void prepare(World *w)
{
    memset(w->pool+OUTPUT,CANARY,OUTPUT_BYTES); memset(w->pool+TARGET,CANARY,8*8*4);
    memcpy(w->expected,w->pool,POOL);
}
static void expected_output(World *w, const uint32_t *words, unsigned count)
{
    memcpy(w->expected+OUTPUT,words,count*4);
}
static void execute(World *w, const char *id, const Calls *calls, unsigned observed_image)
{
    char error[512]=""; const int rc=rt_executor_run_calls(w->executor,calls->call,calls->count,error,sizeof error);
    const int overlap_refusal=strstr(error,"overlapping compute images require the same native view")!=NULL;
    if (rc) fprintf(stderr,"image-cache %s: %s\n",id,error);
    CHECK(rc==0);
    unsigned output_bad=0,image_bad=0,pool_bad=0;
    for (unsigned i=0;i<OUTPUT_BYTES;++i) output_bad+=w->pool[OUTPUT+i]!=w->expected[OUTPUT+i];
    for (unsigned i=0;i<IMAGE_WIDTH*4;++i)
        image_bad+=w->pool[image_offset(observed_image)+i]!=w->expected[image_offset(observed_image)+i];
    for (unsigned i=0;i<POOL;++i) pool_bad+=w->pool[i]!=w->expected[i];
    CHECK(output_bad==0); CHECK(image_bad==0); CHECK(pool_bad==0);
    uint32_t first,red[2]; memcpy(&first,w->pool+OUTPUT,4); memcpy(red,w->pool+image_offset(observed_image),sizeof red);
    printf("image-cache id=%s API=%d overlap_refusal=%d output_bad=%u image_bad=%u pool_bad=%u first=%08x red0=%08x red1=%08x\n",
        id,rc,overlap_refusal,output_bad,image_bad,pool_bad,first,red[0],red[1]); ++scenarios;
}
static void compute(Calls *c, unsigned words, const uint32_t data[16])
{
    rt_render_call *out=append(c,RT_CALL_COMPUTE_SHADER);
    out->u.compute_shader=(rt_compute_shader){.code=at(CS),.vgprs=16,.sgprs=40,.user_sgprs=words,
        .threads={1,1,1},.thread_id_components=2};
    user(c,RT_STAGE_COMPUTE,data,words);
    out=append(c,RT_CALL_DISPATCH); out->u.dispatch.groups[0]=out->u.dispatch.groups[1]=out->u.dispatch.groups[2]=1;
}
static void load(World *w, const char *id, unsigned index, unsigned x, float wanted)
{
    memcpy(w->pool+CS,storage_load_2d,sizeof storage_load_2d);
    uint32_t data[16]={0}; buffer(data,OUTPUT,OUTPUT_BYTES,0); data[6]=x; image(data+8,index);
    prepare(w); const uint32_t result[4]={bits(wanted),0,0,bits(1)}; expected_output(w,result,4);
    Calls c={0}; compute(&c,16,data); execute(w,id,&c,index); ++storage;
}
static void store(World *w, const char *id, unsigned index, float wanted)
{
    memcpy(w->pool+CS,storage_store_2d,sizeof storage_store_2d);
    uint32_t data[16]={bits(wanted),0,0,bits(1)}; data[6]=1; image(data+8,index);
    prepare(w); const uint32_t raw=bits(wanted); memcpy(w->expected+image_offset(index)+4,&raw,4);
    Calls c={0}; compute(&c,16,data); execute(w,id,&c,index); ++storage;
}
static void sample(World *w, const char *id, unsigned index, float wanted)
{
    memcpy(w->pool+PS,image_probe_sample,sizeof image_probe_sample);
    uint32_t data[16]={0}; image(data,index); data[8]=2|2<<3|2<<6;
    data[12]=(uint32_t)at(OUTPUT_DESCRIPTOR); data[13]=(uint32_t)(at(OUTPUT_DESCRIPTOR)>>32);
    data[14]=bits(0.25f); data[15]=bits(0.5f);
    prepare(w); const uint32_t result[4]={bits(wanted),0,0,bits(1)}; expected_output(w,result,4);
    Calls c={0}; rt_render_call *out=append(&c,RT_CALL_COLOR_TARGET); out->u.color_target.enabled=1;
    out->u.color_target.target=(rt_color_target){.address=at(TARGET),.pitch=8,.height=8,.format=0xa,.tile_mode_index=31,.samples=1};
    out=append(&c,RT_CALL_VIEWPORT); out->u.viewport.zmax=1;
    out->u.viewport.scale[0]=out->u.viewport.scale[1]=out->u.viewport.translate[0]=out->u.viewport.translate[1]=4;
    out->u.viewport.scale[2]=1;
    out=append(&c,RT_CALL_VERTEX_SHADER); out->u.vertex_shader=(rt_vertex_shader){.code=at(VS),.vgprs=12,.sgprs=16,.user_sgprs=8,.parameter_count=1,.position_format=4};
    out=append(&c,RT_CALL_PIXEL_SHADER); out->u.pixel_shader=(rt_pixel_shader){.code=at(PS),.vgprs=16,.sgprs=40,.user_sgprs=16,.color_formats=9,.input_enable=2,.input_address=2,.color_mask=15};
    uint32_t vertex[16]={0}; buffer(vertex,POSITIONS,3,16); buffer(vertex+4,COLORS,3,16);
    user(&c,RT_STAGE_VERTEX,vertex,8); user(&c,RT_STAGE_PIXEL,data,16);
    out=append(&c,RT_CALL_REGISTER); out->u.reg.address=0xa08e; /* Fragment buffer writes remain active with color mask0. */
    out=append(&c,RT_CALL_REGISTER); out->u.reg.address=0xa00c;
    out=append(&c,RT_CALL_REGISTER); out->u.reg.address=0xa00d; out->u.reg.value=1|1u<<16;
    out=append(&c,RT_CALL_DRAW_AUTO); out->u.draw_auto.index_count=3;
    execute(w,id,&c,index); ++sampled;
}
int main(void)
{
    World w=open_world(); char id[64];
    for (unsigned i=0;i<IMAGE_COUNT;++i) {
        snprintf(id,sizeof id,"sample/churn%u",i); sample(&w,id,i,1.0f+(float)i/4);
    }
    set_texel(&w,0,0,20.5f); sample(&w,"sample/revisit-evicted-CPU",0,20.5f);
    set_texel(&w,69,0,21.5f); sample(&w,"sample/revisit-hot-CPU",69,21.5f);
    for (unsigned i=0;i<IMAGE_COUNT;++i) {
        const float wanted=50.0f+(float)i/4;
        snprintf(id,sizeof id,"storage/store%u",i); store(&w,id,i,wanted);
        snprintf(id,sizeof id,"storage/readback%u",i); load(&w,id,i,1,wanted);
    }
    load(&w,"storage/revisit-evicted",0,1,50.0f);
    set_texel(&w,69,0,22.5f); load(&w,"storage/revisit-hot-CPU",69,0,22.5f);
    close_world(&w);
    w=open_world();
    for (unsigned i=0;i<WARM_COUNT;++i) {
        snprintf(id,sizeof id,"alias/warm%u",i); load(&w,id,i,0,1.0f+(float)i/4);
    }
    uint32_t descriptors[24]; image(descriptors,0); image(descriptors+8,64); image(descriptors+16,0);
    memcpy(w.pool+TABLE,descriptors,sizeof descriptors); memcpy(w.pool+CS,hit_miss_alias,sizeof hit_miss_alias);
    uint32_t data[16]={0}; data[0]=(uint32_t)at(TABLE); data[1]=(uint32_t)(at(TABLE)>>32); buffer(data+4,OUTPUT,OUTPUT_BYTES,0);
    prepare(&w); const uint32_t result[2]={bits(1),bits(17)}; expected_output(&w,result,2);
    const uint32_t written=bits(0.5f); memcpy(w.expected+image_offset(0)+4,&written,4);
    Calls c={0}; compute(&c,8,data); execute(&w,"alias/hit-miss-write",&c,0); ++aliases;
    load(&w,"alias/published-reload",0,1,0.5f); close_world(&w);
    printf("Metal image cache: %u scenarios, %u sampled, %u storage, %u aliases, %u checks, %u failures\n",
        scenarios,sampled,storage,aliases,checks,failures);
    return failures ? 1 : 0;
}
