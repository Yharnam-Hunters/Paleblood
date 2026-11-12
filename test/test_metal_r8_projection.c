/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Owned R8 storage projections: literal selectors, packed VDATA, and independent backing bytes. */
#define _POSIX_C_SOURCE 200809L
#include "runtime/render_calls.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
enum { OP_LOAD, OP_STORE, OP_RW, OP_RAW, OP_ALIAS, OP_SAMPLE };
enum { EXPECT_OK, EXPECT_PROFILE, EXPECT_PARTIAL, EXPECT_OTHER, EXPECT_RANGE, EXPECT_SUPPRESSED };
enum { POOL=2<<20, CODE=0x1000, RAW_CODE=0x6000, OUTPUT=0x10000, CONTROL=OUTPUT+1024,
       RAW_OUTPUT=0x20000, RAW_CAPACITY=0x20000, IMAGE=0x80000, CANARY=0xa5 };
typedef struct { uint16_t word; unsigned char red, selectors[4]; } Projection;
static const Projection projections[32] = {
    {0x4,0, {4,0,0,0}},
    {0xc,0, {4,1,0,0}},
    {0x44,0, {4,0,1,0}},
    {0x4c,0, {4,1,1,0}},
    {0x204,0, {4,0,0,1}},
    {0x20c,0, {4,1,0,1}},
    {0x244,0, {4,0,1,1}},
    {0x24c,0, {4,1,1,1}},
    {0x20,1, {0,4,0,0}},
    {0x21,1, {1,4,0,0}},
    {0x60,1, {0,4,1,0}},
    {0x61,1, {1,4,1,0}},
    {0x220,1, {0,4,0,1}},
    {0x221,1, {1,4,0,1}},
    {0x260,1, {0,4,1,1}},
    {0x261,1, {1,4,1,1}},
    {0x100,2, {0,0,4,0}},
    {0x101,2, {1,0,4,0}},
    {0x108,2, {0,1,4,0}},
    {0x109,2, {1,1,4,0}},
    {0x300,2, {0,0,4,1}},
    {0x301,2, {1,0,4,1}},
    {0x308,2, {0,1,4,1}},
    {0x309,2, {1,1,4,1}},
    {0x800,3, {0,0,0,4}},
    {0x801,3, {1,0,0,4}},
    {0x808,3, {0,1,0,4}},
    {0x809,3, {1,1,0,4}},
    {0x840,3, {0,0,1,4}},
    {0x841,3, {1,0,1,4}},
    {0x848,3, {0,1,1,4}},
    {0x849,3, {1,1,1,4}},
};
static const signed char packed_rank[16][4] = {
    {-1,-1,-1,-1},
    {0,-1,-1,-1},
    {-1,0,-1,-1},
    {0,1,-1,-1},
    {-1,-1,0,-1},
    {0,-1,1,-1},
    {-1,0,1,-1},
    {0,1,2,-1},
    {-1,-1,-1,0},
    {0,-1,-1,1},
    {-1,0,-1,1},
    {0,1,-1,2},
    {-1,-1,0,1},
    {0,-1,1,2},
    {-1,0,1,2},
    {0,1,2,3},
};

