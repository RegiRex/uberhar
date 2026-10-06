#!/usr/bin/env python3
"""CodexAstraUlt: Reproduce quaternion interpolation divergence with production vertex paths.

CPU conversion/AddTriangle are extracted, and the real guest-program GLSL generator
emits a no-GS vertex shader. The host probe measures a nondegenerate interior sample;
--render additionally executes both vertex shaders and the production quaternion
rotation helper on Mesa. This is synthetic parity evidence, not a Dark Moon capture.
The optional-route rejection itself is tested by test_gpu_input_parity.py.
"""
import argparse
import os
from pathlib import Path
import struct
import subprocess


def extract(source, signature):
    # CodexAstraUlt: Extract complete existing function bodies rather than copy their policy.
    start = source.index(signature)
    return source[start:source.index('\n}', start) + 2]


def build_probe(args, directory):
    cpu = Path('src/video_core/rasterizer_accelerated.cpp').read_text()
    header = Path('src/video_core/rasterizer_accelerated.h').read_text()
    start = header.index('    struct HardwareVertex {')
    vertex_type = header[start:header.index('\n    };', start) + len('\n    };')]
    methods = '\n'.join(extract(cpu, signature) for signature in (
        'RasterizerAccelerated::HardwareVertex::HardwareVertex(',
        'static bool AreQuaternionsOpposite(',
        'void RasterizerAccelerated::AddTriangle('))
    prefix = r'''
// CodexAstraUlt: Use real PICA/register/generator types and only a minimal CPU owner shell.
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <nihstro/inline_assembly.h>
#include "common/logging/log.h"
#include "video_core/pica/regs_internal.h"
#include "video_core/pica/shader_setup.h"
#include "video_core/pica/output_vertex.h"
#include "video_core/shader/generator/shader_gen.h"
#include "video_core/shader/generator/glsl_shader_gen.h"
namespace Common::Log {
void Stop() {}
void FmtLogMessageImpl(Class, Level level, const char*, unsigned int, const char*,
                       fmt::string_view format, const fmt::format_args& args) {
    if (level >= Level::Error) throw std::runtime_error(fmt::vformat(format,args));
}
}
namespace VideoCore {
using Pica::f24;
struct RasterizerAccelerated {
'''
    suffix = r'''
} // CodexAstraUlt: Minimal production CPU owner.
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
// CodexAstraUlt: An independent scalar normal calculation checks the interior sample;
// the optional Mesa run below uses the extracted production GLSL rotation itself.
float NormalZ(Common::Vec4f q) {
    const auto length = std::sqrt(Common::Dot(q,q));
    Check(length > 0.01f,"Fixture accidentally relies on zero-length normalization");
    q /= length;
    return 1.0f-2.0f*(q.x*q.x+q.y*q.y);
}
int main(int argc, char** argv) {
    Check(argc==2,"Expected fixture output directory");
    const std::filesystem::path directory{argv[1]};
    using namespace Pica;
    using namespace Pica::Shader::Generator;
    using O=nihstro::OpCode::Id; using D=nihstro::DestRegister; using S=nihstro::SourceRegister;
    // CodexAstraUlt: A real guest MOV program forwards position and quaternion outputs.
    // Their nonconstant per-vertex data exposes a gap constant-quaternion FS oracles miss.
    ShaderSetup setup;
    const auto binary=nihstro::InlineAsm::CompileToRawBinary({
        {O::MOV,D::MakeOutput(0),S::MakeInput(0)},
        {O::MOV,D::MakeOutput(1),S::MakeInput(1)}, {O::END}});
    for (unsigned i=0;i<binary.program.size();++i) setup.UpdateProgramCode(i,binary.program[i].hex);
    for (unsigned i=0;i<binary.swizzle_table.size();++i) setup.UpdateSwizzleData(i,binary.swizzle_table[i].hex);
    RegsInternal regs{};
    regs.vs.output_mask.Assign(3);
    regs.vs.max_input_attribute_index.Assign(1);
    regs.rasterizer.vs_output_total.Assign(2);
    using Semantic=RasterizerRegs::VSOutputAttributes::Semantic;
    for (unsigned i=0;i<2;++i) {
        auto& mapping=regs.rasterizer.vs_output_attributes[i];
        mapping.map_x.Assign(static_cast<Semantic>(i*4));
        mapping.map_y.Assign(static_cast<Semantic>(i*4+1));
        mapping.map_z.Assign(static_cast<Semantic>(i*4+2));
        mapping.map_w.Assign(static_cast<Semantic>(i*4+3));
    }
    PicaVSConfig config{regs,setup};
    ExtraVSConfig extra{};
    extra.sanitize_mul=true;
    extra.load_flags.fill(AttribLoadFlags::Float);
    const auto source=GLSL::GenerateVertexShader(setup,config,extra);
    Check(source.find("normquat = GetVertexQuaternion();")!=std::string::npos &&
          source.find("AreQuaternionsOpposite")==std::string::npos,
          "Production no-GS output changed; revisit the parity fixture");
    std::ofstream(directory/"promoted.vert") << "#version 430\n" << source;
    std::ofstream(directory/"cpu.vert") << "#version 430\n"
        << GLSL::GenerateTrivialVertexShader(false,false);
    const std::array<Common::Vec4f,3> positions{{{-1,-1,-.5f,1},{1,-1,-.5f,1},{-1,1,-.5f,1}}};
    const float root_half=std::sqrt(.5f);
    const std::array<Common::Vec4f,3> varying{{{0,0,0,1},{0,-root_half,0,-root_half},{0,0,0,1}}};
    // CodexAstraUlt: Pixel-center barycentric coordinates inside a 64x64 triangle.
    // Equal clip W isolates quaternion signs from perspective/clip/depth differences.
    constexpr std::array weights{1.0f-28.5f/64-16.5f/64,28.5f/64,16.5f/64};
    for (unsigned control=0;control<3;++control) {
        auto quaternions=varying;
        if (control==1) quaternions[1]=-quaternions[1];
        if (control==2) quaternions.fill(quaternions[0]);
        std::array<OutputVertex,3> vertices{};
        for (unsigned v=0;v<3;++v) for (unsigned c=0;c<4;++c) {
            vertices[v].pos[c]=f24::FromFloat32(positions[v][c]);
            vertices[v].quat[c]=f24::FromFloat32(quaternions[v][c]);
        }
        VideoCore::RasterizerAccelerated renderer;
        renderer.AddTriangle(vertices[0],vertices[1],vertices[2]);
        Check(renderer.vertex_batch.size()==3,"CPU triangle lost vertices");
        Common::Vec4f raw_sample{}, corrected_sample{};
        std::ofstream raw(directory/("raw"+std::to_string(control)+".bin"),std::ios::binary);
        std::ofstream cpu(directory/("cpu"+std::to_string(control)+".bin"),std::ios::binary);
        for (unsigned v=0;v<3;++v) {
            const VideoCore::RasterizerAccelerated::HardwareVertex unchanged{vertices[v],false};
            const auto& corrected=renderer.vertex_batch[v];
            Check(unchanged.position==corrected.position,"Correction changed position");
            raw.write(reinterpret_cast<const char*>(unchanged.position.AsArray()),16);
            raw.write(reinterpret_cast<const char*>(unchanged.normquat.AsArray()),16);
            cpu.write(reinterpret_cast<const char*>(corrected.position.AsArray()),16);
            cpu.write(reinterpret_cast<const char*>(corrected.normquat.AsArray()),16);
            raw_sample+=unchanged.normquat*weights[v];
            corrected_sample+=corrected.normquat*weights[v];
        }
        const float raw_normal=NormalZ(raw_sample), corrected_normal=NormalZ(corrected_sample);
        if (control==0) {
            Check(raw_normal<-.1f && corrected_normal>.65f,
                  "Opposite-hemisphere interpolation no longer reproduces a dark patch");
        } else {
            Check(std::abs(raw_normal-corrected_normal)<1e-6f,"Same-hemisphere control diverged");
        }
        std::printf("quaternion_case=%u raw_normal_z=%.6f cpu_normal_z=%.6f\n",
                    control,raw_normal,corrected_normal);
    }
    std::puts("PASS: production CPU triangle correction, real no-GS guest vertex generator, "
              "nondegenerate interior divergence and two ordinary controls");
}
'''
    cpp = args.output.with_suffix('.cpp')
    declaration = '''
    void AddTriangle(const Pica::OutputVertex&,const Pica::OutputVertex&,const Pica::OutputVertex&);
    std::vector<HardwareVertex> vertex_batch;
};
'''
    cpp.write_text(prefix + vertex_type + declaration + methods + suffix)
    flags = ['-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie'] if args.sanitize else ['-O2']
    subprocess.run([os.environ.get('CXX','c++'), '-std=c++20', *flags,
                    '-DENABLE_VULKAN', '-DFMT_HEADER_ONLY', '-DXXH_INLINE_ALL',
                    '-Isrc', '-Ibuild/uberhar-profile', '-Iexternals/fmt/include',
                    '-Iexternals/boost', '-Iexternals/xxHash', '-Iexternals/nihstro/include',
                    str(cpp), 'src/video_core/shader/generator/glsl_shader_gen.cpp',
                    'src/video_core/shader/generator/glsl_shader_decompiler.cpp',
                    'src/video_core/shader/generator/shader_gen.cpp',
                    'src/video_core/pica/shader_setup.cpp', '-o', str(args.output)], check=True)
    subprocess.run([str(args.output), str(directory)], check=True)


