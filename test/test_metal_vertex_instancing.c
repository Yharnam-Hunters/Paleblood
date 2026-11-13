/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Owned GFX7 inputs: literal tile pixels and disjoint (instance,vertex) quotient records.
   No original shader, private executor getter or software rasterizer is used. */
#include "runtime/render_executor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

enum { POOL = 64 << 10, VS = 0x1000, PS = 0x2000, POSITIONS = 0x3000, COLORS = 0x5000,
       INDEX16 = 0x6000, INDEX32 = 0x6100, TUPLES = 0x8000, TARGET = 0xc000,
       WIDTH = 32, HEIGHT = 8, PIXELS = WIDTH * HEIGHT, TILES = 8, VERTICES = 6,
       RECORDS = TILES * VERTICES, INSTANCES = 7, CALLS = 48,
       STEP0 = 0xa2a8, STEP1 = 0xa2a9 };
static const uint64_t base = UINT64_C(0x3000000000);
static unsigned primitive=4;
/* LLVM Bonaire instruction shapes from gcn_programs.h's VERTEX_INSTANCE_COLOR:
   first consume the enabled input, then ordinary loads overwrite v1..v3. */
#define TILE_PROGRAM(input) { \
    0xd2860009, 0x04010d00 | (input), /* v_mad_u32_u24 v9,v<input>,6,v0 */ \
    0xebf32000, 0x80000109, /* position[v9] -> v1..v4 */ \
    0xebf32000, 0x80010509, /* color[v9] -> v5..v8 */ \
    0xbf8c0f70, 0xf800020f, 0x08070605, 0xf80008cf, 0x04030201, 0xbf810000 }
static const uint32_t stepped0[] = TILE_PROGRAM(1), stepped1[] = TILE_PROGRAM(2), raw_instance[] = TILE_PROGRAM(3);
/* Both quotients and the raw index are copied before any position load. The address
   uses the raw index, giving every physical invocation its own four-word record. */
#define TUPLE_PROGRAM(color_input) { \
    0x7e140301, 0x7e160302, 0x7e180303, 0x7e1a0300, /* v10..v13 = v1,v2,v3,v0 */ \
    0xd2860009, 0x04010d03, /* v9 = 6*v3 + v0 (position and unique tuple slot) */ \
    0xd286000f, 0x04010d00 | (color_input), /* v15 = 6*v<color_input> + v0 */ \
    0x341c1284, /* v_lshlrev_b32 v14,4,v9 */ \
    0xe0781000, 0x80020a0e, /* buffer_store_dwordx4 v[10:13],v14,s[8:11],0 offen */ \
    0xebf32000, 0x80000109, 0xebf32000, 0x8001050f, 0xbf8c0f70, \
    0xf800020f, 0x08070605, 0xf80008cf, 0x04030201, 0xbf810000 }
