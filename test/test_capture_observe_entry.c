/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The production entry assembly, with controlled dispatch and real SysV variadic callees. */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
void rt_capture_entry(void);
void rt_observe_entry(void);
typedef struct {uint64_t a,b;} integers;
typedef struct {double a,b;} vectors;
typedef struct {
    uint64_t gpr[6],rax,hook,caller_rsp,pad;
    unsigned char xmm[8][16];
    uint64_t ret_rax,ret_rdx;
    unsigned char ret_xmm0[16],ret_xmm1[16];
} capture_frame;
_Static_assert(sizeof(capture_frame)==256,"capture frame");
_Static_assert(__builtin_offsetof(capture_frame,xmm)==80,"capture vector offset");
static unsigned checks,failures,capture_calls,observe_calls,replacement_calls;
static int traced;
static const uint64_t args[6]={11,22,33,44,55,66};
#define CHECK(x) do {++checks;if (!(x)) {++failures;fprintf(stderr,"entry check %d: %s\n",__LINE__,#x);}} while (0)
static void dirty_registers(void)
{
    __asm__ volatile("pxor %%xmm0,%%xmm0; pxor %%xmm1,%%xmm1; pxor %%xmm2,%%xmm2; pxor %%xmm3,%%xmm3;"
                     "pxor %%xmm4,%%xmm4; pxor %%xmm5,%%xmm5; pxor %%xmm6,%%xmm6; pxor %%xmm7,%%xmm7;"
                     "xor %%edi,%%edi; xor %%esi,%%esi; xor %%edx,%%edx; xor %%ecx,%%ecx;"
                     "xor %%r8d,%%r8d; xor %%r9d,%%r9d; xor %%eax,%%eax"
        :::"xmm0","xmm1","xmm2","xmm3","xmm4","xmm5","xmm6","xmm7","rdi","rsi","rdx","rcx","r8","r9","rax","cc");
}
static integers replacement(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f,...)
{
    ++replacement_calls;
    CHECK(a==args[0] && b==args[1] && c==args[2] && d==args[3] && e==args[4] && f==args[5]);
    va_list list;va_start(list,f);
    for (unsigned i=0;i<8;++i) CHECK(va_arg(list,double)==(double)i+0.25);
    CHECK(va_arg(list,uint64_t)==UINT64_C(0x1122334455667788));
    CHECK(va_arg(list,uint64_t)==UINT64_C(0x8877665544332211));
    va_end(list);return (integers){777,888};
}
int rt_capture_dispatch(const void *hook,capture_frame *frame)
{
    ++capture_calls;CHECK(frame->hook==(uintptr_t)hook);
    CHECK((frame->rax&255)==8);CHECK(memcmp(frame->gpr,args,sizeof args)==0);
    for (unsigned i=0;i<8;++i) {double x;memcpy(&x,frame->xmm[i],sizeof x);CHECK(x==(double)i+0.25);}
    const uint64_t *stack=(const uint64_t *)(uintptr_t)frame->caller_rsp;
    CHECK(stack[1]==UINT64_C(0x1122334455667788) && stack[2]==UINT64_C(0x8877665544332211));
    frame->ret_rax=123;frame->ret_rdx=456;
    const double x=3.5,y=9.25;memcpy(frame->ret_xmm0,&x,sizeof x);memcpy(frame->ret_xmm1,&y,sizeof y);
    dirty_registers();return traced;
}
void rt_observe_hook_dispatch(const void *hook,const uint64_t *frame)
{
    ++observe_calls;CHECK(frame[7]==(uintptr_t)hook);
    CHECK((frame[6]&255)==8);CHECK(memcmp(frame,args,sizeof args)==0);
    for (unsigned i=0;i<8;++i) {double x;memcpy(&x,(const unsigned char *)frame+64+16*i,sizeof x);CHECK(x==(double)i+0.25);}
    dirty_registers();
}
static void *thunk(unsigned char *out,const void *hook,const void *entry)
{
    /* movabs r11, hook; jmp [rip]; .quad entry */
    out[0]=0x49;out[1]=0xbb;const uint64_t h=(uintptr_t)hook,e=(uintptr_t)entry;
    memcpy(out+2,&h,8);const unsigned char jump[6]={0xff,0x25,0,0,0,0};
    memcpy(out+10,jump,6);memcpy(out+16,&e,8);return out;
}
#define INVOKE(fn) fn(11,22,33,44,55,66,0.25,1.25,2.25,3.25,4.25,5.25,6.25,7.25,UINT64_C(0x1122334455667788),UINT64_C(0x8877665544332211))
int main(void)
{
    alarm(10);const size_t page=(size_t)sysconf(_SC_PAGESIZE);
    unsigned char *code=mmap(NULL,page,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if (code==MAP_FAILED) return 1;
    struct {const void *target;} hook={(const void *)replacement};
    typedef integers (*integer_fn)(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,...);
    typedef vectors (*vector_fn)(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,...);
    integer_fn capture=(integer_fn)thunk(code,&hook,(const void *)rt_capture_entry);
    integer_fn observe=(integer_fn)thunk(code+64,&hook,(const void *)rt_observe_entry);
    integers result=INVOKE(capture);CHECK(result.a==777 && result.b==888 && replacement_calls==1 && capture_calls==1);
    traced=1;result=INVOKE(capture);CHECK(result.a==123 && result.b==456 && replacement_calls==1 && capture_calls==2);
    vector_fn capture_vectors=(vector_fn)(void *)code;
    vectors vector=INVOKE(capture_vectors);CHECK(vector.a==3.5 && vector.b==9.25 && replacement_calls==1 && capture_calls==3);
    result=INVOKE(observe);CHECK(result.a==777 && result.b==888 && replacement_calls==2 && observe_calls==1);
    CHECK(munmap(code,page)==0);
    printf("capture/observe entries: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
