/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Owned LLVM Bonaire programs exercise native Metal fragment ordering through
   physical buffer writes, final-VM discard and literal linear D32/S8/RGBA planes.
   Markers are idempotent coverage witnesses, never fragment invocation counts or
   claims about PS4 depth-rejected fragments. No target program or recording is used. */
#include "runtime/render_executor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

enum { POOL=1<<20, VS=0x1000, PS=0x2000, POS=0x4000,
       PRE0=0x10000, POST0=0x11000, PRE1=0x12000, POST1=0x13000,
       Z=0x20000, S=0x30000, COLOR=0x40000, W=16, H=8, PIXELS=W*H,
       GUARD=64, PRE_MARK=0x13572468, POST_MARK=0x24681357 };
static const uint64_t base=UINT64_C(0x3000000000);
/* llvm-mc -triple=amdgcn -mcpu=bonaire --show-encoding */
static const uint32_t position_vertex[]={
    0xebf32000,0x80000100, /* tbuffer_load_format_xyzw v[1:4], v0, s[0:3], 0 format:[32_32_32_32,float] idxen */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xf80008cf,0x04030201, /* exp pos0, v1, v2, v3, v4 done */
    0xbf810000, /* s_endpgm */
};
static const uint32_t storage_discard[]={
    0x7e080f02, /* v_cvt_u32_f32_e32 v4, v2 */
    0x7e0a0f03, /* v_cvt_u32_f32_e32 v5, v3 */
    0x340a0a84, /* v_lshlrev_b32_e32 v5, 4, v5 */
    0x4a080b04, /* v_add_i32_e32 v4, vcc, v4, v5 */
    0x34080882, /* v_lshlrev_b32_e32 v4, 2, v4 */
    0x7e0c02ff,PRE_MARK, /* v_mov_b32_e32 v6, 0x13572468 */
    0xe0701000,0x80000604, /* buffer_store_dword v6, v4, s[0:3], 0 offen */
    0x7c260408, /* v_cmpx_le_f32_e32 vcc, s8, v2 */
    0x7e0e02ff,POST_MARK, /* v_mov_b32_e32 v7, 0x24681357 */
    0x7e100209, /* v_mov_b32_e32 v8, s9 */
    0xf800180f,0x08080808, /* exp mrt0, v8, v8, v8, v8 done vm */
    0xe0701000,0x80010704, /* buffer_store_dword v7, v4, s[4:7], 0 offen */
    0xbf810000, /* s_endpgm: no later export or EXEC revival */
};
static const unsigned banks[4]={PRE0,POST0,PRE1,POST1};
static uint8_t color_expected[PIXELS*4+2*GUARD],z_expected[PIXELS*4+2*GUARD];
static uint8_t s_expected[PIXELS+2*GUARD],store_expected[4][PIXELS*4+2*GUARD];
static unsigned checks,failures,scenarios,epoch;
static uint8_t color_written[PIXELS];
#define CHECK(x) do{++checks;if(!(x)){++failures;if(failures<=20)fprintf(stderr,"line %d: %s\n",__LINE__,#x);}}while(0)
static rt_render_call calls[64];static size_t count;
static uint32_t bits(float value){uint32_t word;memcpy(&word,&value,4);return word;}
static rt_render_call *event(rt_render_call_kind kind)
{
    if(count>=64){fprintf(stderr,"event capacity exceeded\n");exit(1);}
    rt_render_call *c=&calls[count++];memset(c,0,sizeof *c);c->kind=kind;return c;
}
static void reg(uint32_t address,uint32_t value)
{
    rt_render_call *c=event(RT_CALL_REGISTER);c->u.reg.address=address;c->u.reg.value=value;
}
static void descriptor(uint32_t *words,unsigned offset,unsigned stride,unsigned records)
{
    uint64_t address=base+offset;words[0]=(uint32_t)address;
    words[1]=(uint32_t)(address>>32)|stride<<16;words[2]=records;words[3]=0;
}
static int submit(rt_executor *x,const char *reason,int draw)
{
    if(draw){rt_render_call *c=event(RT_CALL_DRAW_AUTO);c->u.draw_auto.index_count=3;++scenarios;}
    char error[1024]="";int rc=rt_executor_run_calls(x,calls,count,error,sizeof error);count=0;
    if(reason){if(rc!=-1||!strstr(error,reason))fprintf(stderr,"wanted %s, got %d: %s\n",reason,rc,error);
        CHECK(rc==-1&&strstr(error,reason));}
    else{if(rc)fprintf(stderr,"submit: %s\n",error);CHECK(rc==0);}return rc;
}
static void positions(uint8_t *pool,unsigned rectangle,float z)
{
    const float edge=rectangle?1:3;
    const float p[3][4]={{-1,-1,z,1},{edge,-1,z,1},{-1,edge,z,1}};
    memcpy(pool+POS,p,sizeof p);
}
static void pattern(uint8_t *expected,uint8_t *actual,size_t size,unsigned salt)
{
    for(size_t i=0;i<size;++i)expected[i]=(uint8_t)(0x53+i*37+(i>>3)*11+epoch*19+salt);
    memcpy(actual,expected,size);
}
static void initialize(uint8_t *pool)
{
    ++epoch;memset(color_written,0,sizeof color_written);pattern(color_expected,pool+COLOR-GUARD,sizeof color_expected,1);
    pattern(z_expected,pool+Z-GUARD,sizeof z_expected,2);
    pattern(s_expected,pool+S-GUARD,sizeof s_expected,3);
    for(unsigned i=0;i<PIXELS;++i){float z=.75f;memcpy(z_expected+GUARD+i*4,&z,4);s_expected[GUARD+i]=0x2a;}
    memcpy(pool+Z-GUARD,z_expected,sizeof z_expected);memcpy(pool+S-GUARD,s_expected,sizeof s_expected);
    for(unsigned bank=0;bank<4;++bank)pattern(store_expected[bank],pool+banks[bank]-GUARD,sizeof store_expected[bank],4+bank);
}
static void setup(rt_executor *x,uint8_t *pool,unsigned rectangle,int sy,unsigned readonly)
{
    rt_executor_reset(x);count=0;initialize(pool);positions(pool,rectangle,.25f);
    rt_render_call *c=event(RT_CALL_COLOR_TARGET);c->u.color_target.enabled=1;
    c->u.color_target.target=(rt_color_target){.address=base+COLOR,.pitch=W,.height=H,
        .format=0xa,.tile_mode_index=31,.samples=1};
    c=event(RT_CALL_VIEWPORT);c->u.viewport.zmax=1;c->u.viewport.scale[0]=8;
    c->u.viewport.scale[1]=(float)sy*4;c->u.viewport.scale[2]=1;
    c->u.viewport.translate[0]=8;c->u.viewport.translate[1]=4;
    c=event(RT_CALL_VERTEX_SHADER);c->u.vertex_shader=(rt_vertex_shader){.code=base+VS,
        .vgprs=8,.sgprs=8,.user_sgprs=4,.parameter_count=0,.position_format=4};
    c=event(RT_CALL_USER_DATA);c->u.user_data.stage=RT_STAGE_VERTEX;c->u.user_data.count=4;
    descriptor(c->u.user_data.words,POS,16,3);
    reg(0xc242,rectangle?17:4);reg(0xa00f,0);reg(0xa010,3);reg(0xa011,1);
    reg(0xa012,(uint32_t)((base+Z)>>8));reg(0xa014,(uint32_t)((base+Z)>>8));
    reg(0xa013,(uint32_t)((base+S)>>8));reg(0xa015,(uint32_t)((base+S)>>8));
    reg(0xa016,(W/8-1)|((H/8-1)<<11));reg(0xa017,PIXELS/64-1);
    reg(0xa002,readonly<<24);reg(0xa000,0);
    reg(0xa200,1|2|4|1<<4|7<<8); /* Stencil ALWAYS, depth LESS, writes enabled. */
}
static void shader(uint32_t flags,unsigned draw,float threshold,float color,unsigned masked)
{
    rt_render_call *c=event(RT_CALL_PIXEL_SHADER);
    c->u.pixel_shader=(rt_pixel_shader){.code=base+PS,.vgprs=12,.sgprs=16,.user_sgprs=10,
        .color_formats=9,.input_enable=0x302,.input_address=0x302,.interpolated=0,.depth_control=flags,.color_mask=15};
    c=event(RT_CALL_USER_DATA);c->u.user_data.stage=RT_STAGE_PIXEL;c->u.user_data.count=8;
    descriptor(c->u.user_data.words,banks[draw*2],4,PIXELS);
    descriptor(c->u.user_data.words+4,banks[draw*2+1],4,PIXELS);
    c=event(RT_CALL_USER_DATA);c->u.user_data.stage=RT_STAGE_PIXEL;c->u.user_data.slot=8;c->u.user_data.count=2;
    c->u.user_data.words[0]=bits(threshold);c->u.user_data.words[1]=bits(color);
    reg(0xa10b,3<<4|(draw?0:3<<8)); /* First: replace on pass or Z-fail. Second: keep on Z-fail. */
    reg(0xa10c,0x01ffff00|(draw?0x69:0x17));reg(0xa10d,0x01ffff00|(draw?0x69:0x17));
    reg(0xa08e,masked?0:15); /* CB_TARGET_MASK: preserve fragment/depth/stencil effects. */
}
static void word(uint8_t *plane,unsigned pixel,uint32_t value)
{
    memcpy(plane+GUARD+pixel*4,&value,4);
}
static void compare_plane(const char *name,const uint8_t *actual,const uint8_t *expected,size_t size,int unorm)
{
    for(size_t i=0;i<size;++i){int delta=(int)actual[i]-expected[i];
        int approximate=unorm&&i>=GUARD&&i<GUARD+PIXELS*4&&color_written[(i-GUARD)/4];
        int correct=approximate?(delta>=-1&&delta<=1):delta==0;
        if(!correct&&failures<20)fprintf(stderr,"%s byte %zu: %02x expected %02x\n",name,i,actual[i],expected[i]);
        CHECK(correct);
    }
}
static void check_all(const uint8_t *pool)
{
    compare_plane("color",pool+COLOR-GUARD,color_expected,sizeof color_expected,1);
    compare_plane("depth",pool+Z-GUARD,z_expected,sizeof z_expected,0);
    compare_plane("stencil",pool+S-GUARD,s_expected,sizeof s_expected,0);
    for(unsigned bank=0;bank<4;++bank)compare_plane("physical store",pool+banks[bank]-GUARD,store_expected[bank],sizeof store_expected[bank],0);
}
/* Each mode uses a fresh guarded image but retains the executor's shader/state
   between its two overlapping draws, at exactly the same PS code address. */
