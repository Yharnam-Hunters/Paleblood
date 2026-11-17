/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Owned compiled-MSL value oracle. This exercises the rectangle helper, not the
   source vertex shader, capture pass, rasterizer or original GPU behavior. */
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <dispatch/dispatch.h>
#include "runtime/gcn_spirv.h"
#include "spirv_msl.hpp"
#include <array>
#include <string>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <utility>
#include <vector>
#include "../runtime/renderer/metal_rectangles.h"
#include "metal_executor_programs.h"

static unsigned checks, failures, scenarios;
static void require(bool good, const std::string &message)
{
    ++checks;
    if (!good) { ++failures; throw std::runtime_error(message); }
}
static uint32_t bits(float value) { uint32_t word; std::memcpy(&word,&value,4); return word; }
struct Fields {
    std::string position, clips;
    std::array<std::string,32> parameters{};
    std::vector<unsigned> locations;
    unsigned clip_count = 0;
    unsigned width() const { return 4 + 4 * unsigned(locations.size()) + clip_count; }
};
static Fields reflect(MetalCompiler &compiler)
{
    Fields f; const auto &type = compiler.capture_type();
    for (unsigned i = 0; i < type.member_types.size(); ++i) {
        const auto name = compiler.get_member_name(type.self,i);
        if (compiler.has_member_decoration(type.self,i,spv::DecorationBuiltIn)) f.position = name;
        else {
            const auto location = compiler.get_member_decoration(type.self,i,spv::DecorationLocation);
            if (location == 32) { f.clips = name; f.clip_count = compiler.get_type(type.member_types[i]).array[0]; }
            else { require(location < 32,"unexpected parameter location"); f.parameters[location] = name; }
        }
    }
    for (unsigned i = 0; i < 32; ++i) if (!f.parameters[i].empty()) f.locations.push_back(i);
    require(!f.position.empty(),"missing reflected position"); return f;
}
static std::string kernel_source(MetalCompiler &compiler, const Fields &f)
{
    std::ostringstream s;
    s << "\nkernel void owned_rectangle_values(const device float *input [[buffer(0)]], device "
      << compiler.capture_name() << " *values [[buffer(1)]], const device uint *ids [[buffer(2)]], "
      << "device float *output [[buffer(3)]], constant uint2 &placement [[buffer(4)]]) {\n"
      << "for (uint row=0;row<3u;++row) { uint id=ids[placement.y+row]; uint n=row*" << f.width() << "u;\n";
    unsigned offset = 0;
    auto fill = [&](const std::string &field) {
        s << "values[id]." << field << " = float4(";
        for (unsigned k=0;k<4;++k) s << (k?",":"") << "input[n+" << offset++ << "u]";
        s << ");\n";
    };
    fill(f.position); for (unsigned p : f.locations) fill(f.parameters[p]);
    for (unsigned k=0;k<f.clip_count;++k) s << "values[id]." << f.clips << "[" << k << "] = input[n+" << offset++ << "u];\n";
    s << "}\nfor(uint v=0;v<6u;++v) { auto q=pb_rectangle_value(placement.x+v,values,ids); uint o=v*" << f.width() << "u;\n";
    auto flatten = [&](const std::string &field) {
        for (unsigned k=0;k<4;++k) s << "output[o++] = q." << field << "[" << k << "];\n";
    };
    flatten("position"); for (unsigned p : f.locations) flatten("parameter" + std::to_string(p));
    for (unsigned k=0;k<f.clip_count;++k) s << "output[o++] = q.clips[" << k << "];\n";
    s << "}\n}\n"; return s.str();
}
struct Fixture { std::string name; unsigned corner; std::vector<float> values; };
static std::vector<Fixture> fixtures(const Fields &f)
{
    const unsigned width=f.width(); std::vector<Fixture> cases;
    auto add = [&](const char *name,unsigned corner) -> Fixture & {
        Fixture c{name,corner,std::vector<float>(3*width)};
        for(unsigned row=0;row<3;++row) for(unsigned k=0;k<width;++k) c.values[row*width+k]=float(16*(row+1)+k);
        const float positions[3][4]={{-1,-1,0.25f,1},{1,-1,0.5f,1},{-1,1,0.75f,1}};
        for(unsigned row=0;row<3;++row) std::memcpy(c.values.data()+row*width,positions[row],16);
        cases.push_back(std::move(c)); return cases.back();
    };
    auto xy = [&](Fixture &c,unsigned row,float x,float y) { c.values[row*width]=x; c.values[row*width+1]=y; };
    add("corner0",0);
    auto &c1=add("corner1",1); xy(c1,0,1,-1); xy(c1,1,-1,-1); xy(c1,2,-1,1);
    auto &c2=add("corner2",2); xy(c2,0,1,-1); xy(c2,1,-1,1); xy(c2,2,-1,-1);
    auto &priority1=add("corner1-before-corner2",1);
    xy(priority1,0,0.25f,-0.25f); xy(priority1,1,0.25f,0.25f); xy(priority1,2,0.25f,0.25f); priority1.values[2*width+3]=1.5f;
    auto &priority2=add("corner2-before-fallback",2);
    xy(priority2,0,0.25f,0.25f); xy(priority2,1,0.25f,-0.25f); xy(priority2,2,0.25f,0.25f); priority2.values[2*width+3]=1.5f;
    auto &all=add("all-match-selects1",1); for(unsigned row=0;row<3;++row) xy(all,row,0.25f,0.25f);
    auto &none=add("no-match-fallback0",0); xy(none,0,-0.75f,-0.5f); xy(none,1,0.25f,-0.25f); xy(none,2,0.5f,0.75f);
    auto &nan=add("unordered-fallback0",0); nan.values[width]=std::numeric_limits<float>::quiet_NaN();
    auto &zero=add("signed-zero-corner1",1); xy(zero,0,0.0f,2); xy(zero,1,-0.0f,0.0f); xy(zero,2,2,-0.0f);
    auto &order=add("add-before-subtract",0);
    for(unsigned k=2;k<width;++k) { order.values[k]=1; order.values[width+k]=16777216.0f; order.values[2*width+k]=-16777216.0f; }
    for(unsigned clip=0;clip<f.clip_count;++clip) {
        auto &c=add("negative-fourth-clip",0); c.name+=std::to_string(clip);
        const unsigned k=4+4*unsigned(f.locations.size())+clip;
        c.values[k]=0.75f; c.values[width+k]=c.values[2*width+k]=0.25f;
    }
    return cases;
}
/* Corner labels above are explicit fixtures, not a second implementation of the
   selection predicate. Volatile forces the oracle's two float operations in order. */