static const uint32_t tuple_color0[] = TUPLE_PROGRAM(1), tuple_color1[] = TUPLE_PROGRAM(2);
static const uint32_t overwrite_inputs[] = {
    0xebf32000,0x80000100, 0xebf32000,0x80010500, 0xbf8c0f70,
    0xf800020f,0x08070605, 0xf80008cf,0x04030201, 0xbf810000
};
/* Owned pixel_color from gcn_programs.h, interpolated literal zero/one colors. */
static const uint32_t pixel[] = {
    0xbefc0300,0xc8080000,0xc8090001,0xc80c0100,0xc80d0101,
    0xc8100200,0xc8110201,0xc8140300,0xc8150301,
    0x5e0c0702,0x5e0e0b04,0xf8001c0f,0x00000706,0xbf810000
};
static const float palette[TILES][4] = {
    {1,0,0,1},{0,1,0,1},{0,0,1,1},{1,1,0,1},
    {1,0,1,1},{0,1,1,1},{1,1,1,1},{0,0,0,1}
};
static const uint32_t rgba[TILES] = {0xff0000ff,0xff00ff00,0xffff0000,0xff00ffff,0xffff00ff,0xffffff00,0xffffffff,0xff000000};
static const uint32_t bgra[TILES] = {0xffff0000,0xff00ff00,0xff0000ff,0xffffff00,0xffff00ff,0xff00ffff,0xffffffff,0xff000000};
/* Independent literal quotient tables, not a second division implementation. */
static const unsigned quotient2[INSTANCES] = {0,0,1,1,2,2,3};
static const unsigned quotient3[INSTANCES] = {0,0,0,1,1,1,2};
static const unsigned quotient_high[INSTANCES] = {0,0,0,0,0,0,0};
static unsigned checks, failures, cases, data_cases, refusal_cases, pool_scans;
#define CHECK(c) do { ++checks; if (!(c)) { ++failures; if(failures<=32) \
    fprintf(stderr,"line%d: %s\n",__LINE__,#c); } } while(0)
typedef enum { AUTO, DIRECT16, DIRECT32, OFFSET16, OFFSET32, LOGICAL16, LOGICAL32 } Route;
static const char *route_names[] = {"auto","direct16","direct32","offset16","offset32","logical16","logical32"};
typedef enum { RUN, SUBMIT, RECORD } Method;
typedef struct { rt_render_call calls[CALLS]; size_t count; } Calls;
typedef struct { rt_executor *x; unsigned char *pool; unsigned char expected[POOL]; } Fixture;
static uint64_t at(unsigned n) { return base+n; }
static rt_render_call *event(Calls *c,rt_render_call_kind kind)
{
    if(c->count==CALLS){fputs("owned call overflow\n",stderr);exit(1);}
    rt_render_call *r=&c->calls[c->count++];memset(r,0,sizeof *r);r->kind=kind;return r;
}
static void reg(Calls *c,unsigned address,uint32_t value)
{ rt_render_call *r=event(c,RT_CALL_REGISTER);r->u.reg.address=address;r->u.reg.value=value; }
static void instances(Calls *c,uint32_t count) { event(c,RT_CALL_INSTANCES)->u.instances.count=count; }
static void descriptor(uint32_t *words,unsigned offset)
{
    const uint64_t address=at(offset);words[0]=(uint32_t)address;
    words[1]=(uint32_t)(address>>32)|16u<<16;words[2]=RECORDS;words[3]=0;
}
static void setup(Calls *c,unsigned profile,unsigned program,unsigned allocation)
{
    rt_render_call *r=event(c,RT_CALL_COLOR_TARGET);r->u.color_target.enabled=1;
    r->u.color_target.target=(rt_color_target){.address=at(TARGET),.pitch=WIDTH,.height=HEIGHT,
        .format=0xa,.tile_mode_index=31,.samples=1};
    r=event(c,RT_CALL_VIEWPORT);r->u.viewport.zmax=1;
    r->u.viewport.scale[0]=r->u.viewport.translate[0]=WIDTH/2;
    r->u.viewport.scale[1]=r->u.viewport.translate[1]=HEIGHT/2;r->u.viewport.scale[2]=1;
    r=event(c,RT_CALL_VERTEX_SHADER);r->u.vertex_shader=(rt_vertex_shader){.code=at(VS+program*0x100),
        .vgprs=allocation,.sgprs=16,.vgpr_inputs=profile,.user_sgprs=12,.parameter_count=1,.position_format=4};
    r=event(c,RT_CALL_PIXEL_SHADER);r->u.pixel_shader=(rt_pixel_shader){.code=at(PS),.vgprs=12,.sgprs=8,
        .color_formats=4,.input_enable=2,.input_address=2,.interpolated=1,.color_mask=15};
    r=event(c,RT_CALL_USER_DATA);r->u.user_data.stage=RT_STAGE_VERTEX;r->u.user_data.count=8;
    descriptor(r->u.user_data.words,POSITIONS);descriptor(r->u.user_data.words+4,COLORS);
    r=event(c,RT_CALL_USER_DATA);r->u.user_data.stage=RT_STAGE_VERTEX;r->u.user_data.slot=8;r->u.user_data.count=4;
    descriptor(r->u.user_data.words,TUPLES);
    reg(c,0xa202,0xcc0010);reg(c,0xa08e,15);reg(c,0xc242,primitive);
}
static void draw(Calls *c,Route route)
{
    rt_render_call *r;
    if(route==AUTO){event(c,RT_CALL_DRAW_AUTO)->u.draw_auto.index_count=VERTICES;return;}
    const int wide=route==DIRECT32 || route==OFFSET32 || route==LOGICAL32;
    const unsigned width=wide?4:2,offset=wide?INDEX32:INDEX16;
    event(c,RT_CALL_INDEX_TYPE)->u.index_type.type=wide?1:0;
    if(route==DIRECT16 || route==DIRECT32){
        r=event(c,RT_CALL_DRAW_INDEXED);r->u.draw_indexed.address=at(offset)+width;
        r->u.draw_indexed.header=0xc0042700;r->u.draw_indexed.max_size=VERTICES;r->u.draw_indexed.index_count=VERTICES;
    }else{
        event(c,RT_CALL_INDEX_BASE)->u.index_base.address=at(offset);
        event(c,RT_CALL_INDEX_SIZE)->u.index_size.count=VERTICES+1;
        r=event(c,RT_CALL_DRAW_INDEX_OFFSET);r->u.draw_index.first=1;r->u.draw_index.count=VERTICES;
        if(route==OFFSET16 || route==OFFSET32){r->u.draw_index.has_packet_fields=1;r->u.draw_index.header=0xc0033500;r->u.draw_index.max_size=VERTICES;}
    }
}
static void prepare(Fixture *f)
{
    memset(f->pool+TARGET,0x55,PIXELS*4);memset(f->pool+TUPLES,0x6d,RECORDS*16);
    memcpy(f->expected,f->pool,POOL);
}
static unsigned scan(Fixture *f)
{
    unsigned bad=0;for(unsigned i=0;i<POOL;++i)bad+=f->pool[i]!=f->expected[i];
    CHECK(bad==0);++pool_scans;return bad;
}
static int invoke(Fixture *f,const char *id,Calls *c,Method method,const char *reason)
{
    char error[512]="";uint64_t ticket=UINT64_MAX;int rc;
    if(method==SUBMIT)rc=rt_executor_submit_calls(f->x,c->calls,c->count,&ticket,error,sizeof error);
    else if(method==RECORD)rc=rt_executor_record_calls(f->x,c->calls,c->count,&ticket,error,sizeof error);
    else rc=rt_executor_run_calls(f->x,c->calls,c->count,error,sizeof error);
    if(!rc && method!=RUN){
        CHECK(ticket!=0 && ticket!=UINT64_MAX);
        if(method==RECORD)CHECK(rt_executor_submit(f->x,error,sizeof error)==0);
        CHECK(rt_executor_wait_ticket(f->x,ticket,error,sizeof error)==0);
    }
    const int named=reason && strstr(error,reason)!=NULL;
    CHECK(reason ? rc==-1 && named : rc==0);
    printf("vertex-instancing API id=%s method=%u actual=%d expected=%d named=%d error=%s\n",id,(unsigned)method,rc,reason?-1:0,named,error);
    return rc;
}
static void success(Fixture *f,const char *id,Calls *c,Route route,Method method,unsigned mask,
                    const unsigned *q0,const unsigned *q1,int tuple_color)
{
    prepare(f);draw(c,route);const int rc=invoke(f,id,c,method,NULL);
    uint32_t read[PIXELS];memset(read,0,sizeof read);char error[256]="";
    const int read_rc=rt_executor_read_target(f->x,at(TARGET),read,WIDTH,HEIGHT,error,sizeof error);CHECK(read_rc==0);
    unsigned raw_bad=0,read_bad=0,tuple_bad=0;
    for(unsigned y=0;y<HEIGHT;++y)for(unsigned x=0;x<WIDTH;++x){
        const unsigned tile=x/4,visible=(mask>>tile)&1;
        const unsigned color=tuple_color==1?q0[tile<INSTANCES?tile:0]:tuple_color==2?q1[tile<INSTANCES?tile:0]:tile;
        const uint32_t want=visible?rgba[color]:0x55555555,want_read=visible?bgra[color]:0x55555555;
        uint32_t actual;memcpy(&actual,f->pool+TARGET+(y*WIDTH+x)*4,4);
        raw_bad+=actual!=want;read_bad+=read[y*WIDTH+x]!=want_read;
        memcpy(f->expected+TARGET+(y*WIDTH+x)*4,&want,4);
    }
    if(tuple_color)for(unsigned instance=0;instance<INSTANCES;++instance)for(unsigned vertex=0;vertex<VERTICES;++vertex){
        const uint32_t literal[4]={q0[instance],q1[instance],instance,vertex};uint32_t actual[4];
        for(unsigned word=0;word<4;++word){
            const unsigned offset=TUPLES+(instance*VERTICES+vertex)*16+word*4;
            memcpy(&actual[word],f->pool+offset,4);tuple_bad+=actual[word]!=literal[word];memcpy(f->expected+offset,&literal[word],4);
        }
        printf("vertex-instancing TUPLE id=%s instance=%u vertex=%u actual=%08x,%08x,%08x,%08x expected=%08x,%08x,%08x,%08x\n",
            id,instance,vertex,actual[0],actual[1],actual[2],actual[3],literal[0],literal[1],literal[2],literal[3]);
    }
    CHECK(raw_bad==0);CHECK(read_bad==0);CHECK(tuple_bad==0);
    const unsigned pool_bad=scan(f);
    printf("vertex-instancing DATA id=%s route=%u api=%d read=%d mask=%02x pixels=%u raw_bad=%u read_bad=%u tuple_words=%u tuple_bad=%u pool_bytes=%u pool_bad=%u\n",
        id,(unsigned)route,rc,read_rc,mask,PIXELS,raw_bad,read_bad,tuple_color?INSTANCES*VERTICES*4:0,tuple_bad,POOL,pool_bad);
    ++data_cases;++cases;
}
static void refuse(Fixture *f,const char *id,Calls *c,const char *reason)
{
    prepare(f);draw(c,AUTO);const int rc=invoke(f,id,c,RUN,reason);const unsigned bad=scan(f);
    printf("vertex-instancing REFUSAL id=%s api=%d pool_bytes=%u pool_bad=%u\n",id,rc,POOL,bad);++refusal_cases;++cases;
}
static void configure(Calls *c,unsigned profile,unsigned program)
{ setup(c,profile,program,16);instances(c,INSTANCES); }
static void rates(Calls *c,uint32_t a,uint32_t b) { reg(c,STEP0,a);reg(c,STEP1,b); }
static void routes(Fixture *f)
{
    for(unsigned route=0;route<7;++route)for(unsigned profile=1;profile<=3;++profile){
        char id[80];snprintf(id,sizeof id,"routes/%s/profile%u",route_names[route],profile);
        rt_executor_reset(f->x);Calls c={0};configure(&c,profile,profile);rates(&c,2,3);
        success(f,id,&c,(Route)route,RUN,profile==1?0x0f:profile==2?0x07:0x7f,NULL,NULL,0);
    }
}
static void tuples(Fixture *f)
{
    for(unsigned route=0;route<7;++route){
        char id[80];snprintf(id,sizeof id,"tuples/%s",route_names[route]);
        rt_executor_reset(f->x);Calls c={0};configure(&c,3,4);rates(&c,2,3);
        success(f,id,&c,(Route)route,RUN,0x7f,quotient2,quotient3,1);
    }
    for(unsigned profile=2;profile<=3;++profile)for(unsigned input=1;input<profile;++input){
        char id[80];snprintf(id,sizeof id,"enabled/profile%u/input%u",profile,input);
        rt_executor_reset(f->x);Calls c={0};configure(&c,profile,input);rates(&c,2,3);
        success(f,id,&c,AUTO,RUN,input==1?0x0f:0x07,NULL,NULL,0);
    }
}
static void cache(Fixture *f,unsigned slot)
{
    const uint32_t words[4]={2,3,0x80000002,2};
    const unsigned *literal[4]={quotient2,quotient3,quotient_high,quotient2};
    rt_executor_reset(f->x);
    for(unsigned step=0;step<4;++step){
        char id[80];snprintf(id,sizeof id,"cache%u/%u/%08x",slot,step,words[step]);
        Calls c={0};configure(&c,3,slot?5:4);
        if(step==0)rates(&c,2,2);
        reg(&c,slot?STEP1:STEP0,words[step]);
        success(f,id,&c,AUTO,RUN,0x7f,slot?quotient2:literal[step],slot?literal[step]:quotient2,slot?2:1);
    }
}
static void guards(Fixture *f)
{
    for(unsigned slot=0;slot<2;++slot){
        const char *reason=slot?"VGT_INSTANCE_STEP_RATE_1":"VGT_INSTANCE_STEP_RATE_0";
        char id[80];
        for(unsigned mode=0;mode<3;++mode){
            rt_executor_reset(f->x);Calls c={0};configure(&c,slot+1,slot+1);
            if(mode==1)reg(&c,slot?STEP1:STEP0,0);
            if(mode==2)reg(&c,slot?STEP0:STEP1,2);
            snprintf(id,sizeof id,"guards/rate%u/%s",slot,mode==0?"unwritten":mode==1?"zero":"other-only");refuse(f,id,&c,reason);
        }
        rt_executor_reset(f->x);Calls warm={0};configure(&warm,slot+1,slot+1);rates(&warm,2,2);
        snprintf(id,sizeof id,"guards/rate%u/warm",slot);success(f,id,&warm,AUTO,RUN,0x0f,NULL,NULL,0);
        Calls zero={0};reg(&zero,slot?STEP1:STEP0,0);snprintf(id,sizeof id,"guards/rate%u/zero-after-warm",slot);refuse(f,id,&zero,reason);
        Calls restore={0};reg(&restore,slot?STEP1:STEP0,2);snprintf(id,sizeof id,"guards/rate%u/restore",slot);success(f,id,&restore,AUTO,RUN,0x0f,NULL,NULL,0);
        rt_executor_reset(f->x);Calls reset={0};configure(&reset,slot+1,slot+1);
        snprintf(id,sizeof id,"guards/rate%u/reset-cleared",slot);refuse(f,id,&reset,reason);
    }
    for(unsigned profile=1;profile<=3;++profile){
        char id[80];snprintf(id,sizeof id,"guards/overwrite-before-read/profile%u",profile);
        rt_executor_reset(f->x);Calls c={0};configure(&c,profile,0);success(f,id,&c,AUTO,RUN,1,NULL,NULL,0);
    }
    for(unsigned profile=2;profile<=3;++profile)for(unsigned input=1;input<=2;++input){
        char id[80];snprintf(id,sizeof id,"guards/unread-other/profile%u/input%u",profile,input);
        rt_executor_reset(f->x);Calls c={0};configure(&c,profile,input);reg(&c,input==1?STEP0:STEP1,input==1?2:3);
        success(f,id,&c,AUTO,RUN,input==1?0x0f:0x07,NULL,NULL,0);
    }
    for(unsigned value=0;value<=1;++value){
        rt_executor_reset(f->x);Calls c={0};configure(&c,3,3);reg(&c,0xa2a1,value);
        refuse(f,value?"guards/primitive-id-one":"guards/primitive-id-zero",&c,"does not interpret");
    }
    rt_executor_reset(f->x);Calls invalid={0};configure(&invalid,4,3);refuse(f,"guards/profile4",&invalid,"exceeds VGPR_COMP_CNT");
    rt_executor_reset(f->x);Calls small={0};setup(&small,3,3,3);instances(&small,2);refuse(f,"guards/allocation3",&small,"declared VGPR allocation");
    rt_executor_reset(f->x);Calls large={0};setup(&large,3,3,257);instances(&large,2);refuse(f,"guards/allocation257",&large,"declared VGPR allocation");
}
static void state(Fixture *f)
{
    const uint32_t counts[4]={0,1,2,7};const unsigned masks[4]={1,1,3,0x7f};
    rt_executor_reset(f->x);Calls first={0};setup(&first,3,3,16);success(f,"state/default-one",&first,AUTO,RUN,1,NULL,NULL,0);
    for(unsigned i=0;i<4;++i){
        char id[64];snprintf(id,sizeof id,"state/count%u",counts[i]);Calls c={0};instances(&c,counts[i]);
        success(f,id,&c,AUTO,i==2?SUBMIT:i==3?RECORD:RUN,masks[i],NULL,NULL,0);
    }
    rt_executor_reset(f->x);Calls reset={0};setup(&reset,3,3,256);success(f,"state/reset-one-max-allocation",&reset,AUTO,RUN,1,NULL,NULL,0);
    Calls ordered={0};configure(&ordered,1,1);reg(&ordered,STEP0,0);reg(&ordered,STEP0,2);
    success(f,"state/zero-overwritten-before-draw",&ordered,AUTO,RUN,0x0f,NULL,NULL,0);
    Calls reverse={0};configure(&reverse,3,4);reg(&reverse,STEP1,3);reg(&reverse,STEP0,2);
    success(f,"state/rate1-then-rate0",&reverse,AUTO,RUN,0x7f,quotient2,quotient3,1);
}
static void independent(Fixture *f,const rt_executor_memory *memory)
{
    char error[256]="";Fixture *other=calloc(1,sizeof *other);CHECK(other!=NULL);if(!other)return;
    other->pool=f->pool;const int opened=rt_executor_open(memory,&other->x,error,sizeof error);CHECK(opened==0);
    if(opened){free(other);return;}
    rt_executor_reset(f->x);Calls a={0};configure(&a,3,4);rates(&a,2,3);
    success(f,"state/independent-A",&a,AUTO,RUN,0x7f,quotient2,quotient3,1);
    Calls b={0};configure(&b,3,4);rates(&b,3,2);
    success(other,"state/independent-B",&b,AUTO,RUN,0x7f,quotient3,quotient2,1);
    Calls retained={0};success(f,"state/independent-A-retained",&retained,AUTO,RUN,0x7f,quotient2,quotient3,1);
    CHECK(rt_executor_finish(other->x,error,sizeof error)==0);rt_executor_close(other->x);free(other);
}
static int selected(const char *requested,const char *group) { return !requested || !strcmp(requested,group); }
int main(int argc,char **argv)
{
    const char *group=NULL;int primitive_set=0;
    for(int i=1;i<argc;++i){
        if(!strcmp(argv[i],"--group") && !group && i+1<argc)group=argv[++i];
        else if(!strcmp(argv[i],"--primitive") && !primitive_set && i+1<argc){
            const char *value=argv[++i];primitive_set=1;
            if(!strcmp(value,"4"))primitive=4;else if(!strcmp(value,"6"))primitive=6;else return 2;
        }else return 2;
    }
    if(group && strcmp(group,"routes") && strcmp(group,"tuples") && strcmp(group,"cache0") && strcmp(group,"cache1") && strcmp(group,"guards") && strcmp(group,"state"))return 2;
    if(primitive==6 && (!group || (strcmp(group,"routes") && strcmp(group,"tuples"))))return 2;
    printf("vertex-instancing MODE primitive=%u group=%s\n",primitive,group?group:"full");
    char path[]="/tmp/paleblood-metal-instancing-XXXXXX",error[512]="";
    const int fd=mkstemp(path);CHECK(fd>=0);if(fd<0)return 1;unlink(path);CHECK(ftruncate(fd,POOL)==0);
    unsigned char *pool=mmap(NULL,POOL,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);CHECK(pool!=MAP_FAILED);if(pool==MAP_FAILED)return 1;
    memset(pool,0x6d,POOL);
    const struct {const uint32_t *words;size_t bytes;} programs[]={{overwrite_inputs,sizeof overwrite_inputs},{stepped0,sizeof stepped0},{stepped1,sizeof stepped1},{raw_instance,sizeof raw_instance},{tuple_color0,sizeof tuple_color0},{tuple_color1,sizeof tuple_color1}};
    for(unsigned i=0;i<6;++i)memcpy(pool+VS+i*0x100,programs[i].words,programs[i].bytes);
    memcpy(pool+PS,pixel,sizeof pixel);
    for(unsigned tile=0;tile<TILES;++tile){
        const float left=-1+(float)tile*0.25f,right=left+0.25f;
        const float p[VERTICES][4]={{left,-1,0,1},{right,-1,0,1},{left,1,0,1},{left,1,0,1},{right,-1,0,1},{right,1,0,1}};
        /* A strip uses four corners followed by a degenerate tail. A Triangle fallback
           still has six valid vertices, but leaves half of every visible tile untouched. */
        const float strip[VERTICES][4]={{left,-1,0,1},{right,-1,0,1},{left,1,0,1},{right,1,0,1},{right,1,0,1},{right,1,0,1}};
        memcpy(pool+POSITIONS+tile*VERTICES*16,primitive==6?strip:p,sizeof p);
        for(unsigned vertex=0;vertex<VERTICES;++vertex)memcpy(pool+COLORS+(tile*VERTICES+vertex)*16,palette[tile],16);
    }
    const uint16_t indices16[8]={UINT16_MAX,5,4,3,2,1,0,UINT16_MAX};
    const uint32_t indices32[8]={UINT32_MAX,5,4,3,2,1,0,UINT32_MAX};
    memcpy(pool+INDEX16,indices16,sizeof indices16);memcpy(pool+INDEX32,indices32,sizeof indices32);
    Fixture *f=calloc(1,sizeof *f);CHECK(f!=NULL);if(!f)return 1;f->pool=pool;
    const rt_executor_memory memory={.fd=fd,.size=POOL,.guest_base=base};
    const int opened=rt_executor_open(&memory,&f->x,error,sizeof error);CHECK(opened==0);printf("vertex-instancing OPEN actual=%d\n",opened);
    if(!opened){
        if(selected(group,"routes"))routes(f);
        if(selected(group,"tuples"))tuples(f);
        if(selected(group,"cache0"))cache(f,0);
        if(selected(group,"cache1"))cache(f,1);
        if(selected(group,"guards"))guards(f);
        if(selected(group,"state")){state(f);independent(f,&memory);}
        CHECK(cases>0 && cases==data_cases+refusal_cases && cases==pool_scans);
        if(!group){CHECK(cases==76);CHECK(data_cases==61);CHECK(refusal_cases==15);}
        CHECK(rt_executor_finish(f->x,error,sizeof error)==0);rt_executor_close(f->x);
    }else fprintf(stderr,"open: %s\n",error);
    printf("vertex-instancing SUMMARY cases=%u data=%u refusals=%u pool_scans=%u checks=%u failures=%u\n",cases,data_cases,refusal_cases,pool_scans,checks,failures);
    free(f);munmap(pool,POOL);close(fd);return failures?1:0;
}
