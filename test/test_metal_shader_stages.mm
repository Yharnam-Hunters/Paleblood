/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Owned GFX7 programs and literal pool oracles. Include the real executor to observe
   retained state/caches; the queue proxy forwards actual Metal work without mocking it. */
#include "../runtime/renderer/render_executor_metal.mm"

@interface PBStageQueue : NSProxy {
@public
    id<MTLCommandQueue> native_queue;
    unsigned command_buffers;
}
@end
@implementation PBStageQueue
- (NSMethodSignature *)methodSignatureForSelector:(SEL)selector
{ return [(NSObject *)native_queue methodSignatureForSelector:selector]; }
- (void)forwardInvocation:(NSInvocation *)invocation
{ [invocation invokeWithTarget:native_queue]; }
- (id<MTLCommandBuffer>)commandBuffer
{ ++command_buffers; return [native_queue commandBuffer]; }
@end

namespace stage_fixture {
constexpr unsigned Pool = 1 << 20, Width = 16, Height = 8, Pixels = Width * Height, Guard = 64;
constexpr unsigned VertexCode = 0x1000, PixelCode = 0x2000, Positions = 0x8000, Colors = 0x8100;
constexpr unsigned Index16 = 0x9000, Index32 = 0x9100, StoreMarker = 0x13572468;
constexpr uint64_t Base = UINT64_C(0x3000000000);
constexpr unsigned Color[3] = {0x20000,0x30000,0x40000};
constexpr unsigned Depth[3] = {0x50000,0x51000,0x52000};
constexpr unsigned Store[3] = {0x60000,0x60100,0x60200};
/* LLVM Bonaire encodings, shared instruction shapes with test_metal_executor.c.
   Fetch position/color, write one marker per vertex ID, and export both vectors. */
constexpr uint32_t VertexProgram[] = {
    0xebf32000,0x80000100, 0xebf32000,0x80010500, 0xbf8c0f70,
    0x7e1202ff,StoreMarker, 0x34140082, 0xe0701000,0x8002090a,
    0xf800020f,0x08070605, 0xf80008cf,0x04030201, 0xbf810000
};
/* The owned pixel_color program in gcn_programs.h: interpolate/export literal RGBA. */
constexpr uint32_t PixelProgram[] = {
    0xbefc0300, 0xc8080000,0xc8090001, 0xc80c0100,0xc80d0101,
    0xc8100200,0xc8110201, 0xc8140300,0xc8150301,
    0x5e0c0702,0x5e0e0b04, 0xf8001c0f,0x00000706, 0xbf810000
};
unsigned checks = 0, failures = 0, cases = 0;
const char *current = "setup";
#define STAGE_CHECK(condition) do { ++checks; if (!(condition)) { ++failures; \
    if (failures <= 32) std::fprintf(stderr,"%s line%d: %s\n",current,__LINE__,#condition); } } while(0)
using Calls = std::vector<rt_render_call>;
using Bytes = std::vector<uint8_t>;
enum class Route { Auto, Direct16, Direct32, Offset16, Offset32, Logical16, Logical32, Rectangle };
enum class API { Run, Submit, Record };
constexpr const char *RouteNames[] = {"auto","direct16","direct32","offset16","offset32","logical16","logical32","rectangle"};
rt_render_call &event(Calls &calls,rt_render_call_kind kind)
{ calls.emplace_back(); calls.back().kind = kind; return calls.back(); }
void reg(Calls &calls,uint32_t address,uint32_t value)
{ auto &c=event(calls,RT_CALL_REGISTER);c.u.reg.address=address;c.u.reg.value=value; }
void stages(Calls &calls,uint32_t word) { reg(calls,0xa2d5,word); }
void instances(Calls &calls,uint32_t count) { event(calls,RT_CALL_INSTANCES).u.instances.count=count; }
void descriptor(uint32_t *words,unsigned offset,unsigned stride,unsigned records)
{
    const uint64_t address=Base+offset;
    words[0]=static_cast<uint32_t>(address);words[1]=static_cast<uint32_t>(address>>32)|(stride<<16);
    words[2]=records;words[3]=0;
}
void positions(uint8_t *pool,Route route)
{
    const float edge=route==Route::Rectangle?1.f:3.f;
    const float vertices[3][4]={{-1,-1,.25f,1},{edge,-1,.25f,1},{-1,edge,.25f,1}};
    std::memcpy(pool+Positions,vertices,sizeof vertices);
}
void seed(uint8_t *pool,unsigned salt)
{
    for(unsigned scene=0;scene<3;++scene) {
        std::memset(pool+Color[scene]-Guard,static_cast<int>(0x31+salt),Pixels*4+2*Guard);
        std::memset(pool+Depth[scene]-Guard,0xa5,Pixels*4+2*Guard);
        const uint32_t far_z=0x3f400000; // Literal D32 .75.
        for(unsigned i=0;i<Pixels;++i)std::memcpy(pool+Depth[scene]+4*i,&far_z,4);
        std::memset(pool+Store[scene]-Guard,0x6d,12+2*Guard);
    }
}
void expected_draw(Bytes &expected,unsigned scene,bool color=true)
{
    const uint8_t rgba[4]={255,0,0,255};const uint32_t z=0x3e800000; // Literal D32 .25.
    for(unsigned i=0;i<Pixels;++i) {
        if(color)std::memcpy(expected.data()+Color[scene]+4*i,rgba,4);
        std::memcpy(expected.data()+Depth[scene]+4*i,&z,4);
    }
    for(unsigned i=0;i<3;++i)std::memcpy(expected.data()+Store[scene]+4*i,&StoreMarker,4);
}
void setup(Calls &calls,unsigned scene,Route route)
{
    auto &color=event(calls,RT_CALL_COLOR_TARGET);color.u.color_target.enabled=1;
    auto &target=color.u.color_target.target;target.address=Base+Color[scene];target.pitch=Width;
    target.height=Height;target.format=0xa;target.tile_mode_index=31;target.samples=1;
    auto &v=event(calls,RT_CALL_VIEWPORT);v.u.viewport.zmax=1;
    v.u.viewport.scale[0]=v.u.viewport.translate[0]=Width/2;
    v.u.viewport.scale[1]=v.u.viewport.translate[1]=Height/2;v.u.viewport.scale[2]=1;
    auto &vs=event(calls,RT_CALL_VERTEX_SHADER).u.vertex_shader;
    vs.code=Base+VertexCode;vs.vgprs=12;vs.sgprs=16;vs.user_sgprs=12;vs.parameter_count=1;vs.position_format=4;
    auto &ps=event(calls,RT_CALL_PIXEL_SHADER).u.pixel_shader;
    ps.code=Base+PixelCode;ps.vgprs=12;ps.sgprs=8;ps.color_formats=4;ps.input_enable=2;ps.input_address=2;
    ps.interpolated=1;ps.color_mask=15;
    auto &user=event(calls,RT_CALL_USER_DATA).u.user_data;user.stage=RT_STAGE_VERTEX;user.count=8;
    descriptor(user.words,Positions,16,3);descriptor(user.words+4,Colors,16,3);
    auto &store=event(calls,RT_CALL_USER_DATA).u.user_data;store.stage=RT_STAGE_VERTEX;store.slot=8;store.count=4;
    descriptor(store.words,Store[scene],0,12);
    reg(calls,0xc242,route==Route::Rectangle?17:4);reg(calls,0xa08e,15);
    reg(calls,0xa00f,0);reg(calls,0xa010,3);reg(calls,0xa011,0);
    reg(calls,0xa012,static_cast<uint32_t>((Base+Depth[scene])>>8));
    reg(calls,0xa014,static_cast<uint32_t>((Base+Depth[scene])>>8));
    reg(calls,0xa016,(Width/8-1)|((Height/8-1)<<11));reg(calls,0xa017,Pixels/64-1);
    reg(calls,0xa200,2|4|1<<4); // D32 LESS, write enabled; the seed is .75 and geometry .25.
    const bool wide=route==Route::Direct32||route==Route::Offset32||route==Route::Logical32;
    event(calls,RT_CALL_INDEX_TYPE).u.index_type.type=wide?1:0;
    event(calls,RT_CALL_INDEX_BASE).u.index_base.address=Base+(wide?Index32:Index16);
    event(calls,RT_CALL_INDEX_SIZE).u.index_size.count=5;
}
void draw_call(Calls &calls,Route route,uint32_t vertices=3)
{
    if(route==Route::Auto||route==Route::Rectangle)event(calls,RT_CALL_DRAW_AUTO).u.draw_auto.index_count=vertices;
    else if(route==Route::Direct16||route==Route::Direct32) {
        auto &c=event(calls,RT_CALL_DRAW_INDEXED).u.draw_indexed;c.header=0xc0042700;c.max_size=vertices;
        c.address=Base+(route==Route::Direct32?Index32+4:Index16+2);c.index_count=vertices;
    } else {
        auto &c=event(calls,RT_CALL_DRAW_INDEX_OFFSET).u.draw_index;
        c.first=1;c.count=vertices;
        if(route==Route::Offset16||route==Route::Offset32){c.header=0xc0033500;c.max_size=vertices;c.has_packet_fields=1;}
    }
}
std::array<size_t,6> caches(rt_executor *x)
{ return {x->shaders.size(),x->pipelines.size(),x->targets.size(),x->images.size(),x->samplers.size(),x->compute_pipelines.size()}; }
int perform(rt_executor *x,PBStageQueue *queue,uint8_t *pool,const char *id,const Calls &calls,
            const Bytes &expected,uint32_t retained,const char *reason,bool no_work,API api=API::Run)
{
    current=id;const auto before=caches(x);const unsigned commands=queue->command_buffers;
    char error[512]={};uint64_t ticket=UINT64_C(0xa5a5a5a5a5a5a5a5);int rc;
    if(api==API::Submit)rc=rt_executor_submit_calls(x,calls.data(),calls.size(),&ticket,error,sizeof error);
    else if(api==API::Record)rc=rt_executor_record_calls(x,calls.data(),calls.size(),&ticket,error,sizeof error);
    else rc=rt_executor_run_calls(x,calls.data(),calls.size(),error,sizeof error);
    const bool named=!reason||std::strstr(error,reason);
    STAGE_CHECK(rc==(reason?-1:0));STAGE_CHECK(named);STAGE_CHECK(x->state.shader_stages==retained);
    if(api!=API::Run){if(reason)STAGE_CHECK(ticket==UINT64_C(0xa5a5a5a5a5a5a5a5));else STAGE_CHECK(ticket&&rt_executor_wait_ticket(x,ticket,error,sizeof error)==0);}
    const bool pool_match=!std::memcmp(pool,expected.data(),Pool),cache_match=before==caches(x);
    STAGE_CHECK(pool_match);
    if(!pool_match&&failures<=32){size_t at=0;while(at<Pool&&pool[at]==expected[at])++at;
        std::fprintf(stderr,"%s pool byte%zu: %02x expected%02x\n",id,at,pool[at],expected[at]);}
    if(no_work){STAGE_CHECK(cache_match);STAGE_CHECK(queue->command_buffers==commands);}
    std::printf("stage_case id=%s rc=%d expected_rc=%d retained=0x%08x expected_stage=0x%08x named_reason=%u pool_match=%u caches_unchanged=%u command_delta=%u\n",
        id,rc,reason?-1:0,static_cast<unsigned>(x->state.shader_stages),static_cast<unsigned>(retained),static_cast<unsigned>(named),static_cast<unsigned>(pool_match),static_cast<unsigned>(cache_match),queue->command_buffers-commands);
    ++cases;return rc;
}
void reject(rt_executor *x,PBStageQueue *queue,uint8_t *pool,const char *id,const Calls &calls,uint32_t word,API api=API::Run)
{
    char reason[80];std::snprintf(reason,sizeof reason,"VGT_SHADER_STAGES_EN 0x%08x",static_cast<unsigned>(word));
    perform(x,queue,pool,id,calls,Bytes(pool,pool+Pool),word,reason,true,api);
}
void successful(rt_executor *x,PBStageQueue *queue,uint8_t *pool,const char *id,const Calls &calls,unsigned scene,
                bool color=true,API api=API::Run)
{
    Bytes expected(pool,pool+Pool);expected_draw(expected,scene,color);const unsigned commands=queue->command_buffers;
    perform(x,queue,pool,id,calls,expected,0,nullptr,false,api);STAGE_CHECK(queue->command_buffers>commands);
}
void stage_write(rt_executor *x,PBStageQueue *queue,uint8_t *pool,const char *id,uint32_t word,API api=API::Run)
{ Calls calls;stages(calls,word);perform(x,queue,pool,id,calls,Bytes(pool,pool+Pool),word,nullptr,true,api); }
void cases_for(rt_executor *x,PBStageQueue *queue,uint8_t *pool)
{
    const std::array<size_t,6> empty{};
    STAGE_CHECK(x->state.shader_stages==0&&caches(x)==empty);
    seed(pool,0);positions(pool,Route::Auto);Calls calls;setup(calls,0,Route::Auto);draw_call(calls,Route::Auto);
    successful(x,queue,pool,"default/fresh-active",calls,0);
    for(unsigned route=0;route<8;++route) {
        const Route r=static_cast<Route>(route);seed(pool,route+1);positions(pool,r);calls.clear();
        if(!(route&1))stages(calls,0);setup(calls,0,r);if(route&1)stages(calls,0);draw_call(calls,r);
        char id[96];std::snprintf(id,sizeof id,"zero/%s-warm",RouteNames[route]);successful(x,queue,pool,id,calls,0);
        seed(pool,route+10);calls.clear();setup(calls,0,r);stages(calls,0x45);draw_call(calls,r);
        std::snprintf(id,sizeof id,"refuse/%s-00000045",RouteNames[route]);reject(x,queue,pool,id,calls,0x45);
    }
    const uint32_t composites[]={3,0x18,0xc0,0x45,0xb0,0xad,0xffffffff};
    for(unsigned i=0;i<39;++i) {
        const uint32_t word=i<32?uint32_t(1)<<i:composites[i-32];seed(pool,i+20);positions(pool,Route::Auto);
        rt_executor_reset(x);calls.clear();setup(calls,0,Route::Auto);stages(calls,word);
        char id[96];std::snprintf(id,sizeof id,"word/%08x-write",static_cast<unsigned>(word));
        perform(x,queue,pool,id,calls,Bytes(pool,pool+Pool),word,nullptr,true);
        calls.clear();draw_call(calls,Route::Auto);std::snprintf(id,sizeof id,"word/%08x-draw",static_cast<unsigned>(word));reject(x,queue,pool,id,calls,word);
    }
    /* A write can be unsupported for drawing, then replaced before any draw. */
    seed(pool,60);positions(pool,Route::Auto);calls.clear();setup(calls,0,Route::Auto);stages(calls,0xffffffff);stages(calls,0);draw_call(calls,Route::Auto);
    successful(x,queue,pool,"order/nonzero-zero-draw",calls,0);
    for(unsigned api=0;api<3;++api) {
        const API path=static_cast<API>(api);char id[96];seed(pool,70+api);calls.clear();setup(calls,0,Route::Auto);stages(calls,0x200);
        std::snprintf(id,sizeof id,"carry/api%u-setup-nonzero",api);perform(x,queue,pool,id,calls,Bytes(pool,pool+Pool),0x200,nullptr,true,path);
        calls.clear();draw_call(calls,Route::Auto);std::snprintf(id,sizeof id,"carry/api%u-refused-draw",api);reject(x,queue,pool,id,calls,0x200,path);
        calls.clear();stages(calls,0);draw_call(calls,Route::Auto);std::snprintf(id,sizeof id,"carry/api%u-restored-draw",api);successful(x,queue,pool,id,calls,0,true,path);
    }
    stage_write(x,queue,pool,"reset/retained-nonzero",0x80000000);const auto before=caches(x);const unsigned commands=queue->command_buffers;
    rt_executor_reset(x);STAGE_CHECK(x->state.shader_stages==0&&caches(x)==before&&queue->command_buffers==commands);
    seed(pool,80);calls.clear();setup(calls,0,Route::Auto);draw_call(calls,Route::Auto);successful(x,queue,pool,"reset/default-active",calls,0);
    /* Earlier completed work survives a later refusal; neither later target nor store bank changes. */
    seed(pool,81);calls.clear();setup(calls,0,Route::Auto);draw_call(calls,Route::Auto);setup(calls,1,Route::Auto);stages(calls,0xb0);draw_call(calls,Route::Auto);
    Bytes partial(pool,pool+Pool);expected_draw(partial,0);perform(x,queue,pool,"order/A-completes-B-refused",calls,partial,0xb0,"VGT_SHADER_STAGES_EN 0x000000b0",false);
    calls.clear();setup(calls,2,Route::Auto);stages(calls,0);draw_call(calls,Route::Auto);successful(x,queue,pool,"order/C-restores-after-B",calls,2);
    /* The new state cannot erase the independent instance requirement, including after restore. */
    seed(pool,82);calls.clear();setup(calls,0,Route::Auto);instances(calls,2);stages(calls,0x45);draw_call(calls,Route::Auto);reject(x,queue,pool,"instances/stage-first",calls,0x45);
    calls.clear();stages(calls,0);draw_call(calls,Route::Auto);perform(x,queue,pool,"instances/stage-zero-still-two",calls,Bytes(pool,pool+Pool),0,"2 instances",true);
    calls.clear();instances(calls,1);draw_call(calls,Route::Auto);successful(x,queue,pool,"instances/restore-one",calls,0);
    for(unsigned mode=0;mode<2;++mode) {
        seed(pool,90+mode);calls.clear();setup(calls,0,Route::Auto);stages(calls,0x100);if(mode)reg(calls,0xa08e,0);draw_call(calls,Route::Auto,mode?3:0);
        reject(x,queue,pool,mode?"inactive/masked-stage-refused":"inactive/zero-count-stage-refused",calls,0x100);
    }
    seed(pool,92);calls.clear();setup(calls,0,Route::Auto);stages(calls,0);reg(calls,0xa08e,0);draw_call(calls,Route::Auto);
    successful(x,queue,pool,"inactive/zero-mask-still-store-depth",calls,0,false);
}
} // namespace stage_fixture
int main(void)
{
    using namespace stage_fixture;
    @autoreleasepool {
        char name[]="/tmp/paleblood-metal-stages-XXXXXX",error[512]={};const int fd=mkstemp(name);
        if(fd<0)return 1;
        if(unlink(name)||ftruncate(fd,Pool)){close(fd);return 1;}
        auto *pool=static_cast<uint8_t *>(mmap(nullptr,Pool,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0));
        if(pool==MAP_FAILED){close(fd);return 1;}std::memset(pool,0xcc,Pool);
        std::memcpy(pool+VertexCode,VertexProgram,sizeof VertexProgram);std::memcpy(pool+PixelCode,PixelProgram,sizeof PixelProgram);
        const float color[3][4]={{1,0,0,1},{1,0,0,1},{1,0,0,1}};std::memcpy(pool+Colors,color,sizeof color);
        const uint16_t small[5]={99,2,0,1,98};const uint32_t wide[5]={99,2,0,1,98};
        std::memcpy(pool+Index16,small,sizeof small);std::memcpy(pool+Index32,wide,sizeof wide);
        rt_executor *x=nullptr;rt_executor_memory memory{};memory.fd=fd;memory.size=Pool;memory.guest_base=Base;
        const int opened=rt_executor_open(&memory,&x,error,sizeof error);
        if(opened||!x){std::fprintf(stderr,"owned stage executor open: %s\n",error);close(fd);munmap(pool,Pool);return 1;}
        rt_executor *cold=nullptr;const int cold_opened=rt_executor_open(&memory,&cold,error,sizeof error);close(fd);
        if(cold_opened||!cold){std::fprintf(stderr,"owned cold stage executor open: %s\n",error);rt_executor_close(x);munmap(pool,Pool);return 1;}
        PBStageQueue *cold_queue=[PBStageQueue alloc];cold_queue->native_queue=cold->queue;cold_queue->command_buffers=0;cold->queue=(id<MTLCommandQueue>)cold_queue;
        seed(pool,0);positions(pool,Route::Auto);Calls cold_calls;setup(cold_calls,0,Route::Auto);stages(cold_calls,0x45);draw_call(cold_calls,Route::Auto);
        reject(cold,cold_queue,pool,"refuse/cold-auto-00000045",cold_calls,0x45);rt_executor_close(cold);
        PBStageQueue *queue=[PBStageQueue alloc];queue->native_queue=x->queue;queue->command_buffers=0;x->queue=(id<MTLCommandQueue>)queue;
        cases_for(x,queue,pool);STAGE_CHECK(cases==116);
        std::printf("stage_summary cases=%u checks=%u failures=%u command_buffers=%u\n",cases,checks,failures,queue->command_buffers);
        rt_executor_close(x);munmap(pool,Pool);return failures?1:0;
    }
}
