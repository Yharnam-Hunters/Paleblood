/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Owned vertex-only effects, attachment/cache transitions and independent full-pool goldens. */
#include "runtime/render_executor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

enum { POOL=64<<10, MARKER_CODE=0x1000, TUPLE_CODE=0x1200, COUNTER_CODE=0x1400, CLIP_CODE=0x1600,
       PS=0x2000, POSITIONS=0x3000, COLORS=0x3400, MARKERS=0x5000, TUPLES=0x5400,
       COUNTERS=0x5800, INDEX16=0x6000, INDEX32=0x6100, COLOR=0x8000, Z=0xa000, S=0xb000,
       WIDTH=16, HEIGHT=8, PIXELS=WIDTH*HEIGHT, RECORDS=32, SLICES=3, CALL_LIMIT=64,
       COLOR_BYTES=PIXELS*4, Z_BYTES=SLICES*PIXELS*4, S_BYTES=SLICES*PIXELS,
       GUARD=64, MARKER_BYTES=RECORDS*4, TUPLE_BYTES=RECORDS*16 };
static const uint64_t base=UINT64_C(0x3000000000);
static const uint32_t marker=UINT32_C(0x13572468);
/* Exact owned vertex_store_witness from test_metal_executor.c: marker at4*vertex_id. */
static const uint32_t marker_vertex[] = {
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

/* Exact owned TUPLE_PROGRAM(1) from test_metal_vertex_instancing.c. Slots use
   6*raw_instance+vertex; four words record both quotients, instance and vertex. */
#define TUPLE_PROGRAM(color_input) { \
    0x7e140301, 0x7e160302, 0x7e180303, 0x7e1a0300, /* v10..v13 = v1,v2,v3,v0 */ \
    0xd2860009, 0x04010d03, /* v9 = 6*v3 + v0 (position and unique tuple slot) */ \
    0xd286000f, 0x04010d00 | (color_input), /* v15 = 6*v<color_input> + v0 */ \
    0x341c1284, /* v_lshlrev_b32 v14,4,v9 */ \
    0xe0781000, 0x80020a0e, /* buffer_store_dwordx4 v[10:13],v14,s[8:11],0 offen */ \
    0xebf32000, 0x80000109, 0xebf32000, 0x8001050f, 0xbf8c0f70, \
    0xf800020f, 0x08070605, 0xf80008cf, 0x04030201, 0xbf810000 }
