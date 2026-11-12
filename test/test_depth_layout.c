/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Owned GFX7 register layouts: direct geometry, slice stride and guarded plane copies. */
#include "runtime/tiling.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,failures;
#define CHECK(x) do {++checks;if(!(x)){++failures;if(failures<=20)fprintf(stderr,"line %d: %s\n",__LINE__,#x);}} while(0)
static uint32_t extent(unsigned width,unsigned height){return (width/8-1) | ((height/8-1)<<11);}
static uint32_t stride(unsigned pixels){return pixels/64-1;}
static int decode(uint32_t info,uint32_t z,uint32_t s,unsigned width,unsigned height,unsigned pixels,rt_depth_aspect aspect,rt_depth_layout *out)
{
    char error[256]="";
    return rt_tiling_depth_layout(info,z,s,extent(width,height),stride(pixels),aspect,out,error,sizeof error);
}
static void guarded_copy(const rt_depth_layout *d)
{
    const rt_tiling *t=&d->tiles;const unsigned slices=3;
    CHECK(d->width&&d->height&&t->element_bytes&&t->slice_bytes);
    if(!d->width||!d->height||!t->element_bytes||!t->slice_bytes)return;
    const size_t size=(size_t)rt_tiling_size(t,slices),linear=(size_t)d->width*d->height*t->element_bytes*slices;
    uint8_t *raw=malloc(size+32),*marks=calloc(size,1),*input=malloc(linear),*output=malloc(linear);CHECK(raw&&marks&&input&&output);
    if(!raw||!marks||!input||!output)exit(1);
    memset(raw,0xa5,size+32);for(size_t i=0;i<linear;++i)input[i]=(uint8_t)(i*37+19);
    for(unsigned slice=0;slice<slices;++slice)for(unsigned y=0;y<d->height;++y)for(unsigned x=0;x<d->width;++x){
        const uint64_t at=rt_tiling_offset(t,x,y,slice);CHECK(at>=slice*t->slice_bytes && at+t->element_bytes<=(slice+1)*t->slice_bytes);
        if(at+t->element_bytes>size){free(raw);free(marks);free(input);free(output);return;}
        for(unsigned byte=0;byte<t->element_bytes;++byte){CHECK(!marks[at+byte]);marks[at+byte]=1;}
    }
    rt_tiling_tile(t,input,raw,d->width,d->height,slices);
    for(size_t i=0;i<size;++i)if(!marks[i])CHECK(raw[i]==0xa5);
    for(size_t i=size;i<size+32;++i)CHECK(raw[i]==0xa5);
    rt_tiling_detile(t,raw,output,d->width,d->height,slices);CHECK(memcmp(input,output,linear)==0);
    free(raw);free(marks);free(input);free(output);
}
static void canonical(void)
{
    for(unsigned plane=0;plane<3;++plane){
        const unsigned bytes=plane==0?2:plane==1?4:1;const rt_depth_aspect aspect=plane==2?RT_DEPTH_ASPECT_STENCIL:RT_DEPTH_ASPECT_Z;
        rt_depth_layout d={0};rt_tiling known;char error[160]="";
        CHECK(decode(0x001d0c40,plane==0?1:3,1,256,8,256*128,aspect,&d)==0);
        CHECK(d.width==256&&d.height==8&&d.tiles.tiling_index==UINT32_MAX);
        CHECK(d.tiles.pipes==8&&d.tiles.banks==16&&d.tiles.bank_width==1&&d.tiles.bank_height==4&&d.tiles.macro_aspect==4);
        CHECK(rt_tiling_layout(0,bytes,256,8,&known,error,sizeof error)==0);
        for(unsigned slice=0;slice<3;++slice)for(unsigned y=0;y<128;++y)for(unsigned x=0;x<256;++x)
            CHECK(rt_tiling_offset(&d.tiles,x,y,slice)==rt_tiling_offset(&known,x,y,slice));
        guarded_copy(&d);
        rt_depth_layout padded={0};
        CHECK(decode(0x001d0c40,plane==0?1:3,1,256,8,256*256,aspect,&padded)==0);
        CHECK(padded.tiles.slice_bytes==2*d.tiles.slice_bytes&&padded.height==d.height);
        CHECK(rt_tiling_offset(&padded.tiles,0,0,1)>=padded.tiles.slice_bytes);
        if(plane==0){CHECK(rt_tiling_offset(&padded.tiles,0,4,0)==83968);CHECK(rt_tiling_offset(&padded.tiles,0,0,1)==145408);}
        if(plane==1){CHECK(rt_tiling_offset(&padded.tiles,0,4,0)==135168);CHECK(rt_tiling_offset(&padded.tiles,0,0,1)==276480);}
        guarded_copy(&padded);
    }
}
static void direct_and_stride(void)
{
    rt_depth_layout z={0},s={0},other={0};rt_tiling known;char error[160]="";
    /* Direct geometry matches ordinary thin2D for Z32, independent of retained SI index bits. */
    CHECK(decode(0x001a0c40,3|(2u<<13),1,128,64,128*256,RT_DEPTH_ASPECT_Z,&z)==0);
    CHECK(rt_tiling_layout(14,4,128,256,&known,error,sizeof error)==0);
    CHECK(z.tiles.macro_width==128&&z.tiles.macro_height==64&&z.tiles.tile_split==0);
    for(unsigned slice=0;slice<3;++slice)for(unsigned y=0;y<64;++y)for(unsigned x=0;x<128;++x)
        CHECK(rt_tiling_offset(&z.tiles,x,y,slice)==rt_tiling_offset(&known,x,y,slice));
    CHECK(decode(0x001a0c40,3|(2u<<13)|(7u<<20),1|(7u<<20),128,64,128*256,RT_DEPTH_ASPECT_STENCIL,&s)==0);
    CHECK(s.tiles.element_bytes==1&&s.tiles.bank_height==1&&s.tiles.macro_aspect==2&&s.tiles.slice_bytes==32768);
    guarded_copy(&z);guarded_copy(&s);
    CHECK(decode(0x001a0c40,3|(2u<<13)|(7u<<20),1,128,64,128*256,RT_DEPTH_ASPECT_Z,&other)==0);
    for(unsigned slice=0;slice<3;++slice)for(unsigned y=0;y<64;++y)for(unsigned x=0;x<128;++x)
        CHECK(rt_tiling_offset(&z.tiles,x,y,slice)==rt_tiling_offset(&other.tiles,x,y,slice));
    for(unsigned mode=0;mode<=2;++mode){
        CHECK(decode(mode<<4,1,1,24,8,256,RT_DEPTH_ASPECT_Z,&z)==0);
        CHECK(z.tiles.slice_bytes==512&&z.height==8);
        CHECK(rt_tiling_offset(&z.tiles,0,0,1)==512);
        guarded_copy(&z);
    }
    /* Smallest pipe/bank-group padding beyond a full canonical D32 slice. */
    CHECK(decode(0x001d0c40,3,1,256,8,40960,RT_DEPTH_ASPECT_Z,&z)==0);
    CHECK(z.tiles.slice_bytes==163840&&z.tiles.height==128);
    CHECK(rt_tiling_offset(&z.tiles,0,4,0)==69760);
    CHECK(rt_tiling_offset(&z.tiles,0,0,1)==178176);guarded_copy(&z);
    /* D32 split128: two pieces per micro tile, still with direct shared geometry. */
    CHECK(decode(0x001d0c40,3|(1u<<13),1,256,8,32768,RT_DEPTH_ASPECT_Z,&z)==0);
    CHECK(z.tiles.tile_split==128);guarded_copy(&z);
    /* An 8-bank direct layout, still using the supported P8 permutation. */
    CHECK(decode(0x00100c40,3|(2u<<13),1,64,64,64*128,RT_DEPTH_ASPECT_Z,&z)==0);
    CHECK(z.tiles.banks==8&&z.tiles.macro_width==64&&z.tiles.macro_height==64);guarded_copy(&z);
    CHECK(decode(0,3,1,16384,16384,268435456,RT_DEPTH_ASPECT_Z,&z)==0);
    CHECK(z.tiles.slice_bytes==UINT64_C(1073741824));
    CHECK(rt_tiling_offset(&z.tiles,16383,16383,2)==3*z.tiles.slice_bytes-4);
}
/* Hand-worked non-default direct geometry. A 256-byte in-channel increment occupies
   address bit15; bank bits occupy11..14, and the P8 pipe occupies8..10. */
