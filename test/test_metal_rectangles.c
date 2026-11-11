/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Owned shaders and explicit quad references. Rectangle output is compared with
   an ordinary six-vertex draw, including every target/depth/stencil byte and guards. */
#include "runtime/render_executor.h"
#include "runtime/tiling.h"
#include "gcn_programs.h"
#include "metal_executor_programs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

enum { POOL=4<<20, VS=0x1000, PS=0x2000, POS=0x4000, COL=0x5000, COUNTER=0x6000,
       INDEX=0x7000, TARGET=0x100000, Z=0x200000, S=0x300000, W=32, H=32, VERTICES=12 };
static const uint64_t base=UINT64_C(0x3000000000);
static unsigned checks,failures,scenarios;
#define CHECK(x) do{++checks;if(!(x)){++failures;if(failures<=20)fprintf(stderr,"line %d: %s\n",__LINE__,#x);}}while(0)
static const uint32_t counted_vertex[]={
    0xebf32000,0x80000100, /* tbuffer_load_format_xyzw v[1:4], v0, s[0:3], 0 format:[32_32_32_32,float] idxen */
    0xebf32000,0x80010500, /* tbuffer_load_format_xyzw v[5:8], v0, s[4:7], 0 format:[32_32_32_32,float] idxen */
    0x34140082, /* v_lshlrev_b32_e32 v10, 2, v0 */
    0xe0301000,0x8002090a, /* buffer_load_dword v9, v10, s[8:11], 0 offen */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0x061212f2, /* v_add_f32_e32 v9, 1.0, v9 */
    0xe0701000,0x8002090a, /* buffer_store_dword v9, v10, s[8:11], 0 offen */
    0xf800020f,0x08070605, /* exp param0, v5, v6, v7, v8 */
    0xf80008cf,0x04030201, /* exp pos0, v1, v2, v3, v4 done */
    0xbf810000, /* s_endpgm */
};