static float expected(const Fixture &c,unsigned width,unsigned vertex,unsigned field,bool flip)
{
    static const unsigned sources[3][6]={{0,1,2,2,1,3},{1,2,0,0,2,3},{2,0,1,1,0,3}};
    const unsigned row=sources[c.corner][vertex]; float value;
    if(row<3) value=c.values[row*width+field];
    else {
        volatile float sum=c.values[((c.corner+1)%3)*width+field]+c.values[((c.corner+2)%3)*width+field];
        value=sum-c.values[c.corner*width+field];
    }
    return flip && field==1 ? -value : value;
}
static id<MTLComputePipelineState> pipeline(id<MTLDevice> device,unsigned profile,bool flip,Fields &fields,size_t &stride)
{
    gcn_vertex_layout layout{}; layout.user_sgprs=8;
    if(profile!=1) layout.output_control=0xc000ff;
    const uint32_t *code=profile==1?vertex_two_parameters:vertex_clip_eight;
    const size_t count=profile==1?sizeof vertex_two_parameters/4:sizeof vertex_clip_eight/4;
    uint32_t *spirv=nullptr; size_t words=0; char reason[512]={};
    const int translated=gcn_translate_vertex(code,count,&layout,&spirv,&words,reason,sizeof reason);
    std::unique_ptr<uint32_t,decltype(&std::free)> owned(spirv,&std::free);
    require(translated==0 && spirv && words,"owned vertex translation: "+std::string(reason));
    MetalCompiler compiler(spirv,words);
    if(profile==2) { // Derived owned interface: exercise sparse location31, without changing its float values.
        unsigned changed=0;
        for(const auto &r:compiler.get_shader_resources().stage_outputs)
            if(compiler.has_decoration(r.id,spv::DecorationLocation) && compiler.get_decoration(r.id,spv::DecorationLocation)==0) {
                compiler.set_decoration(r.id,spv::DecorationLocation,31); ++changed;
            }
        require(changed==1,"sparse owned parameter relabel");
    }
    auto options=compiler.get_msl_options(); options.set_msl_version(2,3); compiler.set_msl_options(options);
    auto common=compiler.get_common_options(); common.vertex.flip_vert_y=flip; compiler.set_common_options(common);
    spirv_cross::MSLResourceBinding binding{}; binding.stage=spv::ExecutionModelVertex;
    binding.desc_set=spirv_cross::ResourceBindingPushConstantDescriptorSet; binding.binding=spirv_cross::ResourceBindingPushConstantBinding;
    binding.msl_buffer=0; compiler.add_msl_resource_binding(binding);
    rectangle_capture_options(compiler); std::string source=compiler.compile();
    MetalRectangleShader shape; source+=rectangle_raster_source(compiler,flip,shape); fields=reflect(compiler); stride=shape.stride;
    require(fields.clip_count==(profile==1?0u:8u),"reflected clip count");
    require(fields.locations==(profile==1?std::vector<unsigned>{0,1}:std::vector<unsigned>{profile==2?31u:0u}),"reflected parameters");
    source+=kernel_source(compiler,fields);
    auto settings=[MTLCompileOptions new]; settings.languageVersion=MTLLanguageVersion2_3;
    if (@available(macOS 15.0, *)) settings.mathMode=MTLMathModeSafe;
    else throw std::runtime_error("safe helper MSL math requires macOS 15 or later");
    NSError *error=nil; auto library=[device newLibraryWithSource:[NSString stringWithUTF8String:source.c_str()] options:settings error:&error];
    const char *detail=error.localizedDescription.UTF8String;
    require(library!=nil,std::string("helper MSL compile: ")+(detail?detail:"no diagnostic"));
    auto function=[library newFunctionWithName:@"owned_rectangle_values"]; require(function!=nil,"owned helper entry point");
    auto result=[device newComputePipelineStateWithFunction:function error:&error]; detail=error.localizedDescription.UTF8String;
    require(result!=nil,std::string("helper pipeline: ")+(detail?detail:"no diagnostic")); return result;
}
static void evaluate(id<MTLDevice> device,id<MTLCommandQueue> queue,id<MTLComputePipelineState> state,
                     const Fields &fields,size_t stride,const Fixture &c,bool flip,unsigned profile)
{
    constexpr size_t guard=16, slots=18; const unsigned width=fields.width();
    const bool second=(scenarios&1)!=0; const std::array<uint32_t,2> placement={second?6u:0u,second?3u:0u};
    const std::array<uint32_t,12> indices=second?std::array<uint32_t,12>{0,1,2,17,3,9,0,1,2,0,1,2}:
        std::array<uint32_t,12>{17,3,9,0,1,2,0,1,2,0,1,2};
    auto input=[device newBufferWithBytes:c.values.data() length:c.values.size()*4 options:MTLResourceStorageModeShared];
    auto ids=[device newBufferWithBytes:indices.data() length:sizeof indices options:MTLResourceStorageModeShared];
    auto values=[device newBufferWithLength:slots*stride+guard options:MTLResourceStorageModeShared];
    auto output=[device newBufferWithLength:6*width*4+2*guard options:MTLResourceStorageModeShared];
    require(input && ids && values && output,"owned helper buffers");
    std::memset(values.contents,0xa5,values.length); std::memset(output.contents,0xa5,output.length);
    auto command=[queue commandBuffer]; auto encoder=[command computeCommandEncoder]; require(command && encoder,"owned helper encoder");
    [encoder setComputePipelineState:state]; [encoder setBuffer:input offset:0 atIndex:0];
    [encoder setBuffer:values offset:0 atIndex:1]; [encoder setBuffer:ids offset:0 atIndex:2];
    [encoder setBuffer:output offset:guard atIndex:3]; [encoder setBytes:placement.data() length:sizeof placement atIndex:4];
    [encoder dispatchThreadgroups:MTLSizeMake(1,1,1) threadsPerThreadgroup:MTLSizeMake(1,1,1)]; [encoder endEncoding];
    auto done=dispatch_semaphore_create(0); require(done!=nullptr,"completion semaphore");
    [command addCompletedHandler:^(id<MTLCommandBuffer> completed) { (void)completed; dispatch_semaphore_signal(done); }]; [command commit];
    require(dispatch_semaphore_wait(done,dispatch_time(DISPATCH_TIME_NOW,10ll*NSEC_PER_SEC))==0,"helper GPU completion timed out");
    require(command.status==MTLCommandBufferStatusCompleted && !command.error,"helper GPU submission failed"); ++scenarios;
    const auto *data=reinterpret_cast<const float *>(static_cast<const uint8_t *>(output.contents)+guard);
    for(unsigned v=0;v<6;++v) for(unsigned k=0;k<width;++k) {
        const float want=expected(c,width,v,k,flip),got=data[v*width+k]; ++checks;
        if(!(std::isnan(want)?std::isnan(got):bits(want)==bits(got))) {
            if(++failures<=20) std::fprintf(stderr,"profile%u Y%u %s v%u field%u: %08x != %08x\n",profile,unsigned(flip),c.name.c_str(),v,k,bits(got),bits(want));
        }
    }
    const auto *bytes=static_cast<const uint8_t *>(output.contents);
    for(size_t i=0;i<guard;++i) require(bytes[i]==0xa5 && bytes[output.length-guard+i]==0xa5,"output canary");
    const auto *captured=static_cast<const uint8_t *>(values.contents);
    for(size_t slot=0;slot<slots;++slot) if(slot!=17 && slot!=3 && slot!=9)
        for(size_t i=0;i<stride;++i) require(captured[slot*stride+i]==0xa5,"unreferenced capture slot");
    for(size_t i=0;i<guard;++i) require(captured[slots*stride+i]==0xa5,"capture canary");
}
int main(void)
{
    @autoreleasepool { @try { try {
        auto device=MTLCreateSystemDefaultDevice(); require(device!=nil,"real Metal device required");
        auto queue=[device newCommandQueue]; require(queue!=nil,"Metal command queue");
        for(unsigned profile=0;profile<3;++profile) for(unsigned flip=0;flip<2;++flip) {
            Fields fields; size_t stride=0; auto state=pipeline(device,profile,flip!=0,fields,stride);
            for(const auto &c:fixtures(fields)) evaluate(device,queue,state,fields,stride,c,flip!=0,profile);
        }
        std::printf("metal rectangle values: %u helper cases, %u checks, %u failures\n",scenarios,checks,failures);
        return failures?1:0;
    } catch(const std::exception &error) { std::fprintf(stderr,"rectangle values: %s\n",error.what()); return 1; }
    } @catch(NSException *error) { std::fprintf(stderr,"rectangle values: %s\n",error.reason.UTF8String); return 1; } }
}
