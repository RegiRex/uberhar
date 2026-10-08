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
    rasterizer = read('vk_rasterizer.cpp')
    helper = read('uberhar_compute_census.h')
    observe = block(header, 'void ObserveState(')
    members = header[header.index('    ComputeStateCensus state_census;'):header.index('    double timestamp_period{};')]
    report = block(cpp, 'void ComputeRectRenderer::ReportCensus(') + '\n' + block(cpp, 'void ComputeRectRenderer::Report()')
    admission = block(rasterizer, 'if (compute_rect && !accelerate)')
    tick = block(rasterizer, 'void RasterizerVulkan::TickFrame()')
    # CodexAstraLocal: These integration checks bind the cheap observation to its
    # existing denominator and cadence, rather than only testing an orphan helper.
    assert admission.count('ComputeRectStateRejections(regs)') == 1
    assert admission.count('ObserveState(reasons, vertex_batch.size())') == 1
    assert admission.index('++compute_rect->considered') < admission.index('ObserveState(') < admission.index('if (reasons != 0)')
    assert 'RejectState' not in admission
    draw = block(rasterizer, 'bool RasterizerVulkan::Draw(')
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
    for name in ('uberhar_compute_rect.h', '../pica/regs_internal.h', '../pica/regs_framebuffer.h', '../pica/regs_rasterizer.h', '../pica/regs_texturing.h'):
        path = (root / relative / name).resolve()
        inputs[str(path)] = hashlib.sha256(path.read_bytes()).hexdigest()
    fixture = Path(__file__).with_suffix('.cpp').resolve()
    inputs[str(fixture)] = hashlib.sha256(fixture.read_bytes()).hexdigest()
    inputs[str(Path(__file__).resolve())] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    # CodexAstraLocal: Mutations must fail a specific behavioral assertion, not
    # merely fail compilation. All subprocesses have finite time limits.
    variants = [('normal', helper, admission, None)]
    if args.mutants:
        variants += [
            ('drop-zero-mask', helper.replace('auto& counts = mask < Bins', 'if (mask == 0) return;\n        auto& counts = mask < Bins'), admission, 'joint populations include mask zero'),
            ('no-consume-reset', helper.replace('bins[mask] = {};', '(void)mask;'), admission, 'consume resets interval'),
            ('wrong-six-count', helper, admission.replace('ObserveState(reasons, vertex_batch.size())', 'ObserveState(reasons, 6)'), 'callsite observes actual batch once'),
            ('missing-observation', helper, admission.replace('compute_rect->ObserveState(reasons, vertex_batch.size());', '(void)reasons;'), 'callsite observes actual batch once'),
        ]
    cases = []
    for name, census_source, route, expected in variants:
        target = out / name
        include = target / 'video_core/renderer_vulkan'
        include.mkdir(parents=True)
        (include / 'uberhar_compute_census.h').write_text(census_source)
        for filename, body in [('observe.inc', observe), ('members.inc', members), ('report.inc', report), ('admission.inc', route)]:
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
              'scope': 'Real helper/classifier/geometry and extracted report/admission, recording resource/log endpoints. No Vulkan or device performance proof.'}
    (out / 'provenance.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({'pass': True, 'cases': [(c['name'], c['output']) for c in cases], 'provenance': str(out / 'provenance.json')}, indent=2))


if __name__ == '__main__':
    main()
