#!/usr/bin/env python3
"""CodexAstraLocal: Adapt the existing command-order gate to deferred coherent upload.

Actual ring/lease, scheduler submit/dispatch, render-pass and rasterizer setup/draw
seams execute with recording driver/packet/selection endpoints. All original ordinary cases remain; only non-sampled
deferred draws are added because permanent compute rejection cannot reserve a query.
The actual pass methods also preserve old/new attachment barrier metadata and
natural profiling boundaries, with an executed old-order defect control.
The scheduler's optional Calculated tracker stays null; adapters reject activation.
No Vulkan execution, pixels, executor behavior, diagnostic packet behavior or full
cached-UBO/LUT proof follows.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def need(b,why):
    if not b:raise RuntimeError(why)
def replace(source,old,new):
    need(source.count(old)==1,'Changed adaptation anchor: '+old[:100]);return source.replace(old,new,1)
def section(source,start,end):
    need(source.count(start)==1,'Changed section start: '+start)
    i=source.index(start);return source[i:source.index(end,i)]

def main():
    global ROOT
    # CodexAstraLocal: A source shadow supports finite defects/private review;
    # normal CI extracts actual repository sources and writes a new proof folder.
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo',type=Path,default=ROOT)
    parser.add_argument('--source-root',type=Path,default=ROOT)
    parser.add_argument('--output',type=Path,default=Path('build/uberhar-probe/vulkan-stream-order'))
    args=parser.parse_args()
    ROOT=args.repo.resolve()
    source_root=args.source_root.resolve()
    args.output.mkdir(parents=True,exist_ok=True)
    output=Path(tempfile.mkdtemp(prefix='deferred-order-',dir=args.output.resolve()))
    proof={'author':'CodexAstraLocal','scope':__doc__,'inputs':{},'seams':{},'variants':[]}
    def read(path):
        path=path.resolve();rel=str(path);data=path.read_bytes()
        proof['inputs'][rel]=hashlib.sha256(data).hexdigest()
        target=output/'sources'/('input-'+str(len(proof['inputs']))+'-'+path.name);target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
        return data.decode()
    helper=ROOT/'tools/uberhar/test_ready_cpu_fragments.py';read(helper)
    spec=importlib.util.spec_from_file_location('extractor',helper)
    extractor=importlib.util.module_from_spec(spec);spec.loader.exec_module(extractor)
    def function(source,signature):
        body=extractor.function(source,signature);proof['seams'][signature]=hashlib.sha256(body.encode()).hexdigest();return body
    try:
        read(Path(__file__).resolve())
        fixture=read(HERE/'test_vulkan_stream_order.cpp')
        stream=read(source_root/'src/video_core/renderer_vulkan/vk_stream_buffer.cpp')
        header=read(source_root/'src/video_core/renderer_vulkan/vk_stream_buffer.h')
        schedule=read(ROOT/'src/video_core/renderer_vulkan/vk_scheduler.cpp')
        read(source_root/'src/video_core/renderer_vulkan/vk_scheduler.h')
        # CodexAstraLocal: Bind the interface behind the null observer adapters;
        # Wait/SubmitExecution remain complete, unmodified production extracts.
        read(source_root/'src/video_core/renderer_vulkan/compute_failure_packet.h')
        render=read(ROOT/'src/video_core/renderer_vulkan/vk_render_manager.cpp')
        render_header=read(ROOT/'src/video_core/renderer_vulkan/vk_render_manager.h')
        # CodexAstraLocal: Reuse the real category declarations; timing/query
        # behavior belongs to the separate complete WorkProfile method gate.
        profile_header=read(ROOT/'src/video_core/renderer_vulkan/vk_work_profile.h')
        (output/'profile-types.inc').write_text('\n'.join(
            function(profile_header,sig)+';' for sig in
            ['enum class GpuWork : u32', 'enum class HostWait : u32'])+'\n')
        pipeline=read(ROOT/'src/video_core/renderer_vulkan/vk_pipeline_cache.cpp')
        raster=read(source_root/'src/video_core/renderer_vulkan/vk_rasterizer.cpp')
        # CodexAstraLocal: Keep complete production bodies; adapters only provide
        # Vulkan signatures, scheduler endpoints and controlled backing storage.
        parts=[function(stream,'class StreamBuffer::Allocation final')+';']
        for sig in ['bool StreamBuffer::DeferredUpload::Publish(',
            'vk::Buffer StreamBuffer::DeferredUpload::Handle()',
            'std::tuple<u8*, u32, bool> StreamBuffer::Map(', 'void StreamBuffer::Commit(',
            'bool StreamBuffer::CanDeferUpload()',
            'std::optional<StreamBuffer::DeferredMapping> StreamBuffer::MapDeferredUpload(',
            'std::optional<StreamBuffer::DeferredUpload> StreamBuffer::CommitDeferredUpload(',
            'void StreamBuffer::ReserveWatches(', 'void StreamBuffer::WaitPendingOperations(']:
            parts.append(function(stream,sig))
        for sig in ['void Scheduler::Flush(', 'void Scheduler::Wait(u64 tick)',
            'void Scheduler::DispatchWork(', 'void Scheduler::SubmitExecution(',
            'void Scheduler::AllocateWorkerCommandBuffers()']:parts.append(function(schedule,sig))
        for sig in ['void RenderManager::BeginRendering(const Framebuffer*',
            'void RenderManager::BeginRendering(const RenderPass&',
            'void RenderManager::EndRendering(EndReason']:
            body=function(render,sig)
            if sig.endswith('const Framebuffer*'):
                # CodexAstraLocal: Recreate only the former metadata ordering;
                # the old/new barrier assertion must detect it at execution.
                invoke='    BeginRendering(new_pass, GpuWork::Graphics);\n'
                metadata=('    images = framebuffer->Images();\n'
                          '    aspects = framebuffer->Aspects();\n'
                          '    shadow_rendering = framebuffer->shadow_rendering;')
                old_order=replace(body,invoke,'')
                old_order=replace(old_order,metadata,metadata+'\n'+invoke.rstrip())
                parts.append('#if defined(UBERHAR_OLD_ATTACHMENT_ORDER)\n'+old_order+
                             '\n#else\n'+body+'\n#endif')
            else:
                parts.append(body)
        (output/'production.inc').write_text('\n\n'.join(parts)+'\n')
        # CodexAstraLocal: Keep the exact reason enum while modeling only the recording endpoint.
        (output/'end-reason.inc').write_text(function(render_header, 'enum class EndReason : u32')+';\n')
        (output/'renderpass.inc').write_text(section(render_header,'struct RenderPass {','\n};')+'\n};\n')
        (output/'bind-commands.inc').write_text(section(pipeline,
            '        if (is_dirty || bound_pipeline != selected) {','        // AstraEH: Invalidate even'))
        draw=function(raster,'bool RasterizerVulkan::Draw(')
        prepare=section(draw,'    // CodexAstraLocal: Map may submit the current tick',
                        '    const auto draw_rect = fb_helper.DrawRect();')
        setup=section(draw,'    PipelineCache::CpuFragmentToken cpu_fragment_use{};',
                      '    // AstraPro: Sample synchronized state')
        # CodexAstraLocal: Brace-balanced extraction includes both branches of
        # the actual CPU tail while excluding the surrounding accelerated branch.
        outer=function(draw,'    if (accelerate) {\n        succeeded = AccelerateDrawBatchInternal')
        i=draw.index(outer)+len(outer)
        need(draw[i:].startswith(' else {'),'CPU else boundary changed')
        cpu=extractor.function('void SelectedCpuTail() '+draw[i+len(' else '):],
                               'void SelectedCpuTail()')
        tail=cpu[cpu.index('{')+1:-1]
        end_sample=section(draw,'    // AstraEH: This sample covers the native draw',
                           '    // AstraPro: No draw occurred')
        for name,body in [('setup.inc',setup),('end-sample.inc',end_sample),
                          ('candidate-prepare.inc',prepare),('candidate-tail.inc',tail)]:
            (output/name).write_text(body+'\n')
        need(prepare.count('MapDeferredUpload(')==1 and tail.count('CommitDeferredUpload(')==1,
             'Deferred Map/seal population changed')
        need('MapDeferredUpload(' not in setup+tail and 'CommitDeferredUpload(' not in prepare,
             'Deferred reservation/seal moved out of required scopes')
        need(tail.index('pipeline_cache.BindPipeline(')<tail.index('CommitDeferredUpload(')<
             tail.index('scheduler.Record(')<tail.index('pipeline_cache.CompleteReadyCpuDraw('),
             'Final bind/seal/record/token order changed')
        map_block=prepare[prepare.index('    if (deferred) {'):]
        late_prepare=replace(prepare,map_block,'')
        late_tail=replace(tail,'        // CodexAstraLocal: A pass change',
                          map_block+'\n        // CodexAstraLocal: A pass change')
        early_prepare=prepare+'''\n    // CodexAstraLocal: Intentional defect stamps before final pass/bind.
    std::optional<StreamBuffer::DeferredUpload> early_upload;
    if(deferred) early_upload=stream_buffer.CommitDeferredUpload(*deferred_mapping);
    else { std::memcpy(vertex_data,vertex_batch.data(),vertex_size);stream_buffer.Commit(vertex_size); }
'''
        early_tail=replace(tail,'auto upload = stream_buffer.CommitDeferredUpload(*deferred_mapping);',
                           'auto upload = std::move(early_upload);')
        early_tail=replace(early_tail,
            '            std::memcpy(vertex_data, vertex_batch.data(), vertex_size);\n            stream_buffer.Commit(vertex_size);\n','')
        for name,body in [('late-prepare.inc',late_prepare),('late-tail.inc',late_tail),
                          ('early-prepare.inc',early_prepare),('early-tail.inc',early_tail)]:
            (output/name).write_text(body+'\n')
        nested='\n'.join(function(header,s)+';' for s in
            ['    class DeferredMapping final {','    class DeferredUpload final {'])
        (output/'stream-deferred-types.inc').write_text(nested+'\n')
        (output/'fixture.cpp').write_text(fixture)
        argv=['c++','-std=c++20','-O2','-g0','-DNDEBUG','-pthread',str(output/'fixture.cpp'),'-o',str(output/'test')]
        built=subprocess.run(argv,cwd=ROOT,capture_output=True,timeout=90)
        (output/'compile.stdout').write_bytes(built.stdout);(output/'compile.stderr').write_bytes(built.stderr)
        proof['compile_argv']=argv;proof['compile_exit']=built.returncode
        need(built.returncode==0,'Compile failed: '+str(output))
        for name,number,route,error in [('candidate',0,0,None),
                ('late_map_ordinary',1,1,b'FAIL final owner state ready\n'),
                ('early_seal_ordinary',2,1,b'FAIL geometry watch stamped at final draw tick\n'),
                ('late_map_deferred',1,2,b'FAIL final owner state ready\n'),
                ('early_seal_deferred',2,2,b'FAIL geometry watch stamped at final draw tick\n')]:
            result=subprocess.run([str(output/'test'),str(number),str(route)],cwd=ROOT,capture_output=True,timeout=20)
            (output/(name+'.stdout')).write_bytes(result.stdout);(output/(name+'.stderr')).write_bytes(result.stderr)
            proof['variants'].append({'name':name,'returncode':result.returncode})
            if error:need(result.returncode==1 and result.stderr==error,'Unexpected defect result: '+name)
            # CodexAstraLocal: Each actual CPU draw also checks the new final-tick
            # cached-buffer hook; all original geometry/order cases are retained.
            else:need(result.returncode==0 and b'PASS cases=36 checks=1252\n' in result.stdout
                      and result.stdout.endswith(b'PASS metadata_cases=2\n'),
                      'Candidate population/result')
            print(name+': PASS',flush=True)
        # CodexAstraLocal: Preserve all four existing order controls, and add
        # one isolated attachment regression without rerunning other populations.
        old_argv=argv[:-1]+[str(output/'old-attachment-order'),'-DUBERHAR_OLD_ATTACHMENT_ORDER']
        old_built=subprocess.run(old_argv,cwd=ROOT,capture_output=True,timeout=90)
        (output/'old-attachment-order.compile.stdout').write_bytes(old_built.stdout)
        (output/'old-attachment-order.compile.stderr').write_bytes(old_built.stderr)
        proof['old_attachment_compile']={'argv':old_argv,'returncode':old_built.returncode}
        need(old_built.returncode==0,'Old attachment order control did not compile')
        old_run=subprocess.run([str(output/'old-attachment-order'),'0','3'],cwd=ROOT,
                               capture_output=True,timeout=20)
        (output/'old-attachment-order.stdout').write_bytes(old_run.stdout)
        (output/'old-attachment-order.stderr').write_bytes(old_run.stderr)
        proof['variants'].append({'name':'old_attachment_order','returncode':old_run.returncode})
        need(old_run.returncode==1 and old_run.stderr==
             b'FAIL previous pass barrier retains its image\n','Old attachment order was not detected')
        print('old_attachment_order: PASS',flush=True)
        proof['passed']=True
    finally:
        proof['inputs_after']={rel:sha(ROOT/rel) for rel in proof['inputs']}
        proof['unchanged']=proof['inputs']==proof['inputs_after']
        proof['artifacts']={str(p.relative_to(output)):sha(p) for p in output.rglob('*') if p.is_file()}
        (output/'provenance.json').write_text(json.dumps(proof,indent=2)+'\n')
        print('Proof: '+str(output),flush=True)
    need(proof['unchanged'],'Source changed during finite control')

if __name__=='__main__':main()
