/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Execute real probe stubs; keep the periodic writer controlled for this process-owned test. */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
static unsigned starts,detaches;
static int controlled_create(pthread_t *thread,const pthread_attr_t *attr,void *(*entry)(void *),void *arg)
{
    (void)attr;(void)entry;(void)arg;*thread=pthread_self();++starts;return 0;
}
static int controlled_detach(pthread_t thread) {(void)thread;++detaches;return 0;}
#define pthread_create controlled_create
#define pthread_detach controlled_detach
#include "../runtime/probe.c"
#undef pthread_create
#undef pthread_detach
static unsigned checks,failures;
#define CHECK(x) do {++checks;if (!(x)) {++failures;fprintf(stderr,"probe check %d: %s\n",__LINE__,#x);}} while (0)
int main(void)
{
    alarm(10);const size_t page=(size_t)sysconf(_SC_PAGESIZE);
    unsigned char *image=mmap(NULL,page,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if (image==MAP_FAILED) return 1;
    const unsigned char body[]={0x89,0xf8,0x01,0xf0,0x90,0xc3};
    memcpy(image+0x100,body,sizeof body);image[0]=0x5a;image[page-1]=0xa5;
    int (*call)(int,int)=(int (*)(int,int))(void *)(image+0x100);
    CHECK(call(5,8)==13);
    char path[]="/tmp/paleblood-probes-XXXXXX";const int fd=mkstemp(path);CHECK(fd>=0);if (fd<0) return 1;
    FILE *list=fdopen(fd,"w");CHECK(list!=NULL);if (!list) return 1;
    fprintf(list,"0x%08x 5 89f801f090\n",RT_EBOOT_BASE+0x100);CHECK(fclose(list)==0);
    CHECK(setenv("BB_PROBES",path,1)==0 && setenv("BB_PROBE_OUT","unused-owned-probe-output",1)==0);
    CHECK(rt_probes_install(image,page)==0);
    CHECK(starts==1 && detaches==1 && probe_count==1 && counters && counters[0]==0);
    if (probe_count==1) {
        CHECK(image[0x100]==0xe9 && call(5,8)==13 && counters[0]==1);
        CHECK(call(7,9)==16 && counters[0]==2);
    }
    CHECK(image[0]==0x5a && image[page-1]==0xa5);
    CHECK(unlink(path)==0 && munmap(image,page)==0);
    printf("probe bridge: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
