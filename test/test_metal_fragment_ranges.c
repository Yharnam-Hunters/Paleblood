/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Owned dead canonical UInt32 reads through the actual public Metal executor.
   Valid pointer changes keep the shader code fixed. Cache-hit range refusals must
   precede vertex markers, guest colour publication and public BGRA changes. */
#include "runtime/render_executor.h"
#include "runtime/render_calls.h"
#include "metal_fragment_range_programs.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

enum { POOL_BYTES = 4 << 20, VERTEX_CODE = 0x1000, PIXEL_ONE = 0x2000, PIXEL_TWO = 0x3000,
       POSITIONS = 0x4000, UVS = 0x5000, TABLE = 0x6000, VERTEX_WRITES = 0x7000,
       TARGET = 0x100000, WIDTH = 16, HEIGHT = 16, PIXELS = WIDTH * HEIGHT,
       TARGET_BYTES = PIXELS * 4, VERTEX_BYTES = 3 * sizeof(uint32_t), CALL_CAPACITY = 32 };
#define GUEST_BASE UINT64_C(0x3000000000)
#define AT(offset) (GUEST_BASE + (uint64_t)(offset))
#define POOL_END AT(POOL_BYTES)
/* Existing independently owned vertex-store witness: interpolate attr0 and write
   one marker per vertex through s[8:11]. No raster result can replace this witness. */
static const uint32_t vertex_store_witness[] = {
    0xebf32000, 0x80000100, /* tbuffer_load_format_xyzw v[1:4], v0, s[0:3], 0 format:[32_32_32_32,float] idxen */
    0xebf32000, 0x80010500, /* tbuffer_load_format_xyzw v[5:8], v0, s[4:7], 0 format:[32_32_32_32,float] idxen */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0x7e1202ff, 0x13572468, /* v_mov_b32_e32 v9, 0x13572468 */
    0x34140082, /* v_lshlrev_b32_e32 v10, 2, v0 */
    0xe0701000, 0x8002090a, /* buffer_store_dword v9, v10, s[8:11], 0 offen */
    0xf800020f, 0x08070605, /* exp param0, v5, v6, v7, v8 */
    0xf80008cf, 0x04030201, /* exp pos0, v1, v2, v3, v4 done */
    0xbf810000, /* s_endpgm */
};
_Static_assert(sizeof pixel_fragment_range_one == 26 * sizeof(uint32_t), "owned one-read extent");
_Static_assert(sizeof pixel_fragment_range_two == 27 * sizeof(uint32_t), "owned two-read extent");
_Static_assert(sizeof vertex_store_witness == 15 * sizeof(uint32_t), "owned vertex marker extent");

typedef enum { ACCEPT, REFUSE_RANGE, REFUSE_USER_OR_RANGE } Expected;
typedef struct {
    const char *name;
    unsigned program, reset, declared, pair_words, unique_cache;
    uint64_t pointer;
    uint32_t upper_bits;
    float red;
    Expected expected;
} Scenario;
/* pair_words bit0 supplies s4, bit1 supplies s5. Missing pair cases are supplementary:
   ordinary user_data_read validation also rejects them, so they are not unique
   witnesses of the cached removed-read check. Every decisive pointer case writes both. */
