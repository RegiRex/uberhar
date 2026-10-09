#!/usr/bin/env python3
"""CodexAstraLocal: Check census conservation and actual report/admission hooks.

Registers, geometry, histogram and extracted methods are real production code;
resource/log endpoints are recording adapters, not a Vulkan/device simulation.
Consumers must partition exact app/process/renderer lifecycles before window IDs.
Ranked rows can be dropped by optional logging; unranked counts are censorship,
not delivery loss. Only a valid summary supports the full-bank upper bounds.
"""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import subprocess
import tempfile


def block(text, signature):
    # CodexAstraLocal: Fail on ambiguous anchors and keep the complete production
    # brace-delimited block, ignoring ordinary strings and comments so format
    # placeholders cannot change its boundary.
    assert text.count(signature) == 1, signature
    start = text.index(signature)
    lexical = re.sub(r"""//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'""",
                     lambda match: ' ' * len(match.group()), text, flags=re.S)
    opened = lexical.index('{', start)
    depth = 0
    for index in range(opened, len(text)):
        depth += (lexical[index] == '{') - (lexical[index] == '}')
        if depth == 0:
            return text[start:index + 1]
    raise AssertionError('unterminated block')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-dir', type=Path, default=Path('.'))
    parser.add_argument('--output', type=Path, default=Path('build/uberhar-probe/compute-census'))
    parser.add_argument('--mutants', action='store_true')
    args = parser.parse_args()
    root = Path.cwd().resolve()
    source = args.source_dir.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix='run-', dir=args.output.resolve()))
    relative = Path('src/video_core/renderer_vulkan')
    inputs = {}

    def read(name):
        path = source / relative / name
        inputs[str(path)] = hashlib.sha256(path.read_bytes()).hexdigest()
        return path.read_text()

    header = read('vk_compute_rect.h')
    cpp = read('vk_compute_rect.cpp')
    rasterizer_header = read('vk_rasterizer.h')
    rasterizer = read('vk_rasterizer.cpp')
    helper = read('uberhar_compute_census.h')
    rectangle = read('uberhar_compute_rect.h')
    observe = block(header, 'void ObserveState(')
    members = header[header.index('    ComputeStateCensus state_census;'):header.index('    double timestamp_period{};')]
    report = block(cpp, 'bool ComputeRectRenderer::AllowsExpandedRectangles()') + '\n' + block(cpp, 'void ComputeRectRenderer::ReportCensus(') + '\n' + block(cpp, 'void ComputeRectRenderer::Report()')
    admission = block(rasterizer, 'if (compute_rect && !accelerate)')
    tick = block(rasterizer, 'void RasterizerVulkan::TickFrame()')
    # CodexAstraLocal: These integration checks bind the cheap observation to its
    # existing denominator and cadence, rather than only testing an orphan helper.
    # CodexAstraLocal: Use the exact prepared-state callsite, preserving one raw
    # observation while effective state rejection has its separate route counter.
    # CodexAstraLocal: Deferred packets have no owner-side vertex vector yet.
    # Bind their actual count selection and the real permanent-rejection
    # preflight; an empty vector must never turn a deferred six into other.
    match = re.search(r'ObserveState\(compute_state.raw_rejections,\s*'
                      r'deferred \? deferred->VertexCount\(\) : vertex_batch.size\(\)\)', admission)
    assert match is not None, 'actual ordinary/deferred census count'
    observation = match[0]
    preflight = block(rasterizer, 'RasterizerVulkan::DeferredHardwareWriter RasterizerVulkan::PrepareDeferredVertices(')
    preflight = preflight.replace('RasterizerVulkan::DeferredHardwareWriter RasterizerVulkan::', 'DeferredHardwareWriter ')
    assert admission.count('PrepareComputeRectState(') == 1
    assert admission.count('MakeComputeRectPrepared(') == 1
    assert admission.count('AllowsExpandedRectangles()') == 1
    assert admission.count(observation) == 1
    assert admission.index('++compute_rect->considered') < admission.index('ObserveState(') < admission.index('if (!compute_state)')
    assert admission.count('ComputeRectState{ComputeRectStateRejections(regs), 0, true}') == 1
    assert admission.count('ComputeRectStateRejections(regs)') == 1
    assert 'RejectState' not in admission
    draw = block(rasterizer, 'bool RasterizerVulkan::Draw(')
    # CodexAstraLocal: Execute the actual complete pre-graphics Draw prefix,
    # including no-target exits and the owner-independent strict terminal gate.
    # A recording fallthrough sentinel replaces only the unchanged graphics tail;
    # ordering checks forbid moving guest graphics work before that boundary.
    boundary = draw.index('    // Update scissor uniforms')
    prefix = draw[draw.index('{') + 1:boundary]
    assert admission in prefix
    for call in ('SyncTextureUnits(', 'SyncUtilityTextures(', 'UseFragmentShader(',
                 'UploadUniforms(', 'BindPipeline(', 'stream_buffer.Map(', 'cmdbuf.draw('):
        assert call not in prefix and call in draw[boundary:], call
    strict_stats = block(rasterizer_header, 'struct StrictComputeStats') + ';'
    strict_report = block(rasterizer, 'void RasterizerVulkan::ReportStrictCompute() const')
    strict_report = strict_report.replace('RasterizerVulkan::', 'Fixture::')
    triangles = block(rasterizer, 'void RasterizerVulkan::DrawTriangles()')
    empty = block(triangles, 'if (vertex_batch.empty())')
    accelerated = block(rasterizer, 'bool RasterizerVulkan::AccelerateDrawBatch(bool is_indexed)')
    assert accelerated.index('if (strict_compute)') < accelerated.index('AnalyzeVertexArray(')
    assert 'Settings::RequiresComputeOnly(Settings::values.uberhar_test_mode.GetValue())' in rasterizer
    assert block(rasterizer, 'RasterizerVulkan::~RasterizerVulkan()').count('ReportStrictCompute();') == 1
    assert draw.index('if (!framebuffer->Handle())') < draw.index('if (compute_rect && !accelerate)')
    assert tick.count('ReportCensus(now)') == 1
    cadence = block(tick, 'if (now >= next_memory_snapshot)')
    assert 'ReportCensus(now)' in cadence and 'std::chrono::seconds{30}' in cadence
    assert 'if (compute_rect)' in cadence
    assert 'steady_clock::now()' in members and 'state_census.Record(reasons, vertices)' in observe
    record = block(helper, 'void Record(')
    for forbidden in ('chrono', 'new ', 'MakeComputeRect', 'Hash(', 'vector', 'string'):
        assert forbidden not in record + observe, forbidden

    # CodexAstraLocal: Retain the real classifier/register inputs as well as
    # extracted changed source; no private generated helper is a hidden dependency.
    for name in ('../pica/regs_internal.h', '../pica/regs_framebuffer.h', '../pica/regs_rasterizer.h', '../pica/regs_texturing.h'):
        path = (root / relative / name).resolve()
        inputs[str(path)] = hashlib.sha256(path.read_bytes()).hexdigest()
    # CodexAstraLocal: The production enum and capability predicates exercise the
    # real mode gate without importing the unrelated global settings singleton.
    settings = root / 'src/common/settings.h'
    profile = root / 'src/common/uberhar_test_profile.h'
    for path in (settings, profile):
        inputs[str(path)] = hashlib.sha256(path.read_bytes()).hexdigest()
    policy = 'namespace Settings {\n' + block(settings.read_text(), 'enum class UberharTestMode') + ';\n'
    policy += block(profile.read_text(), 'constexpr bool UsesAutomaticCompute(') + '\n'
    policy += block(profile.read_text(), 'constexpr bool AllowsComputeRendering(') + '\n'
    policy += block(profile.read_text(), 'constexpr bool RequiresComputeOnly(') + '\n}\n'
    fixture = Path(__file__).with_suffix('.cpp').resolve()
    inputs[str(fixture)] = hashlib.sha256(fixture.read_bytes()).hexdigest()
    inputs[str(Path(__file__).resolve())] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    # CodexAstraLocal: Mutations must fail a specific behavioral assertion, not
    # merely fail compilation. All subprocesses have finite time limits.
    variants = [('normal', helper, prefix, observe, report, None)]
    preflight_defects = {}
    if args.mutants:
        variants += [
            ('drop-zero-mask', helper.replace('auto& counts = mask < Bins', 'if (mask == 0) return;\n        auto& counts = mask < Bins'), prefix, observe, report, 'joint populations include mask zero'),
            ('no-consume-reset', helper.replace('bins[mask] = {};', '(void)mask;'), prefix, observe, report, 'consume resets interval'),
            ('wrong-six-count', helper, prefix.replace(observation, 'ObserveState(compute_state.raw_rejections, 6)'), observe, report, 'callsite observes actual batch once'),
            ('missing-observation', helper, prefix.replace('compute_rect->' + observation + ';', '(void)compute_state;'), observe, report, 'callsite observes actual batch once'),
            # CodexAstraLocal: These defects would resurrect schema1 conflation
            # or lose the new cumulative raw counter without changing rendering.
            ('raw-as-effective', helper, prefix, observe, report.replace('effective_admitted, effective_rejected, considered, unsupported,', 'snapshot.admitted_draws, snapshot.rejected, considered, unsupported,'), 'raw and effective admission differ'),
            ('missing-raw-total', helper, prefix, observe.replace('ComputeStateCensus::Add(raw_unsupported, 1, census_overflow);', '(void)raw_unsupported;'), report, 'first interval retains preceding draws'),
            ('raw-route-rejection', helper, prefix.replace('if (!compute_state)', 'if (compute_state.raw_rejections != 0)'), observe, report, 'expanded state selects complete draw'),
            # CodexAstraLocal: Fail actual strict-route behavior, not a mirrored
            # policy model. Graphics reachability, retries and ownership each
            # have a separate negative control, including absent compute owner.
            ('strict-graphics-fallthrough', helper, prefix.replace('if (strict_compute) {\n        if (!compute_rect)', 'if (false) {\n        if (!compute_rect)'), observe, report, 'strict rejected draw is consumed before graphics'),
            ('strict-false-retry', helper, prefix.replace('vertex_batch.clear();\n        return true;', 'vertex_batch.clear();\n        return false;'), observe, report, 'strict rejected draw is consumed before graphics'),
            ('strict-false-invalidation', helper, prefix.replace('fb_helper.CancelInvalidation();', '(void)fb_helper;'), observe, report, 'strict omitted draw has no pixel ownership'),
            ('strict-stale-batch', helper, prefix.replace('vertex_batch.clear();\n        return true;', '(void)vertex_batch;\n        return true;'), observe, report, 'strict omitted draw consumes vertex batch'),
            # CodexAstraLocal: Keep the new packet denominator/raw mask and its
            # prerequisite separate; each defect must reach a behavioral check.
            ('deferred-empty-count', helper, prefix.replace(observation, 'ObserveState(compute_state.raw_rejections, vertex_batch.size())'), observe, report, 'deferred census counts packet vertices exactly once'),
            ('deferred-zero-mask', helper, prefix.replace('ComputeRectState{ComputeRectStateRejections(regs), 0, true}', 'ComputeRectState{0, 0, true}'), observe, report, 'deferred census preserves exact raw mask'),
            ('deferred-unsafe-preflight', helper, prefix, observe, report, 'deferred preflight requires permanent rejection'),
        ]
        preflight_defects['deferred-unsafe-preflight'] = preflight.replace('if (!(raw & ~expandable))', 'if (false)')
        assert preflight_defects['deferred-unsafe-preflight'] != preflight
    cases = []
    for name, census_source, route, observation_source, report_source, expected in variants:
        target = out / name
        include = target / 'video_core/renderer_vulkan'
        include.mkdir(parents=True)
        (include / 'uberhar_compute_census.h').write_text(census_source)
        # CodexAstraLocal: A shadow-source run must compile its prepared-state
        # helper, not silently fall back to the checkout's older classifier API.
        (include / 'uberhar_compute_rect.h').write_text(rectangle)
        for filename, body in [('observe.inc', observation_source), ('members.inc', members), ('report.inc', report_source), ('admission.inc', route), ('deferred_preflight.inc', preflight_defects.get(name, preflight)), ('policy.inc', policy), ('strict_stats.inc', strict_stats), ('strict_report.inc', strict_report), ('empty.inc', empty)]:
            (target / filename).write_text(body + '\n')
        command = [os.environ.get('CXX', 'c++'), '-std=c++20', '-O2', '-DFMT_HEADER_ONLY',
                   '-I' + str(target), '-I' + str(root / 'src'),
                   '-I' + str(root / 'externals/fmt/include'), '-I' + str(root / 'externals/boost'),
                   str(fixture), '-o', str(target / 'test')]
        built = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
        (target / 'compile.log').write_text(built.stdout)
        if built.returncode:
            raise RuntimeError(f'compile failed: {target / "compile.log"}\n{built.stdout[-3000:]}')
        ran = subprocess.run([str(target / 'test')], text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
        (target / 'execute.log').write_text(ran.stdout)
        passed = ran.returncode == 0 if expected is None else ran.returncode == 1 and ('FAILED: ' + expected) in ran.stdout
        cases.append({'name': name, 'command': command, 'returncode': ran.returncode, 'output': ran.stdout.strip(), 'expected_assertion': expected, 'pass': passed,
                      'artifacts': {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in target.iterdir() if p.is_file()}})
        if not passed:
            raise RuntimeError(f'case {name} failed: {ran.stdout}')
    result = {'author': 'CodexAstraLocal', 'inputs': inputs, 'cases': cases,
              'scope': 'Real helper/classifier/geometry, complete Draw prefix before graphics, deferred preflight and extracted reports/empty branch; recording resource/log/packet-metadata endpoints. No CPU executor, Vulkan or device performance proof.'}
    (out / 'provenance.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({'pass': True, 'cases': [(c['name'], c['output']) for c in cases], 'provenance': str(out / 'provenance.json')}, indent=2))


if __name__ == '__main__':
    main()
