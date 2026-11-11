/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Owned LLVM Bonaire programs test final executed VM validity independently of
   later EXEC changes. Physical stores are idempotent per-pixel/program witnesses,
   never invocation counts. The literal framebuffer oracle uses no renderer helper.
   No target program, recording, original-side harness or expected failure is used.
   The no-VM case and two opposing non-VM cases exercise translator compatibility,
   not valid GFX7 programs: a DONE colour export requires EXEC to contain validity,
   and the last executed export must have DONE (Sea Islands ISA11.2.1/13.8). */
#include "runtime/render_executor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

enum { POOL=1<<20, VS=0x1000, POS=0x4000, CODE=0x8000, STORES=0x10000,
       COLOR=0x80000, W=16, H=16, PIXELS=W*H, GUARD=64, PROGRAMS=10,
       PRE_MARK=0x13570000, POST_MARK=0x24680000 };
static const uint64_t base=UINT64_C(0x3000000000);
/* EXP reads EXEC asynchronously: every export is drained with expcnt(0) before
   any later EXEC change. llvm-mc -triple=amdgcn -mcpu=bonaire. The prefix computes byte
   offset4*(floor(windowY)*16+floor(windowX)), initializes marker/color registers,
   and physically stores the prefix marker before any EXEC/VM manipulation. */
#define PIXEL_PREFIX \
    0x7e080f02, /* v_cvt_u32_f32_e32 v4, v2 */ \
    0x7e0a0f03, /* v_cvt_u32_f32_e32 v5, v3 */ \
    0x340a0a84, /* v_lshlrev_b32_e32 v5, 4, v5 */ \
    0x4a080b04, /* v_add_i32_e32 v4, vcc, v4, v5 */ \
    0x34080882, /* v_lshlrev_b32_e32 v4, 2, v4 */ \
    0x7e0c0209, /* v_mov_b32_e32 v6, s9 */ \
    0x7e0e020a, /* v_mov_b32_e32 v7, s10 */ \
    0x7e1002ff,0x3e800000, /* v_mov_b32_e32 v8, 0.25 */ \
    0xe0701000,0x80000604 /* buffer_store_dword v6, v4, s[0:3], 0 offen */