def render(directory):
    # CodexAstraUlt: Execute the production emitted vertex shaders and rotation helper.
    # Only the final normal/diffuse readout is an adapter; no title/Adreno validation is implied.
    import moderngl
    source = Path('src/video_core/shader/generator/glsl_fs_shader_gen.cpp').read_text()
    rotate = extract(source, 'vec3 quaternion_rotate(')
    fragment = '#version 430\nin vec4 normquat;\nout vec4 color;\n' + rotate + '''
void main() {
    vec3 normal=quaternion_rotate(normalize(normquat),vec3(0.0,0.0,1.0));
    color=vec4(normal.z,max(normal.z,0.0),0.0,1.0);
}
'''
    ctx = moderngl.create_standalone_context(require=430, backend='egl')
    target = ctx.simple_framebuffer((64,64), components=4, dtype='f4')
    target.use()
    uniforms = ctx.buffer(bytes(32))
    uniforms.bind_to_uniform_block(1)
    programs = [ctx.program(vertex_shader=(directory/name).read_text(), fragment_shader=fragment)
                for name in ['promoted.vert','cpu.vert']]
    for control in range(3):
        outputs = []
        for program, prefix, attributes in zip(programs, ['raw','cpu'],
                [('vs_in_typed_reg0','vs_in_typed_reg1'),('vert_position','vert_normquat')]):
            buffer = ctx.buffer((directory/f'{prefix}{control}.bin').read_bytes())
            vao = ctx.vertex_array(program, [(buffer,'4f 4f',*attributes)])
            target.clear()
            vao.render(mode=moderngl.TRIANGLES, vertices=3)
            data = target.read(components=4, dtype='f4')
            outputs.append(struct.unpack(f'<{64*64*4}f',data))
            vao.release()
            buffer.release()
        raw, cpu = outputs
        sample = (16*64+28)*4
        if control == 0:
            assert raw[sample] < -.1 and cpu[sample] > .65, (raw[sample],cpu[sample])
            different = sum(abs(raw[i]-cpu[i])>.1 for i in range(0,len(raw),4))
            assert different > 100, different
            print(f'Mesa quaternion divergence: raw_normal_z={raw[sample]:.6f} '
                  f'cpu_normal_z={cpu[sample]:.6f} changed_pixels={different}', flush=True)
        else:
            assert max(abs(a-b) for a,b in zip(raw,cpu)) < 1e-5, control
    for program in programs:
        program.release()
    uniforms.release()
    target.release()
    print(f'PASS: three production vertex-path/rotation comparisons on {ctx.info["GL_RENDERER"]}; '
          'opposite-sign divergence and same-hemisphere/constant controls', flush=True)
    ctx.release()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, default=Path('build/uberhar-probe/gpu-quaternion-parity'))
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--render', action='store_true')
    parser.add_argument('--render-only', action='store_true')
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True,exist_ok=True)
    directory = args.output.parent/(args.output.name+'-fixtures')
    directory.mkdir(exist_ok=True)
    if not args.render_only:
        build_probe(args,directory)
    if args.render or args.render_only:
        render(directory)


if __name__ == '__main__':
    main()
