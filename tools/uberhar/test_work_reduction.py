#!/usr/bin/env python3
"""CodexAstraLocal: Execute production pass boundaries and pure early-eye policy.

Extract unchanged RenderPass equality, both BeginRendering overloads,
EndRendering, and the actual driver flush policy. Include the actual early-eye
header. Recording endpoints replace Vulkan command submission only; GPU image
correctness and future guest reads require separate device validation.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import time

from test_ready_cpu_fragments import function, replace_once


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--mutants', action='store_true')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    source = root / 'src/video_core/renderer_vulkan/vk_render_manager.cpp'
    header = source.with_suffix('.h')
    eye = root / 'src/video_core/renderer_vulkan/uberhar_eye_policy.h'
    union = root / 'src/video_core/renderer_vulkan/uberhar_pass_union.h'
    fixture = Path(__file__).with_suffix('.cpp')
    text, declarations = source.read_text(), header.read_text()
    enum = re.search(r'enum class PassEndReason[^;]+;', declarations)
    budget = re.search(r'constexpr u32 MinDrawsToFlush = \d+;', text)
    if not enum or not budget:
        raise RuntimeError('Production enum/flush budget extraction changed')
    types = enum.group() + '\n' + function(declarations, 'struct RenderPass {') + ';\n'
    methods = budget.group() + '\n' + '\n'.join(function(text, signature) for signature in [
        'bool RenderManager::ShouldFlushPass() const',
        'void RenderManager::BeginRendering(const Framebuffer* framebuffer,',
        'void RenderManager::BeginRendering(const RenderPass& new_pass,',
        'void RenderManager::EndRendering(PassEndReason reason)',
    ])
    inputs = [source, header, eye, union, fixture, Path(__file__).resolve(),
              root / 'src/common/math_util.h', root / 'tools/uberhar/test_ready_cpu_fragments.py']
    hashes = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
    proof = {'author': 'CodexAstraLocal', 'inputs': hashes, 'runs': [], 'status': 'running'}

    def save():
        (out / 'provenance.json').write_text(json.dumps(proof, indent=2) + '\n')

    # CodexAstraLocal: Mutation controls establish that the fixture notices loss
    # of the claimed original-area, hazard, submission, and eye-side-effect guards.
    variants = [('production', methods, eye.read_text(), union.read_text(), False)]
    if args.mutants:
        variants += [
            ('replace-active-area', replace_once(methods, '++contained_pass_reuses;',
                                                '++contained_pass_reuses; pass = new_pass;'), eye.read_text(), union.read_text(), True),
            ('drop-hazard-revocation', replace_once(methods, 'containment_eligible &= allow_containment;',
                                                   '(void)allow_containment;'), eye.read_text(), union.read_text(), True),
            ('ignore-submission-budget', replace_once(methods, 'reuse_contained_pass && !submission_due &&',
                                                       'reuse_contained_pass &&'), eye.read_text(), union.read_text(), True),
            ('ignore-eye-depth-write', methods, replace_once(eye.read_text(),
                'in.writes_depth_stencil || in.geometry', 'in.geometry'), union.read_text(), True),
            ('ignore-union-area-budget', methods, eye.read_text(), replace_once(union.read_text(),
                'if (after - before > before)', 'if (false)'), True),
            ('ignore-union-framebuffer-bounds', methods, eye.read_text(), replace_once(union.read_text(),
                'if (!inside(old) || !inside(next))', 'if (false && (!inside(old) || !inside(next)))'), True),
            ('carry-union-through-boundary', replace_once(methods, 'pass.render_pass = VK_NULL_HANDLE;',
                'if (reason == PassEndReason::PassChange) pass.render_pass = VK_NULL_HANDLE;'),
                eye.read_text(), union.read_text(), True),
        ]
    try:
        for name, body, eye_header, union_header, expect_failure in variants:
            directory = out / name
            directory.mkdir()
            (directory / 'actual_pass_types.inc').write_text(types)
            (directory / 'actual_pass_methods.inc').write_text(body)
            shadow = directory / 'video_core/renderer_vulkan/uberhar_eye_policy.h'
            shadow.parent.mkdir(parents=True)
            shadow.write_text(eye_header)
            (shadow.parent / union.name).write_text(union_header)
            binary = directory / 'fixture'
            command = ['c++', '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror',
                       '-I' + str(directory), '-I' + str(root / 'src'), str(fixture), '-o', str(binary)]
            started = time.monotonic()
            compiled = subprocess.run(command, cwd=root, capture_output=True, timeout=90)
            (directory / 'compile.log').write_bytes(compiled.stdout + compiled.stderr)
            compiled.check_returncode()
            result = subprocess.run([str(binary)], cwd=root, capture_output=True, timeout=15)
            (directory / 'run.log').write_bytes(result.stdout + result.stderr)
            passed = (result.returncode != 0) if expect_failure else (result.returncode == 0)
            proof['runs'].append({'name': name, 'expected_rejection': expect_failure,
                                  'exit': result.returncode, 'pass': passed,
                                  'seconds': round(time.monotonic() - started, 3),
                                  'output': (result.stdout + result.stderr).decode()})
            save()
            if not passed:
                raise RuntimeError(f'{name} failed its acceptance condition')
        if any(hashlib.sha256(p.read_bytes()).hexdigest() != hashes[str(p)] for p in inputs):
            raise RuntimeError('Production inputs changed during validation; qualify the final tree')
        proof['status'] = 'passed'
        print('PASS production work reduction and mutation guards:', out)
    except Exception as error:
        proof['status'] = 'failed'
        proof['error'] = str(error)
        raise
    finally:
        save()
        (out / 'DONE.json').write_text(json.dumps({'status': proof['status']}) + '\n')


if __name__ == '__main__':
    main()
