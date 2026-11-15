/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "runtime/capture_stack.h"
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
static unsigned checks,failures;
#define CHECK(x) do {++checks;if (!(x)) {++failures;fprintf(stderr,"stack check %d: %s\n",__LINE__,#x);}} while (0)
static void check_words(uint64_t caller,const uint64_t *expected,size_t count)
{
    struct {uint64_t before,words[RT_TRACE_STACK_SLOTS],after;} out;
    memset(&out,0xa5,sizeof out);out.before=11;out.after=22;
    rt_capture_copy_stack(caller,out.words);
    CHECK(out.before==11 && out.after==22);
    for (size_t i=0;i<RT_TRACE_STACK_SLOTS;++i) CHECK(out.words[i]==(i<count?expected[i]:0));
}
int main(void)
{
    alarm(10);
    const size_t page=(size_t)sysconf(_SC_PAGESIZE);
    unsigned char *base=mmap(NULL,3*page,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    CHECK(base!=MAP_FAILED);if (base==MAP_FAILED) return 1;
    CHECK(mprotect(base+page,page,PROT_READ|PROT_WRITE)==0);
    uint64_t *words=(uint64_t *)(void *)(base+page);
    const size_t count=page/sizeof(uint64_t);
    for (size_t i=0;i<count;++i) words[i]=0xabc000+i;
    check_words((uintptr_t)words,words+1,RT_TRACE_STACK_SLOTS);
    /* Only complete words before a guard page are inputs, including the zero-word case. */
    for (size_t available=0;available<=8;++available) {
        uint64_t *caller=words+count-available-1;
        check_words((uintptr_t)caller,caller+1,available);
    }
    check_words((uintptr_t)base,NULL,0);
    check_words(UINT64_MAX,NULL,0);
    check_words(UINT64_MAX-7,NULL,0);
    /* An incomplete word must not leak from a failed read into the zero-filled result. */
    check_words((uintptr_t)(base+2*page-12),NULL,0);
    CHECK(munmap(base,3*page)==0);
    printf("capture stack: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