static rt_render_call calls[64]; static size_t count;
static rt_render_call *event(rt_render_call_kind kind)
{
    CHECK(count<64);rt_render_call *c=&calls[count++];memset(c,0,sizeof *c);c->kind=kind;return c;
}
static void reg(uint32_t address,uint32_t value){rt_render_call *c=event(RT_CALL_REGISTER);c->u.reg.address=address;c->u.reg.value=value;}
static void descriptor(uint32_t *p,uint64_t address)
{
    p[0]=(uint32_t)address;p[1]=(uint32_t)(address>>32)|16u<<16;p[2]=VERTICES;p[3]=0;
}
static void setup(rt_executor *x,uint8_t *pool,unsigned profile,int sy,unsigned primitive)
{
    rt_executor_reset(x);count=0;
    const uint32_t *vs=profile==1?vertex_clip_eight:profile==2?vertex_two_parameters:profile==3?counted_vertex:vertex_position_color;
    const size_t n=profile==1?sizeof vertex_clip_eight:profile==2?sizeof vertex_two_parameters:profile==3?sizeof counted_vertex:sizeof vertex_position_color;
    memset(pool+VS,0,512);memcpy(pool+VS,vs,n);
    const uint32_t *ps=profile==2?pixel_two_parameters:pixel_color;
    memcpy(pool+PS,ps,profile==2?sizeof pixel_two_parameters:sizeof pixel_color);
    memset(pool+COUNTER,0,VERTICES*4+64);memset(pool+TARGET,0x55,W*H*4+64);
    for(unsigned i=0;i<W*H;++i){float z=0.75f;memcpy(pool+Z+i*4,&z,4);}memset(pool+Z+W*H*4,0xa5,64);
    memset(pool+S,0x2a,W*H);memset(pool+S+W*H,0xa5,64);
    rt_render_call *c=event(RT_CALL_COLOR_TARGET);c->u.color_target.enabled=1;
    c->u.color_target.target=(rt_color_target){.address=base+TARGET,.pitch=W,.height=H,.format=0xa,.tile_mode_index=31,.samples=1};
    c=event(RT_CALL_VIEWPORT);c->u.viewport.zmax=1;c->u.viewport.scale[0]=16;c->u.viewport.scale[1]=(float)sy*16;
    c->u.viewport.scale[2]=1;c->u.viewport.translate[0]=c->u.viewport.translate[1]=16;
    c=event(RT_CALL_VERTEX_SHADER);c->u.vertex_shader=(rt_vertex_shader){.code=base+VS,.vgprs=12,.sgprs=16,.user_sgprs=profile==3?12:8,
        .parameter_count=profile==2?2:1,.position_format=profile==1?0x444:4,.output_control=profile==1?255u|(1u<<22)|(1u<<23):0};
    c=event(RT_CALL_PIXEL_SHADER);c->u.pixel_shader=(rt_pixel_shader){.code=base+PS,.vgprs=16,.sgprs=16,
        .color_formats=4,.input_enable=2,.input_address=2,.interpolated=profile==2?2:1,.color_mask=15};
    c=event(RT_CALL_USER_DATA);c->u.user_data.stage=RT_STAGE_VERTEX;c->u.user_data.count=8;
    descriptor(c->u.user_data.words,base+POS);descriptor(c->u.user_data.words+4,base+COL);
    if(profile==3){c=event(RT_CALL_USER_DATA);c->u.user_data.stage=RT_STAGE_VERTEX;c->u.user_data.slot=8;c->u.user_data.count=4;
        descriptor(c->u.user_data.words,base+COUNTER);}
    reg(0xc242,primitive);
    /* Native depth and two-sided stencil make winding and output Z observable. */
    reg(0xa002,0);reg(0xa000,0);reg(0xa00f,0);reg(0xa010,3);reg(0xa011,1);
    reg(0xa012,(uint32_t)((base+Z)>>8));reg(0xa014,(uint32_t)((base+Z)>>8));
    reg(0xa013,(uint32_t)((base+S)>>8));reg(0xa015,(uint32_t)((base+S)>>8));
    reg(0xa016,(W/8-1)|((H/8-1)<<11));reg(0xa017,W*H/64-1);
    reg(0xa200,1|2|4|1<<4|7<<8|7<<20|1<<7);reg(0xa10b,3<<4|6<<16);
    reg(0xa10c,0x01ffff17);reg(0xa10d,0x01ffff39);
}
static int submit(rt_executor *x,unsigned vertices,unsigned indexed,const char *reason)
{
    rt_render_call *c;
    if(indexed){c=event(RT_CALL_INDEX_TYPE);c->u.index_type.type=indexed==1?0:1;
        c=event(RT_CALL_DRAW_INDEXED);
        c->u.draw_indexed.address=vertices?base+INDEX:UINT64_MAX;c->u.draw_indexed.index_count=vertices;
        c->u.draw_indexed.header=0xc0042700;c->u.draw_indexed.max_size=vertices;
    }else{c=event(RT_CALL_DRAW_AUTO);c->u.draw_auto.index_count=vertices;}
    char error[1024]="";const int rc=rt_executor_run_calls(x,calls,count,error,sizeof error);
    if(reason){CHECK(rc==-1&&strstr(error,reason));if(rc!=-1||!strstr(error,reason))fprintf(stderr,"wanted %s, got %d %s\n",reason,rc,error);}
    else {if(rc)fprintf(stderr,"submit: %s\n",error);CHECK(rc==0);}++scenarios;count=0;return rc;
}
/* The first three entries are explicit right-angle, horizontal and vertical
   neighbours. Each permutation's reference is this fixed quad, never a copy of
   the production corner-selection algorithm. Reverse permutations reverse its winding. */
