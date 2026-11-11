/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Owned true-3D compute storage observed in independent canonical guest bytes,
   image loads and later physical-buffer loads. Markers prove coverage, not counts.
   Literal ADDR_THICK tables are independent of every production volume helper. */
#include "runtime/render_executor.h"
#include "metal_volume_programs.h"
#include "volume_fixture.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

enum { POOL=16<<20, CODE=0x1000, OUTPUT=0x10000, OUTPUT_MAX=1<<20,
       IMAGE=0x200000, SECOND=0x600000, GUARD=64, CANARY=0xa5, MARKER=0x13572468 };
static const uint64_t base=UINT64_C(0x3000000000);
static unsigned scenarios,checks,failures;
static const char *context="setup";
#define CHECK(x) do{++checks;if(!(x)){++failures;if(failures<=20)fprintf(stderr,"%s line%d: %s\n",context,__LINE__,#x);}}while(0)
#define COUNT(a) (sizeof(a)/sizeof *(a))
typedef struct {unsigned data,number,components,bytes;} Format;
static const Format formats[]={
    {1,0,1,1},{3,0,2,2},{4,7,1,4},{4,4,1,4},{4,5,1,4},
    {11,7,2,8},{14,7,4,16},{10,0,4,4},{10,4,4,4},
};
typedef struct {unsigned offset,pitch,w,h,d,ph,pd,pow2;const Format *format;} Volume;
typedef struct {uint8_t *pool;rt_executor *executor;} World;
typedef struct {const char *name;const uint32_t *words;size_t bytes;} Program;
#define PROGRAM(x) {#x,x,sizeof x}
static const Program programs[]={
    PROGRAM(volume_grid_load),PROGRAM(volume_grid_store_float),PROGRAM(volume_grid_store_bits),
    PROGRAM(volume_probe_load),PROGRAM(volume_probe_store_float),PROGRAM(volume_probe_inplace_float),
    PROGRAM(volume_probe_inplace_bits),PROGRAM(volume_raw_copy),PROGRAM(volume_dual_copy),
    PROGRAM(volume_read_marker),PROGRAM(volume_store_then_stop),
};
enum { LOAD,STORE_FLOAT,STORE_BITS,PROBE,PROBE_STORE,INPLACE_FLOAT,INPLACE_BITS,RAW,DUAL,READ_MARKER,STORE_STOP };
static uint32_t bits(float f){uint32_t b;memcpy(&b,&f,4);return b;}
static float as_float(uint32_t b){float f;memcpy(&f,&b,4);return f;}
static uint64_t address(unsigned offset){return base+offset;}
static size_t span(const Volume *v){return (size_t)v->pitch*v->ph*v->pd*v->format->bytes;}
static size_t offset(const Volume *v,unsigned x,unsigned y,unsigned z)
{
    return (size_t)volume_fixture_offset(v->format->bytes,v->pitch,v->ph,x,y,z);
}
static Volume normal(unsigned f){return (Volume){IMAGE,24,13,11,9,16,12,0,formats+f};}
static uint8_t *allocate(size_t n)
{
    uint8_t *p=malloc(n);CHECK(p!=NULL);if(!p){exit(1);}return p;
}
static void descriptor(uint32_t words[8],const Volume *v)
{
    uint64_t a=address(v->offset)>>8;memset(words,0,32);
    words[0]=(uint32_t)a;words[1]=(uint32_t)(a>>32)|v->format->data<<20|v->format->number<<26;
    words[2]=(v->w-1)|((v->h-1)<<14);
    words[3]=(4|5<<3|6<<6|7<<9)|19u<<20|v->pow2<<25|(uint32_t)RT_IMAGE_3D<<28;
    words[4]=(v->d-1)|((v->pitch-1)<<13); /* Array fields are neutral, not a Z window. */
}
static void buffer(uint32_t words[4],unsigned where,size_t bytes)
{
    CHECK(bytes<=UINT32_MAX);uint64_t a=address(where);
    words[0]=(uint32_t)a;words[1]=(uint32_t)(a>>32);words[2]=(uint32_t)bytes;words[3]=0;
}
static void put_initial(uint8_t *p,const Format *f,unsigned seed)
{
    for(unsigned c=0;c<f->components;++c){
        if(f->number==0){p[c]=(uint8_t)(20+(seed+11*c)%80);}
        else if(f->number==4&&f->data==10){p[c]=(uint8_t)((seed+37*c)%224);}
        else{uint32_t value=f->number==7?bits((float)((seed+7*c)%29)/64):
            f->number==5?(uint32_t)-(int32_t)(seed+c+1):0x81230000u+257*seed+c;
            memcpy(p+4*c,&value,4);}
    }
}
static void initialize(World *w,const Volume *v,unsigned seed)
{
    CHECK(v->offset>=GUARD&&v->offset+span(v)<=POOL);
    memset(w->pool+v->offset-GUARD,CANARY,span(v)+2*GUARD);
    for(unsigned z=0;z<v->d;++z)for(unsigned y=0;y<v->h;++y)for(unsigned x=0;x<v->w;++x)
        put_initial(w->pool+v->offset+offset(v,x,y,z),v->format,seed+x+3*y+5*z);
}
static void compare(const uint8_t *actual,const uint8_t *expected,const uint8_t *tolerance,size_t n)
{
    for(size_t i=0;i<n;++i){int delta=(int)actual[i]-expected[i];int ok=tolerance&&tolerance[i]?delta>=-1&&delta<=1:delta==0;
        if(!ok&&failures<20){fprintf(stderr,"%s byte%zu: %02x expected%02x\n",context,i,actual[i],expected[i]);}
        CHECK(ok);
    }
}
static void output_reset(World *w,size_t n)
{
    CHECK(n<=OUTPUT_MAX);memset(w->pool+OUTPUT-GUARD,CANARY,n+2*GUARD);
}
static void output_canary(World *w,size_t n)
{
    for(size_t i=0;i<n+2*GUARD;++i){CHECK(w->pool[OUTPUT-GUARD+i]==CANARY);}
}
static void output_marker(World *w)
{
    uint32_t value;memcpy(&value,w->pool+OUTPUT,4);CHECK(value==MARKER);
    for(unsigned i=0;i<GUARD;++i){CHECK(w->pool[OUTPUT-GUARD+i]==CANARY);CHECK(w->pool[OUTPUT+16+i]==CANARY);}
    for(unsigned i=4;i<16;++i){CHECK(w->pool[OUTPUT+i]==CANARY);}
}
static int run(World *w,unsigned program,const uint32_t user[16],unsigned declared,unsigned workgroup,
               unsigned localx,unsigned localy,unsigned localz,unsigned groups,const char *reason,uint32_t written)
{
    rt_render_call calls[20]={{0}};size_t n=0;context=programs[program].name;
    calls[n].kind=RT_CALL_COMPUTE_SHADER;
    calls[n++].u.compute_shader=(rt_compute_shader){.code=address(CODE+program*256),.vgprs=16,.sgprs=24,.user_sgprs=declared,
        .workgroup_id_enables=workgroup,.thread_id_components=2,.threads={localx,localy,localz}};
    /* At most8 words per event; missing-word negatives deliberately use single slots. */
    for(unsigned slot=0;slot<declared;){
        if(!(written&(1u<<slot))){++slot;continue;}
        unsigned words=1;
        while(words<8&&slot+words<declared&&(written&(1u<<(slot+words)))){++words;}
        calls[n].kind=RT_CALL_USER_DATA;calls[n].u.user_data.stage=RT_STAGE_COMPUTE;
        calls[n].u.user_data.slot=slot;calls[n].u.user_data.count=words;
        memcpy(calls[n++].u.user_data.words,user+slot,words*4);slot+=words;
    }
    CHECK(n<COUNT(calls));calls[n].kind=RT_CALL_DISPATCH;
    calls[n].u.dispatch.groups[0]=groups;calls[n].u.dispatch.groups[1]=calls[n].u.dispatch.groups[2]=1;++n;
    char error[512]="";int rc=rt_executor_run_calls(w->executor,calls,n,error,sizeof error);++scenarios;
    if(reason){
        if(rc!=-1||!strstr(error,reason)){fprintf(stderr,"wanted %s, got%d: %s\n",reason,rc,error);}
        CHECK(rc==-1&&strstr(error,reason));
    }else{if(rc){fprintf(stderr,"volume run: %s\n",error);}CHECK(rc==0);}
    return rc;
}
static void user_grid(uint32_t user[16],const Volume *v,unsigned z,size_t output_bytes)
{
    memset(user,0,64);descriptor(user,v);buffer(user+8,OUTPUT,output_bytes);user[12]=z;user[13]=v->w;user[14]=v->h;
}
static void user_probe(uint32_t user[16],const Volume *v,unsigned x,unsigned y,unsigned z)
{
    memset(user,0,64);descriptor(user,v);buffer(user+8,OUTPUT,16);user[12]=x;user[13]=y;user[14]=z;
}
static uint32_t component(const uint8_t *texel,const Format *f,unsigned c)
{
    uint32_t result=c==3?(f->number==4||f->number==5?1:bits(1)):0;
    if(c<f->components){
        if(f->number==0){result=bits((float)texel[c]/255);}
        else if(f->number==4&&f->data==10){result=texel[c];}
        else{memcpy(&result,texel+4*c,4);}
    }
    return result;
}
static void load_output(World *w,const Volume *v,const uint8_t *input,unsigned single,unsigned px,unsigned py,unsigned pz)
{
    size_t n=single?16:(size_t)v->w*v->h*v->d*16;
    for(size_t at=0;at<n/4;++at){unsigned c=(unsigned)(at%4);size_t index=at/4;
        unsigned x=single?px:(unsigned)(index%v->w),y=single?py:(unsigned)((index/v->w)%v->h),z=single?pz:(unsigned)(index/(v->w*v->h));
        uint32_t got;memcpy(&got,w->pool+OUTPUT+at*4,4);
        uint32_t expected=component(input+offset(v,x,y,z),v->format,c);
        if(v->format->number==0){CHECK(isfinite(as_float(got))&&fabsf(as_float(got)-as_float(expected))<=0.000001f);}
        else{CHECK(got==expected);}
    }
    for(unsigned i=0;i<GUARD;++i){CHECK(w->pool[OUTPUT-GUARD+i]==CANARY);CHECK(w->pool[OUTPUT+n+i]==CANARY);}
}
static void load_all(World *w,const Volume *v)
{
    size_t n=(size_t)v->w*v->h*v->d*16;output_reset(w,n);uint8_t *input=allocate(span(v));memcpy(input,w->pool+v->offset,span(v));
    uint32_t user[16];for(unsigned z=0;z<v->d;z+=4){user_grid(user,v,z,n);run(w,LOAD,user,15,1,4,4,4,12,NULL,0x7fff);}
    load_output(w,v,input,0,0,0,0);compare(w->pool+v->offset,input,NULL,span(v));free(input);
}
static void raw_publication(World *w,const Volume *v,const uint8_t *expected,const uint8_t *mask)
{
    size_t n=span(v);output_reset(w,n);uint32_t user[16]={0};buffer(user,v->offset,n);buffer(user+8,OUTPUT,n);
    CHECK(n%256==0);run(w,RAW,user,12,1,64,1,1,(unsigned)(n/256),NULL,0xfff);
    compare(w->pool+OUTPUT-GUARD,expected,mask,n+2*GUARD);
    compare(w->pool+v->offset-GUARD,expected,mask,n+2*GUARD);
}
static void store_value(uint8_t *p,uint8_t *mask,const Format *f,unsigned x,unsigned y,unsigned z)
{
    const float floats[4]={(float)x/32,(float)y/32,(float)z/32,.5f};const uint32_t integers[4]={37+x,53+y,71+z,167};
    for(unsigned c=0;c<f->components;++c){
        if(f->number==0){p[c]=(uint8_t)(floats[c]*255+.5f);mask[c]=1;}
        else if(f->number==4&&f->data==10){p[c]=(uint8_t)integers[c];}
        else{uint32_t value=f->number==7?bits(floats[c]):integers[c];memcpy(p+4*c,&value,4);}
    }
}
static void partial_store(World *w,const Volume *v,unsigned z_bias,unsigned groups,uint8_t *expected,uint8_t *mask)
{
    size_t n=(size_t)v->w*v->h*v->d*16;output_reset(w,n);uint8_t *out=allocate(n+2*GUARD);memset(out,CANARY,n+2*GUARD);
    for(unsigned g=0;g<groups;++g)for(unsigned lz=0;lz<4;++lz)for(unsigned ly=0;ly<4;++ly)for(unsigned lx=0;lx<4;++lx){
        unsigned x=(g%4)*4+lx,y=(g/4)*4+ly,z=z_bias+lz;
        if(x<v->w&&y<v->h&&z<v->d){
            store_value(expected+GUARD+offset(v,x,y,z),mask+GUARD+offset(v,x,y,z),v->format,x,y,z);
            uint32_t marker=MARKER;memcpy(out+GUARD+16*((z*v->h+y)*v->w+x),&marker,4);
        }
    }
    uint32_t user[16];user_grid(user,v,z_bias,n);
    run(w,v->format->number==4||v->format->number==5?STORE_BITS:STORE_FLOAT,user,15,1,4,4,4,groups,NULL,0x7fff);
    compare(w->pool+OUTPUT-GUARD,out,NULL,n+2*GUARD);compare(w->pool+v->offset-GUARD,expected,mask,span(v)+2*GUARD);free(out);
}
static void matrix(World *w)
{
    const unsigned order[]={2,0,3,4,1,5,6,7,8,2};
    for(size_t f=0;f<COUNT(order);++f){Volume v=normal(order[f]);initialize(w,&v,7+(unsigned)f);
        uint8_t *expected=allocate(span(&v)+2*GUARD),*mask=allocate(span(&v)+2*GUARD);
        memcpy(expected,w->pool+v.offset-GUARD,span(&v)+2*GUARD);memset(mask,0,span(&v)+2*GUARD);
        load_all(w,&v);partial_store(w,&v,4,2,expected,mask);raw_publication(w,&v,expected,mask);
        partial_store(w,&v,8,1,expected,mask);raw_publication(w,&v,expected,mask);
        put_initial(w->pool+v.offset+offset(&v,12,10,0),v.format,99);
        put_initial(expected+GUARD+offset(&v,12,10,0),v.format,99);
        load_all(w,&v);compare(w->pool+v.offset-GUARD,expected,mask,span(&v)+2*GUARD);free(mask);free(expected);
    }
    const Volume cache[]={{IMAGE,16,8,8,8,8,8,1,formats+2},{IMAGE,24,9,5,5,8,8,0,formats+2}};
    for(size_t i=0;i<COUNT(cache);++i){initialize(w,cache+i,31+(unsigned)i);load_all(w,cache+i);}
}
static void read_write(World *w)
{
    for(unsigned f=0;f<COUNT(formats);++f){Volume v=normal(f);initialize(w,&v,11);
        uint8_t *expected=allocate(span(&v)+2*GUARD),*mask=allocate(span(&v)+2*GUARD);
        memcpy(expected,w->pool+v.offset-GUARD,span(&v)+2*GUARD);memset(mask,0,span(&v)+2*GUARD);
        uint32_t user[16];user_probe(user,&v,1,2,8);output_reset(w,16);
        if(f==1||f==5){run(w,INPLACE_FLOAT,user,15,0,1,1,1,1,"no native read_write",0x7fff);output_canary(w,16);}
        else{
            uint8_t *pixel=expected+GUARD+offset(&v,1,2,8);
            if(v.format->number==0){pixel[0]=(uint8_t)(pixel[0]+64);mask[GUARD+offset(&v,1,2,8)]=1;}
            else{uint32_t value;memcpy(&value,pixel,4);value=v.format->number==7?bits(as_float(value)+.25f):value^1u;memcpy(pixel,&value,4);}
            run(w,v.format->number==4||v.format->number==5?INPLACE_BITS:INPLACE_FLOAT,user,15,0,1,1,1,1,NULL,0x7fff);
            output_canary(w,16);raw_publication(w,&v,expected,mask);
        }
        compare(w->pool+v.offset-GUARD,expected,mask,span(&v)+2*GUARD);
        free(mask);free(expected);
    }
}
static void aliases_and_stops(World *w)
{
    Volume v=normal(2);initialize(w,&v,3);size_t n=span(&v)+2*GUARD;
    uint8_t *expected=allocate(n),*mask=allocate(n);memcpy(expected,w->pool+v.offset-GUARD,n);memset(mask,0,n);
    uint32_t user[16];user_probe(user,&v,0,0,0);buffer(user+8,v.offset+(unsigned)offset(&v,12,10,8),4);
    uint32_t marker=bits(.625f);memcpy(expected+GUARD+offset(&v,12,10,8),&marker,4);
    output_reset(w,16);run(w,READ_MARKER,user,15,0,1,1,1,1,NULL,0x7fff);raw_publication(w,&v,expected,mask);
    descriptor(user,&v);descriptor(user+8,&v);memcpy(expected+GUARD+offset(&v,1,0,0),expected+GUARD+offset(&v,0,0,0),4);
    run(w,DUAL,user,16,0,1,1,1,1,NULL,0xffff);raw_publication(w,&v,expected,mask);
    Volume other=v;other.offset+=(unsigned)(2*v.pitch*v.ph*4*v.format->bytes);initialize(w,&other,9);
    size_t union_bytes=other.offset-v.offset+span(&other)+2*GUARD;uint8_t *saved=allocate(union_bytes);
    memcpy(saved,w->pool+v.offset-GUARD,union_bytes);descriptor(user,&v);descriptor(user+8,&other);output_reset(w,16);
    run(w,DUAL,user,16,0,1,1,1,1,"overlapping compute images require the same native view",0xffff);
    compare(w->pool+v.offset-GUARD,saved,NULL,union_bytes);output_canary(w,16);free(saved);free(mask);free(expected);
    v=normal(6);initialize(w,&v,17);n=span(&v)+2*GUARD;expected=allocate(n);mask=allocate(n);
    memcpy(expected,w->pool+v.offset-GUARD,n);memset(mask,0,n);user_probe(user,&v,3,4,8);
    user[11]=(4|5u<<3|6u<<6|7u<<9)|2u<<15|7u<<12; /* Unsupported typed load after a completed image store. */
    for(unsigned c=0;c<4;++c){uint32_t half=bits(.5f);memcpy(expected+GUARD+offset(&v,3,4,8)+4*c,&half,4);}
    output_reset(w,16);run(w,STORE_STOP,user,15,0,1,1,1,1,"DATA_FORMAT 2",0x7fff);raw_publication(w,&v,expected,mask);
    free(mask);free(expected);
}
static void refused(World *w,const Volume *v,const uint32_t words[8],const char *reason,uint32_t written)
{
    initialize(w,v,23);size_t n=span(v)+2*GUARD;uint8_t *saved=allocate(n);memcpy(saved,w->pool+v->offset-GUARD,n);
    /* Covers deep-plane pool-bound refusals, including the accessible CPU tail canary. */
    uint8_t *tail=allocate(65536+GUARD);memcpy(tail,w->pool+POOL-65536,65536+GUARD);
    size_t output=(size_t)v->w*v->h*v->d*16;output_reset(w,output);uint32_t user[16];user_grid(user,v,0,output);memcpy(user,words,32);
    rt_executor_reset(w->executor);run(w,STORE_FLOAT,user,15,1,4,4,4,1,reason,written);
    compare(w->pool+v->offset-GUARD,saved,NULL,n);
    output_canary(w,output);compare(w->pool+POOL-65536,tail,NULL,65536+GUARD);free(tail);free(saved);
}
static void refusals(World *w)
{
    Volume v=normal(2);uint32_t good[8],bad[8];descriptor(good,&v);
    for(unsigned tile=0;tile<32;++tile){if(tile==19){continue;}memcpy(bad,good,32);bad[3]=(bad[3]&~(31u<<20))|tile<<20;refused(w,&v,bad,"compute volume layout",0x7fff);}
    const unsigned types[]={RT_IMAGE_CUBE,14,15};
    for(size_t i=0;i<COUNT(types);++i){memcpy(bad,good,32);bad[3]=(bad[3]&0x0fffffffu)|types[i]<<28;refused(w,&v,bad,"compute image type",0x7fff);}
    for(unsigned axis=0;axis<3;++axis){memcpy(bad,good,32);
        if(axis==0){bad[2]=(bad[2]&~0x3fffu)|2048u;bad[4]=(bad[4]&0x1fffu)|(2055u<<13);}
        if(axis==1){bad[2]=(bad[2]&~(0x3fffu<<14))|2048u<<14;}
        if(axis==2){bad[4]=(bad[4]&~0x1fffu)|2048u;}
        refused(w,&v,bad,"compute volume dimensions",0x7fff);
    }
    memcpy(bad,good,32);bad[5]=1;refused(w,&v,bad,"compute volume view",0x7fff);
    memcpy(bad,good,32);bad[5]=(v.d-1)<<13;refused(w,&v,bad,"compute volume view",0x7fff);
    memcpy(bad,good,32);bad[3]|=1u<<12;refused(w,&v,bad,"require relative and physical mip zero",0x7fff);
    memcpy(bad,good,32);bad[3]|=1u<<16;refused(w,&v,bad,"require relative and physical mip zero",0x7fff);
    memcpy(bad,good,32);bad[3]|=1u<<25;refused(w,&v,bad,"compute volume layout",0x7fff);
    memcpy(bad,good,32);bad[4]=(bad[4]&0x1fffu)|22u<<13;refused(w,&v,bad,"compute volume layout",0x7fff);
    memcpy(bad,good,32);bad[3]=(bad[3]&~7u)|1;refused(w,&v,bad,"swizzle must preserve",0x7fff);
    memcpy(bad,good,32);bad[1]=(bad[1]&~(63u<<20|15u<<26))|35u<<20;refused(w,&v,bad,"compressed and sRGB",0x7fff);
    memcpy(bad,good,32);bad[1]=(bad[1]&~(63u<<20|15u<<26))|10u<<20|9u<<26;refused(w,&v,bad,"compressed and sRGB",0x7fff);
    memcpy(bad,good,32);uint64_t a=address(POOL-(unsigned)span(&v)+256)>>8;bad[0]=(uint32_t)a;bad[1]=(bad[1]&~63u)|(uint32_t)(a>>32);
    refused(w,&v,bad,"compute image is outside the GPU-visible pool",0x7fff);
    refused(w,&v,good,"compute descriptor reads unset",0x7ffe);
}
static void native_limits(World *w)
{
    const Volume volumes[]={
        {SECOND,8,3,1,1,8,4,0,formats+2},
        {SECOND,2048,2048,1,1,8,4,0,formats+2},
        {SECOND,8,1,2048,1,2048,4,0,formats+2},
        {SECOND,8,1,1,2048,8,2048,0,formats+2},
        {POOL-18432,24,13,11,9,16,12,0,formats+2},
    };
    for(size_t i=0;i<COUNT(volumes);++i){const Volume *v=volumes+i;initialize(w,v,37+(unsigned)i);
        unsigned x=v->w-1,y=v->h-1,z=v->d-1;uint32_t user[16];user_probe(user,v,x,y,z);output_reset(w,16);
        uint8_t *input=allocate(span(v));memcpy(input,w->pool+v->offset,span(v));
        run(w,PROBE,user,15,0,1,1,1,1,NULL,0x7fff);load_output(w,v,input,1,x,y,z);compare(w->pool+v->offset,input,NULL,span(v));free(input);
        size_t n=span(v)+2*GUARD;uint8_t *expected=allocate(n),*mask=allocate(n);memcpy(expected,w->pool+v->offset-GUARD,n);memset(mask,0,n);
        uint32_t half=bits(.5f);memcpy(expected+GUARD+offset(v,x,y,z),&half,4);output_reset(w,16);
        run(w,PROBE_STORE,user,15,0,1,1,1,1,NULL,0x7fff);output_marker(w);raw_publication(w,v,expected,mask);free(mask);free(expected);
    }
}
int main(void)
{
    char path[]="/tmp/paleblood-metal-volumes-XXXXXX",error[512]="";int fd=mkstemp(path);
    if(fd<0){perror("mkstemp");return 1;}unlink(path);
    if(ftruncate(fd,POOL+GUARD)){perror("ftruncate");close(fd);return 1;}
    uint8_t *pool=mmap(NULL,POOL+GUARD,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    if(pool==MAP_FAILED){perror("mmap");close(fd);return 1;}memset(pool,CANARY,POOL+GUARD);
    rt_executor *x=NULL;const rt_executor_memory memory={fd,POOL,base,NULL};int rc=rt_executor_open(&memory,&x,error,sizeof error);close(fd);
    CHECK(rc==0);if(rc){fprintf(stderr,"open: %s\n",error);munmap(pool,POOL+GUARD);return 1;}
    for(size_t p=0;p<COUNT(programs);++p){CHECK(programs[p].bytes<=256);memcpy(pool+CODE+p*256,programs[p].words,programs[p].bytes);}
    World world={pool,x};matrix(&world);read_write(&world);aliases_and_stops(&world);refusals(&world);native_limits(&world);
    CHECK(scenarios==190);rt_executor_close(x);munmap(pool,POOL+GUARD);
    printf("metal volumes: %u scenarios, %u checks, %u failures\n",scenarios,checks,failures);return failures?1:0;
}
