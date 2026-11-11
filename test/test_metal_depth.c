/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Owned GFX7 programs, register layouts and guarded guest planes on a real Metal device. */
#include "runtime/render_executor.h"
#include "runtime/render_calls.h"
#include "runtime/tiling.h"
#include "gcn_programs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

enum { POOL = 8 << 20, VS = 0x1000, PS = 0x2000, POSITIONS = 0x4000, COLORS = 0x5000,
       Z_BASE = 0x100000, S_BASE = 0x200000, COLOR_BASE = 0x300000, W = 16, H = 8, SLICES = 3 };
static const uint64_t base = UINT64_C(0x3000000000);
static unsigned checks, failures, scenarios;
#define CHECK(x) do {++checks; if(!(x)){++failures;if(failures<=20)fprintf(stderr,"line %d: %s\n",__LINE__,#x);}} while(0)
static uint32_t bits(float f){uint32_t v;memcpy(&v,&f,4);return v;}
typedef struct {
    unsigned format, stencil, geometry, width, height, pixels, slice, control, view_flags, render_control;
    unsigned ops, references[2], color, pixel, reverse;
    float sy, z;
    rt_depth_layout layout[2];
    uint8_t *expected[2]; size_t span[2];
} Scene;
static rt_render_call calls[80]; static size_t count; static unsigned vertices=3;
static rt_render_call *event(rt_render_call_kind kind){CHECK(count<80);rt_render_call *c=&calls[count++];memset(c,0,sizeof *c);c->kind=kind;return c;}
static void reg(uint32_t address,uint32_t value){rt_render_call *c=event(RT_CALL_REGISTER);c->u.reg.address=address;c->u.reg.value=value;}
static Scene scene(void)
{
    Scene s={0};s.format=3;s.width=W;s.height=H;s.pixels=W*H;s.control=2|4|7<<4;
    s.color=s.pixel=1;s.sy=4;s.z=0.25f;s.references[0]=s.references[1]=0x01ffff17;return s;
}
static void descriptor(uint32_t *p,uint64_t address){p[0]=(uint32_t)address;p[1]=(uint32_t)(address>>32)|16u<<16;p[2]=3;p[3]=0;}
static void setup(rt_executor *x,uint8_t *pool,Scene *s)
{
    rt_executor_reset(x);count=0;vertices=3;
    float positions[3][4]={{-1,-1,s->z,1},{3,-1,s->z,1},{-1,3,s->z,1}};
    if(s->reverse){float tmp[4];memcpy(tmp,positions[1],sizeof tmp);memcpy(positions[1],positions[2],sizeof tmp);memcpy(positions[2],tmp,sizeof tmp);}
    memcpy(pool+POSITIONS,positions,sizeof positions);
    memset(pool+COLOR_BASE,0x55,W*H*4);
    for(unsigned plane=0;plane<2;++plane){
        if(!(plane?s->stencil:s->format))continue;
        char error[256]="";
        CHECK(rt_tiling_depth_layout(s->geometry,s->format,s->stencil,(s->width/8-1)|((s->height/8-1)<<11),
            s->pixels/64-1,plane?RT_DEPTH_ASPECT_STENCIL:RT_DEPTH_ASPECT_Z,&s->layout[plane],error,sizeof error)==0);
        s->span[plane]=(size_t)rt_tiling_size(&s->layout[plane].tiles,SLICES)+64;
        uint8_t *raw=pool+(plane?S_BASE:Z_BASE);memset(raw,0xa5,s->span[plane]);
        const rt_tiling *t=&s->layout[plane].tiles;
        uint32_t value=plane?0x2a:s->format==1?0xbfff:bits(0.75f);
        for(unsigned slice=0;slice<SLICES;++slice)for(unsigned y=0;y<s->height;++y)for(unsigned col=0;col<s->width;++col)
            memcpy(raw+rt_tiling_offset(t,col,y,slice),&value,t->element_bytes);
        s->expected[plane]=malloc(s->span[plane]);CHECK(s->expected[plane]!=NULL);if(!s->expected[plane])exit(1);
        memcpy(s->expected[plane],raw,s->span[plane]);
    }
    rt_render_call *c=event(RT_CALL_COLOR_TARGET);c->u.color_target.enabled=s->color;
    c->u.color_target.target=(rt_color_target){.address=base+COLOR_BASE,.pitch=W,.height=H,.format=0xa,.component_swap=0,.tile_mode_index=31,.samples=1};
    c=event(RT_CALL_VIEWPORT);c->u.viewport.zmax=1;c->u.viewport.scale[0]=8;c->u.viewport.scale[1]=s->sy;c->u.viewport.scale[2]=1;
    c->u.viewport.translate[0]=8;c->u.viewport.translate[1]=4;
    c=event(RT_CALL_VERTEX_SHADER);c->u.vertex_shader=(rt_vertex_shader){.code=base+VS,.vgprs=12,.sgprs=16,.user_sgprs=8,.parameter_count=1,.position_format=4};
    c=event(RT_CALL_PIXEL_SHADER);c->u.pixel_shader=(rt_pixel_shader){.code=s->pixel?base+PS:0,.vgprs=12,.sgprs=16,
        .color_formats=4,.input_enable=2,.input_address=2,.interpolated=1,.color_mask=15};
    c=event(RT_CALL_USER_DATA);c->u.user_data.stage=RT_STAGE_VERTEX;c->u.user_data.count=8;
    descriptor(c->u.user_data.words,base+POSITIONS);descriptor(c->u.user_data.words+4,base+COLORS);
    reg(0xa00f,s->geometry);reg(0xa010,s->format);reg(0xa011,s->stencil);
    reg(0xa012,(uint32_t)((base+Z_BASE)>>8));reg(0xa013,(uint32_t)((base+S_BASE)>>8));
    reg(0xa014,(uint32_t)((base+Z_BASE)>>8));reg(0xa015,(uint32_t)((base+S_BASE)>>8));
    reg(0xa016,(s->width/8-1)|((s->height/8-1)<<11));reg(0xa017,s->pixels/64-1);
    reg(0xa002,s->slice|(s->slice<<13)|s->view_flags);reg(0xa000,s->render_control);
    reg(0xa200,s->control);reg(0xa10b,s->ops);reg(0xa10c,s->references[0]);reg(0xa10d,s->references[1]);
    if(!s->pixel)reg(0xa08e,0);
}
static int submit(rt_executor *x,const char *reason)
{
    char error[1024]="";rt_render_call *c=event(RT_CALL_DRAW_AUTO);c->u.draw_auto.index_count=vertices;
    const int rc=rt_executor_run_calls(x,calls,count,error,sizeof error);
    if(reason){if(rc!=-1||!strstr(error,reason))fprintf(stderr,"wanted '%s', got %d: %s\n",reason,rc,error);CHECK(rc==-1&&strstr(error,reason));}
    else {if(rc)fprintf(stderr,"draw failed: %s\n",error);CHECK(rc==0);}
    ++scenarios;return rc;
}
static void expected(Scene *s,unsigned plane,uint32_t value,unsigned width,unsigned height)
{
    const rt_tiling *t=&s->layout[plane].tiles;
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x)
        memcpy(s->expected[plane]+rt_tiling_offset(t,x,y,s->slice),&value,t->element_bytes);
}
static void finish(uint8_t *pool,Scene *s)
{
    for(unsigned plane=0;plane<2;++plane)if(s->expected[plane]){
        const uint8_t *raw=pool+(plane?S_BASE:Z_BASE);
        for(size_t i=0;i<s->span[plane];++i)CHECK(raw[i]==s->expected[plane][i]);
        free(s->expected[plane]);s->expected[plane]=NULL;
    }
}
static void check_color(const uint8_t *pool,int written)
{
    for(unsigned i=0;i<W*H*4;++i)CHECK(pool[COLOR_BASE+i]==(written?255:0x55));
}
/* Two overlapping triangles distinguish native write suppression from merely skipping
   CPU writeback: a wrong first update makes the second triangle fail its test. */