static const Scenario cases[] = {
    {"one-cold-valid",1,1,6,3,0,AT(TABLE),0,.75f,ACCEPT},
    {"one-cache-valid-moved",1,0,6,3,0,AT(TABLE+32),0,.25f,ACCEPT},
    {"one-cache-outside-all-pair-written",1,0,6,3,1,POOL_END+0x1000,0,.875f,REFUSE_RANGE},
    {"one-restore-after-outside",1,0,6,3,0,AT(TABLE),0,.5f,ACCEPT},
    {"one-cache-onepast-all-pair-written",1,0,6,3,1,POOL_END-12,0,.875f,REFUSE_RANGE},
    {"one-restore-after-onepast",1,0,6,3,0,AT(TABLE),0,.25f,ACCEPT},
    {"one-cache-misaligned-all-pair-written",1,0,6,3,1,AT(TABLE)+1,0,.875f,REFUSE_RANGE},
    {"one-restore-after-misalignment",1,0,6,3,0,AT(TABLE),0,.75f,ACCEPT},
    {"one-exact-last-four-bytes",1,0,6,3,0,POOL_END-16,0,.5f,ACCEPT},
    {"one-effective-first-four-bytes",1,0,6,3,0,GUEST_BASE-12,0,.25f,ACCEPT},
    {"one-upper-highword-bits-ignored",1,0,6,3,0,AT(TABLE),0xbeef0000u,.75f,ACCEPT},
    {"one-reset-retained-cache-outside",1,1,6,3,1,POOL_END+0x1000,0,.875f,REFUSE_RANGE},
    {"one-restore-after-reset",1,0,6,3,0,AT(TABLE),0,.5f,ACCEPT},
    {"one-missing-both-pair-words",1,1,6,0,0,AT(TABLE),0,.875f,REFUSE_USER_OR_RANGE},
    {"one-restore-after-missing-both",1,0,6,3,0,AT(TABLE),0,.25f,ACCEPT},
    {"one-missing-low-pair-word",1,1,6,2,0,AT(TABLE),0,.875f,REFUSE_USER_OR_RANGE},
    {"one-restore-after-missing-low",1,0,6,3,0,AT(TABLE),0,.75f,ACCEPT},
    {"one-missing-high-pair-word",1,1,6,1,0,AT(TABLE),0,.875f,REFUSE_USER_OR_RANGE},
    {"one-restore-after-missing-high",1,0,6,3,0,AT(TABLE),0,.5f,ACCEPT},
    {"one-declared-pair-truncated",1,1,5,3,0,AT(TABLE),0,.875f,REFUSE_USER_OR_RANGE},
    {"one-restore-declared-pair",1,1,6,3,0,AT(TABLE),0,.25f,ACCEPT},
    {"two-cold-last-read-onepast",2,1,6,3,0,POOL_END-12,0,.875f,REFUSE_RANGE},
    {"two-cold-valid",2,0,6,3,0,AT(TABLE),0,.75f,ACCEPT},
    {"two-cache-only-offset12-outside",2,0,6,3,1,POOL_END-12,0,.875f,REFUSE_RANGE},
    {"two-restore-after-upper",2,0,6,3,0,AT(TABLE),0,.25f,ACCEPT},
    {"two-cache-only-offset0-outside",2,0,6,3,1,GUEST_BASE-4,0,.875f,REFUSE_RANGE},
    {"two-restore-after-lower",2,0,6,3,0,AT(TABLE),0,.5f,ACCEPT},
    {"two-reset-retained-cache-upper",2,1,6,3,1,POOL_END-12,0,.875f,REFUSE_RANGE},
    {"two-restore-after-reset",2,0,6,3,0,AT(TABLE),0,.75f,ACCEPT},
    {"two-exact-last-four-bytes",2,0,6,3,0,POOL_END-16,0,.25f,ACCEPT},
    {"two-upper-highword-bits-ignored",2,0,6,3,0,AT(TABLE),0xffff0000u,.5f,ACCEPT},
    {"two-cache-misaligned-all-pair-written",2,0,6,3,1,AT(TABLE)+1,0,.875f,REFUSE_RANGE},
    {"two-final-valid-after-reset",2,1,6,3,0,AT(TABLE),0,.75f,ACCEPT}
};
static unsigned scenarios, accepted, refused, unique_refusals, pixels_checked, checks, failures;
#define CHECK(condition) do { ++checks; if (!(condition)) { ++failures; \
    if (failures <= 40) fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); } } while (0)

