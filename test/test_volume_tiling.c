/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Owned CPU oracle from pinned AMD PAL7ce51b19 ADDR_THICK bit tables and
   microtile/group strides. Derived without reading volume_tiling.c. This tests
   the explicitly named CPU profile, not a console's programmed tile-mode table.
   Copy expectations never call the production layout/offset/copy functions. */
#include "runtime/volume_tiling.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { GUARD=64 };
static unsigned checks,failures,admissions,refusals,copies,permutations;
#define CHECK(x) do{++checks;if(!(x)){++failures;if(failures<=20)fprintf(stderr,"line%d: %s\n",__LINE__,#x);}}while(0)
#define COUNT(a) (sizeof(a)/sizeof *(a))
/* Linear microtile index is64*z+8*y+x. Entries are literal physical element
   indices. Low-to-high address bits from Lib::ComputePixelIndexWithinMicroTile:
   BPE1/2: x0 y0 x1 y1 z0 z1 x2 y2; BPE4: x0 y0 x1 z0 y1 z1 x2 y2;
   BPE8/16: x0 y0 z0 x1 y1 z1 x2 y2. */
static const uint8_t microtile[3][256]={
    { /* 1/2-byte elements */
        0,1,4,5,64,65,68,69,2,3,6,7,66,67,70,71,
        8,9,12,13,72,73,76,77,10,11,14,15,74,75,78,79,
        128,129,132,133,192,193,196,197,130,131,134,135,194,195,198,199,
        136,137,140,141,200,201,204,205,138,139,142,143,202,203,206,207,
        16,17,20,21,80,81,84,85,18,19,22,23,82,83,86,87,
        24,25,28,29,88,89,92,93,26,27,30,31,90,91,94,95,
        144,145,148,149,208,209,212,213,146,147,150,151,210,211,214,215,
        152,153,156,157,216,217,220,221,154,155,158,159,218,219,222,223,
        32,33,36,37,96,97,100,101,34,35,38,39,98,99,102,103,
        40,41,44,45,104,105,108,109,42,43,46,47,106,107,110,111,
        160,161,164,165,224,225,228,229,162,163,166,167,226,227,230,231,
        168,169,172,173,232,233,236,237,170,171,174,175,234,235,238,239,
        48,49,52,53,112,113,116,117,50,51,54,55,114,115,118,119,
        56,57,60,61,120,121,124,125,58,59,62,63,122,123,126,127,
        176,177,180,181,240,241,244,245,178,179,182,183,242,243,246,247,
        184,185,188,189,248,249,252,253,186,187,190,191,250,251,254,255,
    },
    { /* 4-byte elements */
        0,1,4,5,64,65,68,69,2,3,6,7,66,67,70,71,
        16,17,20,21,80,81,84,85,18,19,22,23,82,83,86,87,
        128,129,132,133,192,193,196,197,130,131,134,135,194,195,198,199,
        144,145,148,149,208,209,212,213,146,147,150,151,210,211,214,215,
        8,9,12,13,72,73,76,77,10,11,14,15,74,75,78,79,
        24,25,28,29,88,89,92,93,26,27,30,31,90,91,94,95,
        136,137,140,141,200,201,204,205,138,139,142,143,202,203,206,207,
        152,153,156,157,216,217,220,221,154,155,158,159,218,219,222,223,
        32,33,36,37,96,97,100,101,34,35,38,39,98,99,102,103,
        48,49,52,53,112,113,116,117,50,51,54,55,114,115,118,119,
        160,161,164,165,224,225,228,229,162,163,166,167,226,227,230,231,
        176,177,180,181,240,241,244,245,178,179,182,183,242,243,246,247,
        40,41,44,45,104,105,108,109,42,43,46,47,106,107,110,111,
        56,57,60,61,120,121,124,125,58,59,62,63,122,123,126,127,
        168,169,172,173,232,233,236,237,170,171,174,175,234,235,238,239,
        184,185,188,189,248,249,252,253,186,187,190,191,250,251,254,255,
    },
    { /* 8/16-byte elements */
        0,1,8,9,64,65,72,73,2,3,10,11,66,67,74,75,
        16,17,24,25,80,81,88,89,18,19,26,27,82,83,90,91,
        128,129,136,137,192,193,200,201,130,131,138,139,194,195,202,203,
        144,145,152,153,208,209,216,217,146,147,154,155,210,211,218,219,
        4,5,12,13,68,69,76,77,6,7,14,15,70,71,78,79,
        20,21,28,29,84,85,92,93,22,23,30,31,86,87,94,95,
        132,133,140,141,196,197,204,205,134,135,142,143,198,199,206,207,
        148,149,156,157,212,213,220,221,150,151,158,159,214,215,222,223,
        32,33,40,41,96,97,104,105,34,35,42,43,98,99,106,107,
        48,49,56,57,112,113,120,121,50,51,58,59,114,115,122,123,
        160,161,168,169,224,225,232,233,162,163,170,171,226,227,234,235,
        176,177,184,185,240,241,248,249,178,179,186,187,242,243,250,251,
        36,37,44,45,100,101,108,109,38,39,46,47,102,103,110,111,
        52,53,60,61,116,117,124,125,54,55,62,63,118,119,126,127,
        164,165,172,173,228,229,236,237,166,167,174,175,230,231,238,239,
        180,181,188,189,244,245,252,253,182,183,190,191,246,247,254,255,
    },
};
typedef struct {uint32_t pitch,width,height,depth,ph,pd;int pow2;} Shape;
static const Shape shapes[]={
    {8,1,1,1,8,4,0}, {24,13,11,7,16,8,0},
    {32,19,9,5,16,8,0}, {32,16,8,4,8,4,1},
};
static unsigned family(unsigned bytes){return bytes<=2?0:bytes==4?1:2;}
/* Outer tile rows/groups use the independent physical Shape literals, never
   fields returned by the helper. Inside a brick use only the literal table. */
