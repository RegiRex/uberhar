#!/usr/bin/env python3
"""CodexAstraLocal: Immutable sampled-image descriptors with actual Vulkan consumers."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

from test_cached_buffer_lifetime import ROOT, definition, digest, replace_once, require


def source_contract(cache, header, raster, texture, instance, texture_header):
    """CodexAstraLocal: Bind controlled execution to the actual final owner seam."""
    bind = definition(cache, 'bool PipelineCache::BindPipeline(')
    resolve = definition(cache, 'void PipelineCache::ResolveImageBindings()')
    final = bind.index('    ResolveImageBindings();')
    capture = bind.index('    scheduler.Record(', final)
    between = re.sub(r'//[^\n]*', '', bind[final+len('    ResolveImageBindings();'):capture]).strip()
    require(not between, 'work inserted between final resolution and immutable capture')
    require(bind.index('EnsureDynamicTevState();') < final, 'TEV preparation after final resolve')
    require('descriptor_sets = bound_descriptor_sets' in bind[capture:], 'queued immutable sets missing')
    require('descriptor_sets, offsets);' in bind[capture:], 'actual binding no longer uses captured sets')
    require('scheduler.CurrentTick()' in resolve and 'instance.ImageDescriptorEpoch()' in resolve,
            'final tick/epoch inputs missing')
    require('write.binding, write.element,' in resolve and 'write.view, write.sampler, write.layout' in resolve,
            'array/sampler/layout fields dropped from final writes')
    require('update_queue.AddStorageImage(allocated, write.binding, write.view, write.layout)' in resolve,
            'storage image final-write contract missing')
    require('pending_images[static_cast<u32>(type) - 1].count = 0;' in header,
            'preparation reset missing')
    sync = definition(raster, 'void RasterizerVulkan::SyncTextureUnits(')
    require('BeginImageBindings(DescriptorHeapType::Texture)' in sync,
            'texture preparation no longer resets pending writes')
    utility = definition(raster, 'void RasterizerVulkan::SyncUtilityTextures(')
    require('BeginImageBindings(DescriptorHeapType::Utility)' in utility,
            'utility preparation no longer resets pending writes')
    switch = definition(cache, 'void PipelineCache::SwitchCache(')
    require('entry.Invalidate();' in switch, 'title switch retains descriptor owner')
    destroy = definition(texture, 'void Handle::Destroy()')
    require(destroy.index('instance.RetireImageDescriptors();') < destroy.index('device.destroyImageView('),
            'view epoch must precede destruction')
    sampler_move = definition(texture, 'Sampler& Sampler::operator=(')
    require(sampler_move.index('RetireImageDescriptors();') < sampler_move.index('sampler = std::move('),
            'sampler replacement missing retirement')
    require('RetireImageDescriptors();' in definition(texture, 'Sampler::~Sampler()'),
            'sampler destruction missing retirement')
    require('RetireImageDescriptors();' in definition(texture, 'void Framebuffer::RetireOwnedViews()'),
            'framebuffer owned-view retirement missing')
    require('RetireOwnedViews();' in texture_header, 'framebuffer move replacement hook missing')
    require('std::numeric_limits<u64>::max()' in definition(instance, '    void RetireImageDescriptors()'),
            'resource epoch wrap protection missing')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', type=Path, default=ROOT)
    parser.add_argument('--output', type=Path, default=ROOT/'build/uberhar-probe/image-descriptors')
    parser.add_argument('--glslang', type=Path, default=ROOT/'build/uberhar-validators/glslang/StandAlone/glslang')
    parser.add_argument('--spirv-val', type=Path, default=ROOT/'build/uberhar-validators/spirv-tools/tools/spirv-val')
    parser.add_argument('--icd', type=Path, required=True)
    args = parser.parse_args()
    repo = args.source_root.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix='run-', dir=args.output.resolve()))
    proof = {'author': 'CodexAstraLocal', 'status': 'INCOMPLETE', 'inputs': {}, 'commands': [],
             'variants': [], 'source_controls': [],
             'scope': 'Real Vulkan-Hpp descriptor identity and sampled-image integer pixels; '
                      'controlled callback/tick/epoch ownership plus source-bound final capture. '
                      'No full emulator/title, target-driver or performance qualification.'}

    def read(path):
        path = path.resolve()
        proof['inputs'][str(path)] = digest(path)
        relative = path.relative_to(repo) if path.is_relative_to(repo) else Path('external')/path.name
        target = output/'sources'/relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(path.read_bytes())
        return path.read_text()

    environment = os.environ.copy()
    environment.update(VK_ICD_FILENAMES=str(args.icd.resolve()), VK_DRIVER_FILES=str(args.icd.resolve()),
                       MESA_SHADER_CACHE_DISABLE='true', LP_NUM_THREADS='2')

    def command(argv, stem, timeout):
        argv = list(map(str, argv))
        result = subprocess.run(argv, cwd=repo, env=environment, capture_output=True, timeout=timeout)
        stem.with_suffix('.stdout').write_bytes(result.stdout)
        stem.with_suffix('.stderr').write_bytes(result.stderr)
        proof['commands'].append({'argv': argv, 'timeout_s': timeout, 'returncode': result.returncode})
        return result

    try:
        for path in (Path(__file__), ROOT/'tools/uberhar/test_cached_buffer_lifetime.py', args.icd):
            read(path)
        fixture = read(repo/'tools/uberhar/test_image_descriptors.cpp')
        read(repo/'tools/uberhar/test_cached_buffer_lifetime_vulkan.cpp')
        policy = read(repo/'src/video_core/renderer_vulkan/uberhar_image_descriptors.h')
        cache = read(repo/'src/video_core/renderer_vulkan/vk_pipeline_cache.cpp')
        header = read(repo/'src/video_core/renderer_vulkan/vk_pipeline_cache.h')
        raster = read(repo/'src/video_core/renderer_vulkan/vk_rasterizer.cpp')
        texture = read(repo/'src/video_core/renderer_vulkan/vk_texture_runtime.cpp')
        texture_header = read(repo/'src/video_core/renderer_vulkan/vk_texture_runtime.h')
        instance = read(repo/'src/video_core/renderer_vulkan/vk_instance.h')
        sources = (cache, header, raster, texture, instance, texture_header)
        source_contract(*sources)
        for label, index, old, new in (
            ('omit-final-resolve', 0, '    ResolveImageBindings();', ''),
            ('omit-set-capture', 0, 'descriptor_sets = bound_descriptor_sets', 'descriptor_sets = old_sets'),
            ('omit-pending-reset', 1, 'pending_images[static_cast<u32>(type) - 1].count = 0;', ''),
            ('omit-view-retirement', 3, '        instance.RetireImageDescriptors();\n    // Image views', '    // Image views')):
            altered = list(sources)
            altered[index] = replace_once(altered[index], old, new)
            try:
                source_contract(*altered)
            except (ValueError, RuntimeError):
                proof['source_controls'].append(label)
            else:
                raise RuntimeError('source control undetected: '+label)
        stub = output/'stub/common/assert.h'
        stub.parent.mkdir(parents=True)
        stub.write_text('#pragma once\n// CodexAstraLocal: Only assertion reporting is adapted; Vulkan types and policy are real.\n'
                        '#define ASSERT(x) Require(bool(x), "production assertion: " #x)\n')
        shaders = {
            'vert': '''#version 450
void main() { const vec2 p[3]=vec2[3](vec2(-1,-1),vec2(3,-1),vec2(-1,3));
gl_Position=vec4(p[gl_VertexIndex],0,1); }
''',
            'frag': '''#version 450
layout(set=0,binding=0) uniform usampler2D faces[6];
layout(set=0,binding=1) uniform usampler2D unit1;
layout(set=0,binding=2) uniform usampler2D unit2;
layout(push_constant) uniform P { uint ordinal; } p;
layout(location=0) out uvec4 color;
void main() {
 vec2 uv=vec2(1.25,.5); uint v=0u;
 switch(p.ordinal%6u) {
 case 0u:v=texture(faces[0],uv).x;break;
 case 1u:v=texture(faces[1],uv).x;break;
 case 2u:v=texture(faces[2],uv).x;break;
 case 3u:v=texture(faces[3],uv).x;break;
 case 4u:v=texture(faces[4],uv).x;break;
 case 5u:v=texture(faces[5],uv).x;break;
 }
 color=uvec4(v,texture(unit1,uv).x,texture(unit2,uv).x,p.ordinal);
}
'''}
        modules = []
        for tool in (args.glslang, args.spirv_val):
            require(tool.is_file(), 'required shader tool absent: '+str(tool))
            proof['inputs'][str(tool.resolve())] = digest(tool)
        for stage, text in shaders.items():
            source = output/('image.'+stage)
            source.write_text(text)
            module = output/('image.'+stage+'.spv')
            modules.append(module)
            compiled = command([args.glslang, '-V', '--target-env', 'vulkan1.1', '-S', stage,
                                '-o', module, source], output/('shader-'+stage), 30)
            require(compiled.returncode == 0, 'sampler shader compilation failed')
            validated = command([args.spirv_val, '--target-env', 'vulkan1.1', module], output/('validate-'+stage), 30)
            require(validated.returncode == 0, 'sampler SPIR-V validation failed')
        variants = [
            ('candidate', policy, [], None),
            ('omit-tick', replace_once(policy, 'tick == final_tick && ', ''), [], 'tick must allocate'),
            ('omit-epoch', replace_once(policy, 'epoch == resource_epoch &&', ''), [], 'epoch must allocate'),
            ('rewrite-queued', policy, ['-DREWRITE_QUEUED_SET'], 'queued image descriptor pixels changed'),
        ]
        for label, source, flags, error in variants:
            case = output/label
            copied = case/'include/video_core/renderer_vulkan/uberhar_image_descriptors.h'
            copied.parent.mkdir(parents=True)
            copied.write_text(source)
            (case/'fixture.cpp').write_text(fixture)
            argv = [os.environ.get('CXX', 'c++'), '-std=c++20', '-O2', *flags,
                    '-I'+str(case/'include'), '-I'+str(output/'stub'), '-I'+str(repo/'src'),
                    '-I'+str(repo/'externals/vulkan-headers/include'),
                    '-MMD', '-MF', case/'dependencies.d', case/'fixture.cpp', '-l:libvulkan.so.1', '-o', case/'fixture']
            compiled = command(argv, case/'compile', 90)
            require(compiled.returncode == 0, 'image fixture compilation: '+compiled.stderr.decode()[-6000:])
            result = command([case/'fixture', *modules], case/'run', 30)
            if error is None:
                require(result.returncode == 0 and b'PASS image descriptors rendered=41 ' in result.stdout,
                        'image candidate failed: '+result.stderr.decode())
            else:
                require(result.returncode == 23 and result.stderr == ('FAIL '+error+'\n').encode(),
                        'image descriptor defect not detected: '+label+' '+result.stderr.decode())
            proof['variants'].append({'label': label, 'returncode': result.returncode})
            print(label+': PASS', flush=True)
        proof['status'] = 'PASS'
    finally:
        proof['inputs_after'] = {path: digest(Path(path)) for path in proof['inputs']}
        proof['unchanged'] = proof['inputs'] == proof['inputs_after']
        proof['artifacts'] = {str(p.relative_to(output)): digest(p) for p in output.rglob('*') if p.is_file()}
        (output/'provenance.json').write_text(json.dumps(proof, indent=2)+'\n')
        print('Proof: '+str(output), flush=True)
    require(proof['unchanged'], 'image descriptor inputs changed during gate')


if __name__ == '__main__':
    main()