typedef struct {
    rt_executor *executor;
    unsigned char *pool, *snapshot;
    uint32_t previous_bgra[PIXELS];
    unsigned have_target;
} Fixture;
static uint32_t bits(float value) { uint32_t word; memcpy(&word,&value,sizeof word); return word; }
static void descriptor(uint32_t out[4], unsigned offset)
{
    const uint64_t pointer = AT(offset);
    out[0] = (uint32_t)pointer;
    out[1] = (uint32_t)(pointer >> 32) | 16u << 16;
    out[2] = 3;
    out[3] = 0;
}
static rt_render_call *event(rt_render_call calls[CALL_CAPACITY], unsigned *count, rt_render_call_kind kind)
{
    rt_render_call *call = &calls[(*count)++];
    memset(call,0,sizeof *call); call->kind = kind; return call;
}
static void reg(rt_render_call calls[CALL_CAPACITY], unsigned *count, uint32_t word, uint32_t value)
{
    rt_render_call *call = event(calls,count,RT_CALL_REGISTER);
    call->u.reg.address = word; call->u.reg.value = value;
}
static unsigned setup(Fixture *fixture, rt_render_call calls[CALL_CAPACITY], const Scenario *scenario)
{
    unsigned count = 0;
    rt_render_call *call;
    if (scenario->reset) {
        /* The public reset discards state and written-user bits, retaining shader caches. */
        rt_executor_reset(fixture->executor);
        call = event(calls,&count,RT_CALL_COLOR_TARGET);
        call->u.color_target.enabled = 1;
        call->u.color_target.target = (rt_color_target){.address=AT(TARGET),.pitch=WIDTH,.height=HEIGHT,
            .format=0xa,.component_swap=0,.tile_mode_index=31,.samples=1};
        call = event(calls,&count,RT_CALL_VIEWPORT);
        call->u.viewport.zmax = 1;
        call->u.viewport.scale[0] = call->u.viewport.translate[0] = WIDTH / 2;
        call->u.viewport.scale[1] = call->u.viewport.translate[1] = HEIGHT / 2;
        call->u.viewport.scale[2] = 1;
        call = event(calls,&count,RT_CALL_VERTEX_SHADER);
        call->u.vertex_shader = (rt_vertex_shader){.code=AT(VERTEX_CODE),.vgprs=12,.sgprs=16,
            .user_sgprs=12,.parameter_count=1,.position_format=4};
        call = event(calls,&count,RT_CALL_USER_DATA);
        call->u.user_data.stage = RT_STAGE_VERTEX; call->u.user_data.count = 12;
        descriptor(call->u.user_data.words,POSITIONS);
        descriptor(call->u.user_data.words+4,UVS);
        descriptor(call->u.user_data.words+8,VERTEX_WRITES);
        reg(calls,&count,0xa00c,0);
        reg(calls,&count,0xa00d,WIDTH | HEIGHT << 16);
        reg(calls,&count,0xa200,0);
    }
    call = event(calls,&count,RT_CALL_PIXEL_SHADER);
    call->u.pixel_shader = (rt_pixel_shader){.code=AT(scenario->program==1 ? PIXEL_ONE : PIXEL_TWO),
        .vgprs=8,.sgprs=32,.user_sgprs=scenario->declared,.color_formats=9,.input_enable=2,
        .input_address=2,.interpolated=1,.color_mask=15};
    call = event(calls,&count,RT_CALL_USER_DATA);
    call->u.user_data.stage = RT_STAGE_PIXEL; call->u.user_data.count = 3;
    call->u.user_data.words[0] = bits(.4375f); call->u.user_data.words[1] = bits(.3125f);
    call->u.user_data.words[2] = bits(scenario->red);
    if (scenario->pair_words) {
        call = event(calls,&count,RT_CALL_USER_DATA);
        call->u.user_data.stage = RT_STAGE_PIXEL;
        call->u.user_data.slot = scenario->pair_words==2 ? 5 : 4;
        call->u.user_data.count = scenario->pair_words==3 ? 2 : 1;
        if (scenario->pair_words & 1) call->u.user_data.words[0] = (uint32_t)scenario->pointer;
        if (scenario->pair_words & 2)
            call->u.user_data.words[scenario->pair_words==3 ? 1 : 0] =
                (uint32_t)(scenario->pointer >> 32) | scenario->upper_bits;
    }
    call = event(calls,&count,RT_CALL_DRAW_AUTO); call->u.draw_auto.index_count = 3;
    CHECK(count <= CALL_CAPACITY);
    return count;
}
static size_t differences(const unsigned char *a, const unsigned char *b, size_t bytes)
{
    size_t count = 0; for (size_t i=0;i<bytes;++i) count += a[i]!=b[i]; return count;
}
static int near_byte(unsigned actual, float expected) { return fabsf((float)actual-expected*255.0f)<=2.0f; }
static unsigned expanded(unsigned x, unsigned y)
{
    const unsigned left=x&~1u,top=y&~1u;
    for (unsigned dy=0;dy<2;++dy) for (unsigned dx=0;dx<2;++dx)
        if (((float)(left+dx)+.5f)/WIDTH < .4375f && ((float)(top+dy)+.5f)/HEIGHT < .3125f) return 1;
    return 0;
}
static void run_case(Fixture *fixture, const Scenario *scenario)
{
    unsigned char *pool=fixture->pool,*snapshot=fixture->snapshot;
    if (scenario->expected==ACCEPT) memset(pool+TARGET,0x35,TARGET_BYTES);
    for (unsigned i=0;i<3;++i) {
        const uint32_t marker=0xa5b60000u+scenarios*4+i;
        memcpy(pool+VERTEX_WRITES+sizeof marker*i,&marker,sizeof marker);
    }
    rt_render_call calls[CALL_CAPACITY];
    const unsigned count=setup(fixture,calls,scenario);
    memcpy(snapshot,pool,POOL_BYTES);
    char error[2048]="",later_error[2048]="";
    const int result=rt_executor_run_calls(fixture->executor,calls,count,error,sizeof error);
    ++scenarios;
    const int finish=rt_executor_finish(fixture->executor,later_error,sizeof later_error);
    CHECK(finish==0);
    uint32_t bgra[PIXELS];
    const int read=rt_executor_read_target(fixture->executor,AT(TARGET),bgra,WIDTH,HEIGHT,later_error,sizeof later_error);
    CHECK(read==0);
    unsigned wrong_rgba=0,wrong_bgra=0;
    size_t pool_changed=0;
    if (scenario->expected==ACCEPT) {
        accepted+=result==0; CHECK(result==0);
        if (result) fprintf(stderr,"%s: unexpected refusal: %s\n",scenario->name,error);
        if (!result && !finish && !read) {
            for (unsigned y=0;y<HEIGHT;++y) for (unsigned x=0;x<WIDTH;++x) {
                const unsigned pixel=y*WIDTH+x; const float red=expanded(x,y) ? scenario->red : 0;
                const unsigned char *raw=pool+TARGET+4*pixel; const uint32_t display=bgra[pixel];
                const unsigned r=display>>16&255,g=display>>8&255,b=display&255,a=display>>24;
                const int raw_ok=near_byte(raw[0],red)&&near_byte(raw[1],.25f)&&near_byte(raw[2],.5f)&&raw[3]==255;
                const int display_ok=near_byte(r,red)&&near_byte(g,.25f)&&near_byte(b,.5f)&&a==255;
                wrong_rgba+=!raw_ok; wrong_bgra+=!display_ok;
                CHECK(raw_ok); CHECK(display_ok); CHECK(raw[0]==r&&raw[1]==g&&raw[2]==b&&raw[3]==a);
                ++pixels_checked;
            }
            for (unsigned i=0;i<3;++i) {
                uint32_t marker;memcpy(&marker,pool+VERTEX_WRITES+sizeof marker*i,sizeof marker);
                CHECK(marker==0x13572468u);
            }
            pool_changed=differences(pool,snapshot,VERTEX_WRITES)+
                differences(pool+VERTEX_WRITES+VERTEX_BYTES,snapshot+VERTEX_WRITES+VERTEX_BYTES,
                    TARGET-VERTEX_WRITES-VERTEX_BYTES)+
                differences(pool+TARGET+TARGET_BYTES,snapshot+TARGET+TARGET_BYTES,POOL_BYTES-TARGET-TARGET_BYTES);
            CHECK(pool_changed==0);
            memcpy(fixture->previous_bgra,bgra,sizeof bgra);fixture->have_target=1;
        }
    } else {
        refused+=result!=0;unique_refusals+=scenario->unique_cache&&result!=0;CHECK(result!=0);
        if (scenario->expected==REFUSE_RANGE) CHECK(strstr(error,"removed physical read")!=NULL);
        else CHECK(strstr(error,"physical read")!=NULL||strstr(error,"user")!=NULL||
                   strstr(error,"scalar")!=NULL||strstr(error,"canonical")!=NULL);
        pool_changed=differences(pool,snapshot,POOL_BYTES);CHECK(pool_changed==0);
        CHECK(fixture->have_target);
        if (!read && fixture->have_target) CHECK(memcmp(bgra,fixture->previous_bgra,sizeof bgra)==0);
    }
    printf("CASE name=%s expected=%s unique_cache=%u reset=%u declared=%u pair_words=%u api=%d finish=%d read=%d pool_changed=%zu rgba_wrong=%u bgra_wrong=%u reason=%s\n",
        scenario->name,scenario->expected==ACCEPT ? "accept" : "refuse",scenario->unique_cache,
        scenario->reset,scenario->declared,scenario->pair_words,result,finish,read,pool_changed,wrong_rgba,wrong_bgra,error);
}
int main(void)
{
    char filename[]="/tmp/paleblood-metal-fragment-ranges-XXXXXX",error[2048]="";
    const int fd=mkstemp(filename);CHECK(fd>=0);if(fd<0)return 1;
    CHECK(unlink(filename)==0);
    const int sized=ftruncate(fd,POOL_BYTES);CHECK(sized==0);if(sized){close(fd);return 1;}
    unsigned char *pool=mmap(NULL,POOL_BYTES,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    CHECK(pool!=MAP_FAILED);if(pool==MAP_FAILED){close(fd);return 1;}
    unsigned char *snapshot=malloc(POOL_BYTES);CHECK(snapshot!=NULL);
    if(!snapshot){munmap(pool,POOL_BYTES);close(fd);return 1;}
    memset(pool,0xa7,POOL_BYTES);
    memcpy(pool+VERTEX_CODE,vertex_store_witness,sizeof vertex_store_witness);
    memcpy(pool+PIXEL_ONE,pixel_fragment_range_one,sizeof pixel_fragment_range_one);
    memcpy(pool+PIXEL_TWO,pixel_fragment_range_two,sizeof pixel_fragment_range_two);
    const float positions[3][4]={{-1,-1,0,1},{3,-1,0,1},{-1,3,0,1}};
    const float uvs[3][4]={{0,0,0,1},{2,0,0,1},{0,2,0,1}};
    memcpy(pool+POSITIONS,positions,sizeof positions);memcpy(pool+UVS,uvs,sizeof uvs);
    const rt_executor_memory memory={.fd=fd,.size=POOL_BYTES,.guest_base=GUEST_BASE};
    rt_executor *executor=NULL;const int opened=rt_executor_open(&memory,&executor,error,sizeof error);
    if(opened)fprintf(stderr,"rt_executor_open returned %d: %s\n",opened,error);
    CHECK(opened==0&&executor!=NULL);
    if(!opened&&executor){
        Fixture fixture={.executor=executor,.pool=pool,.snapshot=snapshot};
        for(unsigned i=0;i<sizeof cases/sizeof *cases;++i)run_case(&fixture,&cases[i]);
        rt_executor_close(executor);
    }
    CHECK(scenarios==sizeof cases/sizeof *cases);CHECK(accepted==20);CHECK(refused==13);
    CHECK(unique_refusals==8);CHECK(pixels_checked==20*PIXELS);
    free(snapshot);CHECK(munmap(pool,POOL_BYTES)==0);CHECK(close(fd)==0);
    printf("SUMMARY scenarios=%u accepted=%u refused=%u unique_cache_refusals=%u pixels=%u checks=%u failures=%u\n",
        scenarios,accepted,refused,unique_refusals,pixels_checked,checks,failures);
    return failures ? 1 : 0;
}