static const uint32_t position_vertex[]={
    0xebf32000,0x80000100, /* tbuffer_load_format_xyzw v[1:4], v0, s[0:3], 0 format:[BUF_DATA_FORMAT_32_32_32_32,BUF_NUM_FORMAT_FLOAT] idxen */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xf80008cf,0x04030201, /* exp pos0, v1, v2, v3, v4 done */
    0xbf8c0f0f, /* s_waitcnt expcnt(0) */
    0xbf810000, /* s_endpgm */
};
static const uint32_t vm_false_true[]={PIXEL_PREFIX,
    0xbefe0480, /* s_mov_b64 exec, 0 */
    0xf800100f,0x08080808, /* exp mrt0, v8, v8, v8, v8 vm */
    0xbf8c0f0f, /* s_waitcnt expcnt(0) */
    0xbefe04c1, /* s_mov_b64 exec, -1 */
    0xf800180f,0x08080808, /* exp mrt0, v8, v8, v8, v8 done vm */
    0xbf8c0f0f, /* s_waitcnt expcnt(0) */
    0xe0701000,0x80010704, /* buffer_store_dword v7, v4, s[4:7], 0 offen */
    0xbf810000, /* s_endpgm */
};
static const uint32_t vm_false_revive_store[]={PIXEL_PREFIX,
    0xbefe0480, /* s_mov_b64 exec, 0 */
    0xf800180f,0x08080808, /* exp mrt0, v8, v8, v8, v8 done vm */
    0xbf8c0f0f, /* s_waitcnt expcnt(0) */
    0xbefe04c1, /* s_mov_b64 exec, -1 */
    0xe0701000,0x80010704, /* buffer_store_dword v7, v4, s[4:7], 0 offen */
    0xbf810000, /* s_endpgm */
};
static const uint32_t vm_true_clear_exec[]={PIXEL_PREFIX,
    0xf800180f,0x08080808, /* exp mrt0, v8, v8, v8, v8 done vm */
    0xbf8c0f0f, /* s_waitcnt expcnt(0) */
    0xbefe0480, /* s_mov_b64 exec, 0 */
    0xe0701000,0x80010704, /* buffer_store_dword v7, v4, s[4:7], 0 offen */
    0xbf810000, /* s_endpgm */
};
static const uint32_t vm_true_false[]={PIXEL_PREFIX,
    0xf800100f,0x08080808, /* exp mrt0, v8, v8, v8, v8 vm */
    0xbf8c0f0f, /* s_waitcnt expcnt(0) */
    0xbefe0480, /* s_mov_b64 exec, 0 */
    0xf800180f,0x08080808, /* exp mrt0, v8, v8, v8, v8 done vm */
    0xbf8c0f0f, /* s_waitcnt expcnt(0) */
    0xbefe04c1, /* s_mov_b64 exec, -1 */
    0xe0701000,0x80010704, /* buffer_store_dword v7, v4, s[4:7], 0 offen */
    0xbf810000, /* s_endpgm */
};
static const uint32_t vm_branch_exits[]={PIXEL_PREFIX,
    0xbf068008, /* s_cmp_eq_u32 s8, 0 */
    0xbf850008, /* s_cbranch_scc1 .Lvalid_exit */
    0xbefe0480, /* s_mov_b64 exec, 0 */
    0xf800180f,0x08080808, /* exp mrt0, v8, v8, v8, v8 done vm */
    0xbf8c0f0f, /* s_waitcnt expcnt(0) */
    0xbefe04c1, /* s_mov_b64 exec, -1 */
    0xe0701000,0x80010704, /* buffer_store_dword v7, v4, s[4:7], 0 offen */
    0xbf810000, /* s_endpgm */
    0xf800180f,0x08080808, /* exp mrt0, v8, v8, v8, v8 done vm */
    0xbf8c0f0f, /* s_waitcnt expcnt(0) */
    0xbefe0480, /* s_mov_b64 exec, 0 */
    0xe0701000,0x80010704, /* buffer_store_dword v7, v4, s[4:7], 0 offen */
    0xbf810000, /* s_endpgm */
};
static const uint32_t vm_one_valid[]={PIXEL_PREFIX,
    0xf800180f,0x08080808, /* exp mrt0, v8, v8, v8, v8 done vm */
    0xbf8c0f0f, /* s_waitcnt expcnt(0) */
    0xe0701000,0x80010704, /* buffer_store_dword v7, v4, s[4:7], 0 offen */
    0xbf810000, /* s_endpgm */
};
static const uint32_t vm_one_discard[]={PIXEL_PREFIX,
    0x7c26040b, /* v_cmpx_le_f32_e32 vcc, s11, v2 */
    0xf800180f,0x08080808, /* exp mrt0, v8, v8, v8, v8 done vm */
    0xbf8c0f0f, /* s_waitcnt expcnt(0) */
    0xe0701000,0x80010704, /* buffer_store_dword v7, v4, s[4:7], 0 offen */
    0xbf810000, /* s_endpgm */
};
static const uint32_t vm_no_valid_mask[]={PIXEL_PREFIX,
    0xf800080f,0x08080808, /* exp mrt0, v8, v8, v8, v8 done */
    0xbf8c0f0f, /* s_waitcnt expcnt(0) */
    0xbefe0480, /* s_mov_b64 exec, 0 */
    0xe0701000,0x80010704, /* buffer_store_dword v7, v4, s[4:7], 0 offen */
    0xbf810000, /* s_endpgm */
};
/* A later non-VM export leaves the preceding VM validity unchanged. */
static const uint32_t vm_valid_then_nonvm_inactive[]={PIXEL_PREFIX,
    0xf800100f,0x08080808, /* exp mrt0, v8, v8, v8, v8 vm */
    0xbf8c0f0f, /* s_waitcnt expcnt(0) */
    0xbefe0480, /* s_mov_b64 exec, 0 */
    0xf800080f,0x08080808, /* exp mrt0, v8, v8, v8, v8 done */
    0xbf8c0f0f, /* s_waitcnt expcnt(0) */
    0xe0701000,0x80010704, /* buffer_store_dword v7, v4, s[4:7], 0 offen */
    0xbf810000, /* s_endpgm */
};
/* A later non-VM export leaves the preceding VM validity unchanged. */
static const uint32_t vm_invalid_then_nonvm_active[]={PIXEL_PREFIX,
    0xbefe0480, /* s_mov_b64 exec, 0 */
    0xf800100f,0x08080808, /* exp mrt0, v8, v8, v8, v8 vm */
    0xbf8c0f0f, /* s_waitcnt expcnt(0) */
    0xbefe04c1, /* s_mov_b64 exec, -1 */
    0xf800080f,0x08080808, /* exp mrt0, v8, v8, v8, v8 done */
    0xbf8c0f0f, /* s_waitcnt expcnt(0) */
    0xe0701000,0x80010704, /* buffer_store_dword v7, v4, s[4:7], 0 offen */
    0xbf810000, /* s_endpgm */
};
#undef PIXEL_PREFIX

