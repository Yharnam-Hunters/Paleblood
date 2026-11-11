/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Owned Bonaire shaders (llvm-mc -triple=amdgcn -mcpu=bonaire --show-encoding).
   The oracle is literal framebuffer pixel centres, independent of shader lowering.
   No game program, recording, original-side harness or offline Metal compiler is used. */
#include "runtime/render_executor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

enum { POOL=1<<20, VS=0x1000, SINGLE=0x2000, BOTH=0x3000, CONSTANT=0x4000,
       POS=0x5000, WITNESS=0x6000, INDEX=0x7000, TARGET=0x10000,
       W=40, H=24, VERTICES=16, GUARD=64, MARKER=0x13572468 };
static const uint64_t base=UINT64_C(0x3000000000);
static const uint32_t single_coordinate[]={
    0x100404ff,0x3c800000, /* v_mul_f32_e32 v2, 0x3c800000, v2 (1/64) */
    0xf800180f,0x02020202, /* exp mrt0, v2, v2, v2, v2 done vm */
    0xbf810000, /* s_endpgm */
};
static const uint32_t both_coordinates[]={
    0x100404ff,0x3c800000, /* v_mul_f32_e32 v2, 0x3c800000, v2 */
    0x100606ff,0x3c800000, /* v_mul_f32_e32 v3, 0x3c800000, v3 */
    0xf800180f,0x03020302, /* exp mrt0, v2, v3, v2, v3 done vm */
    0xbf810000, /* s_endpgm */
};
static const uint32_t constant_pixel[]={
    0x7e0002f0, /* v_mov_b32_e32 v0, 0.5 */
    0xf800180f,0x00000000, /* exp mrt0, v0, v0, v0, v0 done vm */
    0xbf810000, /* s_endpgm */
};
static const uint32_t position_and_witness[]={
    0xebf32000,0x80000100, /* tbuffer_load_format_xyzw v[1:4], v0, s[0:3], 0 format:[32_32_32_32,float] idxen */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0x7e0a02ff,MARKER, /* v_mov_b32_e32 v5, 0x13572468 */
    0x340c0082, /* v_lshlrev_b32_e32 v6, 2, v0 */
    0xe0701000,0x80010506, /* buffer_store_dword v5, v6, s[4:7], 0 offen */
    0xf80008cf,0x04030201, /* exp pos0, v1, v2, v3, v4 done */
    0xbf810000, /* s_endpgm */
};
static unsigned checks,failures,scenarios,phase;
#define CHECK(x) do{++checks;if(!(x)){++failures;if(failures<=20)fprintf(stderr,"line %d: %s\n",__LINE__,#x);}}while(0)
static rt_render_call calls[64];
static size_t count;
static rt_render_call *event(rt_render_call_kind kind)
{
    if(count>=64){fprintf(stderr,"event capacity exceeded\n");exit(1);}
    rt_render_call *c=&calls[count++];memset(c,0,sizeof *c);c->kind=kind;return c;
}
static void reg(uint32_t address,uint32_t value)
{
    rt_render_call *c=event(RT_CALL_REGISTER);c->u.reg.address=address;c->u.reg.value=value;
}
static void descriptor(uint32_t *words,unsigned offset,unsigned stride)
{
    uint64_t address=base+offset;words[0]=(uint32_t)address;
    words[1]=(uint32_t)(address>>32)|stride<<16;words[2]=VERTICES;words[3]=0;
}
static int submit(rt_executor *x,const char *reason)
{
    char error[1024]="";int rc=rt_executor_run_calls(x,calls,count,error,sizeof error);count=0;
    if(reason){CHECK(rc==-1&&strstr(error,reason));
        if(rc!=-1||!strstr(error,reason))fprintf(stderr,"wanted %s, got %d: %s\n",reason,rc,error);
    }else{if(rc)fprintf(stderr,"submit: %s\n",error);CHECK(rc==0);}
    return rc;
}
static void pixel(uint32_t enable,uint32_t address,unsigned vgprs,uint32_t barycentric)
{
    rt_render_call *c=event(RT_CALL_PIXEL_SHADER);
    c->u.pixel_shader=(rt_pixel_shader){.code=base+(address==2?CONSTANT:address==0x302?BOTH:SINGLE),
        .vgprs=vgprs,.sgprs=8,.color_formats=9,.input_enable=enable,.input_address=address,
        .interpolated=0,.barycentric_control=barycentric,.color_mask=15};
}
static unsigned source_id(unsigned path,unsigned i)
{
    static const unsigned sparse[3]={7,2,11};return path==2?sparse[i]:i;
}
static void draw(unsigned path)
{
    rt_render_call *c;
    if(path==2){c=event(RT_CALL_INDEX_TYPE);c->u.index_type.type=1;
        c=event(RT_CALL_DRAW_INDEXED);c->u.draw_indexed.header=0xc0042700;
        c->u.draw_indexed.address=base+INDEX;c->u.draw_indexed.max_size=3;c->u.draw_indexed.index_count=3;
    }else{c=event(RT_CALL_DRAW_AUTO);c->u.draw_auto.index_count=path?3:6;}
}
static void setup(rt_executor *x,uint8_t *pool,unsigned path,int sy,unsigned offset,unsigned scissor)
{
    static const float quad[6][4]={{-1,-1,.25f,1},{1,-1,.25f,1},{-1,1,.25f,1},
        {-1,1,.25f,1},{1,-1,.25f,1},{1,1,.25f,1}};
    rt_executor_reset(x);count=0;memset(pool+POS,0x39,VERTICES*16+GUARD);
    memset(pool+INDEX,0x91,GUARD);
    for(unsigned i=0;i<(path?3u:6u);++i){unsigned id=source_id(path,i);memcpy(pool+POS+id*16,quad[i],16);
        if(path==2)memcpy(pool+INDEX+i*4,&id,4);}
    rt_render_call *c=event(RT_CALL_COLOR_TARGET);c->u.color_target.enabled=1;
    c->u.color_target.target=(rt_color_target){.address=base+TARGET,.pitch=W,.height=H,
        .format=0xa,.tile_mode_index=31,.samples=1};
    c=event(RT_CALL_VIEWPORT);c->u.viewport.zmax=1;c->u.viewport.scale[0]=12;
    c->u.viewport.scale[1]=(float)sy*6;c->u.viewport.scale[2]=1;
    c->u.viewport.translate[0]=(float)(12+(offset?5:0));c->u.viewport.translate[1]=(float)(6+(offset?3:0));
    c=event(RT_CALL_VERTEX_SHADER);c->u.vertex_shader=(rt_vertex_shader){.code=base+VS,
        .vgprs=8,.sgprs=8,.user_sgprs=8,.parameter_count=0,.position_format=4};
    c=event(RT_CALL_USER_DATA);c->u.user_data.stage=RT_STAGE_VERTEX;c->u.user_data.count=8;
    descriptor(c->u.user_data.words,POS,16);descriptor(c->u.user_data.words+4,WITNESS,4);
    reg(0xc242,path?17:4);
    if(scissor){reg(0xa00c,9|5<<16);reg(0xa00d,26|12<<16);}
}
static unsigned char pattern(unsigned i)
{
    return (unsigned char)(0x53u+i*37u+(i>>4)*11u+phase*19u);
}
static void initialize(uint8_t *pool)
{
    ++phase;for(unsigned i=0;i<W*H*4+2*GUARD;++i)pool[TARGET-GUARD+i]=pattern(i);
    memset(pool+WITNESS,0x65,VERTICES*4+GUARD);
}
static void check_witness(const uint8_t *pool,unsigned path,int executed)
{
    for(unsigned id=0;id<VERTICES;++id){uint32_t got;memcpy(&got,pool+WITNESS+id*4,4);
        int active=0;for(unsigned i=0;i<(path?3u:6u);++i)if(id==source_id(path,i))active=1;
        CHECK(got==(executed&&active?MARKER:0x65656565u));}
    for(unsigned i=VERTICES*4;i<VERTICES*4+GUARD;++i)CHECK(pool[WITNESS+i]==0x65);
}
static void check_target(const uint8_t *pool,uint32_t mask,unsigned offset,unsigned scissor,int executed)
{
    const unsigned left=offset?5:0,top=offset?3:0;
    for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x)for(unsigned c=0;c<4;++c){
        unsigned byte=(y*W+x)*4+c;
        int inside=executed&&x>=left&&x<left+24&&y>=top&&y<top+12;
        if(scissor)inside=inside&&x>=9&&x<26&&y>=5&&y<12;
        if(!inside)CHECK(pool[TARGET+byte]==pattern(GUARD+byte));
        else{
            /* Guest target is RGBA8. No viewport inversion or relative coordinate
               enters this oracle: FragCoord is the absolute pixel centre for either Y sign. */
            float value=mask==2?.5f:mask==0x102?((float)x+.5f)/64:
                mask==0x202?((float)y+.5f)/64:((float)(c&1?y:x)+.5f)/64;
            int expected=(int)(value*255+.5f),delta=(int)pool[TARGET+byte]-expected;
            CHECK(delta>=-1&&delta<=1);
        }
    }
    for(unsigned i=0;i<GUARD;++i){CHECK(pool[TARGET-GUARD+i]==pattern(i));
        CHECK(pool[TARGET+W*H*4+i]==pattern(GUARD+W*H*4+i));}
}
static void positive(rt_executor *x,uint8_t *pool,unsigned path,int sy,unsigned offset,unsigned scissor)
{
    /* The same SINGLE bytes/address read VGPR2 in both single-axis modes. The
       asymmetric image and retained state distinguish either stale cache direction. */
    static const uint32_t sequence[]={0x102,0x202,0x102,0x302,0x202};
    setup(x,pool,path,sy,offset,scissor);if(submit(x,NULL))return;
    uint8_t source[VERTICES*16+GUARD],indices[GUARD];memcpy(source,pool+POS,sizeof source);memcpy(indices,pool+INDEX,sizeof indices);
    for(unsigned i=0;i<sizeof sequence/sizeof *sequence;++i){uint32_t mask=sequence[i];initialize(pool);
        pixel(mask,mask,mask==0x302?4:3,0);if(submit(x,NULL))continue; /* Separate retained-state submission. */
        draw(path);++scenarios;if(submit(x,NULL))continue;
        check_target(pool,mask,offset,scissor,1);check_witness(pool,path,1);
        CHECK(memcmp(source,pool+POS,sizeof source)==0);CHECK(memcmp(indices,pool+INDEX,sizeof indices)==0);
    }
}
static void rejected(rt_executor *x,uint8_t *pool,unsigned path,uint32_t enable,uint32_t address,
                     unsigned vgprs,uint32_t barycentric,const char *reason)
{
    setup(x,pool,path,1,1,0);initialize(pool);
    uint8_t source[VERTICES*16+GUARD],indices[GUARD];memcpy(source,pool+POS,sizeof source);memcpy(indices,pool+INDEX,sizeof indices);
    pixel(enable,address,vgprs,barycentric);draw(path);++scenarios;submit(x,reason);
    check_target(pool,address,1,0,0);check_witness(pool,path,0);
    CHECK(memcmp(source,pool+POS,sizeof source)==0);CHECK(memcmp(indices,pool+INDEX,sizeof indices)==0);
}
int main(void)
{
    char path[]="/tmp/paleblood-metal-window-position-XXXXXX",error[1024]="";int fd=mkstemp(path);
    if(fd<0){perror("mkstemp");return 1;}unlink(path);
    if(ftruncate(fd,POOL)){perror("ftruncate");close(fd);return 1;}
    uint8_t *pool=mmap(NULL,POOL,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    if(pool==MAP_FAILED){perror("mmap");close(fd);return 1;}
    memcpy(pool+VS,position_and_witness,sizeof position_and_witness);
    memcpy(pool+SINGLE,single_coordinate,sizeof single_coordinate);memcpy(pool+BOTH,both_coordinates,sizeof both_coordinates);
    memcpy(pool+CONSTANT,constant_pixel,sizeof constant_pixel);
    rt_executor *x=NULL;rt_executor_memory memory={fd,POOL,base,NULL};int rc=rt_executor_open(&memory,&x,error,sizeof error);
    CHECK(rc==0);if(rc){fprintf(stderr,"open: %s\n",error);munmap(pool,POOL);close(fd);return 1;}
    for(unsigned p=0;p<3;++p)for(int sy=-1;sy<=1;sy+=2)for(unsigned offset=0;offset<2;++offset)
        for(unsigned scissor=0;scissor<2;++scissor)positive(x,pool,p,sy,offset,scissor);
    /* Perspective-centre still reserves two incoming registers even with no
       interpolants and a constant shader that uses only VGPR0. */
    for(unsigned p=0;p<3;++p)for(int sy=-1;sy<=1;sy+=2){setup(x,pool,p,sy,1,0);initialize(pool);
        pixel(2,2,2,0);draw(p);++scenarios;if(!submit(x,NULL)){check_target(pool,2,1,0,1);check_witness(pool,p,1);}}
    /* Unused allocation remains valid; input capacity is a lower bound, not an exact size. */
    const uint32_t allocated_masks[]={0x102,0x202,0x302};
    for(unsigned p=0;p<3;++p)for(int sy=-1;sy<=1;sy+=2)for(unsigned i=0;i<3;++i){
        setup(x,pool,p,sy,1,0);initialize(pool);pixel(allocated_masks[i],allocated_masks[i],8,0);draw(p);++scenarios;
        if(!submit(x,NULL)){check_target(pool,allocated_masks[i],1,0,1);check_witness(pool,p,1);}
    }
    for(unsigned p=0;p<=2;p+=2){
        /* Every other low input bit, including Z/W and front-face, plus a high bit. */
        for(unsigned bit=0;bit<16;++bit)if(bit!=1&&bit!=8&&bit!=9)
            rejected(x,pool,p,0x102|(1u<<bit),0x102|(1u<<bit),4,0,"pixel inputs");
        rejected(x,pool,p,0x80000102u,0x80000102u,4,0,"pixel inputs");
        rejected(x,pool,p,0x102,0x202,4,0,"pixel inputs");
        rejected(x,pool,p,0x302,0x102,4,0,"pixel inputs");
        const uint32_t no_perspective[]={0,0x100,0x200,0x300};
        for(unsigned i=0;i<4;++i)rejected(x,pool,p,no_perspective[i],no_perspective[i],4,0,"pixel inputs");
        const uint32_t barycentric[]={1,1u<<20,0x80000000u};
        for(unsigned i=0;i<3;++i)rejected(x,pool,p,0x102,0x102,3,barycentric[i],"SPI_BARYC_CNTL");
        const uint32_t masks[]={2,0x102,0x202,0x302};
        for(unsigned i=0;i<4;++i)rejected(x,pool,p,masks[i],masks[i],i==0?1:i==3?3:2,0,"incoming inputs require");
    }
    rt_executor_close(x);munmap(pool,POOL);close(fd);
    printf("metal window position: %u scenarios, %u checks, %u failures\n",scenarios,checks,failures);
    return failures?1:0;
}
