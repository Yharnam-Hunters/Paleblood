/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Private SPIRV-Cross output capture adapter. The raster pass follows rect_list.c's
   existing project contract; this is not independent evidence of PS4 geometry rules. */
#ifndef PALEBLOOD_METAL_RECTANGLES_H
#define PALEBLOOD_METAL_RECTANGLES_H
#include <sstream>
#include <stdexcept>

struct MetalRectangleShader {
    id<MTLFunction> raster = nil;
    size_t stride = 0;
};
struct MetalCompiler : spirv_cross::CompilerMSL {
    using CompilerMSL::CompilerMSL;
    const spirv_cross::SPIRType &capture_type() { return get_stage_out_struct_type(); }
    std::string capture_name() { return type_to_glsl(get_stage_out_struct_type()); }
};

/* A clip attribute is illegal on a buffer pointee. Capture its unchanged float
   values at a reserved location, then restore the builtin on the raster output. */
static void rectangle_capture_options(MetalCompiler &compiler)
{
    unsigned clips = 0;
    for (const auto &builtin : compiler.get_shader_resources().builtin_outputs)
        if (builtin.builtin == spv::BuiltInClipDistance) {
            const auto id = builtin.resource.id;
            if (++clips != 1 || !compiler.has_decoration(id, spv::DecorationBuiltIn))
                throw std::runtime_error("rectangle clip output has an unexpected interface");
            compiler.unset_decoration(id, spv::DecorationBuiltIn);
            compiler.set_decoration(id, spv::DecorationLocation, 32);
        }
    auto options = compiler.get_msl_options();
    options.capture_output_to_buffer = true; options.disable_rasterization = true;
    options.shader_output_buffer_index = 1; options.indirect_params_buffer_index = 2;
    compiler.set_msl_options(options);
}

/* Reflect the synthesized capture struct after compile(), including its actual
   member order. A generated static_assert also checks the CPU allocation stride
   against Metal's layout, rather than relying on a guessed field offset. */
static std::string rectangle_raster_source(MetalCompiler &compiler, bool flip_y, MetalRectangleShader &out)
{
    const auto &type = compiler.capture_type();
    std::string position, clips;
    std::array<std::string,32> parameters{};
    unsigned clip_count = 0;
    size_t bytes = 0;
    for (unsigned i = 0; i < type.member_types.size(); ++i) {
        const auto &t = compiler.get_type(type.member_types[i]);
        const std::string field = compiler.get_member_name(type.self,i);
        if (t.basetype != spirv_cross::SPIRType::Float || t.width != 32 || t.columns != 1 || field.empty())
            throw std::runtime_error("rectangle capture requires named float outputs");
        if (compiler.has_member_decoration(type.self,i,spv::DecorationBuiltIn)) {
            if (compiler.get_member_decoration(type.self,i,spv::DecorationBuiltIn) != spv::BuiltInPosition ||
                !position.empty() || t.vecsize != 4 || !t.array.empty())
                throw std::runtime_error("rectangle capture has an unsupported builtin");
            position = field;
        } else {
            if (!compiler.has_member_decoration(type.self,i,spv::DecorationLocation))
                throw std::runtime_error("rectangle capture output has no location");
            const unsigned location = compiler.get_member_decoration(type.self,i,spv::DecorationLocation);
            if (location == 32) {
                if (!clips.empty() || t.vecsize != 1 || t.array.size() != 1 ||
                    !t.array_size_literal[0] || !t.array[0] || t.array[0] > 8)
                    throw std::runtime_error("rectangle capture has an unsupported clip array");
                clips = field; clip_count = t.array[0];
            } else {
                if (location >= parameters.size() || !parameters[location].empty() || t.vecsize != 4 || !t.array.empty())
                    throw std::runtime_error("rectangle capture has an unsupported parameter");
                parameters[location] = field;
            }
        }
        if (t.array.empty()) { bytes = (bytes + 15) & ~size_t(15); bytes += 16; }
        else bytes += clip_count * sizeof(float);
    }
    if (position.empty()) throw std::runtime_error("rectangle capture has no position");
    out.stride = (bytes + 15) & ~size_t(15);
    std::ostringstream m;
    m << "\nstatic_assert(sizeof(" << compiler.capture_name() << ") == " << out.stride << ", \"rectangle capture stride\");\n"
      << "struct PBRectangleOut { float4 position [[position]];\n";
    for (unsigned p = 0; p < parameters.size(); ++p) if (!parameters[p].empty())
        m << "float4 parameter" << p << " [[user(locn" << p << ")]];\n";
    if (clip_count) m << "float clips [[clip_distance]] [" << clip_count << "];\n";
    m << "};\ninline PBRectangleOut pb_rectangle_value(uint vid, const device "
      << compiler.capture_name() << " *values, const device uint *indices) {\n"
      << "uint group = (vid / 6u) * 3u;\n"
      << "uint ids[3] = {indices[group], indices[group+1u], indices[group+2u]};\n"
      << "float4 p[3] = {values[ids[0]]." << position << ",values[ids[1]]." << position << ",values[ids[2]]." << position << "};\n"
      << "bool c1 = (p[1].x == p[2].x && p[1].y == p[0].y) || (p[1].y == p[2].y && p[1].x == p[0].x);\n"
      << "bool c2 = (p[2].x == p[0].x && p[2].y == p[1].y) || (p[2].y == p[0].y && p[2].x == p[1].x);\n"
      << "uint corner = c1 ? 1u : (c2 ? 2u : 0u);\n"
      << "uint c = ids[corner], a = ids[(corner+1u)%3u], b = ids[(corner+2u)%3u];\n"
      << "const uint strip[6] = {0u,1u,2u,2u,1u,3u}; uint k = strip[vid%6u];\n"
      << "uint source = k == 0u ? c : (k == 1u ? a : b); PBRectangleOut result;\n";
    auto value = [&](const std::string &destination, const std::string &field) {
        m << "result." << destination << " = k == 3u ? (values[a]." << field << " + values[b]." << field
          << ") - values[c]." << field << " : values[source]." << field << ";\n";
    };
    value("position",position);
    for (unsigned p = 0; p < parameters.size(); ++p) if (!parameters[p].empty())
        value("parameter" + std::to_string(p),parameters[p]);
    for (unsigned c = 0; c < clip_count; ++c)
        value("clips[" + std::to_string(c) + "]",clips + "[" + std::to_string(c) + "]");
    /* SPIRV-Cross intentionally omits its Y fixup in a capture-only shader. */
    if (flip_y) m << "result.position.y = -result.position.y;\n";
    m << "return result;\n}\n"
      << "vertex PBRectangleOut pb_rectangle_raster(uint vid [[vertex_id]], const device "
      << compiler.capture_name() << " *values [[buffer(1)]], const device uint *indices [[buffer(2)]]) {\n"
      << "return pb_rectangle_value(vid,values,indices);\n}\n";
    return m.str();
}
#endif