static uint64_t primary_offset(const Shape *s,unsigned bytes,uint32_t x,uint32_t y,uint32_t z)
{
    uint64_t tile=(uint64_t)(z/4)*(s->pitch/8)*(s->ph/8)+(uint64_t)(y/8)*(s->pitch/8)+x/8;
    return (tile*256+microtile[family(bytes)][64*(z%4)+8*(y%8)+x%8])*bytes;
}
static uint64_t span(const Shape *s,unsigned bytes){return (uint64_t)s->pitch*s->ph*s->pd*bytes;}
static int admit(Shape s,unsigned bytes,rt_volume_tiling *t)
{
    char error[160]="";memset(t,0xa5,sizeof *t);++admissions;
    int rc=rt_volume_layout(19,bytes,s.pitch,s.width,s.height,s.depth,s.pow2,t,error,sizeof error);
    if(rc){fprintf(stderr,"admit %u-byte %ux%ux%u pitch%u: %s\n",bytes,s.width,s.height,s.depth,s.pitch,error);}
    CHECK(rc==0);
    if(rc){return 0;}
    CHECK(t->element_bytes==bytes);CHECK(t->pitch==s.pitch);CHECK(t->width==s.width);
    CHECK(t->height==s.height);CHECK(t->depth==s.depth);CHECK(t->padded_height==s.ph);
    CHECK(t->padded_depth==s.pd);CHECK(t->size_bytes==span(&s,bytes));return 1;
}
typedef struct {uint32_t x,y,z;uint64_t byte[5];} Golden;
static const Golden golden[]={
    {1,0,0,{1,2,4,8,16}},
    {0,1,0,{2,4,8,16,32}},
    {2,0,0,{4,8,16,64,128}},
    {0,2,0,{8,16,64,128,256}},
    {4,0,0,{64,128,256,512,1024}},
    {0,4,0,{128,256,512,1024,2048}},
    {0,0,1,{16,32,32,32,64}},
    {0,0,2,{32,64,128,256,512}},
    {0,0,3,{48,96,160,288,576}},
    {7,7,3,{255,510,1020,2040,4080}},
    {8,0,0,{256,512,1024,2048,4096}},
    {0,8,0,{512,1024,2048,4096,8192}},
    {8,8,0,{768,1536,3072,6144,12288}},
    {0,0,4,{1024,2048,4096,8192,16384}},
    {0,0,5,{1040,2080,4128,8224,16448}},
    {7,7,7,{1279,2558,5116,10232,20464}},
    {8,8,4,{1792,3584,7168,14336,28672}},
    {15,15,7,{2047,4094,8188,16376,32752}},
    {12,8,4,{1856,3712,7424,14848,29696}},
    {11,9,5,{1815,3630,7228,14456,28912}},
    {10,11,6,{1838,3676,7384,14800,29600}},
};
static void literal_offsets(void)
{
    Shape s={16,16,16,8,16,8,0};rt_volume_tiling t;
    for(unsigned b=0;b<5;++b)if(admit(s,1u<<b,&t))for(size_t i=0;i<COUNT(golden);++i)
        CHECK(rt_volume_offset(&t,golden[i].x,golden[i].y,golden[i].z)==golden[i].byte[b]);
    const Shape wide[]={{24,17,19,9,24,12,0},{32,32,32,16,32,16,1}};
    const Golden transitions[2][2]={
        {{16,16,8,{6656,13312,26624,53248,106496}},{23,23,11,{6911,13822,27644,55288,110576}}},
        {{16,16,8,{10752,21504,43008,86016,172032}},{31,31,15,{16383,32766,65532,131064,262128}}},
    };
    for(unsigned w=0;w<2;++w)for(unsigned b=0;b<5;++b)if(admit(wide[w],1u<<b,&t))
        for(unsigned i=0;i<2;++i)CHECK(rt_volume_offset(&t,transitions[w][i].x,transitions[w][i].y,transitions[w][i].z)==transitions[w][i].byte[b]);
}
static uint8_t *allocate(size_t bytes)
{
    uint8_t *p=malloc(bytes);CHECK(p!=NULL);
    if(!p){exit(1);}
    return p;
}
static void physical_coordinates(void)
{
    const Shape s={24,13,11,7,16,8,0};
    for(unsigned b=0;b<5;++b){unsigned bytes=1u<<b;rt_volume_tiling t;
        if(!admit(s,bytes,&t)){continue;}
        size_t elements=(size_t)s.pitch*s.ph*s.pd;uint8_t *seen=allocate(elements);memset(seen,0,elements);
        for(uint32_t z=0;z<s.pd;++z)for(uint32_t y=0;y<s.ph;++y)for(uint32_t x=0;x<s.pitch;++x){
            uint64_t at=rt_volume_offset(&t,x,y,z);CHECK(at==primary_offset(&s,bytes,x,y,z));
            CHECK(at<span(&s,bytes));CHECK(at%bytes==0);
            if(at<span(&s,bytes)&&at%bytes==0){CHECK(seen[at/bytes]==0);seen[at/bytes]=1;}
        }
        for(size_t i=0;i<elements;++i){CHECK(seen[i]==1);}
        free(seen);++permutations;
    }
}
/* Literal seeds make independent owned source/guest bytes. The oracle addresses
   come from the primary table, never from a preceding production tiling call. */