static void overlap(rt_executor *x,uint8_t *pool,unsigned rectangle,int sy,unsigned mode,uint32_t flags)
{
    unsigned readonly=mode==2?1:mode==3?2:mode==4?3:0;
    int masked=mode==5,fail_depth=mode==6,all_discard=mode==1,no_discard=mode==7;
    setup(x,pool,rectangle,sy,readonly);
    if(submit(x,NULL,0))return;
    positions(pool,rectangle,fail_depth?.875f:.25f);
    shader(flags,0,all_discard?16:no_discard?0:8,.25f,(unsigned)masked);
    if(submit(x,NULL,1))return;
    for(unsigned y=0;y<H;++y)for(unsigned col=0;col<W;++col){unsigned i=y*W+col;
        int alive=!all_discard&&(no_discard||col>=8);
        word(store_expected[0],i,PRE_MARK);if(alive)word(store_expected[1],i,POST_MARK);
        if(alive){if(!(readonly&2))s_expected[GUARD+i]=0x17;
            if(!fail_depth){if(!(readonly&1))word(z_expected,i,bits(.25f));
                if(!masked){memset(color_expected+GUARD+i*4,64,4);color_written[i]=1;}}}
    }
    check_all(pool);
    /* An all-discard or hole must leave both native tests untouched. The second,
       farther draw passes there; an early Z/S update would suppress its color. */
    positions(pool,rectangle,.5f);shader(flags^0x10,1,0,.75f,0);
    if(submit(x,NULL,1))return;
    for(unsigned y=0;y<H;++y)for(unsigned col=0;col<W;++col){unsigned i=y*W+col;
        int first_alive=!all_discard&&(no_discard||col>=8);
        int pass=(readonly&1)||fail_depth||!first_alive;
        /* Native late execution preserves these physical stores even where depth
           rejects the second fragment. These are Metal witnesses, not PS4 counts. */
        word(store_expected[2],i,PRE_MARK);word(store_expected[3],i,POST_MARK);
        if(pass){memset(color_expected+GUARD+i*4,191,4);color_written[i]=1;
            if(!(readonly&1))word(z_expected,i,bits(.5f));if(!(readonly&2))s_expected[GUARD+i]=0x69;}
    }
    check_all(pool);
}
static void refused(rt_executor *x,uint8_t *pool,unsigned rectangle,uint32_t flags,int override,uint32_t value)
{
    setup(x,pool,rectangle,1,0);shader(flags,0,8,.25f,0);
    if(override)reg(0xa003,value);
    submit(x,override?"register 0xa003":"DB_SHADER_CONTROL",1);check_all(pool);
}
int main(void)
{
    char path[]="/tmp/paleblood-metal-shader-order-XXXXXX",error[1024]="";int fd=mkstemp(path);
    if(fd<0){perror("mkstemp");return 1;}unlink(path);
    if(ftruncate(fd,POOL)){perror("ftruncate");close(fd);return 1;}
    uint8_t *pool=mmap(NULL,POOL,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    if(pool==MAP_FAILED){perror("mmap");close(fd);return 1;}
    memcpy(pool+VS,position_vertex,sizeof position_vertex);memcpy(pool+PS,storage_discard,sizeof storage_discard);
    rt_executor *x=NULL;const rt_executor_memory memory={fd,POOL,base,NULL,NULL,NULL};
    int rc=rt_executor_open(&memory,&x,error,sizeof error);CHECK(rc==0);
    if(rc){fprintf(stderr,"open: %s\n",error);munmap(pool,POOL);close(fd);return 1;}
    const uint32_t flags[]={0,0x10,0x40,0x50};
    for(unsigned rectangle=0;rectangle<2;++rectangle)for(int sy=-1;sy<=1;sy+=2)
        for(unsigned mode=0;mode<8;++mode)for(unsigned f=0;f<4;++f)overlap(x,pool,rectangle,sy,mode,flags[f]);
    for(unsigned rectangle=0;rectangle<2;++rectangle){
        for(unsigned bit=0;bit<32;++bit)if(bit!=4&&bit!=6)refused(x,pool,rectangle,0x50|(1u<<bit),0,0);
        const uint32_t overrides[]={0,1,2,1u<<4,1u<<6,0xffffffffu};
        for(unsigned i=0;i<6;++i)refused(x,pool,rectangle,0x50,1,overrides[i]);
    }
    rt_executor_close(x);munmap(pool,POOL);close(fd);
    printf("metal shader order: %u scenarios, %u checks, %u failures\n",scenarios,checks,failures);
    return failures?1:0;
}