typedef struct {const char *name;const uint32_t *words;size_t count;} Program;
#define PROGRAM(x) {#x,x,sizeof x/sizeof *x}
static const Program programs[PROGRAMS]={PROGRAM(vm_false_true),PROGRAM(vm_false_revive_store),
    PROGRAM(vm_true_clear_exec),PROGRAM(vm_true_false),PROGRAM(vm_branch_exits),
    PROGRAM(vm_one_valid),PROGRAM(vm_one_discard),PROGRAM(vm_no_valid_mask),
    PROGRAM(vm_valid_then_nonvm_inactive),PROGRAM(vm_invalid_then_nonvm_active)};
enum { NONE, ALL, RIGHT };
typedef struct {unsigned program,selector,color,post;} Case;
static const Case cases[]={
    {0,0,ALL,ALL}, {1,0,NONE,ALL}, {2,0,ALL,NONE}, {3,0,NONE,ALL},
    {4,0,ALL,NONE}, {4,1,NONE,ALL}, {5,0,ALL,ALL}, {6,0,RIGHT,RIGHT}, {7,0,ALL,NONE},
    {8,0,ALL,NONE}, {9,0,NONE,ALL},
};
static uint8_t color_expected[PIXELS*4+2*GUARD];
static uint8_t store_expected[PROGRAMS][2][PIXELS*4+2*GUARD];
static unsigned checks,failures,scenarios,epoch,current_program,current_selector;
static int current_sy;static unsigned current_partial;
#define CHECK(x) do{++checks;if(!(x)){++failures;if(failures<=20)fprintf(stderr,"%s selector%u Y%d partial%u line%d: %s\n", \
    programs[current_program].name,current_selector,current_sy,current_partial,__LINE__,#x);}}while(0)
