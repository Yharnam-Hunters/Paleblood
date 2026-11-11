/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Owned one-pixel probes observe raw image results, independent of framebuffer quantization. */
#include "runtime/render_executor.h"
#include "runtime/render_calls.h"
#include "runtime/tiling.h"
#include "gcn_programs.h"
#include "metal_image_programs.h"
#include "metal_image_slots.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
enum { POOL=8<<20, VS=0x1000, PROGRAM=0x2000, COLOR_SHADER=0x18000, COMPUTE_SHADER=0x19000, COMPUTE_INPUT=0x1a000, POSITIONS=0x10000, COLORS=0x11000,
       PROBE=0x30000, OUTPUT_DESCRIPTOR=0x40000, IMAGE_DESCRIPTOR=0x41000, SAMPLER_DESCRIPTOR=0x42000,
       IMAGE=0x50000, TARGET=0x700000, IDENTITY=4|5<<3|6<<6|7<<9, SENTINEL=0x5a5aa5a5 };
static const uint64_t guest_base=UINT64_C(0x3000000000);
static unsigned scenarios,checks,failures;
#define CHECK(x) do { ++checks; if(!(x)){ ++failures; if(failures<20)fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); } } while(0)
static uint64_t at(unsigned offset){return guest_base+offset;}
static uint32_t bits(float x){uint32_t b;memcpy(&b,&x,4);return b;}
static void buffer_descriptor(uint32_t out[4],unsigned offset)
{
    out[0]=(uint32_t)at(offset);out[1]=(uint32_t)(at(offset)>>32);out[2]=64;out[3]=0;
}
static void pointer(uint32_t out[2],unsigned offset){out[0]=(uint32_t)at(offset);out[1]=(uint32_t)(at(offset)>>32);}
static void descriptor(uint32_t out[8],unsigned offset,unsigned data,unsigned number,unsigned width,unsigned height,
                       unsigned pitch,unsigned tile,unsigned type,unsigned layers,unsigned base,unsigned last,unsigned first,unsigned final,unsigned swizzle)
{
    memset(out,0,8*sizeof *out);uint64_t address=at(offset)>>8;
    out[0]=(uint32_t)address;out[1]=(uint32_t)(address>>32)|data<<20|number<<26;
    out[2]=(width-1)|(height-1)<<14;out[3]=swizzle|base<<12|last<<16|tile<<20|type<<28;
    out[4]=(layers-1)|(pitch-1)<<13;out[5]=first|final<<13;
}
static int probe(rt_executor *x,unsigned char *pool,const uint32_t *program,size_t program_bytes,const uint32_t user[16],uint32_t written,const char *reason)
{
    memcpy(pool+PROGRAM,program,program_bytes);uint32_t *output=(uint32_t *)(pool+PROBE);
    for(unsigned i=0;i<128;++i)output[i]=SENTINEL;
    rt_render_call c[32]={{0}};unsigned n=0;
    c[n].kind=RT_CALL_COLOR_TARGET;c[n].u.color_target.enabled=1;
    c[n++].u.color_target.target=(rt_color_target){.address=at(TARGET),.pitch=8,.height=8,.format=0xa,.tile_mode_index=31,.samples=1};
    c[n].kind=RT_CALL_VIEWPORT;c[n].u.viewport.zmax=1;
    c[n].u.viewport.scale[0]=c[n].u.viewport.scale[1]=c[n].u.viewport.translate[0]=c[n].u.viewport.translate[1]=4;
    c[n++].u.viewport.scale[2]=1;
    c[n].kind=RT_CALL_VERTEX_SHADER;c[n++].u.vertex_shader=(rt_vertex_shader){.code=at(VS),.vgprs=12,.sgprs=16,.user_sgprs=8,.parameter_count=1,.position_format=4};
    c[n].kind=RT_CALL_PIXEL_SHADER;c[n++].u.pixel_shader=(rt_pixel_shader){.code=at(PROGRAM),.vgprs=16,.sgprs=40,.user_sgprs=16,.color_formats=9,.input_enable=2,.input_address=2,.color_mask=15};
    uint32_t vertex[8];buffer_descriptor(vertex,POSITIONS);buffer_descriptor(vertex+4,COLORS);
    vertex[1]|=16u<<16;vertex[2]=3;vertex[5]|=16u<<16;vertex[6]=3;
    c[n].kind=RT_CALL_USER_DATA;c[n].u.user_data.stage=RT_STAGE_VERTEX;c[n].u.user_data.count=8;memcpy(c[n++].u.user_data.words,vertex,sizeof vertex);
    for(unsigned i=0;i<16;++i)if(written&(1u<<i)){
        c[n].kind=RT_CALL_USER_DATA;c[n].u.user_data.stage=RT_STAGE_PIXEL;c[n].u.user_data.slot=i;c[n].u.user_data.count=1;c[n++].u.user_data.words[0]=user[i];
    }
    c[n].kind=RT_CALL_REGISTER;c[n].u.reg.address=0xa08e;c[n++].u.reg.value=0; /* Covered fragment still performs its buffer stores. */
    c[n].kind=RT_CALL_REGISTER;c[n++].u.reg.address=0xa00c;
    c[n].kind=RT_CALL_REGISTER;c[n].u.reg.address=0xa00d;c[n++].u.reg.value=1|1u<<16;
    c[n].kind=RT_CALL_DRAW_AUTO;c[n++].u.draw_auto.index_count=3;
    char error[512]="";int result=rt_executor_run_calls(x,c,n,error,sizeof error);
    if(reason){if(result!=-1||!strstr(error,reason))fprintf(stderr,"expected %s, got%d: %s\n",reason,result,error);CHECK(result==-1&&strstr(error,reason));}
    else{if(result)fprintf(stderr,"image probe: %s\n",error);CHECK(result==0);}
    ++scenarios;return result;
}
static void float_results(unsigned char *pool,const float value[4],float tolerance)
{
    float actual[4];memcpy(actual,pool+PROBE,sizeof actual);
    for(unsigned i=0;i<4;++i){if(fabsf(actual[i]-value[i])>tolerance&&failures<20)fprintf(stderr,"component%u: %.9g expected%.9g\n",i,actual[i],value[i]);CHECK(fabsf(actual[i]-value[i])<=tolerance);}
    for(unsigned i=4;i<16;++i)CHECK(((uint32_t *)(pool+PROBE))[i]==SENTINEL);
}
/* Metal Shading Language specification section 8.7.7 specifies sRGB precision in encoded-byte units:
   inverse-convert the result without integer rounding, with error at most half a byte. */
