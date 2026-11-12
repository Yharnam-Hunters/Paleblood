/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Owned uniform, zero-LDS s_barrier admission witnesses. These prove neither
   LDS visibility nor general divergent-workgroup synchronization. */
#include "runtime/render_executor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

enum { POOL = 64 << 10, CODE = 0x1000, OUTPUT = 0x2000,
       WORDS = 256, GUARD = 64, CANARY = 0xa5, PLANNED_CASES = 7 };
static const uint64_t guest_base = UINT64_C(0x3000000000);
static const uint32_t marker_base = UINT32_C(0x13570000);
static unsigned scenarios,checks,failures;
static const char *context = "setup";
#define CHECK(value) do { ++checks; if (!(value)) { ++failures; if (failures <= 24) \
    fprintf(stderr,"%s:%d [%s]: %s\n",__FILE__,__LINE__,context,#value); } } while (0)
/* Owned LLVM Bonaire bytes: uniformly reached barrier precedes every store.
   Index=x+8*y+64*z, marker=0x13570000|index. */
static const uint32_t barrier_markers[] = {
    0xbf8a0000, /* s_barrier */
    0x34060283, /* v_lshlrev_b32_e32 v3, 3, v1 */
    0x4a060700, /* v_add_i32_e32 v3, vcc, v0, v3 */
    0x34080486, /* v_lshlrev_b32_e32 v4, 6, v2 */
    0x4a060704, /* v_add_i32_e32 v3, vcc, v4, v3 */
    0x380806ff, 0x13570000, /* v_or_b32_e32 v4, 0x13570000, v3 */
    0x34060682, /* v_lshlrev_b32_e32 v3, 2, v3 */
    0xe0701000, 0x80000403, /* buffer_store_dword v4, v3, s[0:3], 0 offen */
    0xbf8c0f70, /* s_waitcnt vmcnt(0) */
    0xbf810000, /* s_endpgm */
};
typedef struct { const char *name; unsigned x,y,z; int admitted,sparse; } Case;
static const Case cases[] = {
    {"full64-64x1x1",64,1,1,1,0},
    {"full64-8x8x1",8,8,1,1,0},
    {"full64-4x4x4",4,4,4,1,1},
    {"partial-1x1x1",1,1,1,0,0},
    {"partial-7x1x1",7,1,1,0,0},
    {"partial-32x1x1",32,1,1,0,0},
    {"partial-63x1x1",63,1,1,0,0}
};
/* Literal sparse positions for4x4x4; the other complete shapes use0..63.
   This table does not call or mirror a translator's coordinate helper. */
static const unsigned sparse_indices[64] = {
    0,1,2,3,8,9,10,11,16,17,18,19,24,25,26,27,
    64,65,66,67,72,73,74,75,80,81,82,83,88,89,90,91,
    128,129,130,131,136,137,138,139,144,145,146,147,152,153,154,155,
    192,193,194,195,200,201,202,203,208,209,210,211,216,217,218,219
};
static uint64_t at(unsigned offset) { return guest_base + offset; }
static uint32_t word_at(const unsigned char *p) { uint32_t word; memcpy(&word,p,4); return word; }
static void put_word(unsigned char *p,uint32_t word) { memcpy(p,&word,4); }
static void run_case(const Case *c)
{
    context=c->name;++scenarios;
    char path[]="/tmp/paleblood-metal-barriers-XXXXXX",error[512]="";
    int fd=mkstemp(path);if(fd<0){perror("mkstemp");exit(1);}
    if(unlink(path)||ftruncate(fd,POOL)){perror("pool");close(fd);exit(1);}
    unsigned char *pool=mmap(NULL,POOL,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    if(pool==MAP_FAILED){perror("mmap");close(fd);exit(1);}
    memset(pool,CANARY,POOL);memcpy(pool+CODE,barrier_markers,sizeof barrier_markers);
    unsigned char *before=malloc(POOL),*expected=malloc(POOL);
    CHECK(before!=NULL&&expected!=NULL);if(!before||!expected){exit(1);}
    memcpy(before,pool,POOL);memcpy(expected,before,POOL);
    if(c->admitted)for(unsigned lane=0;lane<64;++lane){
        const unsigned index=c->sparse?sparse_indices[lane]:lane;
        CHECK(index<WORDS);put_word(expected+OUTPUT+4*index,marker_base|index);
    }
    rt_executor *executor=NULL;
    const rt_executor_memory memory={fd,POOL,guest_base,NULL,NULL,NULL};
    const int opened=rt_executor_open(&memory,&executor,error,sizeof error);CHECK(close(fd)==0);CHECK(opened==0);
    if(opened){fprintf(stderr,"[%s] open: %s\n",context,error);munmap(pool,POOL);free(before);free(expected);exit(1);}
    rt_render_call calls[3]={{0}};
    calls[0].kind=RT_CALL_COMPUTE_SHADER;
    calls[0].u.compute_shader=(rt_compute_shader){.code=at(CODE),.vgprs=8,.sgprs=8,.user_sgprs=4,
        .thread_id_components=2,.threads={c->x,c->y,c->z}};
    calls[1].kind=RT_CALL_USER_DATA;calls[1].u.user_data.stage=RT_STAGE_COMPUTE;calls[1].u.user_data.count=4;
    calls[1].u.user_data.words[0]=(uint32_t)at(OUTPUT);calls[1].u.user_data.words[1]=(uint32_t)(at(OUTPUT)>>32);
    calls[1].u.user_data.words[2]=WORDS*4;
    calls[2].kind=RT_CALL_DISPATCH;
    calls[2].u.dispatch.groups[0]=calls[2].u.dispatch.groups[1]=calls[2].u.dispatch.groups[2]=1;
    const int result=rt_executor_run_calls(executor,calls,3,error,sizeof error);
    if(c->admitted){if(result){fprintf(stderr,"[%s] execute: %s\n",context,error);}CHECK(result==0);}
    else{
        static const char reason[]="partial Metal compute workgroups do not support workgroup barriers";
        if(result!=-1||!strstr(error,reason)){fprintf(stderr,"[%s] expected %s, got%d: %s\n",context,reason,result,error);}
        CHECK(result==-1&&strstr(error,reason));
    }
    unsigned observed_markers=0;
    for(unsigned i=0;i<WORDS;++i){
        const uint32_t actual=word_at(pool+OUTPUT+4*i);
        if(actual==(marker_base|i)){++observed_markers;}
        CHECK(actual==word_at(expected+OUTPUT+4*i));
    }
    /* Every pool byte outside the output is unchanged, including code and both
       output guards. For a refusal all256 output words also remain untouched. */
    for(unsigned i=0;i<POOL;++i)if(i<OUTPUT||i>=OUTPUT+WORDS*4){CHECK(pool[i]==before[i]);}
    for(unsigned i=0;i<GUARD;++i){CHECK(pool[OUTPUT-GUARD+i]==CANARY);CHECK(pool[OUTPUT+WORDS*4+i]==CANARY);}
    CHECK(observed_markers==(c->admitted?64u:0u));
    printf("metal compute barrier case=%s result=%d marker_words=%u\n",context,result,observed_markers);
    rt_executor_close(executor);CHECK(munmap(pool,POOL)==0);free(before);free(expected);
}
int main(void)
{
    CHECK(sizeof cases/sizeof *cases==PLANNED_CASES);
    CHECK(sizeof barrier_markers<=OUTPUT-CODE-GUARD);
    CHECK(sizeof sparse_indices/sizeof *sparse_indices==64);
    for(unsigned i=0;i<64;++i){CHECK(sparse_indices[i]<WORDS);for(unsigned j=0;j<i;++j){CHECK(sparse_indices[i]!=sparse_indices[j]);}}
    for(unsigned i=0;i<sizeof cases/sizeof *cases;++i){run_case(cases+i);}
    context="complete";CHECK(scenarios==PLANNED_CASES);
    printf("metal compute barriers: %u scenarios, %u checks, %u failures\n",scenarios,checks,failures);
    return failures?1:0;
}
