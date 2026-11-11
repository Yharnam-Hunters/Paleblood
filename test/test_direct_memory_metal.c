/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Real renderer input from the runtime pool, beyond the 32-bit file-offset boundary. */
#include "runtime/syslib.h"
#include "runtime/gpu_memory.h"
#include "runtime/render_executor.h"
#include "gcn_programs.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,failures;
#define CHECK(value) do {++checks;if (!(value)){++failures;fprintf(stderr,"%d: %s\n",__LINE__,#value);}} while (0)
static void *find(const char *symbol)
{
    for (const rt_syslib_entry *e=rt_syslib_begin();e && e!=rt_syslib_end();++e)
        if (!strcmp(e->library,"libkernel") && !strcmp(e->symbol,symbol)) return e->fn;
    abort();
}
static void buffer(uint32_t *words,uint64_t address)
{
    words[0]=(uint32_t)address;words[1]=(uint32_t)(address>>32);words[2]=64*4;words[3]=0;
}
int main(void)
{
    typedef int32_t (*Allocate)(int64_t,int64_t,uint64_t,uint64_t,int32_t,int64_t *);
    typedef int32_t (*Map)(void **,uint64_t,int32_t,int32_t,int64_t,uint64_t);
    typedef int32_t (*Unmap)(void *,uint64_t);
    typedef int32_t (*Release)(int64_t,uint64_t);
    Allocate allocate=(Allocate)find("sceKernelAllocateDirectMemory");Map map=(Map)find("sceKernelMapDirectMemory");
    Unmap unmap=(Unmap)find("sceKernelMunmap");Release release=(Release)find("sceKernelReleaseDirectMemory");
    enum {PAGE=0x4000,LENGTH=0x40000,PROGRAM=0x1000,INPUT=0x10000,OUTPUT=0x20000};
    const uint64_t start=(UINT64_C(1)<<32)+PAGE;int64_t offset=-1;
    CHECK(allocate((int64_t)start,(int64_t)(start+LENGTH),LENGTH,PAGE,0,&offset)==0 && offset==(int64_t)start);
    unsigned char *gpu=NULL,*cpu=NULL;CHECK(map((void **)&gpu,LENGTH,0x33,0,offset,PAGE)==0);
    CHECK(map((void **)&cpu,LENGTH,3,0,offset,PAGE)==0);CHECK((uintptr_t)gpu==rt_gpu_memory_base()+start);
    int fd=-1;uint64_t size=0;CHECK(rt_gpu_memory_pool(&fd,&size)==0 && size==(UINT64_C(5)<<30));
    rt_executor *executor=NULL;char error[512]="";rt_executor_memory memory={fd,size,rt_gpu_memory_base()};
    const int opened=rt_executor_open(&memory,&executor,error,sizeof error);if(opened)fprintf(stderr,"runtime pool on Metal: %s\n",error);
    CHECK(opened==0);if(!executor)return 1;
    memcpy(cpu+PROGRAM,scale_add,sizeof scale_add);
    rt_render_call calls[3]={{0}};
    calls[0].kind=RT_CALL_COMPUTE_SHADER;calls[0].u.compute_shader=(rt_compute_shader){.code=(uintptr_t)gpu+PROGRAM,.vgprs=12,.sgprs=16,.user_sgprs=8,.threads={64,1,1},.thread_id_components=2};
    calls[1].kind=RT_CALL_USER_DATA;calls[1].u.user_data.stage=RT_STAGE_COMPUTE;calls[1].u.user_data.count=8;
    buffer(calls[1].u.user_data.words,(uintptr_t)gpu+INPUT);buffer(calls[1].u.user_data.words+4,(uintptr_t)gpu+OUTPUT);
    calls[2].kind=RT_CALL_DISPATCH;calls[2].u.dispatch.groups[0]=calls[2].u.dispatch.groups[1]=calls[2].u.dispatch.groups[2]=1;
    for(unsigned round=0;round<2;++round){
        float *input=(float *)(cpu+INPUT),*output=(float *)(cpu+OUTPUT);
        for(unsigned i=0;i<64;++i){input[i]=(float)(i+2*round);output[i]=-123;}
        const int result=rt_executor_run_calls(executor,calls,3,error,sizeof error);
        if(result)fprintf(stderr,"pool dispatch: %s\n",error);
        CHECK(result==0);
        for(unsigned i=0;i<64;++i)CHECK(output[i]==(float)(i+2*round)*2.5f+1);
    }
    rt_executor_close(executor);CHECK(unmap(cpu,LENGTH)==0);CHECK(unmap(gpu,LENGTH)==0);CHECK(release(offset,LENGTH)==0);
    printf("direct memory Metal: 5 GiB pool, >4 GiB offset, %u checks, %u failures\n",checks,failures);return failures?1:0;
}
