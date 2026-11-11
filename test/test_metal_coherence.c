/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Owned render/compute/CPU accesses to the same tiled pool bytes, on a real Metal device. */
#include "runtime/render_executor.h"
#include "runtime/render_calls.h"
#include "runtime/tiling.h"
#include "gcn_programs.h"
#include "metal_executor_programs.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

enum { POOL_BYTES = 4 << 20, VS_CODE = 0x1000, PS_CODE = 0x2000, COPY_CODE = 0x3000, FORMAT_CODE = 0x4000,
       POSITIONS = 0x10000, COLORS = 0x11000, TARGET = 0x100000, COPY = 0x300000,
       DST_XYZW = 4 | 5 << 3 | 6 << 6 | 7 << 9, CANARY = 0xcd };
static const uint64_t guest_base = UINT64_C(0x3000000000);
/* LLVM bonaire assembler: copy one raw dword at x + 64*s8 between s[0:3] and s[4:7]. */
static const uint32_t copy_words[] = {0x7e060208, 0x34060686, 0x4a000700, 0x34020082, 0xe0301000, 0x80000201, 0xbf8c0f70, 0xe0701000, 0x80010201, 0xbf810000};
static unsigned scenarios, checks, failures;
#define CHECK(c) do { ++checks; if (!(c)) { ++failures; fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
static uint64_t at(unsigned offset) { return guest_base + offset; }
static void descriptor(uint32_t out[4], uint64_t address, unsigned records, unsigned stride)
{
    out[0] = (uint32_t)address; out[1] = (uint32_t)(address >> 32) | stride << 16;
    out[2] = records; out[3] = DST_XYZW | 7u << 12 | 14u << 15;
}
static int run(rt_executor *x, const rt_render_call *calls, unsigned count, const char *reason)
{
    char error[512] = "";
    const int result = rt_executor_run_calls(x, calls, count, error, sizeof error);
    if ((reason && (result != -1 || !strstr(error, reason))) || (!reason && result))
        fprintf(stderr, "expected %s, got%d: %s\n", reason ? reason : "success", result, error);
    CHECK(reason ? result == -1 && strstr(error, reason) : result == 0);
    ++scenarios; return result;
}
static rt_color_target target(unsigned offset, unsigned width, unsigned height, unsigned tile, unsigned swap, unsigned srgb)
{
    return (rt_color_target){.address=at(offset), .pitch=width, .height=height, .format=0xa,
        .number_type=srgb ? 6u : 0u, .component_swap=swap, .tile_mode_index=tile, .samples=1};
}
static unsigned scene(rt_render_call c[10], rt_color_target color, unsigned mask)
{
    c[0] = (rt_render_call){.kind=RT_CALL_COLOR_TARGET}; c[0].u.color_target.enabled=1; c[0].u.color_target.target=color;
    c[1] = (rt_render_call){.kind=RT_CALL_VIEWPORT};
    c[1].u.viewport.zmax=1;
    c[1].u.viewport.scale[0]=c[1].u.viewport.translate[0]=(float)color.pitch / 2;
    c[1].u.viewport.scale[1]=c[1].u.viewport.translate[1]=(float)color.height / 2;
    c[1].u.viewport.scale[2]=1;
    c[2]=(rt_render_call){.kind=RT_CALL_VERTEX_SHADER};
    c[2].u.vertex_shader=(rt_vertex_shader){.code=at(VS_CODE), .vgprs=12, .sgprs=16, .user_sgprs=8, .parameter_count=1, .position_format=4};
    c[3]=(rt_render_call){.kind=RT_CALL_PIXEL_SHADER};
    c[3].u.pixel_shader=(rt_pixel_shader){.code=at(PS_CODE), .vgprs=12, .sgprs=8, .color_formats=4,
        .input_enable=2, .input_address=2, .interpolated=1, .color_mask=15};
    c[4]=(rt_render_call){.kind=RT_CALL_USER_DATA}; c[4].u.user_data.stage=RT_STAGE_VERTEX; c[4].u.user_data.count=8;
    descriptor(c[4].u.user_data.words, at(POSITIONS), 3, 16); descriptor(c[4].u.user_data.words+4, at(COLORS), 3, 16);
    c[5]=(rt_render_call){.kind=RT_CALL_REGISTER}; c[5].u.reg.address=0xa08e; c[5].u.reg.value=mask;
    c[6]=(rt_render_call){.kind=RT_CALL_REGISTER}; c[6].u.reg.address=0xa00c;
    c[7]=(rt_render_call){.kind=RT_CALL_REGISTER}; c[7].u.reg.address=0xa00d; c[7].u.reg.value=color.pitch | color.height << 16;
    c[8]=(rt_render_call){.kind=RT_CALL_DRAW_AUTO}; c[8].u.draw_auto.index_count=3;
    return 9;
}
static void copy(rt_executor *x, unsigned from, unsigned to, unsigned words)
{
    rt_render_call c[4]={{0}};
    c[0].kind=RT_CALL_COMPUTE_SHADER;
    c[0].u.compute_shader=(rt_compute_shader){.code=at(COPY_CODE), .vgprs=12, .sgprs=16, .user_sgprs=8,
        .workgroup_id_enables=1, .thread_id_components=2, .threads={64,1,1}};
    c[1].kind=RT_CALL_USER_DATA; c[1].u.user_data.stage=RT_STAGE_COMPUTE; c[1].u.user_data.count=8;
    descriptor(c[1].u.user_data.words,at(from),words*4,0); descriptor(c[1].u.user_data.words+4,at(to),words*4,0);
    c[2].kind=RT_CALL_DISPATCH; c[2].u.dispatch.groups[0]=words/64; c[2].u.dispatch.groups[1]=c[2].u.dispatch.groups[2]=1;
    run(x,c,3,NULL);
}
static int near_byte(unsigned value, float expected) { return fabsf((float)value-255*expected)<=2; }
static float encode(float value, unsigned srgb)
{
    return srgb ? (value<=0.0031308f ? value*12.92f : 1.055f*powf(value,1/2.4f)-0.055f) : value;
}
static void check_solid(uint32_t raw, unsigned swap, unsigned srgb)
{
    CHECK(near_byte(raw >> (swap ? 16 : 0) &255,encode(0.25f,srgb)));
    CHECK(near_byte(raw >>8 &255,encode(0.5f,srgb)));
    CHECK(near_byte(raw >> (swap ? 0 : 16) &255,encode(0.75f,srgb)));
    CHECK(raw >>24==255);
}
static uint32_t bgra(uint32_t raw, unsigned swap)
{
    return swap ? raw : (raw & 0xff00ff00u) | (raw &255)<<16 | (raw >>16 &255);
}
static void read_pixels(rt_executor *x, uint64_t address, uint32_t *pixels, unsigned width, unsigned height)
{
    char error[256]="";
    CHECK(rt_executor_read_target(x,address,pixels,width,height,error,sizeof error)==0);
}
int main(void)
{
    char name[]="/tmp/paleblood-metal-coherence-XXXXXX", error[256]="";
    const int fd=mkstemp(name); if(fd<0)return 1;
    CHECK(unlink(name)==0); CHECK(ftruncate(fd,POOL_BYTES)==0);
    unsigned char *pool=mmap(NULL,POOL_BYTES,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0); if(pool==MAP_FAILED)return 1;
    memcpy(pool+VS_CODE,vertex_position_color,sizeof vertex_position_color); memcpy(pool+PS_CODE,pixel_color,sizeof pixel_color);
    memcpy(pool+COPY_CODE,copy_words,sizeof copy_words); memcpy(pool+FORMAT_CODE,vertex_descriptor_format,sizeof vertex_descriptor_format);
    const float positions[12]={-1,-1,0,1,3,-1,0,1,-1,3,0,1}; memcpy(pool+POSITIONS,positions,sizeof positions);
    const float colors[12]={0.25f,0.5f,0.75f,1,0.25f,0.5f,0.75f,1,0.25f,0.5f,0.75f,1}; memcpy(pool+COLORS,colors,sizeof colors);
    rt_executor *x=NULL; const rt_executor_memory memory={fd,POOL_BYTES,guest_base};
    CHECK(rt_executor_open(&memory,&x,error,sizeof error)==0); if(!x)return 1;
    CHECK(close(fd)==0); /* Executor and caller mappings keep their shared allocation alive. */
    rt_render_call calls[10];
    const unsigned modes[]={8,31,9,13,10,14};
    for(unsigned mode=0;mode<6;++mode)for(unsigned swap=0;swap<2;++swap)for(unsigned srgb=0;srgb<2;++srgb){
        const unsigned width=mode<4?16:128, height=8;
        rt_tiling layout; CHECK(rt_tiling_layout(modes[mode],4,width,height,&layout,error,sizeof error)==0);
        const unsigned span=(unsigned)rt_tiling_size(&layout,1), offset=TARGET+(4*mode+2*swap+srgb)*0x10000;
        CHECK(span && offset+span+256<POOL_BYTES); memset(pool+offset-256,CANARY,span+512);
        const rt_color_target color=target(offset,width,height,modes[mode],swap,srgb);
        rt_executor_reset(x); unsigned n=scene(calls,color,0); run(x,calls,n,NULL);
        for(unsigned i=0;i<span;++i)CHECK(pool[offset+i]==CANARY); /* Load preserves initial pixels with all writes masked. */
        n=scene(calls,color,15); run(x,calls,n,NULL);
        unsigned char *active=calloc(span,1); CHECK(active!=NULL); if(!active)return 1;
        uint32_t pixels[128*8]; read_pixels(x,color.address,pixels,width,height);
        for(unsigned y=0;y<height;++y)for(unsigned col=0;col<width;++col){
            const unsigned byte=(unsigned)rt_tiling_offset(&layout,col,y,0); uint32_t raw; memcpy(&raw,pool+offset+byte,4);
            check_solid(raw,swap,srgb); CHECK(pixels[y*width+col]==bgra(raw,swap)); memset(active+byte,1,4);
        }
        for(unsigned i=0;i<span;++i)if(!active[i])CHECK(pool[offset+i]==CANARY);
        for(unsigned i=0;i<256;++i)CHECK(pool[offset-256+i]==CANARY && pool[offset+span+i]==CANARY);
        free(active);
        memset(pool+COPY,0x5a,span); copy(x,offset,COPY,span/4);
        CHECK(memcmp(pool+COPY,pool+offset,span)==0); /* Draw -> raw buffer compute, including tiled padding. */
        for(unsigned i=0;i<span/4;++i)((uint32_t *)(pool+COPY))[i]=0xff221100u+i;
        copy(x,COPY,offset,span/4); read_pixels(x,color.address,pixels,width,height);
        for(unsigned y=0;y<height;++y)for(unsigned col=0;col<width;++col){
            const unsigned word=(unsigned)rt_tiling_offset(&layout,col,y,0)/4;
            CHECK(pixels[y*width+col]==bgra(0xff221100u+word,swap));
        }
        n=scene(calls,color,0); run(x,calls,n,NULL); CHECK(memcmp(pool+COPY,pool+offset,span)==0);
        /* A CPU update is visible to readback and the next cached draw, including after state reset. */
        uint32_t patch=0x831f7c25; memcpy(pool+offset,&patch,4); rt_executor_reset(x);
        read_pixels(x,color.address,pixels,1,1); CHECK(pixels[0]==bgra(patch,swap));
        n=scene(calls,color,0); run(x,calls,n,NULL); read_pixels(x,color.address,pixels,1,1); CHECK(pixels[0]==bgra(patch,swap));
    }
    /* Overlapping cached target views, with independent partial draws, preserve shared bytes. */
    rt_color_target a=target(TARGET,16,16,31,0,0), b=target(TARGET+256,16,16,31,0,0);
    rt_executor_reset(x); /* Use new addresses to avoid the intentionally immutable target-shape cache. */
    a.address=at(0x280000); b.address=at(0x280100); memset(pool+0x280000,CANARY,1280);
    unsigned n=scene(calls,a,15); run(x,calls,n,NULL);
    check_solid(*(uint32_t *)(pool+0x280000),0,0);
    unsigned char expected[1280]; memcpy(expected,pool+0x280000,sizeof expected);
    const float other_colors[12]={0.75f,0.125f,0.5f,1,0.75f,0.125f,0.5f,1,0.75f,0.125f,0.5f,1};
    memcpy(pool+COLORS,other_colors,sizeof other_colors);
    n=scene(calls,b,15); calls[7].u.reg.value=8|16u<<16; run(x,calls,n,NULL);
    for(unsigned y=0;y<16;++y)for(unsigned col=0;col<8;++col){
        uint32_t raw; memcpy(&raw,pool+0x280100+4*(y*16+col),4);
        CHECK(near_byte(raw &255,0.75f) && near_byte(raw >>8 &255,0.125f) && near_byte(raw >>16 &255,0.5f) && raw >>24==255);
        memcpy(expected+256+4*(y*16+col),&raw,4);
    }
    CHECK(memcmp(expected,pool+0x280000,sizeof expected)==0);
    n=scene(calls,a,0); run(x,calls,n,NULL); CHECK(memcmp(expected,pool+0x280000,sizeof expected)==0);
    memcpy(pool+COLORS,colors,sizeof colors);
    /* First draw is observable even when the next ordered event is unsupported. */
    a.address=at(0x290000); n=scene(calls,a,15); calls[n]=(rt_render_call){.kind=RT_CALL_REGISTER}; calls[n].u.reg.address=0xffff;
    run(x,calls,n+1,"register 0xffff"); check_solid(*(uint32_t *)(pool+0x290000),0,0);
    /* A later draw reads the first target through a buffer descriptor, without an image binding. */
    rt_color_target consumer=target(0x2c0000,16,16,31,0,0); n=scene(calls,consumer,15);
    calls[2].u.vertex_shader.code=at(FORMAT_CODE);
    descriptor(calls[4].u.user_data.words+4,at(0x290000),256,4);
    calls[4].u.user_data.words[7]=DST_XYZW | 10u << 15; /* 8_8_8_8 UNORM buffer input. */
    run(x,calls,n,NULL); check_solid(*(uint32_t *)(pool+0x2c0000),0,0);
    /* Both pool endpoints and the complete padded span are checked before a texture is used. */
    const unsigned invalid_modes[]={0,1,32};
    for(unsigned i=0;i<3;++i){a=target(0x2a0000+i*0x1000,16,16,invalid_modes[i],0,0); n=scene(calls,a,15); run(x,calls,n,"tiling index");}
    a=target(0x2b0000,16,16,10,0,0); n=scene(calls,a,15); run(x,calls,n,"macro tiles");
    a=target(POOL_BYTES-1024,16,16,31,0,0); memset(pool+POOL_BYTES-1024,0,1024); n=scene(calls,a,15); run(x,calls,n,NULL);
    check_solid(*(uint32_t *)(pool+POOL_BYTES-4),0,0);
    a.address+=256; n=scene(calls,a,15); run(x,calls,n,"outside the GPU-visible pool");
    a.address=guest_base-256; n=scene(calls,a,15); run(x,calls,n,"outside the GPU-visible pool");
    a=target(0x1c0000,128,8,14,0,0); a.address=at(POOL_BYTES-4096); n=scene(calls,a,15); run(x,calls,n,"outside the GPU-visible pool");
    a=target(0x290000,16,16,8,0,0); n=scene(calls,a,15); run(x,calls,n,"changed size, format or tiling");
    rt_executor_close(x); CHECK(munmap(pool,POOL_BYTES)==0);
    printf("metal coherence: %u scenarios, %u checks, %u failures\n",scenarios,checks,failures);
    return failures?1:0;
}
