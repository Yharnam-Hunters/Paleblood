/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Inspect generated SPIR-V inputs, independently of any host GPU or game shader. */
#include "runtime/gcn_spirv.h"
#include "runtime/spirv.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,failures;
#define CHECK(x) do {++checks;if (!(x)) {++failures;fprintf(stderr,"thread-id check %d: %s\n",__LINE__,#x);}} while (0)
static unsigned input_axes(const uint32_t *words,size_t count)
{
    if (count<5 || words[3]>(1u<<20)) return UINT32_MAX;
    unsigned char *id=calloc(words[3],1);if (!id) return UINT32_MAX;
    uint32_t variable=0;unsigned axes=0;
    for (size_t at=5;at<count;) {
        const uint32_t n=words[at]>>16,op=words[at]&0xffff;
        if (!n || n>count-at) {free(id);return UINT32_MAX;}
        if (op==SPV_OP_DECORATE && n==4 && words[at+2]==SPV_DECORATION_BUILT_IN &&
            words[at+3]==SPV_BUILT_IN_LOCAL_INVOCATION_ID) variable=words[at+1];
        at+=n;
    }
    for (size_t at=5;at<count;) {
        const uint32_t n=words[at]>>16,op=words[at]&0xffff;
        if (op==SPV_OP_LOAD && n==4 && words[at+3]==variable && words[at+2]<words[3]) id[words[at+2]]=1;
        if (op==SPV_OP_COMPOSITE_EXTRACT && n==5 && words[at+3]<words[3] && id[words[at+3]]) {
            if (words[at+4]>2) {free(id);return UINT32_MAX;}
            axes|=1u<<words[at+4];
        }
        at+=n;
    }
    free(id);return variable?axes:UINT32_MAX;
}
int main(void)
{
    const uint32_t end=0xbf810000;uint32_t *legacy=NULL;size_t legacy_count=0;char error[256]="";
    gcn_compute_layout layout={.local_size={2,3,4},.workgroup_id_x_sgpr=-1};
    CHECK(gcn_translate_compute(&end,1,&layout,&legacy,&legacy_count,error,sizeof error)==0);
    CHECK(legacy && input_axes(legacy,legacy_count)==7);
    for (unsigned count=1;count<=3;++count) {
        uint32_t *spirv=NULL;size_t words=0;layout.thread_id_vgprs=count;
        const int result=gcn_translate_compute(&end,1,&layout,&spirv,&words,error,sizeof error);
        CHECK(result==0 && spirv);
        if (spirv) {
            CHECK(input_axes(spirv,words)==(1u<<count)-1);
            if (count==3) CHECK(words==legacy_count && memcmp(spirv,legacy,words*sizeof *spirv)==0);
        }
        free(spirv);
    }
    const unsigned invalid[]={4,UINT32_MAX};
    for (unsigned i=0;i<sizeof invalid/sizeof *invalid;++i) {
        uint32_t *spirv=(void *)1;size_t words=1;layout.thread_id_vgprs=invalid[i];
        CHECK(gcn_translate_compute(&end,1,&layout,&spirv,&words,error,sizeof error)==-1);
        CHECK(!spirv && words==0 && strstr(error,"thread-ID VGPR count"));
    }
    free(legacy);printf("compute thread-ID layouts: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