static void geometry_boundaries(void)
{
    static const struct {uint32_t info,width,height,pixels,x,y,slice;uint64_t offset;} golden[]={
        {0x001a2c40,256,128,32768,64,0,0,32768},
        {0x001a2c40,256,128,32768,128,0,0,2048},
        {0x001a2c40,256,128,32768,192,0,0,34816},
        {0x001a2c40,256,128,32768,0,64,0,71680},
        {0x001a2c40,256,128,32768,0,0,1,145408},
        {0x001a4c40,512,64,32768,128,0,0,65536},
        {0x001a4c40,512,64,32768,256,0,0,2048},
        {0x001a6c40,1024,64,65536,448,0,0,229376},
        {0x001a6c40,1024,64,65536,512,0,0,2048},
        {0x001aac40,256,128,32768,0,8,0,65792},
        {0x001aac40,256,128,32768,0,16,0,16896},
        {0x001c2c40,512,64,32768,0,32,0,70656},
        {0x00122c40,256,64,16384,0,8,0,8448},
    };
    for(unsigned i=0;i<sizeof golden/sizeof *golden;++i){
        rt_depth_layout d={0};CHECK(decode(golden[i].info,3|(2u<<13),1,golden[i].width,golden[i].height,golden[i].pixels,RT_DEPTH_ASPECT_Z,&d)==0);
        CHECK(rt_tiling_offset(&d.tiles,golden[i].x,golden[i].y,golden[i].slice)==golden[i].offset);
    }
    const uint32_t geometries[]={0x001a2c40,0x001a4c40,0x001a6c40,0x001aac40,0x001c2c40,0x00122c40,0x001fec40};
    for(unsigned i=0;i<sizeof geometries/sizeof *geometries;++i){
        const unsigned bw=1u<<(geometries[i]>>13&3),bh=1u<<(geometries[i]>>15&3),aspect=1u<<(geometries[i]>>17&3),banks=2u<<(geometries[i]>>19&3);
        const unsigned width=8*bw*8*aspect,stored_height=8*bh*banks/aspect;
        rt_depth_layout d={0};CHECK(decode(geometries[i],3|(2u<<13),1,width,8,width*stored_height,RT_DEPTH_ASPECT_Z,&d)==0);guarded_copy(&d);
    }
}

