/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Validate the cooperative module contract independently of a Metal device. */
#include "runtime/gcn_spirv.h"
#include "runtime/spirv.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "metal_wave_programs.h"
static unsigned checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; fprintf(stderr,"line %d: %s\n",__LINE__,#x); } } while (0)
static int translate(const gcn_compute_layout *layout, uint32_t **words, size_t *count)
{
    char error[256] = "";
    return gcn_translate_compute(wave_threshold,sizeof wave_threshold / sizeof *wave_threshold,layout,words,count,error,sizeof error);
}
static unsigned inspect(const uint32_t *words, size_t count, unsigned opcode, uint32_t scope, uint32_t semantics)
{
    unsigned found = 0;
    for (size_t at = 5; at < count;) {
        const unsigned length = words[at] >> 16;
        if (!length || length > count-at) { CHECK(0); break; }
        if ((words[at] & 0xffffu) == opcode) {
            ++found;
            if (opcode == SPV_OP_CONTROL_BARRIER) {
                CHECK(length == 4);
                CHECK(words[at+1] == scope && words[at+2] == scope && words[at+3] == semantics);
            }
            if (opcode == SPV_OP_ATOMIC_EXCHANGE) {
                CHECK(length == 7);
                CHECK(words[at+4] == scope && words[at+5] == semantics);
            }
        }
        at += length;
    }
    return found;
}
int main(int argc, char **argv)
{
    gcn_compute_layout layout = {.local_size={64,1,1},.user_sgprs=9,.workgroup_id_enables=0,.error_address=0x1000};
    uint32_t *native=NULL,*cooperative=NULL; size_t native_count=0,count=0;
    CHECK(translate(&layout,&native,&native_count)==0);
    CHECK(native && inspect(native,native_count,SPV_OP_GROUP_NON_UNIFORM_ANY,0,0)>0);
    CHECK(native && inspect(native,native_count,SPV_OP_CONTROL_BARRIER,0,0)==0);
    layout.workgroup_wave_votes=1;
    CHECK(translate(&layout,&cooperative,&count)==0);
    uint32_t scope=0,semantics=0,device=0,relaxed=0;
    for (size_t at=5;cooperative && at<count;at+=cooperative[at]>>16) {
        const unsigned length=cooperative[at]>>16;
        CHECK(length && length<=count-at);
        if (!length || length>count-at) break;
        if ((cooperative[at]&0xffffu)==SPV_OP_CONSTANT && length==4) {
            if (cooperative[at+3]==SPV_SCOPE_DEVICE) device=cooperative[at+2];
            if (cooperative[at+3]==0) relaxed=cooperative[at+2];
            if (cooperative[at+3]==SPV_SCOPE_WORKGROUP) scope=cooperative[at+2];
            if (cooperative[at+3]==SPV_MEMORY_ACQUIRE_RELEASE_WORKGROUP) semantics=cooperative[at+2];
        }
    }
    CHECK(scope && semantics && device && relaxed);
    CHECK(cooperative && inspect(cooperative,count,SPV_OP_ATOMIC_EXCHANGE,device,relaxed)>0);
    CHECK(cooperative && inspect(cooperative,count,SPV_OP_GROUP_NON_UNIFORM_ANY,0,0)==0);
    CHECK(cooperative && inspect(cooperative,count,SPV_OP_CONTROL_BARRIER,scope,semantics)>=6);
    if (argc==2 && cooperative) {
        FILE *out=fopen(argv[1],"wb");CHECK(out);
        if (out) { CHECK(fwrite(cooperative,sizeof *cooperative,count,out)==count);CHECK(fclose(out)==0); }
    }
    free(native);free(cooperative);
    const uint32_t invalid[][3]={{0,1,64},{1,1,1},{32,1,1},{63,1,1},{65,1,1},{UINT32_MAX,UINT32_MAX,UINT32_MAX}};
    for (unsigned i=0;i<sizeof invalid/sizeof *invalid;++i) {
        memcpy(layout.local_size,invalid[i],sizeof layout.local_size);
        cooperative=NULL;count=0;CHECK(translate(&layout,&cooperative,&count)==-1);CHECK(!cooperative && count==0);
    }
    layout.local_size[0]=64;layout.local_size[1]=layout.local_size[2]=1;
    layout.error_address=0;CHECK(translate(&layout,&cooperative,&count)==-1);CHECK(!cooperative && count==0);
    layout.error_address=0x1000;layout.workgroup_wave_votes=2;
    CHECK(translate(&layout,&cooperative,&count)==-1);CHECK(!cooperative && count==0);
    printf("compute workgroup votes: %u checks, %u failures\n",checks,failures);
    return failures ? 1 : 0;
}