static void compare(rt_executor *x,uint8_t *pool,unsigned permutation,unsigned indexed,unsigned profile,int sy)
{
    static const unsigned permutations[6][3]={{0,1,2},{1,2,0},{2,0,1},{0,2,1},{2,1,0},{1,0,2}};
    const float quad[4][4]={{-0.75f,-0.75f,0.25f,1},{0.75f,-0.75f,0.25f,1},{-0.75f,0.75f,0.25f,1},{0.75f,0.75f,0.25f,1}};
    float pos[4][4],colors[4][4]={{0.125f,0.25f,0.25f,1},{0.375f,0.25f,0.25f,1},{0.125f,0.5f,0.25f,1},{0.375f,0.5f,0.25f,1}};
    memcpy(pos,quad,sizeof pos);
    if(profile==4){pos[0][3]=1;pos[1][3]=1.25f;pos[2][3]=1.5f;pos[3][3]=1.75f;
        pos[0][2]=0.125f;pos[1][2]=0.25f;pos[2][2]=0.375f;pos[3][2]=0.5f;}
    setup(x,pool,profile,sy,17);
    unsigned ids[3]={0,1,2};if(indexed==1){ids[0]=2;ids[1]=0;ids[2]=1;}if(indexed==2){ids[0]=7;ids[1]=9;ids[2]=11;}
    memset(pool+POS,0xa5,VERTICES*16+64);memset(pool+COL,0xa5,VERTICES*16+64);
    for(unsigned i=0;i<3;++i){unsigned k=permutations[permutation][i];memcpy(pool+POS+ids[i]*16,pos[k],16);memcpy(pool+COL+ids[i]*16,colors[k],16);
        if(indexed==1){uint16_t id=(uint16_t)ids[i];memcpy(pool+INDEX+i*2,&id,2);}else memcpy(pool+INDEX+i*4,&ids[i],4);}
    uint8_t source_guard[2][VERTICES*16+64];memcpy(source_guard[0],pool+POS,sizeof source_guard[0]);memcpy(source_guard[1],pool+COL,sizeof source_guard[1]);
    /* Split setup and draw exercise retained state across command buffers. */
    char error[512]="";CHECK(rt_executor_run_calls(x,calls,count,error,sizeof error)==0);count=0;
    if(submit(x,3,indexed,NULL))return;
    uint8_t reference[W*H*4+64],z[W*H*4+64],stencil[W*H+64];
    memcpy(reference,pool+TARGET,sizeof reference);memcpy(z,pool+Z,sizeof z);memcpy(stencil,pool+S,sizeof stencil);
    for(unsigned i=0;i<sizeof source_guard[0];++i){CHECK(pool[POS+i]==source_guard[0][i]);CHECK(pool[COL+i]==source_guard[1][i]);}
    for(unsigned i=0;i<VERTICES;++i){float got;memcpy(&got,pool+COUNTER+i*4,4);int active=i==ids[0]||i==ids[1]||i==ids[2];CHECK(got==(profile==3&&active?1.0f:0.0f));}
    for(unsigned i=VERTICES*4;i<VERTICES*4+64;++i)CHECK(pool[COUNTER+i]==0);
    setup(x,pool,profile==3?0:profile,sy,4);
    const unsigned forward[6]={0,1,2,2,1,3},reverse[6]={0,2,1,1,2,3};const unsigned *order=permutation<3?forward:reverse;
    for(unsigned i=0;i<6;++i){memcpy(pool+POS+i*16,pos[order[i]],16);memcpy(pool+COL+i*16,colors[order[i]],16);}
    if(submit(x,6,0,NULL))return;
    unsigned colored=0;for(unsigned i=0;i<W*H*4;++i){int delta=(int)pool[TARGET+i]-reference[i];CHECK(delta>=-1&&delta<=1);if(reference[i]!=0x55)++colored;}
    for(unsigned i=0;i<64;++i){CHECK(reference[W*H*4+i]==0x55);CHECK(pool[TARGET+W*H*4+i]==0x55);
        CHECK(z[W*H*4+i]==0xa5);CHECK(pool[Z+W*H*4+i]==0xa5);CHECK(stencil[W*H+i]==0xa5);CHECK(pool[S+W*H+i]==0xa5);}
    for(unsigned i=VERTICES*4;i<VERTICES*4+64;++i)CHECK(pool[COUNTER+i]==0);
    CHECK(colored>0);
    for(unsigned i=0;i<sizeof z;++i)CHECK(pool[Z+i]==z[i]);for(unsigned i=0;i<sizeof stencil;++i)CHECK(pool[S+i]==stencil[i]);
}
/* Native cache reuse, offset-index dispatch, multiple primitive groups and the
   ordinary/capture shader variants all share one executor across these cases. */