typedef struct {
    char id[192]; const char *family, *reason;
    unsigned world, type, tile, op, mask, da, swizzle, width, height, pitch;
    unsigned layers, first, last, x, y, z, local, fill, data, number, bytes, depth, pow2, mip;
    unsigned image, second_swizzle, second_first, second_last, raw, baseline_pass, image_budget, pipeline_budget;
    int projection; unsigned expect;
} Case;
typedef void (*Visitor)(const Case *);
static unsigned planned_world;
static void require(int value, const char *reason)
{
    if (!value) { fprintf(stderr,"R8 fixture preparation: %s\n",reason); exit(2); }
}
static unsigned one_d(unsigned type) { return type==RT_IMAGE_1D || type==RT_IMAGE_1D_ARRAY; }
static unsigned arrayed(unsigned type) { return type==RT_IMAGE_1D_ARRAY || type==RT_IMAGE_2D_ARRAY; }
static unsigned dimension(unsigned type)
{
    return type==RT_IMAGE_1D ? 0 : type==RT_IMAGE_2D ? 1 : type==RT_IMAGE_1D_ARRAY ? 2 : 3;
}
static const char *operation(unsigned op)
{
    static const char *const names[]={"read","write","rw","raw","alias","sample"};
    require(op<COUNT(names),"operation index"); return names[op];
}
static const char *kind(const Case *c)
{
    if (c->expect!=EXPECT_OK && c->expect!=EXPECT_SUPPRESSED) return "refusal";
    return c->expect==EXPECT_SUPPRESSED ? "suppressed" : operation(c->op);
}
static Case make_case(unsigned world, unsigned type, unsigned tile, int map, unsigned op, unsigned mask, unsigned da)
{
    Case c={0}; c.world=world;c.type=type;c.tile=tile;c.projection=map;c.op=op;c.mask=mask;c.da=da;
    c.swizzle=map>=0?projections[map].word:(4u|5u<<3|6u<<6|7u<<9);
    c.width=8;c.height=one_d(type)?1:8;c.pitch=256;c.layers=arrayed(type)?3:1;c.last=arrayed(type)?2:0;
    c.local=one_d(type)?8:64;c.fill=83;c.data=1;c.number=0;c.bytes=1;c.depth=1;c.image=IMAGE;
    c.raw=op==OP_STORE||op==OP_RW;c.image_budget=33;c.pipeline_budget=199;
    c.baseline_pass=map<0 || (projections[map].red==0 && (op==OP_STORE || projections[map].word==0x204));
    return c;
}
static void emit(Visitor visit, Case *c, const char *family, const char *id)
{
    c->family=family; require(strlen(id)<sizeof c->id,"case ID bound");strcpy(c->id,id);
    if(c->expect!=EXPECT_OK && c->expect!=EXPECT_SUPPRESSED) c->raw=0,c->baseline_pass=0;
    if(c->expect==EXPECT_PROFILE)c->reason="swizzle must preserve";
    else if(c->expect==EXPECT_PARTIAL)c->reason="partial stores are not translated";
    else if(c->expect==EXPECT_RANGE)c->reason="image access is outside its view";
    visit(c);
}
static void suite(Visitor visit)
{
    static const unsigned types[]={RT_IMAGE_1D,RT_IMAGE_2D,RT_IMAGE_1D_ARRAY,RT_IMAGE_2D_ARRAY};
    static const unsigned tiles[]={0,8,9,10,13,14,31};char id[192];
    /* Eight projections across seven layouts use56 images. Group0 also checks
       seven legacy identities:63 images, below64. At most55 pipelines including raw. */
    for(unsigned d=0;d<COUNT(types);++d)for(unsigned group=0;group<4;++group){
        const unsigned world=++planned_world;
        for(unsigned t=0;t<COUNT(tiles);++t){
            for(int m=(int)(group*8);m<(int)(group*8+8);++m)for(unsigned op=0;op<=OP_RW;++op)for(unsigned da=0;da<2;++da){
                Case c=make_case(world,types[d],tiles[t],m,op,15,da);c.image_budget=group?56:63;c.pipeline_budget=group?49:55;
                snprintf(id,sizeof id,"matrix/d%u/t%u/p%02d/%s/da%u",d,tiles[t],m,operation(op),da);emit(visit,&c,"matrix",id);
            }
            if(group==0)for(unsigned op=0;op<=OP_RW;++op)for(unsigned da=0;da<2;++da){
                Case c=make_case(world,types[d],tiles[t],-1,op,15,da);c.image_budget=63;c.pipeline_budget=55;
                snprintf(id,sizeof id,"legacy/identity/d%u/t%u/%s/da%u",d,tiles[t],operation(op),da);emit(visit,&c,"legacy-identity",id);
            }
        }
    }
    const unsigned small_tiles[]={13,31};
    for(int m=0;m<32;++m){
        const unsigned world=++planned_world;
        for(unsigned t=0;t<COUNT(small_tiles);++t)for(unsigned op=0;op<=OP_STORE;++op)for(unsigned mask=op==OP_LOAD?1:0;mask<16;++mask)for(unsigned da=0;da<2;++da){
            Case c=make_case(world,RT_IMAGE_2D,small_tiles[t],m,op,mask,da);
            c.local=3;c.x=2;c.y=4;c.image_budget=2;c.pipeline_budget=63;
            if(op==OP_STORE && packed_rank[mask][projections[m].red]<0)c.expect=EXPECT_PARTIAL;
            snprintf(id,sizeof id,"dmask/t%u/p%02d/%s/m%x/da%u",small_tiles[t],m,operation(op),mask,da);emit(visit,&c,"dmask",id);
        }
    }
    unsigned n=0,world=0;
    for(unsigned word=0;word<4096;++word){
        int selected=0;for(unsigned m=0;m<32;++m)if(projections[m].word==word)selected=1;
        if(selected || (word&7)!=4)continue;
        if(n%32==0)world=++planned_world;
        Case c=make_case(world,RT_IMAGE_2D,31,-1,OP_LOAD,15,0);
        c.swizzle=word;c.local=1;c.x=3;c.y=2;c.image_budget=32;c.pipeline_budget=1;
        snprintf(id,sizeof id,"legacy/nonselected/w%03x",word);emit(visit,&c,"legacy-nonselected",id);++n;
    }
    require(n==504,"504 nonselected legacy words");n=0;
    for(unsigned word=0;word<4096;++word){
        int selected=0;for(unsigned m=0;m<32;++m)if(projections[m].word==word)selected=1;
        if(selected || (word&7)==4)continue;
        if(n%32==0)world=++planned_world;
        Case c=make_case(world,RT_IMAGE_2D,31,-1,OP_STORE,15,0);
        c.swizzle=word;c.local=1;c.expect=EXPECT_PROFILE;c.image_budget=32;c.pipeline_budget=33;
        snprintf(id,sizeof id,"refused/word/w%03x",word);emit(visit,&c,"refused-word",id);++n;
    }
    require(n==3560,"3560 refused words");
    for(unsigned da=0;da<2;++da){
        const unsigned invalid[]={0,0x320,0x222,0x223,0x225,0x226,0x227};
        const char *const names[]={"zero","duplicate","reserved2","reserved3","missingG","missingB","missingA"};
        world=++planned_world;
        for(unsigned i=0;i<COUNT(invalid);++i){
            if(i==0 && da==0)continue; /* Full word inventory contains this exact allzero full-F case. */
            Case c=make_case(world,RT_IMAGE_2D,31,-1,OP_STORE,i?2:15,da);
            c.swizzle=invalid[i];c.local=1;c.expect=EXPECT_PROFILE;
            snprintf(id,sizeof id,"invalid/%s/da%u",names[i],da);emit(visit,&c,"invalid-control-anchor",id);
        }
        Case c=make_case(world,RT_IMAGE_2D,31,-1,OP_STORE,15,da);
        c.data=4;c.number=7;c.bytes=4;c.local=1;
        snprintf(id,sizeof id,"format/R32/warm/da%u",da);emit(visit,&c,"format-warm",id);
        c.projection=12;c.swizzle=projections[12].word;c.mask=2;c.expect=EXPECT_PROFILE;
        snprintf(id,sizeof id,"format/R32/projected/da%u",da);emit(visit,&c,"format-refusal",id);
        c=make_case(world,RT_IMAGE_3D,19,-1,OP_STORE,15,da);
        c.pitch=8;c.width=c.height=8;c.depth=4;c.layers=1;c.first=c.last=0;c.local=1;
        snprintf(id,sizeof id,"volume/warm/da%u",da);emit(visit,&c,"volume-warm",id);
        c.projection=12;c.swizzle=projections[12].word;c.mask=2;c.expect=EXPECT_PROFILE;
        snprintf(id,sizeof id,"volume/projected/da%u",da);emit(visit,&c,"volume-refusal",id);
        c=make_case(world,RT_IMAGE_2D,31,12,OP_STORE,15,da);c.local=1;c.data=3;c.bytes=2;c.expect=EXPECT_PROFILE;
        snprintf(id,sizeof id,"format/RG8/projected/da%u",da);emit(visit,&c,"format-refusal",id);
        c=make_case(world,RT_IMAGE_2D,31,12,OP_LOAD,15,da);c.local=1;c.number=9;c.expect=EXPECT_OTHER;c.reason="not modeled";
        snprintf(id,sizeof id,"format/R8-srgb/da%u",da);emit(visit,&c,"format-refusal",id);
        c=make_case(world,RT_IMAGE_2D,31,12,OP_LOAD,15,da);c.local=1;c.data=10;c.number=9;c.bytes=4;c.expect=EXPECT_OTHER;c.reason="sRGB compute storage";
        snprintf(id,sizeof id,"format/RGBA-srgb-read/da%u",da);emit(visit,&c,"format-refusal",id);
        c=make_case(world,RT_IMAGE_CUBE,13,12,OP_STORE,15,da);c.local=1;c.pitch=32;c.layers=6;c.expect=EXPECT_OTHER;c.reason="compute image type";
        snprintf(id,sizeof id,"cube/R8/da%u",da);emit(visit,&c,"dimension-refusal",id);
        c=make_case(world,RT_IMAGE_CUBE,13,12,OP_STORE,15,da);c.local=1;c.pitch=32;c.layers=6;c.data=10;c.bytes=4;c.expect=EXPECT_PROFILE;
        snprintf(id,sizeof id,"cube/RGBA-projected/da%u",da);emit(visit,&c,"dimension-refusal",id);
    }
    for(unsigned da=0;da<2;++da){
        world=++planned_world;
        Case c=make_case(world,RT_IMAGE_2D,31,12,OP_STORE,15,da);
        snprintf(id,sizeof id,"cache/position/warm/da%u",da);emit(visit,&c,"cache-position",id);
        c.projection=20;c.swizzle=projections[20].word;
        snprintf(id,sizeof id,"cache/position/change/da%u",da);emit(visit,&c,"cache-position",id);
        world=++planned_world;c=make_case(world,RT_IMAGE_2D,31,12,OP_LOAD,15,da);
        snprintf(id,sizeof id,"cache/constants/warm/da%u",da);emit(visit,&c,"cache-constants",id);
        c.projection=13;c.swizzle=projections[13].word;
        snprintf(id,sizeof id,"cache/constants/change/da%u",da);emit(visit,&c,"cache-constants",id);
        world=++planned_world;c=make_case(world,RT_IMAGE_2D,31,12,OP_LOAD,15,da);
        snprintf(id,sizeof id,"cache/CPU/warm/da%u",da);emit(visit,&c,"cache-CPU",id);
        c.fill=107;snprintf(id,sizeof id,"cache/CPU/read-refresh/da%u",da);emit(visit,&c,"cache-CPU",id);
        c.fill=131;c.op=OP_STORE;c.raw=1;c.local=1;c.x=3;c.y=2;
        snprintf(id,sizeof id,"cache/CPU/write-refresh/da%u",da);emit(visit,&c,"cache-CPU",id);
    }
    for(unsigned da=0;da<2;++da)for(unsigned d=0;d<COUNT(types);++d){
        world=++planned_world;Case c=make_case(world,types[d],31,29,OP_STORE,15,da);
        c.local=1;c.x=3;c.y=one_d(c.type)?0:2;
        if(arrayed(c.type)){c.first=1;c.last=2;c.z=1;}
        snprintf(id,sizeof id,"bounds/d%u/view-store/da%u",d,da);emit(visit,&c,"view-bounds",id);
        c.x=8;c.expect=EXPECT_SUPPRESSED;
        snprintf(id,sizeof id,"bounds/d%u/x-suppressed/da%u",d,da);emit(visit,&c,"view-bounds",id);
        c.op=OP_LOAD;c.expect=EXPECT_RANGE;
        snprintf(id,sizeof id,"bounds/d%u/x-read-stop/da%u",d,da);emit(visit,&c,"view-bounds",id);
        c.x=3;c.op=OP_STORE;c.raw=1;c.expect=EXPECT_OK;
        if(arrayed(c.type)){
            c.z=2;c.expect=EXPECT_SUPPRESSED;
            snprintf(id,sizeof id,"bounds/d%u/slice-suppressed/da%u",d,da);emit(visit,&c,"view-bounds",id);
            c.op=OP_LOAD;c.expect=EXPECT_RANGE;
            snprintf(id,sizeof id,"bounds/d%u/slice-read-stop/da%u",d,da);emit(visit,&c,"view-bounds",id);
            c.op=OP_STORE;c.z=0;c.first=2;c.last=1;c.expect=EXPECT_OTHER;c.reason="array view exceeds";
            snprintf(id,sizeof id,"bounds/d%u/reversed-view/da%u",d,da);emit(visit,&c,"view-bounds",id);
            c.first=0;c.last=3;c.expect=EXPECT_OTHER;
            snprintf(id,sizeof id,"bounds/d%u/last-outside/da%u",d,da);emit(visit,&c,"view-bounds",id);
        }
        c=make_case(world,types[d],31,29,OP_STORE,15,da);c.local=1;c.mip=1;c.expect=EXPECT_OTHER;c.reason="mip zero";
        snprintf(id,sizeof id,"bounds/d%u/mip/da%u",d,da);emit(visit,&c,"view-bounds",id);
        c=make_case(world,types[d],1,29,OP_STORE,15,da);c.local=1;c.expect=EXPECT_OTHER;c.reason="tiling index";
        snprintf(id,sizeof id,"bounds/d%u/tile/da%u",d,da);emit(visit,&c,"view-bounds",id);
    }
    for(unsigned da=0;da<2;++da){
        world=++planned_world;Case c=make_case(world,RT_IMAGE_2D_ARRAY,13,29,OP_STORE,15,da);
        c.width=c.height=9;c.pitch=32;c.first=1;c.last=2;c.z=1;c.local=1;c.x=3;c.y=5;
        snprintf(id,sizeof id,"literal/thin/physical2-byte1063/da%u",da);emit(visit,&c,"literal-anchor",id);
        c=make_case(world,RT_IMAGE_2D_ARRAY,31,29,OP_STORE,15,da);c.local=1;c.first=1;c.last=2;c.z=1;c.x=3;c.y=2;
        snprintf(id,sizeof id,"literal/linear/physical2-byte4611/da%u",da);emit(visit,&c,"literal-anchor",id);
        c=make_case(world,RT_IMAGE_1D_ARRAY,31,29,OP_STORE,15,da);c.local=1;c.first=1;c.last=2;c.z=1;c.x=3;
        snprintf(id,sizeof id,"literal/linear1d/physical2-byte515/da%u",da);emit(visit,&c,"literal-anchor",id);
        c=make_case(world,RT_IMAGE_2D_ARRAY,31,29,OP_STORE,15,da);c.pow2=1;c.first=1;c.last=3;c.z=2;c.local=1;c.x=3;c.y=2;
        snprintf(id,sizeof id,"bounds/pow2/physical3/da%u",da);emit(visit,&c,"pow2-anchor",id);
        world=++planned_world;c=make_case(world,RT_IMAGE_2D,31,12,OP_STORE,15,da);c.local=1;c.image=POOL-2048;
        snprintf(id,sizeof id,"pool/exact-end/da%u",da);emit(visit,&c,"pool-exact",id);
        c.image=POOL-256;c.expect=EXPECT_OTHER;c.reason="outside the GPU-visible pool";
        snprintf(id,sizeof id,"pool/full-span-refusal/da%u",da);emit(visit,&c,"pool-refusal",id);
        world=++planned_world;c=make_case(world,RT_IMAGE_2D_ARRAY,31,12,OP_STORE,15,da);
        c.first=c.last=0;c.local=1;c.x=3;c.y=2;
        snprintf(id,sizeof id,"alias/warm-A/da%u",da);emit(visit,&c,"alias-warm",id);
        c.projection=20;c.swizzle=projections[20].word;c.first=c.last=1;
        snprintf(id,sizeof id,"alias/warm-B/da%u",da);emit(visit,&c,"alias-warm",id);
        c.op=OP_ALIAS;c.projection=12;c.swizzle=projections[12].word;c.first=c.last=0;c.second_swizzle=projections[20].word;c.second_first=c.second_last=1;c.expect=EXPECT_OTHER;c.reason="overlapping compute images";c.raw=0;
        snprintf(id,sizeof id,"alias/disjoint-views-refused/da%u",da);emit(visit,&c,"alias-refusal",id);
    }
}
static const unsigned char thin[64]={
    0,1,4,5,16,17,20,21,2,3,6,7,18,19,22,23,
    8,9,12,13,24,25,28,29,10,11,14,15,26,27,30,31,
    32,33,36,37,48,49,52,53,34,35,38,39,50,51,54,55,
    40,41,44,45,56,57,60,61,42,43,46,47,58,59,62,63
};
static const unsigned char display[64]={
    0,1,2,3,4,5,6,7,16,17,18,19,20,21,22,23,
    8,9,10,11,12,13,14,15,24,25,26,27,28,29,30,31,
    32,33,34,35,36,37,38,39,48,49,50,51,52,53,54,55,
    40,41,42,43,44,45,46,47,56,57,58,59,60,61,62,63
};
static unsigned physical_layers(const Case *c)
{
    unsigned n=c->layers;
    if(c->pow2 && (arrayed(c->type)||c->type==RT_IMAGE_CUBE)){unsigned v=1;while(v<n)v*=2;n=v;}
    return n;
}
static unsigned physical_height(const Case *c)
{
    unsigned h=one_d(c->type)?1:c->height;
    if(c->tile==0||c->tile==10||c->tile==14)return (h+127u)/128u*128u; /* R8 macro geometry. */
    if(c->tile==9||c->tile==13||c->type==RT_IMAGE_3D)return (h+7u)/8u*8u;
    return h;
}
static size_t image_span(const Case *c)
{
    if(c->type==RT_IMAGE_3D)return (size_t)c->pitch*physical_height(c)*((c->depth+3u)/4u*4u)*c->bytes;
    return (size_t)c->pitch*physical_height(c)*physical_layers(c)*c->bytes;
}
/* Independent literal microtile tables. Linear rows keep their stated pitch. */
static size_t pixel_offset(const Case *c,unsigned x,unsigned y,unsigned slice)
{
    if(c->type==RT_IMAGE_3D){require(x==0&&y==0&&slice==0,"volume oracle is first voxel only");return 0;}
    const size_t layer=(size_t)c->pitch*physical_height(c)*c->bytes;
    if(c->tile==8||c->tile==31)return slice*layer+((size_t)y*c->pitch+x)*c->bytes;
    if(c->tile==9||c->tile==13){
        const unsigned char *table=c->tile==9?display:thin;
        return slice*layer+(((size_t)(y/8)*(c->pitch/8)+x/8)*64+table[(y%8)*8+x%8])*c->bytes;
    }
    require((c->tile==0||c->tile==10||c->tile==14)&&x<8&&y<8&&slice==0,"macro oracle is first physical microtile only");
    return (c->tile==10?display:thin)[y*8+x]*c->bytes;
}
#ifdef R8_FIXTURE_INVENTORY
static unsigned inventory_cases,inventory_raw;
static void inventory(const Case *c)
{
    printf("{\"id\":\"%s\",\"family\":\"%s\",\"world\":%u,\"kind\":\"%s\",\"program_dim\":%u,\"type\":%u,\"span\":%zu,\"first_literal_offset\":%zu,\"tile\":%u,\"projection\":%d,\"op\":%u,\"mask\":%u,\"da\":%u,\"swizzle\":%u,\"width\":%u,\"height\":%u,\"pitch\":%u,\"layers\":%u,\"first\":%u,\"last\":%u,\"x\":%u,\"y\":%u,\"z\":%u,\"local\":%u,\"fill\":%u,\"data\":%u,\"number\":%u,\"bytes\":%u,\"depth\":%u,\"pow2\":%u,\"mip\":%u,\"image\":%u,\"second_swizzle\":%u,\"second_first\":%u,\"second_last\":%u,\"raw_expected\":%u,\"baseline_pass\":%u,\"expect\":%u,\"reason\":\"%s\",\"image_budget\":%u,\"pipeline_budget\":%u}\n",
        c->id,c->family,c->world,kind(c),dimension(c->type),c->type,image_span(c),c->expect==EXPECT_OK?pixel_offset(c,c->x,one_d(c->type)?0:c->y,arrayed(c->type)?c->first+c->z:c->type==RT_IMAGE_3D?c->z:0):SIZE_MAX,c->tile,c->projection,c->op,c->mask,c->da,c->swizzle,c->width,c->height,c->pitch,c->layers,c->first,c->last,c->x,c->y,c->z,c->local,c->fill,c->data,c->number,c->bytes,c->depth,c->pow2,c->mip,c->image,c->second_swizzle,c->second_first,c->second_last,c->raw,c->baseline_pass,c->expect,c->reason?c->reason:"",c->image_budget,c->pipeline_budget);
    ++inventory_cases;inventory_raw+=c->raw;
}
int main(void)
{
    suite(inventory);fprintf(stderr,"R8 inventory: %u cases, %u worlds, %u raw observations planned\n",inventory_cases,planned_world,inventory_raw);return 0;
}
#else
#include "runtime/render_executor.h"
#include "metal_r8_programs.h"
#include <math.h>
#include <sys/mman.h>
#include <unistd.h>
static const uint64_t guest_base=UINT64_C(0x3000000000);
static const uint32_t payload[4]={0x3e000000u,0x3ea00000u,0x3f100000u,0x3f500000u};
static const unsigned char colors[4]={32,80,143,207};
static char case_id[224]="setup";
static unsigned scenarios,worlds,raw_observations,failures;
static unsigned long long checks;
static char r8_error[512];
static void check(int value,const char *expression,unsigned line)
{
    ++checks;if(value)return;++failures;
    static char previous[224];
    if(failures<=24 || strcmp(previous,case_id)){
        fprintf(stderr,"R8 line%u [%s]: %s\n",line,case_id,expression);snprintf(previous,sizeof previous,"%s",case_id);
    }
}
#define CHECK(v) check(!!(v),#v,__LINE__)
typedef struct {
    uint8_t *pool,*before,*expected,*raw_snapshot;rt_executor *executor;unsigned number;
} World;
static World world;
static uint32_t bits(float f){uint32_t v;memcpy(&v,&f,4);return v;}
static float as_float(uint32_t v){float f;memcpy(&f,&v,4);return f;}
static uint32_t read_word(const void *p){uint32_t v;memcpy(&v,p,4);return v;}
static const R8Program *program_for(unsigned dim,unsigned da,unsigned op,unsigned mask)
{
    for(size_t i=0;i<COUNT(r8_programs);++i){const R8Program *p=&r8_programs[i];if(p->dim==dim&&p->da==da&&p->op==op&&p->mask==mask)return p;}
    require(0,"owned program variant missing");return NULL;
}
static uint64_t address(unsigned offset){return guest_base+offset;}
static void buffer(uint32_t words[4],unsigned offset,unsigned bytes)
{
    const uint64_t a=address(offset);words[0]=(uint32_t)a;words[1]=(uint32_t)(a>>32);words[2]=bytes;words[3]=0;
}
static void descriptor(uint32_t words[8],const Case *c)
{
    const uint64_t a=address(c->image)>>8;unsigned depth=arrayed(c->type)?c->layers:c->type==RT_IMAGE_CUBE?c->layers/6:c->type==RT_IMAGE_3D?c->depth:1;
    require(depth>0,"descriptor depth");memset(words,0,32);words[0]=(uint32_t)a;words[1]=(uint32_t)(a>>32)|c->data<<20|c->number<<26;
    words[2]=(c->width-1)|(c->height-1)<<14;
    words[3]=c->swizzle|c->mip<<12|c->mip<<16|c->tile<<20|c->pow2<<25|c->type<<28;
    words[4]=(depth-1)|(c->pitch-1)<<13;words[5]=c->first|c->last<<13;
}
static World open_world(unsigned number)
{
    char path[]="/tmp/paleblood-r8-projection-XXXXXX",error[512]="";int fd=mkstemp(path);require(fd>=0,"temporary pool");
    CHECK(unlink(path)==0);require(ftruncate(fd,POOL)==0,"pool size");
    uint8_t *pool=mmap(NULL,POOL,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);require(pool!=MAP_FAILED,"pool mmap");memset(pool,CANARY,POOL);
    rt_executor_memory memory={fd,POOL,guest_base,NULL,NULL,NULL,NULL,0};rt_executor *executor=NULL;
    if(rt_executor_open(&memory,&executor,error,sizeof error)){fprintf(stderr,"R8 open: %s\n",error);exit(1);}CHECK(close(fd)==0);
    World w={pool,malloc(POOL),malloc(POOL),malloc(RAW_CAPACITY),executor,number};require(w.before&&w.expected&&w.raw_snapshot,"oracle storage");
    const R8Program *raw=program_for(0,0,OP_RAW,0);require(raw->dwords*4<CODE,"raw program bound");memcpy(pool+RAW_CODE,raw->words,raw->dwords*4);++worlds;return w;
}
static void close_world(World *w)
{
    rt_executor_close(w->executor);CHECK(munmap(w->pool,POOL)==0);free(w->before);free(w->expected);free(w->raw_snapshot);memset(w,0,sizeof *w);
}
static int dispatch(World *w,const R8Program *program,const uint32_t *user,unsigned count,unsigned local,unsigned groups,unsigned raw)
{
    rt_executor_reset(w->executor);rt_render_call calls[5]={{0}};unsigned n=0;
    calls[n].kind=RT_CALL_COMPUTE_SHADER;
    calls[n++].u.compute_shader=(rt_compute_shader){.code=address(raw?RAW_CODE:CODE),.vgprs=24,.sgprs=40,.user_sgprs=count,
        .threads={local,1,1},.thread_id_components=0,.workgroup_id_enables=raw?1u:0u};
    for(unsigned first=0;first<count;first+=8){
        calls[n].kind=RT_CALL_USER_DATA;calls[n].u.user_data.stage=RT_STAGE_COMPUTE;calls[n].u.user_data.slot=first;calls[n].u.user_data.count=8;
        memcpy(calls[n].u.user_data.words,user+first,32);++n;
    }
    calls[n].kind=RT_CALL_DISPATCH;calls[n].u.dispatch.groups[0]=groups;calls[n].u.dispatch.groups[1]=calls[n].u.dispatch.groups[2]=1;++n;
    require(n<=COUNT(calls),"call count");require(program->dwords>0,"program words");
    char error[512]="";const int rc=rt_executor_run_calls(w->executor,calls,n,error,sizeof error);
    printf("r8_native id=%s rc=%d\n",case_id,rc);
    if(rc)printf("r8_reason id=%s text=%s\n",case_id,error);
    /* Retain exact reason for the scenario's policy assertion; raw calls expect none. */
    snprintf(r8_error,sizeof r8_error,"%s",error);return rc;
}
static size_t changed(const uint8_t *a,const uint8_t *b,size_t n)
{
    if(!memcmp(a,b,n))return 0;size_t result=0;for(size_t i=0;i<n;++i)result+=a[i]!=b[i];return result;
}
static void compare_data(const uint8_t *actual,const uint8_t *expected,size_t n,const char *domain)
{
    const int same=memcmp(actual,expected,n)==0;
    if(!same)for(size_t i=0;i<n;++i)if(actual[i]!=expected[i]){
        fprintf(stderr,"r8_data_fail id=%s domain=%s offset=%zu actual=%u expected=%u\n",case_id,domain,i,actual[i],expected[i]);break;
    }
    CHECK(same);
}
static void coordinates(const Case *c,unsigned lane,unsigned *x,unsigned *y,unsigned *physical,int *inside)
{
    const unsigned xx=c->x+(one_d(c->type)?lane:lane%8),yy=one_d(c->type)?0:c->y+lane/8;
    const unsigned layer=arrayed(c->type)?c->first+c->z:c->type==RT_IMAGE_3D?c->z:0;
    const int slice_ok=!arrayed(c->type)||c->z<=c->last-c->first;
    *x=xx;*y=yy;*physical=layer;*inside=xx<c->width&&yy<c->height&&slice_ok;
}
static unsigned selected(const Case *c,unsigned logical)
{
    static const unsigned fallback[4]={4,0,0,1};return c->projection>=0?projections[c->projection].selectors[logical]:fallback[logical];
}
static unsigned mark_expected(World *w,const Case *c,unsigned image_offset)
{
    unsigned written=0;
    if(c->op==OP_LOAD||c->op==OP_RW){
        for(unsigned lane=0;lane<c->local;++lane){unsigned output=0;
            for(unsigned logical=0;logical<4;++logical){if(!(c->mask>>logical&1))continue;
                const unsigned sel=selected(c,logical);const uint32_t expected=sel==0?0:sel==1?0x3f800000u:c->number==7?0x3f400000u:bits((float)c->fill/255.0f);
                const size_t at=OUTPUT+16u*lane+4u*output++;const uint32_t got=read_word(w->pool+at);
                if(sel==4&&c->number==0){const int ok=fabsf(as_float(got)-as_float(expected))<=0.000001f;CHECK(ok);if(ok)memcpy(w->expected+at,&got,4);else memcpy(w->expected+at,&expected,4);}
                else memcpy(w->expected+at,&expected,4);
            }
        }
    }
    if(c->op==OP_STORE||c->op==OP_RW){
        const unsigned red=c->projection>=0?projections[c->projection].red:0;const int packed=packed_rank[c->mask][red];require(packed>=0,"expected full component writer");
        for(unsigned lane=0;lane<c->local;++lane){unsigned x,y,physical;int inside;coordinates(c,lane,&x,&y,&physical,&inside);if(!inside)continue;
            const size_t at=image_offset+pixel_offset(c,x,y,physical);require(at+c->bytes<=POOL,"written expected byte range");
            if(c->number==7){memcpy(w->expected+at,payload+packed,4);}
            else{const unsigned target=colors[packed],got=w->pool[at];const int delta=(int)got-(int)target;CHECK(delta>=-1&&delta<=1);w->expected[at]=(uint8_t)(delta>=-1&&delta<=1?got:target);}
            ++written;
        }
    }
    return written;
}
static void run_case(const Case *c)
{
    snprintf(case_id,sizeof case_id,"%s",c->id);printf("r8_begin id=%s\n",c->id);if(!world.executor||world.number!=c->world){if(world.executor)close_world(&world);world=open_world(c->world);}
    const unsigned before_failures=failures;const size_t span=image_span(c);require(span<=RAW_CAPACITY&&span%256==0,"owned allocation/raw-copy span");
    const unsigned image_offset=c->image<=POOL&&span<=POOL-c->image?c->image:IMAGE;
    require(image_offset>=RAW_OUTPUT+RAW_CAPACITY&&span<=POOL-image_offset,"owned image range");
    memset(world.pool+image_offset,c->fill,span);
    if(c->number==7){const uint32_t f=0x3f400000u;for(size_t i=0;i<span;i+=4)memcpy(world.pool+image_offset+i,&f,4);}
    memset(world.pool+OUTPUT,CANARY,1024);memset(world.pool+RAW_OUTPUT,CANARY,RAW_CAPACITY);
    const uint32_t controls[4]={c->x,c->y,c->z,0};memcpy(world.pool+CONTROL,controls,sizeof controls);
    uint32_t user[16]={0};const R8Program *program=program_for(dimension(c->type),c->da,c->op,c->mask);
    if(c->op==OP_ALIAS){
        Case a=*c,b=*c;b.swizzle=c->second_swizzle;b.first=c->second_first;b.last=c->second_last;
        descriptor(user,&a);descriptor(user+8,&b);
    }else{buffer(user,OUTPUT,1040);memcpy(user+4,payload,sizeof payload);descriptor(user+8,c);}
    require(program->dwords*4<RAW_CODE-CODE,"main program bound");memcpy(world.pool+CODE,program->words,program->dwords*4);
    memcpy(world.before,world.pool,POOL);memcpy(world.expected,world.pool,POOL);
    const int rc=dispatch(&world,program,user,16,c->local,1,0);unsigned written=0;
    if(c->expect!=EXPECT_OK&&c->expect!=EXPECT_SUPPRESSED){CHECK(rc==-1);CHECK(c->reason&&strstr(r8_error,c->reason));}
    else{CHECK(rc==0);if(rc==0)written=mark_expected(&world,c,image_offset);}
    compare_data(world.pool,world.expected,POOL,"canonical");
    const size_t image_changes=changed(world.pool+image_offset,world.before+image_offset,span);
    const size_t output_changes=changed(world.pool+OUTPUT,world.before+OUTPUT,1024);
    const size_t pool_changes=changed(world.pool,world.before,POOL);
    const uint32_t first_word=read_word(world.pool+image_offset);
    const unsigned alias_a=c->op==OP_ALIAS?world.pool[image_offset+515]:0,alias_b=c->op==OP_ALIAS?world.pool[image_offset+2563]:0;
    unsigned observed_raw=0;
    if(c->raw&&rc==0){
        memcpy(world.raw_snapshot,world.pool+image_offset,span);
        uint32_t raw_user[8];buffer(raw_user,image_offset,(unsigned)span);buffer(raw_user+4,RAW_OUTPUT,(unsigned)span);
        snprintf(case_id,sizeof case_id,"%s/raw",c->id);const R8Program *raw=program_for(0,0,OP_RAW,0);
        const int raw_rc=dispatch(&world,raw,raw_user,8,64,(unsigned)(span/256),1);CHECK(raw_rc==0);if(raw_rc==0){++raw_observations;++observed_raw;}
        compare_data(world.pool+RAW_OUTPUT,world.expected+image_offset,span,"raw");
        compare_data(world.pool+RAW_OUTPUT,world.raw_snapshot,span,"raw-coherence");
        memcpy(world.expected+RAW_OUTPUT,world.expected+image_offset,span);compare_data(world.pool,world.expected,POOL,"pool-after-raw");
    }
    snprintf(case_id,sizeof case_id,"%s",c->id);
    printf("r8_case kind=%s id=%s rc=%d image_changed_bytes=%zu output_changed_bytes=%zu pool_changed_bytes=%zu written_texels=%u raw_observations=%u first_image_word=%#x output_word0=%#x output_word1=%#x output_word2=%#x output_word3=%#x alias_A=%u alias_B=%u failures_delta=%u\n",
        kind(c),c->id,rc,image_changes,output_changes,pool_changes,written,observed_raw,first_word,read_word(world.pool+OUTPUT),read_word(world.pool+OUTPUT+4),read_word(world.pool+OUTPUT+8),read_word(world.pool+OUTPUT+12),alias_a,alias_b,failures-before_failures);++scenarios;
}
int main(void)
{
    setvbuf(stdout,NULL,_IOLBF,0);suite(run_case);if(world.executor)close_world(&world);
    printf("metal R8 projection host policy: %u scenarios, %u worlds, %u raw observations, %llu checks, %u failures\n",scenarios,worlds,raw_observations,checks,failures);return failures?1:0;
}
#endif
