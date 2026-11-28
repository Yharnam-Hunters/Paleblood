/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Owned fixed-function blend goldens, fresh vertex stores and complete guest-pool guards. */
#include "runtime/render_executor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

enum { POOL=64<<10, VS=0x1000, PS=0x2000, POSITIONS=0x3000, ATTRIBUTES=0x3400,
       MARKERS=0x5000, TARGET=0x8000, WIDTH=16, HEIGHT=8, PIXELS=WIDTH*HEIGHT,
       COLOR_BYTES=PIXELS*4, MARKER_WORDS=8, CALLS=32, BLEND_ENABLE=1u<<30,
       SRC_ALPHA=4, INV_SRC_ALPHA=5, ALPHA_BLEND=BLEND_ENABLE|SRC_ALPHA|(INV_SRC_ALPHA<<8) };
static const uint64_t base=UINT64_C(0x3000000000);
static const uint32_t marker=UINT32_C(0x13572468);
/* Exact owned vertex_store_witness from test_metal_executor.c. */
static const uint32_t vertex_program[] = {
    0xebf32000, 0x80000100, /* tbuffer_load_format_xyzw v[1:4], v0, s[0:3], 0 format:[BUF_DATA_FORMAT_32_32_32_32,BUF_NUM_FORMAT_FLOAT] idxen */
    0xebf32000, 0x80010500, /* tbuffer_load_format_xyzw v[5:8], v0, s[4:7], 0 format:[BUF_DATA_FORMAT_32_32_32_32,BUF_NUM_FORMAT_FLOAT] idxen */
    0xbf8c0f70,           /* s_waitcnt vmcnt(0) */
    0x7e1202ff, 0x13572468, /* v_mov_b32_e32 v9, 0x13572468 */
    0x34140082,           /* v_lshlrev_b32_e32 v10, 2, v0 */
    0xe0701000, 0x8002090a, /* buffer_store_dword v9, v10, s[8:11], 0 offen */
    0xf800020f, 0x08070605, /* exp param0, v5, v6, v7, v8 */
    0xf80008cf, 0x04030201, /* exp pos0, v1, v2, v3, v4 done */
    0xbf810000,           /* s_endpgm */
};
/* Exact owned pixel_user_float32 from metal_executor_programs.h. */
static const uint32_t pixel_program[] = {
    0x7e040200, /* v_mov_b32_e32 v2, s0 */
    0x7e060201, /* v_mov_b32_e32 v3, s1 */
    0x7e080202, /* v_mov_b32_e32 v4, s2 */
    0x7e0a0203, /* v_mov_b32_e32 v5, s3 */
    0xf800180f, 0x05040302, /* exp mrt0, v2, v3, v4, v5 done vm */
    0xbf810000, /* s_endpgm */
};
/* Logical RGBA byte goldens for source(1,0,0,1/4), destination(0,1,0,1).
   Rows and columns select ZERO,ONE,SRC_ALPHA,ONE_MINUS_SRC_ALPHA, respectively.
   Values are independent literal ADD/shared-alpha results, never production helpers. */
static const uint8_t factor_goldens[4][4][4]={
    {{0,0,0,0},{0,255,0,255},{0,64,0,64},{0,191,0,191}},
    {{255,0,0,64},{255,255,0,255},{255,64,0,128},{255,191,0,255}},
    {{64,0,0,16},{64,255,0,255},{64,64,0,80},{64,191,0,207}},
    {{191,0,0,48},{191,255,0,255},{191,64,0,112},{191,191,0,239}},
};

static const uint8_t green[4]={0,255,0,255},purple[4]={255,0,255,0};
static const uint8_t source_color[4]={255,0,0,64},alpha_color[4]={64,191,0,207},
    inverse_color[4]={191,64,0,112},changed_destination[4]={255,0,191,16};
/* IEC61966-2-1: C'=1.055*C^(1/2.4)-0.055 for C>0.0031308.
   Linear1/4 and3/4 encode to137 and225. Alpha13/16 stays linear207.
   https://registry.color.org/rgb-registry/srgb
   https://developer.apple.com/documentation/metal/mtlpixelformat */
