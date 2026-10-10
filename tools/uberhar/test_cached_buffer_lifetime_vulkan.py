#!/usr/bin/env python3
"""CodexAstraLocal: Real Vulkan UBO/LUT consumers challenge cached-ring reuse."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

from test_cached_buffer_lifetime import ROOT, definition, digest, replace_once, require


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', type=Path, default=ROOT)
    parser.add_argument('--output', type=Path, default=ROOT / 'build/uberhar-probe/cached-buffer-lifetime-vulkan')
    parser.add_argument('--glslang', type=Path, default=ROOT / 'build/uberhar-validators/glslang/StandAlone/glslang')
    parser.add_argument('--spirv-val', type=Path, default=ROOT / 'build/uberhar-validators/spirv-tools/tools/spirv-val')
    parser.add_argument('--icd', type=Path, required=True)
    args = parser.parse_args()
    repo = args.source_root.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix='run-', dir=args.output.resolve()))
    proof = {'author': 'CodexAstraLocal', 'status': 'INCOMPLETE', 'inputs': {}, 'commands': [], 'variants': [],
             'scope': 'Actual source-extracted ring methods and marker; real integer fragment output and fences. '
                      'Controlled command-owner seam, not full emulator/title execution or performance.'}

    def read(path):
        path = path.resolve()
        proof['inputs'][str(path)] = digest(path)
        relative = path.relative_to(repo) if path.is_relative_to(repo) else Path('external') / path.name
        target = output / 'sources' / relative
        target.parent.mkdir(parents=True, exist_ok=True); target.write_bytes(path.read_bytes())
        return path.read_text()

    environment = os.environ.copy()
    # CodexAstraLocal: Explicit ICD selection and its hash prevent a silent
    # switch between a software renderer and a host hardware driver.
    environment['VK_ICD_FILENAMES'] = str(args.icd.resolve())
    environment['VK_DRIVER_FILES'] = str(args.icd.resolve())
    environment['MESA_SHADER_CACHE_DISABLE'] = 'true'

    def command(argv, stem, timeout):
        result = subprocess.run(list(map(str, argv)), cwd=repo, env=environment,
                                capture_output=True, timeout=timeout)
        stem.with_suffix('.stdout').write_bytes(result.stdout)
        stem.with_suffix('.stderr').write_bytes(result.stderr)
        proof['commands'].append({'argv': list(map(str, argv)), 'returncode': result.returncode})
        return result

    try:
        for path in (Path(__file__), ROOT / 'tools/uberhar/test_cached_buffer_lifetime.py', args.icd): read(path)
        for tool in (args.glslang, args.spirv_val):
            require(tool.is_file(), 'required shader tool absent: ' + str(tool))
            proof['inputs'][str(tool.resolve())] = digest(tool)
        fixture = read(ROOT / 'tools/uberhar/test_cached_buffer_lifetime_vulkan.cpp')
        stream = read(repo / 'src/video_core/renderer_vulkan/vk_stream_buffer.cpp')
        read(repo / 'src/video_core/renderer_vulkan/vk_stream_buffer.h')
        raster = read(repo / 'src/video_core/renderer_vulkan/vk_rasterizer.cpp')
        ring = '\n\n'.join(definition(stream, sig) for sig in (
            'std::tuple<u8*, u32, bool> StreamBuffer::Map(', 'void StreamBuffer::Commit(',
            'void StreamBuffer::MarkDrawUse(', 'void StreamBuffer::ReserveWatches(',
            'void StreamBuffer::WaitPendingOperations('))
        marker = definition(raster, 'void RasterizerVulkan::MarkCachedShaderBuffersUsed()')
        stub = output / 'stub/video_core/renderer_vulkan/vk_common.h'
        stub.parent.mkdir(parents=True)
        stub.write_text('''// CodexAstraLocal: Thin real Vulkan endpoints for the production ring.
#pragma once
#include <vulkan/vulkan.h>
#include <cstdlib>
#include "common/common_types.h"
namespace vk {
using Buffer=VkBuffer; using DeviceMemory=VkDeviceMemory; using BufferUsageFlags=VkBufferUsageFlags;
struct MappedMemoryRange { DeviceMemory memory; u64 offset, size; };
struct Device {
 VkDevice handle{}; Device()=default; Device(VkDevice value):handle{value}{}
 void invalidateMappedMemoryRanges(MappedMemoryRange r) {
  VkMappedMemoryRange actual{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
  actual.memory=r.memory; actual.offset=r.offset; actual.size=r.size;
  if(vkInvalidateMappedMemoryRanges(handle,1,&actual)!=VK_SUCCESS) std::_Exit(24);
 }
 void flushMappedMemoryRanges(MappedMemoryRange r) {
  VkMappedMemoryRange actual{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
  actual.memory=r.memory; actual.offset=r.offset; actual.size=r.size;
  if(vkFlushMappedMemoryRanges(handle,1,&actual)!=VK_SUCCESS) std::_Exit(24);
 }
};
}
''')
        # CodexAstraLocal: Three independent shader inputs and a draw ordinal
        # expose incorrect cached bytes and recording order without tolerance.
        shaders = {
            'vert': '''#version 450
void main() {
 const vec2 points[3]=vec2[3](vec2(-1,-1),vec2(3,-1),vec2(-1,3));
 gl_Position=vec4(points[gl_VertexIndex],0,1);
}
''',
            'frag': '''#version 450
layout(set=0,binding=0,std140) uniform U { uvec4 value; } u;
layout(set=0,binding=1) uniform usamplerBuffer proctex;
layout(set=0,binding=2) uniform usamplerBuffer lighting_fog;
layout(push_constant) uniform P { uint ordinal; } p;
layout(location=0) out uvec4 color;
void main() { color=uvec4(u.value.x,texelFetch(proctex,0).x,texelFetch(lighting_fog,0).x,p.ordinal); }
'''}
        modules = []
        for stage, text in shaders.items():
            source = output / ('test.' + stage); source.write_text(text)
            module = output / ('test.' + stage + '.spv'); modules.append(module)
            built = command([args.glslang, '-V', '--target-env', 'vulkan1.1', '-S', stage,
                             '-o', module, source], output / ('shader-' + stage), 30)
            require(built.returncode == 0, 'shader compilation failed: ' + built.stderr.decode())
            checked = command([args.spirv_val, '--target-env', 'vulkan1.1', module],
                              output / ('validate-' + stage), 30)
            require(checked.returncode == 0, 'SPIR-V validation failed')
        variants = [('candidate', ring, marker, True),
                    ('omit-last-use', ring, replace_once(marker,
                     '    uniform_buffer.MarkDrawUse();\n    texture_buffer.MarkDrawUse();\n    texture_lf_buffer.MarkDrawUse();', ''), False),
                    ('stamp-before-final-flush', replace_once(ring,
                     'last_draw_use_tick = scheduler.CurrentTick();',
                     'last_draw_use_tick = scheduler.CurrentTick() - 1;'), marker, False),
                    ('omit-wrap-wait', replace_once(ring, 'scheduler.Wait(last_draw_use_tick);',
                     '(void)last_draw_use_tick;'), marker, False)]
        for label, ring_code, mark_code, positive in variants:
            case = output / label; case.mkdir()
            (case / 'fixture.cpp').write_text(fixture)
            (case / 'ring.inc').write_text(ring_code + '\n'); (case / 'mark.inc').write_text(mark_code + '\n')
            argv = [os.environ.get('CXX', 'c++'), '-std=c++20', '-O2', '-pthread',
                    '-I' + str(output / 'stub'), '-I' + str(output / 'sources/src'),
                    '-I' + str(repo / 'src'), '-I' + str(repo / 'externals/vulkan-headers/include'),
                    '-MMD', '-MF', case / 'dependencies.d', case / 'fixture.cpp',
                    '-l:libvulkan.so.1', '-o', case / 'fixture']
            compiled = command(argv, case / 'compile', 90)
            require(compiled.returncode == 0, 'Vulkan fixture compile: ' + compiled.stderr.decode()[-6000:])
            result = command([case / 'fixture', *modules], case / 'run', 45)
            if positive:
                require(result.returncode == 0 and b'PASS Vulkan cached lifetime cases=6 rendered=18 ' in result.stdout,
                        'Vulkan candidate failure: ' + result.stderr.decode())
            else:
                require(result.returncode == 23 and result.stderr == b'FAIL GPU cached draw bytes overwritten\n',
                        'Vulkan defect did not expose intended pixels: ' + label + '\n' + result.stderr.decode())
            proof['variants'].append({'label': label, 'returncode': result.returncode})
            print(label + ': PASS', flush=True)
        proof['status'] = 'PASS'
    finally:
        proof['inputs_after'] = {path: digest(Path(path)) for path in proof['inputs']}
        proof['unchanged'] = proof['inputs'] == proof['inputs_after']
        proof['artifacts'] = {str(p.relative_to(output)): digest(p) for p in output.rglob('*') if p.is_file()}
        (output / 'provenance.json').write_text(json.dumps(proof, indent=2) + '\n')
        print('Proof: ' + str(output), flush=True)
    require(proof['unchanged'], 'source changed during Vulkan gate')


if __name__ == '__main__':
    main()