static void readonly_overlap(rt_executor *x,uint8_t *pool,int stencil)
{
    Scene s=scene();s.view_flags=stencil?1u<<25:1u<<24;s.control=stencil?(1|2<<8):(2|4|1<<4);
    if(stencil){s.format=0;s.stencil=1;s.ops=5<<4;s.references[0]=0x01ffff2a;}
    setup(x,pool,&s);vertices=6;
    memcpy(pool+POSITIONS+48,pool+POSITIONS,48);
    if(!stencil)for(unsigned i=3;i<6;++i){float z=0.5f;memcpy(pool+POSITIONS+i*16+8,&z,4);}
    const float colors[6][4]={{1,1,1,1},{1,1,1,1},{1,1,1,1},{0,0,0,1},{0,0,0,1},{0,0,0,1}};
    memcpy(pool+COLORS,colors,sizeof colors);
    for(size_t i=0;i<count;++i)if(calls[i].kind==RT_CALL_USER_DATA){calls[i].u.user_data.words[2]=6;calls[i].u.user_data.words[6]=6;}
    if(!submit(x,NULL))for(unsigned i=0;i<W*H*4;++i)CHECK(pool[COLOR_BASE+i]==(i%4==3?255:0));
    finish(pool,&s);const float white[3][4]={{1,1,1,1},{1,1,1,1},{1,1,1,1}};memcpy(pool+COLORS,white,sizeof white);
}
static void depth_cases(rt_executor *x,uint8_t *pool)
{
    const unsigned formats[]={1,3};
    for(unsigned f=0;f<2;++f)for(unsigned mode=0;mode<3;++mode){
        Scene s=scene();s.format=formats[f];s.geometry=mode<<4;s.pixels=192;s.slice=1;
        setup(x,pool,&s);if(!submit(x,NULL)){expected(&s,0,s.format==1?0x4000:bits(0.25f),W,H);check_color(pool,1);}finish(pool,&s);
    }
    /* Direct macro geometry, split depth and padded slice; only the color extent changes. */
    Scene s=scene();s.geometry=0x001d0c40;s.width=256;s.pixels=40960;s.slice=2;
    setup(x,pool,&s);if(!submit(x,NULL))expected(&s,0,bits(0.25f),W,H);finish(pool,&s);
    const unsigned truth[]={0,1,0,1,0,1,0,1};
    for(unsigned compare=0;compare<8;++compare){
        s=scene();s.control=2|4|compare<<4;setup(x,pool,&s);
        if(!submit(x,NULL)){if(truth[compare])expected(&s,0,bits(0.25f),W,H);check_color(pool,(int)truth[compare]);}finish(pool,&s);
    }
    for(unsigned compare=2;compare<=3;++compare){
        s=scene();s.z=0.75f;s.control=2|4|compare<<4;setup(x,pool,&s);
        if(!submit(x,NULL))check_color(pool,1);finish(pool,&s);
    }
    /* Scissoring changes only covered depth texels, with full plane guards checked. */
    s=scene();setup(x,pool,&s);reg(0xa00c,2u<<16|3u);reg(0xa00d,6u<<16|11u);
    if(!submit(x,NULL)){uint32_t value=bits(0.25f);for(unsigned y=2;y<6;++y)for(unsigned col=3;col<11;++col)
        memcpy(s.expected[0]+(y*W+col)*4,&value,4);}finish(pool,&s);
    readonly_overlap(x,pool,0);
    /* Canonical guest bytes are reloaded on a subsequent retained-state draw. */
    s=scene();s.control=2|4|1<<4;setup(x,pool,&s);
    if(!submit(x,NULL))expected(&s,0,bits(0.25f),W,H);
    uint32_t near=bits(0.125f);for(unsigned y=0;y<H;++y)for(unsigned col=0;col<W;++col){memcpy(pool+Z_BASE+(y*W+col)*4,&near,4);memcpy(s.expected[0]+(y*W+col)*4,&near,4);}
    memset(pool+COLOR_BASE,0x55,W*H*4);count=0;if(!submit(x,NULL))check_color(pool,0);finish(pool,&s);
    /* Read-only view tests depth without publishing a converted plane. */
    s=scene();s.view_flags=1u<<24;setup(x,pool,&s);submit(x,NULL);finish(pool,&s);
    /* No pixel shader: with and without a masked color attachment. */
    for(unsigned color=0;color<2;++color){s=scene();s.pixel=0;s.color=color;setup(x,pool,&s);if(!submit(x,NULL)){expected(&s,0,bits(0.25f),W,H);check_color(pool,0);}finish(pool,&s);}
    /* Explicit zero format and Z_WRITE without Z_ENABLE disable depth work. */
    s=scene();s.format=0;setup(x,pool,&s);if(!submit(x,NULL))check_color(pool,1);finish(pool,&s);
    s=scene();s.control=4;setup(x,pool,&s);if(!submit(x,NULL))check_color(pool,1);finish(pool,&s);
    s=scene();setup(x,pool,&s);reg(0xa202,0);if(!submit(x,NULL)){expected(&s,0,bits(0.25f),W,H);check_color(pool,0);}finish(pool,&s);
    /* A previous depth-rejecting state must not leak into a disabled draw. */
    s=scene();s.control=2;setup(x,pool,&s);submit(x,NULL);count=0;reg(0xa200,0);if(!submit(x,NULL))check_color(pool,1);finish(pool,&s);
}
static void stencil_cases(rt_executor *x,uint8_t *pool)
{
    const unsigned formats[]={0,1,3};
    for(unsigned f=0;f<3;++f){
        Scene s=scene();s.format=formats[f];s.stencil=1;s.control=1|7<<8;s.ops=3<<4;
        setup(x,pool,&s);if(!submit(x,NULL)){expected(&s,1,0x17,W,H);check_color(pool,1);}finish(pool,&s);
    }
    const unsigned truth[]={0,1,0,1,0,1,0,1};
    for(unsigned compare=0;compare<8;++compare){Scene test=scene();test.format=0;test.stencil=1;test.control=1|compare<<8;
        setup(x,pool,&test);if(!submit(x,NULL))check_color(pool,(int)truth[compare]);finish(pool,&test);}
    Scene disabled=scene();disabled.format=0;disabled.stencil=1;disabled.control=1;
    setup(x,pool,&disabled);submit(x,NULL);count=0;reg(0xa200,0);if(!submit(x,NULL))check_color(pool,1);finish(pool,&disabled);
    /* Failure, pass and depth-failure select their distinct stencil operations. */
    for(unsigned path=0;path<3;++path){
        Scene s=scene();s.stencil=1;s.control=1|(path?7u:0u)<<8;
        if(path==2)s.control|=2; /* depth NEVER */
        s.ops=path==0?3:path==1?3<<4:3<<8;
        setup(x,pool,&s);if(!submit(x,NULL)){expected(&s,1,0x17,W,H);check_color(pool,path==1);}finish(pool,&s);
    }
    const unsigned ops[]={0,1,3,4,5,6,7,8,9};
    const unsigned values[]={0x2a,0,0x17,1,0x2b,0x29,0xd5,0x2b,0x29};
    for(unsigned k=0;k<sizeof ops/sizeof *ops;++k){
        Scene s=scene();s.format=0;s.stencil=1;s.control=1|7<<8;s.ops=ops[k]<<4;
        setup(x,pool,&s);if(!submit(x,NULL))expected(&s,1,values[k],W,H);finish(pool,&s);
    }
    /* Clamp and wrap differ at both eight-bit boundaries. */
    for(unsigned k=0;k<4;++k){Scene edge=scene();edge.format=0;edge.stencil=1;edge.control=1|7<<8;
        const unsigned edge_ops[]={5,8,6,9},initial[]={255,255,0,0},result[]={255,0,0,255};
        edge.ops=edge_ops[k]<<4;setup(x,pool,&edge);memset(pool+S_BASE,initial[k],W*H);memset(edge.expected[1],initial[k],W*H);
        if(!submit(x,NULL))expected(&edge,1,result[k],W,H);finish(pool,&edge);
    }
    readonly_overlap(x,pool,1);
    /* Compare masks, write masks, view read-only and disabled back-face source. */
    Scene s=scene();s.format=0;s.stencil=1;s.control=1|2<<8;s.ops=3<<4;s.references[0]=0x010f0f1a;s.references[1]=0;
    setup(x,pool,&s);if(!submit(x,NULL)){expected(&s,1,0x2a,W,H);check_color(pool,1);}finish(pool,&s);
    s=scene();s.format=0;s.stencil=1;s.control=1|7<<8;s.ops=3<<4;s.references[0]=0x010f0017;
    setup(x,pool,&s);if(!submit(x,NULL))expected(&s,1,0x27,W,H);finish(pool,&s);
    s=scene();s.format=0;s.stencil=1;s.control=1|7<<8;s.ops=3<<4;s.view_flags=1u<<25;
    setup(x,pool,&s);submit(x,NULL);finish(pool,&s);
    /* Vulkan uses negative shoelace area in framebuffer coordinates for its default
       CCW front face. The positive-Y source triangle therefore selects the back face.
       Reversing either its order or viewport Y selects the front face. */
    for(unsigned reverse=0;reverse<2;++reverse)for(unsigned negative=0;negative<2;++negative){
        s=scene();s.format=0;s.stencil=1;s.control=1|0x80|7<<8|7<<20;s.ops=3<<4|3<<16;
        s.references[1]=0x01ffff69;s.reverse=reverse;s.sy=negative?-4:4;
        setup(x,pool,&s);if(!submit(x,NULL))expected(&s,1,(reverse^negative)?0x17:0x69,W,H);finish(pool,&s);
    }
    /* Largest split safe for every documented GFX7 DRAM row size (1KiB). */
    s=scene();s.format=3|4u<<13;s.stencil=1|4u<<13;s.geometry=0x001d0c40;s.width=256;s.pixels=32768;s.slice=1;
    s.control|=1|7<<8;s.ops=3<<4;
    setup(x,pool,&s);if(!submit(x,NULL)){expected(&s,0,bits(0.25f),W,H);expected(&s,1,0x17,W,H);}finish(pool,&s);
    /* Combined D32/S8 plane blits preserve both aspect padding and other slices. */
    s=scene();s.stencil=1;s.geometry=0x001d0c40;s.width=256;s.pixels=65536;s.slice=2;
    s.control|=1|7<<8;s.ops=3<<4;
    setup(x,pool,&s);if(!submit(x,NULL)){expected(&s,0,bits(0.25f),W,H);expected(&s,1,0x17,W,H);}finish(pool,&s);
    s=scene();s.format=0;s.stencil=1;s.control=1|7<<8;s.ops=3<<4;s.color=s.pixel=0;
    setup(x,pool,&s);if(!submit(x,NULL))expected(&s,1,0x17,W,H);finish(pool,&s);
}
static void refusals(rt_executor *x,uint8_t *pool)
{
    const struct {uint32_t address,value;const char *reason;} bad[]={
        {0xa010,3|1<<29,"HTILE"},{0xa010,3|1<<27,"expanded depth"},{0xa000,1,"clears"},
        {0xa002,1<<13,"one slice"},{0xa012,0,"outside"},{0xa014,0,"distinct"},
        {0xa016,0,"smaller"},{0xa017,0,"slice stride"},{0xa010,3|1<<2,"multisampled"},
        {0xa00f,0x001d0a40,"pipe configuration"},{0xa200,2|8,"bounds"},{0xa200,8,"bounds"},{0xa200,0x80000000u,"DB_DEPTH_CONTROL fields"},
    };
    for(unsigned i=0;i<sizeof bad/sizeof *bad;++i){Scene s=scene();setup(x,pool,&s);reg(bad[i].address,bad[i].value);if(bad[i].address==0xa012)reg(0xa014,0);submit(x,bad[i].reason);finish(pool,&s);}
    Scene s=scene();s.stencil=1;s.control=1|8;s.ops=0;setup(x,pool,&s);submit(x,"bounds");finish(pool,&s);
    s=scene();s.format=0;setup(x,pool,&s);reg(0xa200,0x80000000u);submit(x,"DB_DEPTH_CONTROL fields");finish(pool,&s);
    s=scene();s.stencil=1;s.control=1|7<<8;s.ops=2<<4;setup(x,pool,&s);submit(x,"operation 2");finish(pool,&s);
    s=scene();s.stencil=1;s.control=1|7<<8;s.ops=5<<4;s.references[0]=0x02ffff17;setup(x,pool,&s);submit(x,"STENCILOPVAL 1");finish(pool,&s);
    s=scene();s.stencil=1;s.control=1|2<<8;s.ops=4<<4;setup(x,pool,&s);submit(x,"conflicts");finish(pool,&s);
    s=scene();setup(x,pool,&s);reg(0xa011,1|1<<27);reg(0xa200,1);submit(x,"expanded stencil");finish(pool,&s);
    s=scene();setup(x,pool,&s);reg(0xa200,1|7<<8);reg(0xa011,1);reg(0xa013,(uint32_t)((base+Z_BASE)>>8));reg(0xa015,(uint32_t)((base+Z_BASE)>>8));reg(0xa200,3|7<<8|7<<4);submit(x,"overlapping");finish(pool,&s);
    s=scene();s.control=0;s.render_control=1;setup(x,pool,&s);submit(x,"DB_RENDER_CONTROL");finish(pool,&s);
    s=scene();s.format=0;s.render_control=4;setup(x,pool,&s);submit(x,"DB_RENDER_CONTROL");finish(pool,&s);
    s=scene();setup(x,pool,&s);reg(0xa00f,1u<<31);submit(x,"surface fields");finish(pool,&s);
    for(unsigned plane=0;plane<2;++plane)for(unsigned split=5;split<=6;++split){
        s=scene();s.stencil=1;s.geometry=0x001d0c40;s.width=256;s.pixels=32768;s.control|=1|7<<8;s.ops=3<<4;
        if(plane)s.stencil|=split<<13;else s.format|=split<<13;
        setup(x,pool,&s);submit(x,"DRAM row profile");finish(pool,&s);
    }
    /* Deleting each required register must refuse before any guest write. */
    const struct {unsigned address;const char *reason;} missing[]={
        {0xa00f,"geometry"},{0xa010,"explicit depth target"},{0xa011,"explicit depth target"},
        {0xa016,"geometry"},{0xa017,"geometry"},{0xa002,"geometry"},
        {0xa012,"base"},{0xa014,"base"},{0xa013,"base"},{0xa015,"base"},
        {0xa10b,"DB_STENCIL_CONTROL"},{0xa10c,"reference"},{0xa10d,"reference"},
    };
    for(unsigned k=0;k<sizeof missing/sizeof *missing;++k){s=scene();s.stencil=1;s.control|=1|0x80|7<<8|7<<20;s.ops=3<<4|3<<16;
        setup(x,pool,&s);for(size_t i=0;i<count;++i)if(calls[i].kind==RT_CALL_REGISTER&&calls[i].u.reg.address==missing[k].address){
            calls[i].u.reg.address=0xa000;calls[i].u.reg.value=0;
        }
        submit(x,missing[k].reason);finish(pool,&s);
    }
}
int main(void)
{
    char path[]="/tmp/paleblood-metal-depth-XXXXXX",error[512]="";int fd=mkstemp(path);CHECK(fd>=0);if(fd<0)return 1;unlink(path);
    CHECK(ftruncate(fd,POOL)==0);uint8_t *pool=mmap(NULL,POOL,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);CHECK(pool!=MAP_FAILED);if(pool==MAP_FAILED)return 1;
    memcpy(pool+VS,vertex_position_color,sizeof vertex_position_color);memcpy(pool+PS,pixel_color,sizeof pixel_color);
    const float colors[3][4]={{1,1,1,1},{1,1,1,1},{1,1,1,1}};memcpy(pool+COLORS,colors,sizeof colors);
    rt_executor *x=NULL;const rt_executor_memory memory={fd,POOL,base,NULL};
    int rc=rt_executor_open(&memory,&x,error,sizeof error);if(rc)fprintf(stderr,"open: %s\n",error);CHECK(rc==0);if(rc)return 1;
    depth_cases(x,pool);stencil_cases(x,pool);refusals(x,pool);
    rt_executor_close(x);munmap(pool,POOL);close(fd);
    printf("metal depth: %u scenarios, %u checks, %u failures\n",scenarios,checks,failures);return failures?1:0;
}