static const uint8_t srgb_color[4]={137,225,0,207};
typedef struct {
    const char *id;
    uint32_t blend;
    unsigned format,mask,mode,higher_bank;
    const uint8_t *destination,*result;
    const char *reason;
} Case;
static unsigned checks,failures,scenarios;
#define CHECK(c) do {++checks;if(!(c)){++failures;if(failures<=32)fprintf(stderr,"line%d: %s\n",__LINE__,#c);}}while(0)
typedef struct {rt_render_call calls[CALLS];size_t count;} Calls;
typedef struct {rt_executor *x;uint8_t *pool,before[POOL],wanted[POOL],writable[POOL];} Fixture;
static uint64_t at(unsigned offset){return base+offset;}
static rt_render_call *event(Calls *calls,rt_render_call_kind kind)
{
    if(calls->count==CALLS){fputs("owned call inventory overflow\n",stderr);exit(1);}
    rt_render_call *c=&calls->calls[calls->count++];memset(c,0,sizeof *c);c->kind=kind;return c;
}
static void reg(Calls *calls,unsigned address,uint32_t value)
{rt_render_call *c=event(calls,RT_CALL_REGISTER);c->u.reg.address=address;c->u.reg.value=value;}
static void descriptor(uint32_t *words,unsigned offset,unsigned records,unsigned stride)
{
    const uint64_t address=at(offset);words[0]=(uint32_t)address;
    words[1]=(uint32_t)(address>>32)|stride<<16;words[2]=records;words[3]=0;
}
static void physical(const uint8_t logical[4],unsigned swap,uint8_t bytes[4])
{bytes[0]=logical[swap?2:0];bytes[1]=logical[1];bytes[2]=logical[swap?0:2];bytes[3]=logical[3];}
static void setup(Calls *calls,const Case *test)
{
    rt_render_call *c=event(calls,RT_CALL_COLOR_TARGET);c->u.color_target.enabled=1;
    /* Existing owned color-target tests define NUMBER_SRGB=6, SWAP_STD=0 and SWAP_ALT=1. */
    c->u.color_target.target=(rt_color_target){.address=at(TARGET+test->format*0x1000),.pitch=WIDTH,
        .height=HEIGHT,.format=0xa,.number_type=test->format>=2?6:0,.component_swap=test->format&1,
        .tile_mode_index=31,.samples=1};
    c=event(calls,RT_CALL_VIEWPORT);c->u.viewport.zmax=1;
    c->u.viewport.scale[0]=c->u.viewport.translate[0]=WIDTH/2;
    c->u.viewport.scale[1]=c->u.viewport.translate[1]=HEIGHT/2;c->u.viewport.scale[2]=1;
    c=event(calls,RT_CALL_VERTEX_SHADER);c->u.vertex_shader=(rt_vertex_shader){.code=at(VS),.vgprs=12,
        .sgprs=16,.user_sgprs=12,.parameter_count=1,.position_format=4};
    c=event(calls,RT_CALL_PIXEL_SHADER);c->u.pixel_shader=(rt_pixel_shader){.code=at(PS),.vgprs=8,.sgprs=8,
        .user_sgprs=4,.color_formats=9,.input_enable=2,.input_address=2,.interpolated=1,.color_mask=15};
    c=event(calls,RT_CALL_USER_DATA);c->u.user_data.stage=RT_STAGE_VERTEX;c->u.user_data.count=8;
    descriptor(c->u.user_data.words,POSITIONS,3,16);descriptor(c->u.user_data.words+4,ATTRIBUTES,3,16);
    c=event(calls,RT_CALL_USER_DATA);c->u.user_data.stage=RT_STAGE_VERTEX;c->u.user_data.slot=8;c->u.user_data.count=4;
    descriptor(c->u.user_data.words,MARKERS,MARKER_WORDS,4);
    c=event(calls,RT_CALL_USER_DATA);c->u.user_data.stage=RT_STAGE_PIXEL;c->u.user_data.count=4;
    const uint32_t rgba[4]={0x3f800000,0,0,0x3e800000};memcpy(c->u.user_data.words,rgba,sizeof rgba);
    reg(calls,0xc242,4);reg(calls,0xa08e,test->mask);reg(calls,0xa202,0x00cc0000|test->mode<<4);
    reg(calls,0xa200,0);reg(calls,0xa1e0,test->blend);
    if(test->higher_bank)reg(calls,0xa1e7,UINT32_MAX);
}
static void invoke(Fixture *f,const Case *test)
{
    Calls calls={0};setup(&calls,test);
    const unsigned target=TARGET+test->format*0x1000;
    uint8_t destination[4],result[4];physical(test->destination,test->format&1,destination);
    physical(test->result,test->format&1,result);
    memset(f->pool+MARKERS,0,MARKER_WORDS*4);memset(f->pool+MARKERS+MARKER_WORDS*4,0x6d,64);
    for(unsigned pixel=0;pixel<PIXELS;++pixel)memcpy(f->pool+target+pixel*4,destination,4);
    memcpy(f->before,f->pool,POOL);memcpy(f->wanted,f->pool,POOL);memset(f->writable,0,POOL);
    if(!test->reason){
        for(unsigned vertex=0;vertex<3;++vertex){memcpy(f->wanted+MARKERS+vertex*4,&marker,4);memset(f->writable+MARKERS+vertex*4,1,4);}
        if(test->mask && test->mode)for(unsigned pixel=0;pixel<PIXELS;++pixel){
            memcpy(f->wanted+target+pixel*4,result,4);memset(f->writable+target+pixel*4,1,4);
        }
    }
    event(&calls,RT_CALL_DRAW_AUTO)->u.draw_auto.index_count=3;
    char error[1024]="";const int rc=rt_executor_run_calls(f->x,calls.calls,calls.count,error,sizeof error);
    const unsigned reason_match=test->reason?(rc==-1 && strstr(error,test->reason)!=NULL):(rc==0 && error[0]=='\0');
    size_t changed=0,want_changed=0,mismatches=0,writable=0,guard_bad=0;
    for(unsigned i=0;i<POOL;++i){
        changed+=f->pool[i]!=f->before[i];want_changed+=f->wanted[i]!=f->before[i];
        mismatches+=f->pool[i]!=f->wanted[i];writable+=f->writable[i]!=0;
        if(!f->writable[i])guard_bad+=f->pool[i]!=f->before[i];
    }
    unsigned color_bad=0,alpha_bad=0,marker_bad=0;uint32_t raw_first,raw_last,expected_raw;
    for(unsigned pixel=0;pixel<PIXELS;++pixel){
        for(unsigned byte=0;byte<4;++byte)color_bad+=f->pool[target+pixel*4+byte]!=f->wanted[target+pixel*4+byte];
        alpha_bad+=f->pool[target+pixel*4+3]!=f->wanted[target+pixel*4+3];
    }
    for(unsigned word=0;word<MARKER_WORDS;++word)marker_bad+=memcmp(f->pool+MARKERS+word*4,f->wanted+MARKERS+word*4,4)!=0;
    memcpy(&raw_first,f->pool+target,4);memcpy(&raw_last,f->pool+target+COLOR_BYTES-4,4);memcpy(&expected_raw,f->wanted+target,4);
    CHECK(reason_match);CHECK(mismatches==0);CHECK(changed==want_changed);CHECK(guard_bad==0);
    CHECK(color_bad==0);CHECK(alpha_bad==0);CHECK(marker_bad==0);
    printf("alpha-blend id=%s rc=%d reason_match=%u control=0x%08x format=%u mask=%u mode=%u higher_bank=%u "
        "raw_first=0x%08x raw_last=0x%08x expected_raw=0x%08x alpha_first=%u expected_alpha=%u "
        "color_bad_bytes=%u alpha_bad=%u marker_bad=%u pool_compared=%u pool_changed=%zu want_changed=%zu "
        "pool_mismatches=%zu writable_bytes=%zu guard_bytes=%zu guard_bad=%zu reason=%s\n",
        test->id,rc,reason_match,test->blend,test->format,test->mask,test->mode,test->higher_bank,
        raw_first,raw_last,expected_raw,(unsigned)f->pool[target+3],(unsigned)f->wanted[target+3],color_bad,alpha_bad,marker_bad,
        (unsigned)POOL,changed,want_changed,mismatches,writable,(size_t)POOL-writable,guard_bad,error);
    ++scenarios;
}
int main(void)
{
    char path[]="/tmp/paleblood-metal-alpha-blend-XXXXXX",error[512]="";const int fd=mkstemp(path);
    CHECK(fd>=0);if(fd<0)return 1;unlink(path);CHECK(ftruncate(fd,POOL)==0);
    uint8_t *pool=mmap(NULL,POOL,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);CHECK(pool!=MAP_FAILED);
    if(pool==MAP_FAILED){close(fd);return 1;}memset(pool,0x6d,POOL);
    Fixture *f=calloc(1,sizeof *f);CHECK(f!=NULL);if(!f){munmap(pool,POOL);close(fd);return 1;}f->pool=pool;
    memcpy(pool+VS,vertex_program,sizeof vertex_program);memcpy(pool+PS,pixel_program,sizeof pixel_program);
    const float positions[3][4]={{-1,-1,0,1},{3,-1,0,1},{-1,3,0,1}},attributes[3][4]={{0,0,0,1},{0,0,0,1},{0,0,0,1}};
    memcpy(pool+POSITIONS,positions,sizeof positions);memcpy(pool+ATTRIBUTES,attributes,sizeof attributes);
    const rt_executor_memory memory={.fd=fd,.size=POOL,.guest_base=base};
    const int opened=rt_executor_open(&memory,&f->x,error,sizeof error);CHECK(opened==0);
    if(opened)fprintf(stderr,"open: %s\n",error);
    if(!opened){
        /* Same shader code, addresses and target format; only fixed-function state changes. */
        const Case cache[]={
            {"cache-disabled-cold",0,0,15,1,0,green,source_color,NULL},
            {"cache-alpha",ALPHA_BLEND,0,15,1,0,green,alpha_color,NULL},
            {"cache-reciprocal",BLEND_ENABLE|5|(4<<8),0,15,1,0,green,inverse_color,NULL},
            {"cache-alpha-hit",ALPHA_BLEND,0,15,1,0,green,alpha_color,NULL},
            {"cache-disabled-hit",0,0,15,1,0,green,source_color,NULL},
        };
        for(unsigned i=0;i<sizeof cache/sizeof *cache;++i)invoke(f,&cache[i]);
        static const unsigned factors[4]={0,1,4,5};
        for(unsigned source=0;source<4;++source)for(unsigned destination=0;destination<4;++destination){
            char id[48];snprintf(id,sizeof id,"factor-%u-%u",factors[source],factors[destination]);
            const Case test={id,BLEND_ENABLE|factors[source]|factors[destination]<<8,0,15,1,0,
                green,factor_goldens[source][destination],NULL};invoke(f,&test);
        }
        /* Independent alpha uses the literal alpha column above, independently of
           the RGB-factor pair. Holding the shader/address/format fixed also checks
           pipeline-cache changes when only either alpha factor changes. */
        for(unsigned source=0;source<4;++source)for(unsigned destination=0;destination<4;++destination)
            for(unsigned alpha_source=0;alpha_source<4;++alpha_source)for(unsigned alpha_destination=0;alpha_destination<4;++alpha_destination){
                /* Each fixed RGB pair owns a fresh executor: all 16 alpha states
                   exercise one cache without exceeding its bounded capacity. */
                if(!alpha_source && !alpha_destination){
                    rt_executor_close(f->x);f->x=NULL;error[0]='\0';
                    const int reopened=rt_executor_open(&memory,&f->x,error,sizeof error);CHECK(reopened==0);
                    if(reopened){fprintf(stderr,"reopen: %s\n",error);exit(1);}
                }
                char id[80];snprintf(id,sizeof id,"separate-%u-%u-%u-%u",factors[source],factors[destination],factors[alpha_source],factors[alpha_destination]);
                uint8_t result[4];memcpy(result,factor_goldens[source][destination],sizeof result);
                result[3]=factor_goldens[alpha_source][alpha_destination][3];
                const Case test={id,BLEND_ENABLE|(1u<<29)|factors[source]|factors[destination]<<8|
                    factors[alpha_source]<<16|factors[alpha_destination]<<24,0,15,1,0,green,result,NULL};
                invoke(f,&test);
            }
        const uint8_t separate_source[4]={64,191,0,64},separate_zero[4]={64,191,0,0},
            separate_destination[4]={64,191,0,255},separate_srgb[4]={137,225,0,64};
        const Case separate_cache[]={
            {"separate-source",ALPHA_BLEND|(1u<<29)|(1u<<16),0,15,1,0,green,separate_source,NULL},
            {"separate-destination",ALPHA_BLEND|(1u<<29)|(1u<<24),0,15,1,0,green,separate_destination,NULL},
            {"separate-zero",ALPHA_BLEND|(1u<<29),0,15,1,0,green,separate_zero,NULL},
            {"separate-source-hit",ALPHA_BLEND|(1u<<29)|(1u<<16),0,15,1,0,green,separate_source,NULL},
            {"separate-bgra",ALPHA_BLEND|(1u<<29)|(1u<<16),1,15,1,0,green,separate_source,NULL},
            {"separate-srgb-rgba",ALPHA_BLEND|(1u<<29)|(1u<<16),2,15,1,0,green,separate_srgb,NULL},
            {"separate-srgb-bgra",ALPHA_BLEND|(1u<<29)|(1u<<16),3,15,1,0,green,separate_srgb,NULL},
            {"separate-back-to-shared",ALPHA_BLEND,0,15,1,0,green,alpha_color,NULL},
            {"separate-masked-unsupported",ALPHA_BLEND|(1u<<29)|(2u<<16)|(1u<<21),0,0,1,0,green,green,NULL},
            {"separate-mode-zero-unsupported",ALPHA_BLEND|(1u<<29)|(2u<<24)|(1u<<21),0,15,0,0,green,green,NULL},
        };
        for(unsigned i=0;i<sizeof separate_cache/sizeof *separate_cache;++i)invoke(f,&separate_cache[i]);
        const Case extra[]={
            {"linear-bgra",ALPHA_BLEND,1,15,1,0,green,alpha_color,NULL},
            {"srgb-rgba",ALPHA_BLEND,2,15,1,0,green,srgb_color,NULL},
            {"srgb-bgra",ALPHA_BLEND,3,15,1,0,green,srgb_color,NULL},
            {"destination-reload",ALPHA_BLEND,0,15,1,0,purple,changed_destination,NULL},
            {"destination-restore",ALPHA_BLEND,0,15,1,0,green,alpha_color,NULL},
            {"inactive-alpha-fields",ALPHA_BLEND|(1u<<16),0,15,1,0,green,alpha_color,NULL},
            {"masked-unsupported",BLEND_ENABLE|2,0,0,1,0,green,green,NULL},
            {"mode-zero-unsupported",BLEND_ENABLE|2,0,15,0,0,green,green,NULL},
            {"higher-bank-ignored",ALPHA_BLEND,0,15,1,1,green,alpha_color,NULL},
            {"disabled-unsupported-fields",0xbfffffffu,0,15,1,0,green,source_color,NULL},
        };
        for(unsigned i=0;i<sizeof extra/sizeof *extra;++i)invoke(f,&extra[i]);
        const struct {const char *id;uint32_t control;} refused[]={
            {"refuse-source-color",BLEND_ENABLE|2},
            {"refuse-destination-color",BLEND_ENABLE|1|(2<<8)},
            {"refuse-special-factor",BLEND_ENABLE|11},
            {"refuse-subtract",ALPHA_BLEND|(1u<<5)},
            {"refuse-alpha-subtract",ALPHA_BLEND|(1u<<29)|(1u<<21)},
            {"refuse-alpha-source-color",ALPHA_BLEND|(1u<<29)|(2u<<16)},
            {"refuse-alpha-destination-color",ALPHA_BLEND|(1u<<29)|(2u<<24)},
            {"refuse-disable-rop3",ALPHA_BLEND|(1u<<31)},
            {"refuse-reserved13",ALPHA_BLEND|(1u<<13)},
            {"refuse-reserved14",ALPHA_BLEND|(1u<<14)},
            {"refuse-reserved15",ALPHA_BLEND|(1u<<15)},
        };
        for(unsigned i=0;i<sizeof refused/sizeof *refused;++i){
            Case test={refused[i].id,refused[i].control,0,15,1,0,green,green,"blending"};invoke(f,&test);
            char id[80];snprintf(id,sizeof id,"%s/recovery",refused[i].id);
            test=(Case){id,ALPHA_BLEND,0,15,1,0,green,alpha_color,NULL};invoke(f,&test);
        }
    }
    if(f->x)rt_executor_close(f->x);
    free(f);munmap(pool,POOL);close(fd);
    printf("metal alpha-blend: %u scenarios, %u checks, %u failures\n",scenarios,checks,failures);return failures?1:0;
}