static void multiple(rt_executor *x,uint8_t *pool,unsigned width)
{
    const float positions[8][4]={{-.875f,-.75f,.25f,1},{-.125f,-.75f,.25f,1},{-.875f,.25f,.25f,1},{-.125f,.25f,.25f,1},
        {.125f,-.25f,.25f,1},{.875f,-.25f,.25f,1},{.125f,.75f,.25f,1},{.875f,.75f,.25f,1}};
    const unsigned source[6]={0,1,2,4,5,6},reference_order[12]={0,1,2,2,1,3,4,5,6,6,5,7};
    setup(x,pool,0,1,17);memset(pool+INDEX,0xa5,64);
    for(unsigned i=0;i<6;++i){memcpy(pool+POS+i*16,positions[source[i]],16);const float c[4]={1,i<3?.25f:.75f,.125f,1};memcpy(pool+COL+i*16,c,16);
        memcpy(pool+INDEX+(i+2)*width,&i,width);}
    rt_render_call *c=event(RT_CALL_INDEX_TYPE);c->u.index_type.type=width==2?0:1;
    c=event(RT_CALL_INDEX_BASE);c->u.index_base.address=base+INDEX;
    c=event(RT_CALL_DRAW_INDEX_OFFSET);c->u.draw_index.first=2;c->u.draw_index.count=6;c->u.draw_index.has_packet_fields=1;
    c->u.draw_index.header=0xc0033500;c->u.draw_index.max_size=6;
    char error[512]="";int rc=rt_executor_run_calls(x,calls,count,error,sizeof error);if(rc)fprintf(stderr,"offset: %s\n",error);CHECK(rc==0);++scenarios;
    uint8_t color[W*H*4+64],z[W*H*4+64],stencil[W*H+64];memcpy(color,pool+TARGET,sizeof color);memcpy(z,pool+Z,sizeof z);memcpy(stencil,pool+S,sizeof stencil);
    setup(x,pool,0,1,4);
    for(unsigned i=0;i<12;++i){memcpy(pool+POS+i*16,positions[reference_order[i]],16);const float c[4]={1,i<6?.25f:.75f,.125f,1};memcpy(pool+COL+i*16,c,16);}
    if(submit(x,12,0,NULL))return;
    CHECK(memcmp(color,pool+TARGET,sizeof color)==0);CHECK(memcmp(z,pool+Z,sizeof z)==0);CHECK(memcmp(stencil,pool+S,sizeof stencil)==0);
    for(unsigned i=0;i<64;++i){CHECK(color[W*H*4+i]==0x55);CHECK(pool[TARGET+W*H*4+i]==0x55);CHECK(z[W*H*4+i]==0xa5);
        CHECK(pool[Z+W*H*4+i]==0xa5);CHECK(stencil[W*H+i]==0xa5);CHECK(pool[S+W*H+i]==0xa5);}
}
static void effects(rt_executor *x,uint8_t *pool)
{
    const float pos[3][4]={{-.75f,-.75f,.25f,1},{.75f,-.75f,.25f,1},{-.75f,.75f,.25f,1}};
    const float colors[3][4]={{1,1,1,1},{1,1,1,1},{1,1,1,1}};
    for(unsigned mode=0;mode<4;++mode){
        setup(x,pool,3,1,17);memcpy(pool+POS,pos,sizeof pos);memcpy(pool+COL,colors,sizeof colors);
        if(mode==0||mode==2)reg(0xa08e,0);
        if(mode==1){reg(0xa00c,8|8<<16);reg(0xa00d,8|8<<16);}
        if(mode==2){rt_render_call *c=event(RT_CALL_PIXEL_SHADER);c->u.pixel_shader.code=0;}
        if(mode==3){
            /* A raw counter store precedes an unsupported typed store. A completed
               prepass preserves its effects, reports the stop and skips all raster writes. */
            const unsigned prefix=13;uint32_t *program=(uint32_t *)(pool+VS);
            memmove(program+prefix+2,program+prefix,sizeof counted_vertex-prefix*4);
            program[prefix]=0xe01c2000;program[prefix+1]=0x80020500;
            for(size_t i=0;i<count;++i)if(calls[i].kind==RT_CALL_USER_DATA&&calls[i].u.user_data.slot==8)
                calls[i].u.user_data.words[3]=(4u|5u<<3|6u<<6|7u<<9)|5u<<15;
        }
        submit(x,3,0,mode==3?"shader runtime stop: 4-component buffer store, DATA_FORMAT 5":NULL);
        for(unsigned i=0;i<W*H*4+64;++i)CHECK(pool[TARGET+i]==0x55);
        for(unsigned i=0;i<VERTICES;++i){float value;memcpy(&value,pool+COUNTER+i*4,4);CHECK(value==(i<3?1.0f:0.0f));}
        if(mode==3)for(unsigned i=0;i<W*H;++i){float value;memcpy(&value,pool+Z+i*4,4);CHECK(value==.75f);CHECK(pool[S+i]==0x2a);}
    }
    /* Zero indexed count never resolves the deliberately absent source address. */
    setup(x,pool,3,1,17);submit(x,0,2,NULL);
    for(unsigned i=0;i<VERTICES*4+64;++i)CHECK(pool[COUNTER+i]==0);
}
int main(void)
{
    char path[]="/tmp/paleblood-metal-rectangles-XXXXXX",error[1024]="";int fd=mkstemp(path);CHECK(fd>=0);if(fd<0)return 1;unlink(path);
    CHECK(ftruncate(fd,POOL)==0);uint8_t *pool=mmap(NULL,POOL,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);CHECK(pool!=MAP_FAILED);if(pool==MAP_FAILED)return 1;
    rt_executor *x=NULL;rt_executor_memory memory={fd,POOL,base,NULL};int rc=rt_executor_open(&memory,&x,error,sizeof error);CHECK(rc==0);if(rc){fprintf(stderr,"open %s\n",error);return 1;}
    for(unsigned p=0;p<5;++p)for(unsigned permutation=0;permutation<6;++permutation)for(unsigned indexed=0;indexed<3;++indexed)for(int sy=-1;sy<=1;sy+=2)
        compare(x,pool,permutation,indexed,p,sy);
    multiple(x,pool,2);multiple(x,pool,4);effects(x,pool);
    setup(x,pool,3,1,17);submit(x,0,0,NULL);for(unsigned i=0;i<W*H*4+64;++i)CHECK(pool[TARGET+i]==0x55);
    for(unsigned i=0;i<VERTICES*4+64;++i)CHECK(pool[COUNTER+i]==0);
    const unsigned incomplete[]={1,2,4};for(unsigned i=0;i<3;++i){setup(x,pool,0,1,17);submit(x,incomplete[i],0,"complete triples");}
    setup(x,pool,0,1,17);submit(x,UINT32_MAX,0,"bounded expanded count");
    rt_executor_close(x);munmap(pool,POOL);close(fd);
    printf("metal rectangles: %u scenarios, %u checks, %u failures\n",scenarios,checks,failures);return failures?1:0;
}