static void srgb_results(unsigned char *pool,const float value[4])
{
    float actual[4];memcpy(actual,pool+PROBE,sizeof actual);
    for(unsigned i=0;i<3;++i){
        const double encoded=255*(actual[i]<=0.0031308?12.92*actual[i]:1.055*pow(actual[i],1.0/2.4)-0.055);
        const double original=round(255*(value[i]<=0.0031308?12.92*value[i]:1.055*pow(value[i],1.0/2.4)-0.055));
        CHECK(fabs(encoded-original)<=0.5);
    }
    CHECK(fabsf(actual[3]-value[3])<=0.00001f);
    for(unsigned i=4;i<16;++i)CHECK(((uint32_t *)(pool+PROBE))[i]==SENTINEL);
}
static float linear(unsigned x){float s=(float)x/255;return s<=0.04045f?s/12.92f:powf((s+0.055f)/1.055f,2.4f);}
static void put_bits(unsigned char data[16],unsigned *at_bit,unsigned value,unsigned count)
{
    for(unsigned i=0;i<count;++i,++*at_bit)if(value&(1u<<i))data[*at_bit/8]|=(unsigned char)(1u<<(*at_bit%8));
}
static void element(unsigned char out[16],unsigned data,unsigned number,unsigned bytes,float expected[4])
{
    memset(out,0,16);expected[0]=expected[1]=expected[2]=0;expected[3]=1;
    if(data==1||data==3||data==10){const unsigned char values[4]={64,128,192,255};memcpy(out,values,bytes);
        for(unsigned i=0;i<bytes;++i)expected[i]=number==9&&i<3?linear(values[i]):(float)values[i]/255;return;}
    if(data==4||data==11||data==14){const float values[4]={0.25f,0.5f,0.75f,1};memcpy(out,values,bytes);for(unsigned i=0;i<bytes/4;++i)expected[i]=values[i];return;}
    if(data==38||data==39){unsigned count=data==38?1:2;for(unsigned i=0;i<count;++i){out[i*8]=number==1?(i?64:192):(i?192:64);expected[i]=number==1?(float)(int8_t)out[i*8]/127:(float)out[i*8]/255;}return;}
    if(data==41){unsigned at_bit=0;put_bits(out,&at_bit,64,7);const unsigned channels[4]={64,32,16,127};
        for(unsigned i=0;i<4;++i){put_bits(out,&at_bit,channels[i],7);put_bits(out,&at_bit,channels[i],7);expected[i]=number==9&&i<3?linear(channels[i]*2):(float)(channels[i]*2)/255;}
        put_bits(out,&at_bit,0,2);return;}
    const unsigned start=data==35?0:8;out[start]=0xef;out[start+1]=0x7b;
    expected[0]=expected[2]=number==9?linear(123):123.0f/255;expected[1]=number==9?linear(125):125.0f/255;
    if(data==36)memset(out,255,8);if(data==37){out[0]=128;expected[3]=128.0f/255;}
}
static void fill(unsigned char *pool,const uint32_t words[8],unsigned bytes,unsigned block,const unsigned char *element_data)
{
    rt_image_descriptor d;rt_image_descriptor_decode(words,&d);rt_tiling_level levels[16];char error[256]="";
    CHECK(rt_tiling_mip_chain(d.tiling_index,bytes,block,d.pow2_pad,d.pitch,d.height,d.width,d.height,d.last_level+1,d.layers,levels,error,sizeof error)==0);
    for(unsigned level=0;level<=d.last_level;++level){size_t size=(size_t)levels[level].columns*levels[level].rows*bytes*d.layers;
        unsigned char *rows=malloc(size);CHECK(rows!=NULL);if(!rows)exit(1);
        for(size_t i=0;i<size;i+=bytes)memcpy(rows+i,element_data,bytes);
        rt_tiling_tile(&levels[level].layout,rows,pool+(d.address-guest_base)+levels[level].offset,levels[level].columns,levels[level].rows,d.layers);free(rows);}
}
static void fill_levels(unsigned char *pool,const uint32_t words[8])
{
    rt_image_descriptor d;rt_image_descriptor_decode(words,&d);rt_tiling_level levels[16];char error[256]="";
    CHECK(rt_tiling_mip_chain(d.tiling_index,4,1,d.pow2_pad,d.pitch,d.height,d.width,d.height,d.last_level+1,d.layers,levels,error,sizeof error)==0);
    for(unsigned level=0;level<=d.last_level;++level){
        const size_t texels=(size_t)levels[level].columns*levels[level].rows;unsigned char *rows=malloc(texels*4*d.layers);CHECK(rows!=NULL);if(!rows)exit(1);
        for(unsigned slice=0;slice<d.layers;++slice)for(size_t i=0;i<texels;++i){
            unsigned char *to=rows+4*(slice*texels+i);to[0]=(unsigned char)(32+40*level+7*slice);to[1]=(unsigned char)(64+level);to[2]=(unsigned char)(128+slice);to[3]=255;
        }
        rt_tiling_tile(&levels[level].layout,rows,pool+d.address-guest_base+levels[level].offset,levels[level].columns,levels[level].rows,d.layers);free(rows);
    }
}
static void gpu_update(rt_executor *x,unsigned char *pool,unsigned destination)
{
    memset(pool+COMPUTE_INPUT,0,64*sizeof(float));rt_render_call c[3]={{0}};
    c[0].kind=RT_CALL_COMPUTE_SHADER;c[0].u.compute_shader=(rt_compute_shader){.code=at(COMPUTE_SHADER),.vgprs=12,.sgprs=16,.user_sgprs=8,.thread_id_components=2,.threads={64,1,1}};
    c[1].kind=RT_CALL_USER_DATA;c[1].u.user_data.stage=RT_STAGE_COMPUTE;c[1].u.user_data.count=8;
    buffer_descriptor(c[1].u.user_data.words,COMPUTE_INPUT);c[1].u.user_data.words[2]=64*4;
    buffer_descriptor(c[1].u.user_data.words+4,destination);c[1].u.user_data.words[6]=64*4;
    c[2].kind=RT_CALL_DISPATCH;c[2].u.dispatch.groups[0]=c[2].u.dispatch.groups[1]=c[2].u.dispatch.groups[2]=1;
    char error[256]="";CHECK(rt_executor_run_calls(x,c,3,error,sizeof error)==0);++scenarios;
    CHECK(*(uint32_t *)(pool+destination)==bits(1));
}
static void draw_source(rt_executor *x,unsigned char *pool)
{
    const float gradient[12]={0,0,0.25f,1,2,0,0.25f,1,0,2,0.25f,1};memcpy(pool+COLORS,gradient,sizeof gradient);
    rt_render_call c[6]={{0}};
    c[0].kind=RT_CALL_COLOR_TARGET;c[0].u.color_target.enabled=1;c[0].u.color_target.target=(rt_color_target){.address=at(0x680000),.pitch=16,.height=16,.format=0xa,.tile_mode_index=31,.samples=1};
    c[1].kind=RT_CALL_VIEWPORT;c[1].u.viewport.scale[0]=c[1].u.viewport.scale[1]=c[1].u.viewport.translate[0]=c[1].u.viewport.translate[1]=8;
    c[1].u.viewport.scale[2]=c[1].u.viewport.zmax=1;
    c[2].kind=RT_CALL_VERTEX_SHADER;c[2].u.vertex_shader=(rt_vertex_shader){.code=at(VS),.vgprs=12,.sgprs=16,.user_sgprs=8,.parameter_count=1,.position_format=4};
    c[3].kind=RT_CALL_PIXEL_SHADER;c[3].u.pixel_shader=(rt_pixel_shader){.code=at(COLOR_SHADER),.vgprs=12,.sgprs=8,.color_formats=4,.input_enable=2,.input_address=2,.interpolated=1,.color_mask=15};
    c[4].kind=RT_CALL_USER_DATA;c[4].u.user_data.stage=RT_STAGE_VERTEX;c[4].u.user_data.count=8;
    buffer_descriptor(c[4].u.user_data.words,POSITIONS);c[4].u.user_data.words[1]|=16u<<16;c[4].u.user_data.words[2]=3;
    buffer_descriptor(c[4].u.user_data.words+4,COLORS);c[4].u.user_data.words[5]|=16u<<16;c[4].u.user_data.words[6]=3;
    c[5].kind=RT_CALL_DRAW_AUTO;c[5].u.draw_auto.index_count=3;
    char error[256]="";rt_executor_reset(x);CHECK(rt_executor_run_calls(x,c,6,error,sizeof error)==0);++scenarios;
    const unsigned char *pixel=pool+0x680000+4*(1*16+1);
    CHECK(abs((int)pixel[0]-24)<=2&&abs((int)pixel[1]-24)<=2&&abs((int)pixel[2]-64)<=2&&pixel[3]==255);
}
int main(void)
{
    char name[]="/tmp/paleblood-metal-images-XXXXXX",error[256]="";int fd=mkstemp(name);if(fd<0)return 1;
    CHECK(unlink(name)==0);CHECK(ftruncate(fd,POOL)==0);unsigned char *pool=mmap(NULL,POOL,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);if(pool==MAP_FAILED)return 1;
    memcpy(pool+COLOR_SHADER,pixel_color,sizeof pixel_color);memcpy(pool+COMPUTE_SHADER,scale_add,sizeof scale_add);
    memcpy(pool+VS,vertex_position_color,sizeof vertex_position_color);const float positions[12]={-1,-1,0,1,3,-1,0,1,-1,3,0,1};memcpy(pool+POSITIONS,positions,sizeof positions);
    const rt_executor_memory memory={fd,POOL,guest_base};rt_executor *x=NULL;CHECK(rt_executor_open(&memory,&x,error,sizeof error)==0);if(!x)return 1;
    uint32_t user[16]={0};buffer_descriptor((uint32_t *)(pool+OUTPUT_DESCRIPTOR),PROBE);buffer_descriptor(user+12,PROBE);
    const struct {unsigned data,number,components,bytes,block;} formats[]={
        {1,0,1,1,1},{3,0,2,2,1},{4,7,1,4,1},{11,7,2,8,1},{14,7,4,16,1},{10,0,4,4,1},{10,9,4,4,1},
        {35,0,4,8,4},{35,9,4,8,4},{36,0,4,16,4},{36,9,4,16,4},{37,0,4,16,4},{37,9,4,16,4},
        {38,0,1,8,4},{38,1,1,8,4},{39,0,2,16,4},{39,1,2,16,4},{41,0,4,16,4},{41,9,4,16,4}};
    for(unsigned i=0;i<sizeof formats/sizeof formats[0];++i){
        unsigned swizzle=formats[i].components==1?4|0<<3|1<<6|1<<9:formats[i].components==2?4|5<<3|0<<6|1<<9:IDENTITY;
        descriptor(user,IMAGE+i*0x4000,formats[i].data,formats[i].number,16,8,16,13,RT_IMAGE_2D,1,0,0,0,0,swizzle);
        unsigned char data[16];float expected[4];element(data,formats[i].data,formats[i].number,formats[i].bytes,expected);
        if(formats[i].components==1){expected[1]=0;expected[2]=expected[3]=1;}
        fill(pool,user,formats[i].bytes,formats[i].block,data);rt_executor_reset(x);
        unsigned before=failures;
        if(!probe(x,pool,image_probe_load_mip,sizeof image_probe_load_mip,user,0xffff,NULL)){
            if(formats[i].number==9)srgb_results(pool,expected);else float_results(pool,expected,0.0001f);
        }
        if(failures!=before)fprintf(stderr,"format DATA_FORMAT=%u NUM_FORMAT=%u\n",formats[i].data,formats[i].number);
    }
    /* Integer return words remain integers; descriptor type changes at a fixed program address are live. */
    for(unsigned type=0;type<3;++type){unsigned data=type==2?10:4,number=type==0?4:type==1?5:4,components=type==2?4:1;
        descriptor(user,0xb0000,data,number,8,8,8,31,RT_IMAGE_2D,1,0,0,0,0,components==1?4|0<<3|1<<6|1<<9:IDENTITY);
        unsigned char raw[16]={2,11,31,255};uint32_t value=type==0?0x12345678u:(uint32_t)-12345;if(type<2)memcpy(raw,&value,4);
        fill(pool,user,4,1,raw);rt_executor_reset(x);probe(x,pool,image_probe_load,sizeof image_probe_load,user,0xffff,NULL);
        uint32_t *got=(uint32_t *)(pool+PROBE);CHECK(got[0]==(type<2?value:2));CHECK(got[1]==(type<2?0:11));CHECK(got[2]==(type<2?1:31));CHECK(got[3]==(type<2?1:255));}
    rt_executor_close(x);x=NULL;CHECK(rt_executor_open(&memory,&x,error,sizeof error)==0);if(!x)return 1;
    /* Native views and samplers use live descriptors, then live texels at the same image address. */
    memset(user,0,sizeof user);descriptor(user,IMAGE,10,0,8,8,8,31,RT_IMAGE_2D,1,0,0,0,0,IDENTITY);
    unsigned char color[4]={64,128,192,255};fill(pool,user,4,1,color);pointer(user+12,OUTPUT_DESCRIPTOR);user[14]=user[15]=bits(0.5f);
    const float expected[4]={64.0f/255,128.0f/255,192.0f/255,1};
    probe(x,pool,image_probe_sample,sizeof image_probe_sample,user,0xffff,NULL);float_results(pool,expected,0.00001f);
    color[0]=128;fill(pool,user,4,1,color);probe(x,pool,image_probe_sample,sizeof image_probe_sample,user,0xffff,NULL);
    const float changed[4]={128.0f/255,128.0f/255,192.0f/255,1};float_results(pool,changed,0.00001f);
    user[3]=(user[3]&~0xfffu)|(6|5<<3|4<<6|1<<9);probe(x,pool,image_probe_sample,sizeof image_probe_sample,user,0xffff,NULL);
    const float swapped[4]={192.0f/255,128.0f/255,128.0f/255,1};float_results(pool,swapped,0.00001f);
    /* A descriptor loaded from memory must be read again even when the shader is cached. */
    memcpy(pool+IMAGE_DESCRIPTOR,user,8*sizeof *user);pointer(user,IMAGE_DESCRIPTOR);
    probe(x,pool,image_probe_sample_memory,sizeof image_probe_sample_memory,user,0xffff,NULL);float_results(pool,swapped,0.00001f);
    ((uint32_t *)(pool+IMAGE_DESCRIPTOR))[3]=(((uint32_t *)(pool+IMAGE_DESCRIPTOR))[3]&~0xfffu)|IDENTITY;
    probe(x,pool,image_probe_sample_memory,sizeof image_probe_sample_memory,user,0xffff,NULL);float_results(pool,changed,0.00001f);
    memcpy(user,pool+IMAGE_DESCRIPTOR,8*sizeof *user);memcpy(pool+SAMPLER_DESCRIPTOR,user+8,4*sizeof *user);pointer(user+8,SAMPLER_DESCRIPTOR);
    probe(x,pool,image_probe_sample_memory_sampler,sizeof image_probe_sample_memory_sampler,user,0xffff,NULL);float_results(pool,changed,0.00001f);
    memset(user+8,0,4*sizeof *user);
    rt_executor_reset(x);probe(x,pool,image_probe_sample,sizeof image_probe_sample,user,0xff00,"reads unset user data");
    /* All admitted dimensions, canonical tile modes, relative mip levels and array windows. */
    const unsigned tiles[]={8,31,9,13,10,14},types[]={RT_IMAGE_1D,RT_IMAGE_2D,RT_IMAGE_1D_ARRAY,RT_IMAGE_2D_ARRAY};
    for(unsigned tile=0;tile<6;++tile)for(unsigned type=0;type<4;++type){
        const int arrayed=type>=2,one_d=(type&1)==0;const unsigned height=one_d?1:8;
        memset(user,0,sizeof user);descriptor(user,0x100000,10,0,16,height,128,tiles[tile],types[type],arrayed?3:1,1,2,arrayed?1:0,arrayed?2:0,IDENTITY);
        if(arrayed)user[3]|=1u<<25; /* Three logical layers padded to four; view still starts at1. */
        fill_levels(pool,user);buffer_descriptor(user+12,PROBE);user[8]=1;
        if(one_d){user[9]=arrayed?1:0;user[10]=0;}else{user[9]=1;user[10]=arrayed?1:0;user[11]=0;}
        const unsigned slice=arrayed?2:0;const float expected_mip[4]={(72.0f+7*slice)/255,65.0f/255,(128.0f+slice)/255,1};
        rt_executor_reset(x);
        const uint32_t *code=arrayed?image_probe_load_array:image_probe_load_mip;size_t length=arrayed?sizeof image_probe_load_array:sizeof image_probe_load_mip;
        probe(x,pool,code,length,user,0xffff,NULL);float_results(pool,expected_mip,0.00001f);
        if(!arrayed){unsigned lod_slot=one_d?9:10;user[lod_slot]=1;probe(x,pool,code,length,user,0xffff,NULL);
            const float last_mip[4]={112.0f/255,66.0f/255,128.0f/255,1};float_results(pool,last_mip,0.00001f);
            user[lod_slot]=2;probe(x,pool,code,length,user,0xffff,"shader runtime stop");
            user[lod_slot]=0;probe(x,pool,code,length,user,0xffff,NULL);float_results(pool,expected_mip,0.00001f);
        }
    }
    rt_executor_close(x);x=NULL;CHECK(rt_executor_open(&memory,&x,error,sizeof error)==0);if(!x)return 1;
    /* Point/bilinear filtering and each normalized address axis, with a nonuniform2x2 image. */
    memset(user,0,sizeof user);descriptor(user,IMAGE,10,0,2,2,16,31,RT_IMAGE_2D,1,0,0,0,0,IDENTITY);
    rt_tiling_level small;CHECK(rt_tiling_mip_chain(31,4,1,0,16,2,2,2,1,1,&small,error,sizeof error)==0);
    const unsigned char quad[16]={255,0,0,255,0,255,0,255,0,0,255,255,255,255,255,255};
    rt_tiling_tile(&small.layout,quad,pool+IMAGE,2,2,1);pointer(user+12,OUTPUT_DESCRIPTOR);
    for(unsigned mode=0;mode<3;++mode)for(unsigned axis=0;axis<2;++axis)for(unsigned negative=0;negative<2;++negative){
        user[8]=mode<<(3*axis);user[10]=0;user[14]=user[15]=bits(0.25f);user[14+axis]=bits(negative?-0.25f:1.25f);
        const int high=mode==0?negative:!negative;float wanted[4]={high?0.0f:1.0f,high&&axis==0?1.0f:0.0f,high&&axis==1?1.0f:0.0f,1};
        probe(x,pool,image_probe_sample,sizeof image_probe_sample,user,0xffff,NULL);float_results(pool,wanted,0.00001f);
    }
    user[8]=0;user[10]=1u<<20|1u<<22;user[14]=user[15]=bits(0.5f);
    probe(x,pool,image_probe_sample,sizeof image_probe_sample,user,0xffff,NULL);const float blend[4]={0.5f,0.5f,0.5f,1};float_results(pool,blend,0.00001f);
    /* Invalid descriptors stop before native resource reads or allocation, and do not poison a later draw. */
    rt_executor_reset(x);probe(x,pool,image_probe_sample,sizeof image_probe_sample,user,0xf0ff,"reads unset user data");
    const uint32_t base_sampler=user[8],base_filter=user[10];
    const uint32_t bad_sampler[]={1u<<12,1u<<15,1u<<29,3,3u<<3,3u<<6};
    for(unsigned i=0;i<sizeof bad_sampler/sizeof *bad_sampler;++i){user[8]=bad_sampler[i];rt_executor_reset(x);
        probe(x,pool,image_probe_sample,sizeof image_probe_sample,user,0xffff,i<3?"sampler options":"sampler address mode");}
    user[8]=base_sampler;const uint32_t bad_filter[]={1u<<20,2u<<20|2u<<22,3u<<20|3u<<22};
    for(unsigned i=0;i<3;++i){user[10]=bad_filter[i];rt_executor_reset(x);probe(x,pool,image_probe_sample,sizeof image_probe_sample,user,0xffff,"sampler options");}
    user[10]=base_filter;rt_executor_reset(x);probe(x,pool,image_probe_sample,sizeof image_probe_sample,user,0xffff,NULL);float_results(pool,blend,0.00001f);
    const uint32_t good[8]={user[0],user[1],user[2],user[3],user[4],user[5],user[6],user[7]};
    for(unsigned invalid=0;invalid<7;++invalid){memcpy(user,good,sizeof good);user[8]=0;user[10]=0;
        const char *reason="";
        if(invalid==0){user[1]&=~(63u<<20);reason="image DATA_FORMAT";}
        if(invalid==1){user[3]=(user[3]&~(15u<<28))|10u<<28;reason="only 1D/2D";}
        if(invalid==2){user[3]=(user[3]&~(31u<<20))|1u<<20;reason="pixel image layout";}
        if(invalid==3){user[3]=(user[3]&~7u)|2;reason="component selector";}
        if(invalid==4){user[3]|=1u<<12;reason="mip or array view";}
        if(invalid==5){uint64_t address=at(POOL)>>8;user[0]=(uint32_t)address;user[1]=(user[1]&~63u)|(uint32_t)(address>>32);reason="outside the GPU-visible pool";}
        if(invalid==6){user[4]=(user[4]&~(0x3fffu<<13));reason="dimensions, layers or pitch";}
        rt_executor_reset(x);probe(x,pool,image_probe_sample,sizeof image_probe_sample,user,0xffff,reason);
    }
    /* Mixed integer fetch and floating sample: only sampled resources need filterability. */
    memset(user,0,sizeof user);descriptor(user,0x160000,4,4,8,8,8,31,RT_IMAGE_2D,1,0,0,0,0,4|0<<3|1<<6|1<<9);
    const uint32_t integer=0x12345678;unsigned char raw_integer[16]={0};memcpy(raw_integer,&integer,4);fill(pool,user,4,1,raw_integer);
    uint32_t second_image[8];descriptor(second_image,IMAGE,10,0,2,2,16,31,RT_IMAGE_2D,1,0,0,0,0,IDENTITY);
    rt_tiling_tile(&small.layout,quad,pool+IMAGE,2,2,1);memcpy(pool+IMAGE_DESCRIPTOR,second_image,sizeof second_image);
    pointer(user+8,IMAGE_DESCRIPTOR);pointer(user+10,OUTPUT_DESCRIPTOR);user[14]=1u<<20|1u<<22;
    rt_executor_reset(x);probe(x,pool,image_probe_two_images,sizeof image_probe_two_images,user,0xffff,NULL);
    uint32_t *words=(uint32_t *)(pool+PROBE);CHECK(words[0]==integer&&words[1]==0&&words[2]==1&&words[3]==1);
    CHECK(words[4]==bits(0.5f)&&words[5]==bits(0.5f)&&words[6]==bits(0.5f)&&words[7]==bits(1));
    for(unsigned i=8;i<16;++i)CHECK(words[i]==SENTINEL);
    /* A sampler on the integer image is admitted for point, refused for linear. */
    buffer_descriptor(user+12,PROBE);user[8]=user[9]=user[10]=user[11]=0;
    pointer(user+12,OUTPUT_DESCRIPTOR);user[14]=user[15]=bits(0.5f);rt_executor_reset(x);
    probe(x,pool,image_probe_sample,sizeof image_probe_sample,user,0xffff,NULL);CHECK(((uint32_t *)(pool+PROBE))[0]==integer);
    user[10]=1u<<20|1u<<22;rt_executor_reset(x);probe(x,pool,image_probe_sample,sizeof image_probe_sample,user,0xffff,"does not support linear filtering");
    /* Loaded descriptors use both pointer words at their proven48-bit width and permit an exact end. */
    memset(user,0,sizeof user);memcpy(pool+IMAGE_DESCRIPTOR,second_image,sizeof second_image);pointer(user,IMAGE_DESCRIPTOR);buffer_descriptor(user+12,PROBE);
    user[1]|=0xabcd0000u;rt_executor_reset(x);probe(x,pool,image_probe_load_memory,sizeof image_probe_load_memory,user,0xffff,NULL);
    const float red[4]={1,0,0,1};float_results(pool,red,0.00001f);
    memcpy(pool+POOL-32,second_image,sizeof second_image);pointer(user,POOL-32);rt_executor_reset(x);
    probe(x,pool,image_probe_load_memory,sizeof image_probe_load_memory,user,0xffff,NULL);float_results(pool,red,0.00001f);
    const uint64_t invalid_pointers[]={at(POOL-16),at(IMAGE_DESCRIPTOR)+2,guest_base-4};
    for(unsigned i=0;i<3;++i){user[0]=(uint32_t)invalid_pointers[i];user[1]=(uint32_t)(invalid_pointers[i]>>32);rt_executor_reset(x);
        probe(x,pool,image_probe_load_memory,sizeof image_probe_load_memory,user,0xffff,"descriptor is outside the GPU-visible pool or unaligned");}
    pointer(user,IMAGE_DESCRIPTOR);rt_executor_reset(x);probe(x,pool,image_probe_load_memory,sizeof image_probe_load_memory,user,0xfffe,"reads unset user data 0x1");
    rt_executor_reset(x);probe(x,pool,image_probe_load_memory,sizeof image_probe_load_memory,user,0xfffd,"reads unset user data 0x2");
    /* Multi-level image_sample retains fixed-LOD0 semantics; unknown LOD adjustments are explicit. */
    memset(user,0,sizeof user);descriptor(user,0x180000,10,0,16,8,16,31,RT_IMAGE_2D,1,0,2,0,0,IDENTITY);fill_levels(pool,user);
    pointer(user+12,OUTPUT_DESCRIPTOR);user[14]=user[15]=bits(0.5f);rt_executor_reset(x);
    probe(x,pool,image_probe_sample,sizeof image_probe_sample,user,0xffff,NULL);const float top[4]={32.0f/255,64.0f/255,128.0f/255,1};float_results(pool,top,0.00001f);
    user[9]=256;rt_executor_reset(x);probe(x,pool,image_probe_sample,sizeof image_probe_sample,user,0xffff,"requires default LOD controls");
    user[9]=0;user[10]=1;rt_executor_reset(x);probe(x,pool,image_probe_sample,sizeof image_probe_sample,user,0xffff,"requires default LOD controls");
    user[10]=0;user[3]=(user[3]&~(15u<<12))|2u<<12;user[9]=256;user[10]=1;rt_executor_reset(x);
    probe(x,pool,image_probe_sample,sizeof image_probe_sample,user,0xffff,NULL);const float tail[4]={112.0f/255,66.0f/255,128.0f/255,1};float_results(pool,tail,0.00001f);
    /* Sampling uses the same 1D lowering and array-relative views as integer image loads. */
    for(unsigned type=0;type<4;++type){const int arrayed=type>=2,one_d=(type&1)==0;
        memset(user,0,sizeof user);descriptor(user,0x1a0000,10,0,16,one_d?1:8,128,31,types[type],arrayed?3:1,1,2,arrayed?1:0,arrayed?2:0,IDENTITY);
        fill_levels(pool,user);pointer(user+12,OUTPUT_DESCRIPTOR);user[14]=bits(0.5f);user[15]=bits(one_d&&arrayed?1.0f:0.5f);
        rt_executor_reset(x);const uint32_t *code=arrayed?image_probe_sample_array:image_probe_sample;size_t count=arrayed?sizeof image_probe_sample_array:sizeof image_probe_sample;
        probe(x,pool,code,count,user,0xffff,NULL);const float expected_view[4]={(72.0f+(arrayed?14:0))/255,65.0f/255,(128.0f+(arrayed?2:0))/255,1};float_results(pool,expected_view,0.00001f);
    }
    /* Compute writes and rendered pixels become sampled-image input through the shared pool. */
    memset(user,0,sizeof user);descriptor(user,0x1c0000,4,7,8,8,16,31,RT_IMAGE_2D,1,0,0,0,0,4|0<<3|1<<6|1<<9);
    unsigned char initial[16]={0};const float quarter=0.25f;memcpy(initial,&quarter,4);fill(pool,user,4,1,initial);buffer_descriptor(user+12,PROBE);
    rt_executor_reset(x);probe(x,pool,image_probe_load,sizeof image_probe_load,user,0xffff,NULL);const float before_gpu[4]={0.25f,0,1,1};float_results(pool,before_gpu,0.00001f);
    gpu_update(x,pool,0x1c0000);probe(x,pool,image_probe_load,sizeof image_probe_load,user,0xffff,NULL);const float after_gpu[4]={1,0,1,1};float_results(pool,after_gpu,0.00001f);
    draw_source(x,pool);memset(user,0,sizeof user);descriptor(user,0x680000,10,0,16,16,16,31,RT_IMAGE_2D,1,0,0,0,0,IDENTITY);
    user[8]=user[9]=1;buffer_descriptor(user+12,PROBE);probe(x,pool,image_probe_load,sizeof image_probe_load,user,0xffff,NULL);
    const unsigned char *pixel=pool+0x680000+4*(1*16+1);float from_draw[4];for(unsigned i=0;i<4;++i)from_draw[i]=(float)pixel[i]/255;float_results(pool,from_draw,0.00001f);
    /* Complete declared descriptor bounds are checked before constructing slot masks. */
    rt_executor_reset(x);probe(x,pool,image_probe_load_high,sizeof image_probe_load_high,user,0xffff,"outside its declared user data");
    rt_executor_reset(x);probe(x,pool,image_probe_sample_high,sizeof image_probe_sample_high,user,0xffff,"outside its declared user data");
    rt_executor_close(x);x=NULL;CHECK(rt_executor_open(&memory,&x,error,sizeof error)==0);if(!x)return 1;
    /* All sixteen descriptor origins bind distinct texels, including a sampled final slot. */
    memset(user,0,sizeof user);pointer(user+12,OUTPUT_DESCRIPTOR);pointer(user+14,IMAGE_DESCRIPTOR);((uint32_t *)(pool+OUTPUT_DESCRIPTOR))[2]=512;
    for(unsigned i=0;i<16;++i){uint32_t d[8];descriptor(d,0x200000+i*0x4000,10,0,8,8,16,31,RT_IMAGE_2D,1,0,0,0,0,IDENTITY);
        memcpy(pool+IMAGE_DESCRIPTOR+i*sizeof d,d,sizeof d);unsigned char color[4]={(unsigned char)(16+12*i),64,128,255};fill(pool,d,4,1,color);}
    const struct {const uint32_t *code;size_t size;unsigned count;} many[]={
        {image_probe_eight_images,sizeof image_probe_eight_images,8},
        {image_probe_eight_with_sampler,sizeof image_probe_eight_with_sampler,8},
        {image_probe_nine_images,sizeof image_probe_nine_images,9},
        {image_probe_sixteen_images,sizeof image_probe_sixteen_images,16},
        {image_probe_sixteen_with_sampler,sizeof image_probe_sixteen_with_sampler,16}};
    for(unsigned mode=0;mode<sizeof many/sizeof *many;++mode){rt_executor_reset(x);probe(x,pool,many[mode].code,many[mode].size,user,0xffff,NULL);
        float values[64];memcpy(values,pool+PROBE,sizeof values);
        for(unsigned i=0;i<many[mode].count;++i){CHECK(fabsf(values[4*i]-(16.0f+12*i)/255)<=0.00001f);CHECK(fabsf(values[4*i+1]-64.0f/255)<=0.00001f);CHECK(fabsf(values[4*i+2]-128.0f/255)<=0.00001f);CHECK(values[4*i+3]==1);}
        for(unsigned i=4*many[mode].count;i<128;++i)CHECK(((uint32_t *)(pool+PROBE))[i]==SENTINEL);
    }
    rt_executor_reset(x);probe(x,pool,image_probe_seventeen_images,sizeof image_probe_seventeen_images,user,0xffff,"more than 16 images");
    /* Two loaded samplers reuse one SGPR range; their origins and native indices stay distinct. */
    memset(user,0,sizeof user);descriptor(user,IMAGE,10,0,2,2,16,31,RT_IMAGE_2D,1,0,0,0,0,IDENTITY);rt_tiling_tile(&small.layout,quad,pool+IMAGE,2,2,1);
    pointer(user+12,OUTPUT_DESCRIPTOR);pointer(user+14,SAMPLER_DESCRIPTOR);memset(pool+SAMPLER_DESCRIPTOR,0,32);((uint32_t *)(pool+SAMPLER_DESCRIPTOR))[4]=2;
    rt_executor_reset(x);probe(x,pool,image_probe_two_samplers,sizeof image_probe_two_samplers,user,0xffff,NULL);float both[8];memcpy(both,pool+PROBE,sizeof both);
    CHECK(both[0]==1&&both[1]==0&&both[2]==0&&both[3]==1);CHECK(both[4]==0&&both[5]==1&&both[6]==0&&both[7]==1);
    /* Old eight/nine-slot programs and all sixteen native samplers retain distinct origins. */
    const struct {const uint32_t *code;size_t size;unsigned count,linear_slot;} sampling[]={
        {image_probe_eight_samplers,sizeof image_probe_eight_samplers,8,7},
        {image_probe_nine_samplers,sizeof image_probe_nine_samplers,9,7},
        {image_probe_sixteen_samplers,sizeof image_probe_sixteen_samplers,16,15}};
    for(unsigned mode=0;mode<sizeof sampling/sizeof *sampling;++mode){
        const unsigned count=sampling[mode].count,last=sampling[mode].linear_slot;
        memset(pool+SAMPLER_DESCRIPTOR,0,16*16);
        for(unsigned i=0;i<count;++i)((uint32_t *)(pool+SAMPLER_DESCRIPTOR))[4*i]=(i&1?1:0)|(i&2?1u<<3:0);
        ((uint32_t *)(pool+SAMPLER_DESCRIPTOR))[last*4]=0;
        ((uint32_t *)(pool+SAMPLER_DESCRIPTOR))[last*4+2]=1u<<20|1u<<22;
        rt_executor_reset(x);probe(x,pool,sampling[mode].code,sampling[mode].size,user,0xffff,NULL);float samples[64];memcpy(samples,pool+PROBE,sizeof samples);
        for(unsigned i=0;i<count;++i){float r=i==last?0.5f:(i&1||i&2?0.0f:1.0f),g=i==last?0.5f:(i&1?1.0f:0.0f),b=i==last?0.5f:(i&2?1.0f:0.0f);
            if((i&3)==3&&i!=last)r=g=b=1;
            CHECK(fabsf(samples[4*i]-r)<0.00001f&&fabsf(samples[4*i+1]-g)<0.00001f&&fabsf(samples[4*i+2]-b)<0.00001f&&samples[4*i+3]==1);
        }
        for(unsigned i=4*count;i<128;++i)CHECK(((uint32_t *)(pool+PROBE))[i]==SENTINEL);
    }
    rt_executor_reset(x);probe(x,pool,image_probe_seventeen_samplers,sizeof image_probe_seventeen_samplers,user,0xffff,"more than 16 samplers");
    /* The entire padded image allocation can end exactly at the pool boundary. */
    memset(user,0,sizeof user);descriptor(user,POOL-512,10,0,8,8,16,31,RT_IMAGE_2D,1,0,0,0,0,IDENTITY);const unsigned char end_color[4]={32,64,128,255};fill(pool,user,4,1,end_color);
    buffer_descriptor(user+12,PROBE);user[8]=user[9]=7;rt_executor_reset(x);probe(x,pool,image_probe_load,sizeof image_probe_load,user,0xffff,NULL);
    const float end_value[4]={32.0f/255,64.0f/255,128.0f/255,1};float_results(pool,end_value,0.00001f);
    uint64_t past=at(POOL-256)>>8;user[0]=(uint32_t)past;user[1]=(user[1]&~63u)|(uint32_t)(past>>32);rt_executor_reset(x);
    probe(x,pool,image_probe_load,sizeof image_probe_load,user,0xffff,"outside the GPU-visible pool");
    /* Native dimension/layer endpoints and 32-bit filtering are actual device cases. */
    memset(user,0,sizeof user);descriptor(user,0x400000,10,0,16384,1,16384,31,RT_IMAGE_2D,1,0,0,0,0,IDENTITY);
    fill(pool,user,4,1,end_color);buffer_descriptor(user+12,PROBE);user[8]=16383;rt_executor_reset(x);
    probe(x,pool,image_probe_load,sizeof image_probe_load,user,0xffff,NULL);float_results(pool,end_value,0.00001f);
    memset(user,0,sizeof user);descriptor(user,0x400000,10,0,8,1,16,31,RT_IMAGE_2D_ARRAY,2048,0,0,2047,2047,IDENTITY);
    fill_levels(pool,user);buffer_descriptor(user+12,PROBE);rt_executor_reset(x);
    probe(x,pool,image_probe_load_array,sizeof image_probe_load_array,user,0xffff,NULL);const float final_slice[4]={25.0f/255,64.0f/255,127.0f/255,1};float_results(pool,final_slice,0.00001f);
    user[4]=(user[4]&~0x1fffu)|2048;rt_executor_reset(x);probe(x,pool,image_probe_load_array,sizeof image_probe_load_array,user,0xffff,"dimensions, layers or pitch");
    memset(user,0,sizeof user);descriptor(user,0x500000,4,7,2,2,16,31,RT_IMAGE_2D,1,0,0,0,0,4|0<<3|1<<6|1<<9);
    rt_tiling_level fp;CHECK(rt_tiling_mip_chain(31,4,1,0,16,2,2,2,1,1,&fp,error,sizeof error)==0);const float values[4]={0.25f,0.5f,0.75f,1};
    rt_tiling_tile(&fp.layout,(const unsigned char *)values,pool+0x500000,2,2,1);pointer(user+12,OUTPUT_DESCRIPTOR);user[10]=1u<<20|1u<<22;user[14]=user[15]=bits(0.5f);
    rt_executor_reset(x);probe(x,pool,image_probe_sample,sizeof image_probe_sample,user,0xffff,NULL);const float fp_result[4]={0.625f,0,1,1};float_results(pool,fp_result,0.00001f);
    rt_executor_close(x);CHECK(close(fd)==0);CHECK(munmap(pool,POOL)==0);
    printf("metal images: %u scenarios, %u checks, %u failures\n",scenarios,checks,failures);return failures?1:0;
}