static rt_render_call calls[64];static size_t count;
static uint32_t bits(float value){uint32_t word;memcpy(&word,&value,4);return word;}
static unsigned bank(unsigned program,unsigned which){return STORES+program*0x2000+which*0x1000;}
static rt_render_call *event(rt_render_call_kind kind)
{
    if(count>=64){fprintf(stderr,"event capacity exceeded\n");exit(1);}
    rt_render_call *c=&calls[count++];memset(c,0,sizeof *c);c->kind=kind;return c;
}
static void descriptor(uint32_t *words,unsigned offset,unsigned stride,unsigned records)
{
    uint64_t address=base+offset;words[0]=(uint32_t)address;
    words[1]=(uint32_t)(address>>32)|stride<<16;words[2]=records;words[3]=0;
}
static void initialize(uint8_t *pool)
{
    ++epoch;
    for(size_t i=0;i<sizeof color_expected;++i)color_expected[i]=(uint8_t)(0x53+i*37+(i>>3)*11+epoch*19);
    memcpy(pool+COLOR-GUARD,color_expected,sizeof color_expected);
    for(unsigned p=0;p<PROGRAMS;++p)for(unsigned b=0;b<2;++b){
        for(size_t i=0;i<sizeof store_expected[p][b];++i)
            store_expected[p][b][i]=(uint8_t)(0x61+i*29+(i>>4)*13+epoch*17+p*23+b*7);
        memcpy(pool+bank(p,b)-GUARD,store_expected[p][b],sizeof store_expected[p][b]);
    }
}
static void setup(uint8_t *pool,const Case *test,int sy,unsigned partial)
{
    current_program=test->program;current_selector=test->selector;current_sy=sy;current_partial=partial;
    initialize(pool);count=0;memset(pool+POS,0x97,48+GUARD);
    const float positions[3][4]={{-1,-1,.25f,1},{3,-1,.25f,1},{-1,3,.25f,1}};
    memcpy(pool+POS,positions,sizeof positions);
    rt_render_call *c=event(RT_CALL_COLOR_TARGET);c->u.color_target.enabled=1;
    c->u.color_target.target=(rt_color_target){.address=base+COLOR,.pitch=W,.height=H,
        .format=0xa,.tile_mode_index=31,.samples=1};
    c=event(RT_CALL_VIEWPORT);c->u.viewport.zmax=1;c->u.viewport.scale[0]=partial?4.5f:8;
    c->u.viewport.scale[1]=(float)sy*(partial?2.5f:8);c->u.viewport.scale[2]=1;
    c->u.viewport.translate[0]=partial?7.5f:8;c->u.viewport.translate[1]=partial?7.5f:8;
    c=event(RT_CALL_VERTEX_SHADER);c->u.vertex_shader=(rt_vertex_shader){.code=base+VS,
        .vgprs=8,.sgprs=8,.user_sgprs=4,.parameter_count=0,.position_format=4};
    c=event(RT_CALL_PIXEL_SHADER);c->u.pixel_shader=(rt_pixel_shader){.code=base+CODE+test->program*0x100,
        .vgprs=12,.sgprs=16,.user_sgprs=12,.color_formats=9,.input_enable=0x302,.input_address=0x302,
        .interpolated=0,.depth_control=0,.color_mask=15};
    c=event(RT_CALL_USER_DATA);c->u.user_data.stage=RT_STAGE_VERTEX;c->u.user_data.count=4;
    descriptor(c->u.user_data.words,POS,16,3);
    c=event(RT_CALL_USER_DATA);c->u.user_data.stage=RT_STAGE_PIXEL;c->u.user_data.count=8;
    descriptor(c->u.user_data.words,bank(test->program,0),4,PIXELS);
    descriptor(c->u.user_data.words+4,bank(test->program,1),4,PIXELS);
    c=event(RT_CALL_USER_DATA);c->u.user_data.stage=RT_STAGE_PIXEL;c->u.user_data.slot=8;c->u.user_data.count=4;
    c->u.user_data.words[0]=test->selector;c->u.user_data.words[1]=PRE_MARK+test->program;
    c->u.user_data.words[2]=POST_MARK+test->program;c->u.user_data.words[3]=bits(partial?7:8);
    c=event(RT_CALL_REGISTER);c->u.reg.address=0xc242;c->u.reg.value=4;
    c=event(RT_CALL_DRAW_AUTO);c->u.draw_auto.index_count=3;
}
static void word(uint8_t *plane,unsigned pixel,uint32_t value){memcpy(plane+GUARD+pixel*4,&value,4);}
static int covered(unsigned x,unsigned y,unsigned partial)
{
    /* Odd viewport [3,12)x[5,10) leaves all neighbouring helper coordinates
       inside the16x16 physical buffers. Helpers must not create observable stores. */
    return !partial||(x>=3&&x<12&&y>=5&&y<10);
}
static int observed(unsigned mode,unsigned x,unsigned partial)
{
    return mode==ALL||(mode==RIGHT&&x>=(partial?7u:8u));
}
static void check_buffers(const uint8_t *pool)
{
    for(unsigned p=0;p<PROGRAMS;++p)for(unsigned b=0;b<2;++b)
        for(size_t i=0;i<sizeof store_expected[p][b];++i)
            CHECK(pool[bank(p,b)-GUARD+i]==store_expected[p][b][i]);
}
static void scenario(rt_executor *x,uint8_t *pool,const Case *test,int sy,unsigned partial)
{
    setup(pool,test,sy,partial);uint8_t source[48+GUARD];memcpy(source,pool+POS,sizeof source);
    char error[1024]="";int rc=rt_executor_run_calls(x,calls,count,error,sizeof error);++scenarios;
    if(rc)fprintf(stderr,"execute %s: %s\n",programs[test->program].name,error);
    CHECK(rc==0);
    if(rc)return;
    for(unsigned y=0;y<H;++y)for(unsigned col=0;col<W;++col){unsigned i=y*W+col;
        if(covered(col,y,partial)){
            word(store_expected[test->program][0],i,PRE_MARK+test->program);
            if(observed(test->post,col,partial))word(store_expected[test->program][1],i,POST_MARK+test->program);
        }
        for(unsigned component=0;component<4;++component){unsigned byte=i*4+component;
            if(covered(col,y,partial)&&observed(test->color,col,partial)){
                int delta=(int)pool[COLOR+byte]-64;CHECK(delta>=-1&&delta<=1);
            }else CHECK(pool[COLOR+byte]==color_expected[GUARD+byte]);
        }
    }
    for(unsigned i=0;i<GUARD;++i){CHECK(pool[COLOR-GUARD+i]==color_expected[i]);
        CHECK(pool[COLOR+PIXELS*4+i]==color_expected[GUARD+PIXELS*4+i]);}
    check_buffers(pool);CHECK(memcmp(source,pool+POS,sizeof source)==0);
}
int main(void)
{
    char path[]="/tmp/paleblood-pixel-vm-XXXXXX",error[1024]="";int fd=mkstemp(path);
    if(fd<0){perror("mkstemp");return 1;}unlink(path);
    if(ftruncate(fd,POOL)){perror("ftruncate");close(fd);return 1;}
    uint8_t *pool=mmap(NULL,POOL,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    if(pool==MAP_FAILED){perror("mmap");close(fd);return 1;}
    memcpy(pool+VS,position_vertex,sizeof position_vertex);
    for(unsigned p=0;p<PROGRAMS;++p){CHECK(programs[p].count*4<=0x100);
        memcpy(pool+CODE+p*0x100,programs[p].words,programs[p].count*4);}
    rt_executor *x=NULL;const rt_executor_memory memory={fd,POOL,base};
    int rc=rt_executor_open(&memory,&x,error,sizeof error);CHECK(rc==0);
    if(rc){fprintf(stderr,"open: %s\n",error);munmap(pool,POOL);close(fd);return 1;}
    for(int sy=-1;sy<=1;sy+=2)for(unsigned partial=0;partial<2;++partial)
        for(size_t i=0;i<sizeof cases/sizeof *cases;++i)scenario(x,pool,&cases[i],sy,partial);
    rt_executor_close(x);munmap(pool,POOL);close(fd);
    printf("pixel final VM: %u scenarios, %u checks, %u failures\n",scenarios,checks,failures);
    return failures?1:0;
}