static const uint32_t instance_vertex[]=TUPLE_PROGRAM(1);
#undef TUPLE_PROGRAM
/* Exact owned counted_vertex from test_metal_rectangles.c: each source ID adds1.0. */
static const uint32_t counter_vertex[]={
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
/* Exact owned pixel_color from gcn_programs.h; constant white inputs avoid rounding. */
static const uint32_t color_pixel[] = {
    0xbefc0300, /* s_mov_b32 m0, s0 */
    0xc8080000, /* v_interp_p1_f32 v2, v0, attr0.x */
    0xc8090001, /* v_interp_p2_f32 v2, v1, attr0.x */
    0xc80c0100, /* v_interp_p1_f32 v3, v0, attr0.y */
    0xc80d0101, /* v_interp_p2_f32 v3, v1, attr0.y */
    0xc8100200, /* v_interp_p1_f32 v4, v0, attr0.z */
    0xc8110201, /* v_interp_p2_f32 v4, v1, attr0.z */
    0xc8140300, /* v_interp_p1_f32 v5, v0, attr0.w */
    0xc8150301, /* v_interp_p2_f32 v5, v1, attr0.w */
    0x5e0c0702, /* v_cvt_pkrtz_f16_f32_e32 v6, v2, v3 */
    0x5e0e0b04, /* v_cvt_pkrtz_f16_f32_e32 v7, v4, v5 */
    0xf8001c0f, 0x00000706, /* exp mrt0, v6, v6, v7, v7 done compr vm */
    0xbf810000, /* s_endpgm */
};

/* Exact owned vertex_clip_eight from metal_executor_programs.h. This row checks
   the admitted clip interface compiles as void, without claiming a store witness. */
static const uint32_t clip_vertex[] = {
    0xebf32000, 0x80000100, /* tbuffer_load_format_xyzw v[1:4], v0, s[0:3], 0 format:[BUF_DATA_FORMAT_32_32_32_32,BUF_NUM_FORMAT_FLOAT] idxen */
    0xebf32000, 0x80010500, /* tbuffer_load_format_xyzw v[5:8], v0, s[4:7], 0 format:[BUF_DATA_FORMAT_32_32_32_32,BUF_NUM_FORMAT_FLOAT] idxen */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xf800020f, 0x08070605, /* exp param0, v5, v6, v7, v8 */
    0xf80000cf, 0x04030201, /* exp pos0, v1, v2, v3, v4 */
    0xf80000df, 0x02010201, /* exp pos1, v1, v2, v1, v2 */
    0xf80008ef, 0x02010201, /* exp pos2, v1, v2, v1, v2 done */
    0xbf810000, /* s_endpgm */
};

typedef enum { MARKER, TUPLE, COUNTER, CLIP } Program;
typedef struct {
    const char *id;
    unsigned color,depth,program,primitive,instances,vertices,index_width;
    int pixel; /* -1 leaves the pixel state unprogrammed; 0 explicitly programs null. */
    const char *reason;
} Case;
static const Case cases[]={
    {"cold-auto",0,0,MARKER,4,1,3,0,0,NULL},
    {"color-rebound",1,0,MARKER,4,1,3,0,1,NULL},
    {"vertex-only-after-color",0,0,MARKER,4,1,3,0,0,NULL},
    {"depth-stencil-rebound",0,1,MARKER,4,1,3,0,0,NULL},
    {"vertex-only-after-depth",0,0,MARKER,4,1,3,0,0,NULL},
    {"sparse-index16",0,0,MARKER,4,1,3,2,0,NULL},
    {"sparse-index32",0,0,MARKER,4,1,3,4,0,NULL},
    {"instance-tuples",0,0,TUPLE,4,3,3,0,0,NULL},
    {"clip-output-void",0,0,CLIP,4,1,3,0,0,NULL},
    {"zero-count",0,0,MARKER,4,1,0,0,0,NULL},
    {"pixel-present-refused",0,0,MARKER,4,1,3,0,1,"no color target"},
    {"pixel-unprogrammed-refused",0,0,MARKER,4,1,3,0,-1,"no color target"},
    {"rect-source-once",0,0,COUNTER,17,1,3,0,0,NULL},
    {"rect-sparse-source-once",0,0,COUNTER,17,1,3,4,0,NULL},
    {"rect-incomplete",0,0,COUNTER,17,1,2,0,0,"complete triples"},
    {"rect-multi-instance",0,0,COUNTER,17,2,3,0,0,"rectangle-list instancing"},
    {"rect-zero-multi-instance",0,0,COUNTER,17,2,0,0,0,"rectangle-list instancing"},
};
static unsigned checks,failures,scenarios;
#define CHECK(c) do { ++checks; if(!(c)){++failures;if(failures<=32) \
    fprintf(stderr,"line%d: %s\n",__LINE__,#c);} } while(0)
typedef struct { rt_render_call calls[CALL_LIMIT];size_t count; } Calls;
typedef struct {
    rt_executor *x;
    uint8_t *pool,before[POOL],wanted[POOL],writable[POOL];
} Fixture;
static uint64_t at(unsigned offset){return base+offset;}
static rt_render_call *event(Calls *calls,rt_render_call_kind kind)
{
    if(calls->count==CALL_LIMIT){fputs("owned call inventory overflow\n",stderr);exit(1);}
    rt_render_call *c=&calls->calls[calls->count++];memset(c,0,sizeof *c);c->kind=kind;return c;
}
static void reg(Calls *calls,unsigned address,uint32_t value)
{rt_render_call *c=event(calls,RT_CALL_REGISTER);c->u.reg.address=address;c->u.reg.value=value;}
static void descriptor(uint32_t words[4],unsigned offset,unsigned stride)
{
    const uint64_t address=at(offset);words[0]=(uint32_t)address;
    words[1]=(uint32_t)(address>>32)|stride<<16;words[2]=RECORDS;words[3]=0;
}
static void seed(Fixture *f)
{
    memset(f->pool+MARKERS,0x6d,MARKER_BYTES+GUARD);
    memset(f->pool+TUPLES,0x6d,TUPLE_BYTES+GUARD);
    memset(f->pool+COUNTERS,0,MARKER_BYTES);memset(f->pool+COUNTERS+MARKER_BYTES,0x6d,GUARD);
    memset(f->pool+COLOR,0x55,COLOR_BYTES+GUARD);
    const uint32_t far_depth=0x3f400000;
    for(unsigned pixel=0;pixel<SLICES*PIXELS;++pixel)memcpy(f->pool+Z+pixel*4,&far_depth,4);
    memset(f->pool+Z+Z_BYTES,0xa5,GUARD);memset(f->pool+S,0x2a,S_BYTES);
    memset(f->pool+S+S_BYTES,0xa5,GUARD);
}
static void setup(Calls *calls,const Case *test)
{
    rt_render_call *c=event(calls,RT_CALL_COLOR_TARGET);c->u.color_target.enabled=test->color;
    if(test->color)c->u.color_target.target=(rt_color_target){.address=at(COLOR),.pitch=WIDTH,
        .height=HEIGHT,.format=0xa,.tile_mode_index=31,.samples=1};
    c=event(calls,RT_CALL_VIEWPORT);c->u.viewport.zmax=1;
    c->u.viewport.scale[0]=c->u.viewport.translate[0]=WIDTH/2;
    c->u.viewport.scale[1]=c->u.viewport.translate[1]=HEIGHT/2;c->u.viewport.scale[2]=1;
    const unsigned code=test->program==TUPLE?TUPLE_CODE:test->program==COUNTER?COUNTER_CODE:
        test->program==CLIP?CLIP_CODE:MARKER_CODE;
    c=event(calls,RT_CALL_VERTEX_SHADER);c->u.vertex_shader=(rt_vertex_shader){.code=at(code),
        .vgprs=16,.sgprs=16,.user_sgprs=12,.vgpr_inputs=test->program==TUPLE?3:0,
        .parameter_count=1,.position_format=test->program==CLIP?0x444:4,
        .output_control=test->program==CLIP?0x00c000ff:0};
    if(test->pixel>=0){
        c=event(calls,RT_CALL_PIXEL_SHADER);
        c->u.pixel_shader=(rt_pixel_shader){.code=test->pixel?at(PS):0,.vgprs=12,.sgprs=16,
            .color_formats=4,.input_enable=2,.input_address=2,.interpolated=1,.color_mask=15};
    }
    c=event(calls,RT_CALL_USER_DATA);c->u.user_data.stage=RT_STAGE_VERTEX;c->u.user_data.count=8;
    descriptor(c->u.user_data.words,POSITIONS,16);descriptor(c->u.user_data.words+4,COLORS,16);
    c=event(calls,RT_CALL_USER_DATA);c->u.user_data.stage=RT_STAGE_VERTEX;
    c->u.user_data.slot=8;c->u.user_data.count=4;
    descriptor(c->u.user_data.words,test->program==TUPLE?TUPLES:test->program==COUNTER?COUNTERS:MARKERS,
        test->program==TUPLE?16:4);
    c=event(calls,RT_CALL_INSTANCES);c->u.instances.count=test->instances;
    reg(calls,0xc242,test->primitive);reg(calls,0xa08e,15);reg(calls,0xa202,0x00cc0010);
    reg(calls,0xa200,test->depth?1|2|4|7<<4|7<<8:0);
    /* Disabled tests leave these raw metadata bits inert, as in the no-attachment
       profile. Only the active depth/stencil row supplies plane bases. */
    reg(calls,0xa010,test->depth?3:0xa0000003);reg(calls,0xa011,test->depth?1:0x20000001);
    if(test->depth){
        reg(calls,0xa000,0);reg(calls,0xa00f,0);reg(calls,0xa002,1|1<<13);
        reg(calls,0xa012,(uint32_t)(at(Z)>>8));reg(calls,0xa014,(uint32_t)(at(Z)>>8));
        reg(calls,0xa013,(uint32_t)(at(S)>>8));reg(calls,0xa015,(uint32_t)(at(S)>>8));
        reg(calls,0xa016,(WIDTH/8-1)|((HEIGHT/8-1)<<11));reg(calls,0xa017,PIXELS/64-1);
        reg(calls,0xa10b,3<<4);reg(calls,0xa10c,0x01ffff17);reg(calls,0xa10d,0x01ffff17);
    }
    if(test->program==TUPLE){reg(calls,0xa2a8,2);reg(calls,0xa2a9,3);}
}
static void expected_write(Fixture *f,unsigned offset,const void *value,size_t bytes)
{
    CHECK(offset<=POOL && bytes<=POOL-offset);if(offset>POOL || bytes>POOL-offset)exit(1);
    memcpy(f->wanted+offset,value,bytes);memset(f->writable+offset,1,bytes);
}
static void expected(Fixture *f,const Case *test)
{
    memcpy(f->before,f->pool,POOL);memcpy(f->wanted,f->pool,POOL);memset(f->writable,0,POOL);
    if(test->reason)return;
    static const unsigned sparse[3]={4,2,5},quotient2[3]={0,0,1},quotient3[3]={0,0,0};
    for(unsigned vertex=0;vertex<test->vertices;++vertex){
        const unsigned id=test->index_width?sparse[vertex]:vertex;
        if(test->program==MARKER)expected_write(f,MARKERS+id*4,&marker,4);
        else if(test->program==COUNTER){const uint32_t one=0x3f800000;expected_write(f,COUNTERS+id*4,&one,4);}
        else if(test->program==TUPLE)for(unsigned instance=0;instance<3;++instance){
            const uint32_t tuple[4]={quotient2[instance],quotient3[instance],instance,id};
            expected_write(f,TUPLES+(instance*6+id)*16,tuple,sizeof tuple);
        }
    }
    if(test->color){memset(f->wanted+COLOR,255,COLOR_BYTES);memset(f->writable+COLOR,1,COLOR_BYTES);}
    if(test->depth){
        const uint32_t near_depth=0x3e800000;const uint8_t stencil=0x17;
        /* Owned LINEAR geometry, slice1: D32 begins at512, S8 at128. No production
           tiling or attachment decoder forms the expected selected-slice offsets. */
        for(unsigned pixel=0;pixel<PIXELS;++pixel){
            expected_write(f,Z+512+pixel*4,&near_depth,4);expected_write(f,S+128+pixel,&stencil,1);
        }
    }
}
static size_t region_bad(const Fixture *f,unsigned offset,unsigned bytes)
{
    size_t bad=0;for(unsigned i=0;i<bytes;++i)bad+=f->pool[offset+i]!=f->wanted[offset+i];return bad;
}
static void invoke(Fixture *f,const Case *test)
{
    Calls calls={0};setup(&calls,test);seed(f);expected(f,test);
    rt_render_call *draw;
    if(test->index_width){
        event(&calls,RT_CALL_INDEX_TYPE)->u.index_type.type=test->index_width==4;
        draw=event(&calls,RT_CALL_DRAW_INDEXED);draw->u.draw_indexed.header=0xc0042700;
        draw->u.draw_indexed.max_size=test->vertices;draw->u.draw_indexed.index_count=test->vertices;
        draw->u.draw_indexed.address=at(test->index_width==4?INDEX32:INDEX16);
    }else{draw=event(&calls,RT_CALL_DRAW_AUTO);draw->u.draw_auto.index_count=test->vertices;}
    char error[1024]="";const int rc=rt_executor_run_calls(f->x,calls.calls,calls.count,error,sizeof error);
    const unsigned reason_match=test->reason?(rc==-1 && strstr(error,test->reason)!=NULL):(rc==0 && error[0]=='\0');
    size_t changed=0,want_changed=0,mismatches=0,guard_bytes=0,guard_bad=0,writable_bytes=0;
    for(unsigned i=0;i<POOL;++i){
        changed+=f->pool[i]!=f->before[i];want_changed+=f->wanted[i]!=f->before[i];mismatches+=f->pool[i]!=f->wanted[i];
        writable_bytes+=f->writable[i]!=0;if(!f->writable[i]){++guard_bytes;guard_bad+=f->pool[i]!=f->before[i];}
    }
    const size_t marker_bad=region_bad(f,MARKERS,MARKER_BYTES),tuple_bad=region_bad(f,TUPLES,TUPLE_BYTES),
        counter_bad=region_bad(f,COUNTERS,MARKER_BYTES),color_bad=region_bad(f,COLOR,COLOR_BYTES),
        depth_bad=region_bad(f,Z,Z_BYTES),stencil_bad=region_bad(f,S,S_BYTES);
    CHECK(reason_match);CHECK(mismatches==0);CHECK(changed==want_changed);CHECK(guard_bad==0);
    CHECK(marker_bad==0);CHECK(tuple_bad==0);CHECK(counter_bad==0);CHECK(color_bad==0);CHECK(depth_bad==0);CHECK(stencil_bad==0);
    printf("vertex-only id=%s rc=%d reason_match=%u color=%u depth=%u program=%u primitive=%u instances=%u vertices=%u index_width=%u pixel=%d "
        "pool_compared=%u pool_changed=%zu want_changed=%zu pool_mismatches=%zu writable_bytes=%zu guard_bytes=%zu guard_bad=%zu "
        "marker_bad_bytes=%zu tuple_bad_bytes=%zu counter_bad_bytes=%zu color_bad_bytes=%zu depth_bad_bytes=%zu stencil_bad_bytes=%zu reason=%s\n",
        test->id,rc,reason_match,test->color,test->depth,test->program,test->primitive,test->instances,test->vertices,test->index_width,test->pixel,
        (unsigned)POOL,changed,want_changed,mismatches,writable_bytes,guard_bytes,guard_bad,marker_bad,tuple_bad,counter_bad,color_bad,depth_bad,stencil_bad,error);
    ++scenarios;
}
int main(int argc,char **argv)
{
    if(argc!=1){fprintf(stderr,"usage: %s\n",argv[0]);return 2;}
    char path[]="/tmp/paleblood-metal-vertex-only-XXXXXX",error[512]="";
    const int fd=mkstemp(path);CHECK(fd>=0);if(fd<0)return 1;unlink(path);
    CHECK(ftruncate(fd,POOL)==0);uint8_t *pool=mmap(NULL,POOL,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    CHECK(pool!=MAP_FAILED);if(pool==MAP_FAILED){close(fd);return 1;}memset(pool,0x6d,POOL);
    memcpy(pool+MARKER_CODE,marker_vertex,sizeof marker_vertex);memcpy(pool+TUPLE_CODE,instance_vertex,sizeof instance_vertex);
    memcpy(pool+COUNTER_CODE,counter_vertex,sizeof counter_vertex);memcpy(pool+CLIP_CODE,clip_vertex,sizeof clip_vertex);
    memcpy(pool+PS,color_pixel,sizeof color_pixel);
    const float triangle[3][4]={{-1,-1,0.25f,1},{3,-1,0.25f,1},{-1,3,0.25f,1}},white[4]={1,1,1,1};
    for(unsigned i=0;i<RECORDS;++i){memcpy(pool+POSITIONS+i*16,triangle[i%3],16);memcpy(pool+COLORS+i*16,white,16);}
    const uint16_t indices16[3]={4,2,5};const uint32_t indices32[3]={4,2,5};
    memcpy(pool+INDEX16,indices16,sizeof indices16);memcpy(pool+INDEX32,indices32,sizeof indices32);
    Fixture *f=calloc(1,sizeof *f);CHECK(f!=NULL);if(!f){munmap(pool,POOL);close(fd);return 1;}f->pool=pool;
    const rt_executor_memory memory={.fd=fd,.size=POOL,.guest_base=base};
    const int opened=rt_executor_open(&memory,&f->x,error,sizeof error);CHECK(opened==0);if(opened)fprintf(stderr,"open: %s\n",error);
    if(!opened)for(unsigned i=0;i<sizeof cases/sizeof *cases;++i){
        /* The first five draws deliberately retain one VS address through both ABI
           variants and through native color/depth pipelines. reset() retains caches. */
        if(i==0 || i>=5)rt_executor_reset(f->x);
        invoke(f,&cases[i]);
    }
    if(f->x)rt_executor_close(f->x);
    free(f);munmap(pool,POOL);close(fd);
    printf("metal vertex-only: %u scenarios, %u checks, %u failures\n",scenarios,checks,failures);return failures?1:0;
}