static const uint8_t seeds[16]={0x31,0xa7,0x0c,0xe2,0x5b,0x96,0x43,0xfd,0x18,0x72,0xd9,0x64,0x8e,0x25,0xb0,0x4f};
static uint8_t data_byte(size_t i,unsigned salt){return (uint8_t)(seeds[i%16]+(i/16)*37+(i/257)*13+salt);}
static void compare(const uint8_t *got,const uint8_t *wanted,size_t bytes)
{
    for(size_t i=0;i<bytes;++i){
        if(got[i]!=wanted[i]&&failures<20){fprintf(stderr,"byte%zu: %02x wanted%02x\n",i,got[i],wanted[i]);}
        CHECK(got[i]==wanted[i]);
    }
}
static void copy_case(Shape s,unsigned bytes)
{
    rt_volume_tiling t;
    if(!admit(s,bytes,&t)){return;}
    size_t physical=(size_t)span(&s,bytes),linear=(size_t)s.width*s.height*s.depth*bytes;
    uint8_t *guest=allocate(physical+2*GUARD),*wanted=allocate(physical+2*GUARD);
    uint8_t *source=allocate(linear+2*GUARD),*saved=allocate(linear+2*GUARD);
    for(size_t i=0;i<physical+2*GUARD;++i)guest[i]=wanted[i]=data_byte(i,0x19);
    for(size_t i=0;i<linear+2*GUARD;++i)source[i]=saved[i]=data_byte(i,0x73);
    size_t at=0;
    for(uint32_t z=0;z<s.depth;++z)for(uint32_t y=0;y<s.height;++y)for(uint32_t x=0;x<s.width;++x)
        for(unsigned c=0;c<bytes;++c)wanted[GUARD+primary_offset(&s,bytes,x,y,z)+c]=source[GUARD+at++];
    rt_volume_tile(&t,source+GUARD,guest+GUARD);
    compare(guest,wanted,physical+2*GUARD);compare(source,saved,linear+2*GUARD);
    /* Fresh guest bytes replace the tiler's output: detile has its own oracle. */
    for(size_t i=0;i<physical+2*GUARD;++i)guest[i]=wanted[i]=data_byte(i,0xc1);
    for(size_t i=0;i<linear+2*GUARD;++i)source[i]=saved[i]=data_byte(i,0x47);
    at=0;
    for(uint32_t z=0;z<s.depth;++z)for(uint32_t y=0;y<s.height;++y)for(uint32_t x=0;x<s.width;++x)
        for(unsigned c=0;c<bytes;++c)saved[GUARD+at++]=guest[GUARD+primary_offset(&s,bytes,x,y,z)+c];
    rt_volume_detile(&t,guest+GUARD,source+GUARD);
    compare(source,saved,linear+2*GUARD);compare(guest,wanted,physical+2*GUARD);
    free(saved);free(source);free(wanted);free(guest);++copies;
}
static void refuse(unsigned index,unsigned bytes,uint32_t pitch,uint32_t width,uint32_t height,uint32_t depth,int pow2)
{
    rt_volume_tiling output,original;memset(&output,0xa5,sizeof output);memcpy(&original,&output,sizeof output);
    char error[160]="";int rc=rt_volume_layout(index,bytes,pitch,width,height,depth,pow2,&output,error,sizeof error);
    if(rc!=-1){fprintf(stderr,"unexpected admission index%u bytes%u pitch%u %ux%ux%u pow2%d\n",index,bytes,pitch,width,height,depth,pow2);}
    CHECK(rc==-1);CHECK(error[0]!=0);CHECK(memcmp(&output,&original,sizeof output)==0);++refusals;
}
static void admission_edges(void)
{
    for(unsigned index=0;index<32;++index)if(index!=19)refuse(index,1,8,1,1,1,0);
    refuse(32,1,8,1,1,1,0);refuse(UINT_MAX,1,8,1,1,1,0);
    for(unsigned b=0;b<=32;++b)if(b!=1&&b!=2&&b!=4&&b!=8&&b!=16)refuse(19,b,8,1,1,1,0);
    refuse(19,UINT_MAX,8,1,1,1,0);
    refuse(19,1,0,1,1,1,0);refuse(19,1,8,0,1,1,0);refuse(19,1,8,1,0,1,0);refuse(19,1,8,1,1,0,0);
    refuse(19,1,8,9,1,1,0);
    const uint32_t unaligned[]={1,7,9,15,17,31,UINT32_MAX};
    for(size_t i=0;i<COUNT(unaligned);++i)refuse(19,1,unaligned[i],1,1,1,0);
    const int invalid_flags[]={-1,2,INT_MAX};
    for(size_t i=0;i<COUNT(invalid_flags);++i)refuse(19,1,8,1,1,1,invalid_flags[i]);
    refuse(19,1,8,3,1,1,1);refuse(19,1,8,1,3,1,1);refuse(19,1,8,1,1,3,1);refuse(19,1,24,1,1,1,1);
    refuse(19,1,8,1,UINT32_MAX,4,0);refuse(19,1,8,1,8,UINT32_MAX,0);
    /* Each exact2^64 footprint overflows in a different BPE multiplication. */
    for(unsigned b=0;b<5;++b)refuse(19,1u<<b,0x80000000u,1,0x80000000u>>b,4,0);
    refuse(19,16,0xfffffff8u,1,0xfffffff8u,0xfffffffcu,0);
    rt_volume_tiling t;
    for(unsigned b=0;b<5;++b)admit((Shape){8,1,1,1,8,4,1},1u<<b,&t);
    const Shape wide[]={
        {8,1,0xfffffff8u,1,0xfffffff8u,4,0},
        {8,1,1,0xfffffffcu,8,0xfffffffcu,0},
        {0xfffffff8u,1,1,1,8,4,0},
    };
    for(size_t i=0;i<COUNT(wide);++i)if(admit(wide[i],1,&t)){
        CHECK(t.size_bytes>UINT32_MAX);
        CHECK(rt_volume_offset(&t,wide[i].pitch-1,wide[i].ph-1,wide[i].pd-1)==span(wide+i,1)-1);
    }
}
int main(void)
{
    CHECK(RT_VOLUME_1D_THICK==19);literal_offsets();physical_coordinates();
    for(size_t i=0;i<COUNT(shapes);++i)for(unsigned b=0;b<5;++b)copy_case(shapes[i],1u<<b);
    admission_edges();
    CHECK(admissions==48);CHECK(refusals==89);CHECK(copies==20);CHECK(permutations==5);
    printf("volume tiling: %u admissions, %u refusals, %u copy pairs, %u physical permutations, %u checks, %u failures\n",
        admissions,refusals,copies,permutations,checks,failures);return failures?1:0;
}