static void refusal(uint32_t info,uint32_t z,uint32_t s,unsigned width,unsigned height,unsigned pixels,rt_depth_aspect aspect)
{
    rt_depth_layout out,before;memset(&out,0xa5,sizeof out);memcpy(&before,&out,sizeof out);
    CHECK(decode(info,z,s,width,height,pixels,aspect,&out)==-1);CHECK(memcmp(&out,&before,sizeof out)==0);
}
int main(void)
{
    canonical();direct_and_stride();geometry_boundaries();
    for(unsigned samples=1;samples<4;++samples)refusal(0x001d0c40,1|samples<<2,1,256,8,32768,RT_DEPTH_ASPECT_Z);
    refusal(0x001d0c40,1|(1u<<2),1,256,8,32768,RT_DEPTH_ASPECT_STENCIL);
    refusal(0x001d0c40,0,1,256,8,32768,RT_DEPTH_ASPECT_Z);
    refusal(0x001d0c40,2,1,256,8,32768,RT_DEPTH_ASPECT_Z);
    refusal(0x001d0c40,1,0,256,8,32768,RT_DEPTH_ASPECT_STENCIL);
    refusal(0x001d0c40,1,1,256,8,32768,(rt_depth_aspect)99);
    refusal(0x001d0c41,1,1,256,8,32768,RT_DEPTH_ASPECT_Z);
    refusal(0x001d0a40,1,1,256,8,32768,RT_DEPTH_ASPECT_Z);
    refusal(0x00050c40,1,1,256,8,32768,RT_DEPTH_ASPECT_Z);
    refusal(0x001d0c50,1,1,256,8,32768,RT_DEPTH_ASPECT_Z);
    refusal(0x001d0c40,1|(7u<<13),1,256,8,32768,RT_DEPTH_ASPECT_Z);
    refusal(0x001d0c40,1,1|(7u<<13),256,8,32768,RT_DEPTH_ASPECT_STENCIL);
    refusal(0x001d0c40,1,1,128,8,32768,RT_DEPTH_ASPECT_Z);
    refusal(0x001d0c40,1,1,256,8,64,RT_DEPTH_ASPECT_Z);
    refusal(0x001a0c40,3|(2u<<13),1,128,64,128*64,RT_DEPTH_ASPECT_STENCIL);
    printf("depth layouts: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
