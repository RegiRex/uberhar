#!/usr/bin/env python3
"""CodexAstraLocal: Test optional fragment completion using real host workers, without GPU calls."""
# CodexAstraLocal: Preserve each finite host-only run and its exact source inputs;
# compiler/driver endpoints are modeled, actual queue and completion are not.
from pathlib import Path
import argparse
import datetime
import hashlib
import json
import os
import re
import resource
import signal
import subprocess

D = Path(__file__).resolve().parent
ROOT = D.parents[1]
DEFAULT_SOURCE = ROOT / 'src/video_core/renderer_vulkan/vk_shader_disk_cache.cpp'


# CodexAstraLocal: Extract balanced C++ bodies without interpreting braces in
# ordinary strings/comments; the selected bodies contain no raw string literals.
def extract(text, marker):
    start = text.index(marker)
    opening = text.index('{', start)
    depth, state, i = 0, 'code', opening
    while i < len(text):
        char, pair = text[i], text[i:i + 2]
        if state == 'line':
            if char == '\n': state = 'code'
        elif state == 'block':
            if pair == '*/': state = 'code'; i += 1
        elif state in ('"', "'"):
            if char == '\\': i += 1
            elif char == state: state = 'code'
        elif pair == '//': state = 'line'; i += 1
        elif pair == '/*': state = 'block'; i += 1
        elif char in ('"', "'"): state = char
        elif char == '{': depth += 1
        elif char == '}':
            depth -= 1
            if depth == 0: return text[start:i + 1]
        i += 1
    raise ValueError(f'unclosed extraction {marker}')


# CodexAstraLocal: Bind the retained proof to exact source and executable bytes.
def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


# CodexAstraLocal: A repeated gate retains prior fixtures and cannot overwrite evidence.
def write(path, text):
    with path.open('x') as stream:
        stream.write(text)


# CodexAstraLocal: Defective worker mutants may terminate their own child. Disable
# core dumps only there and enforce a process timeout; no device/settings action.
def child_limits():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


# CodexAstraLocal: Explicit source/output arguments support a reviewed candidate
# without overwriting published fixtures or using private files in CI.
def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=DEFAULT_SOURCE)
    parser.add_argument('--output', type=Path, default=ROOT / 'build/uberhar-probe/optional-fragment-worker')
    parser.add_argument('--cxx', default=os.environ.get('CXX', 'c++'))
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    index = 1
    while (args.output / f'run-{index:02d}').exists(): index += 1
    out = (args.output / f'run-{index:02d}').resolve()
    out.mkdir()
    cpp = args.source.resolve()
    header = cpp.with_suffix('.h')
    shader = ROOT / 'src/video_core/renderer_vulkan/vk_graphics_pipeline.h'
    function = extract(cpp.read_text(),
        'std::optional<std::pair<u64, Shader* const>> ShaderDiskCache::UseReadyFragmentShader(')
    shader_decl = extract(shader.read_text(), 'struct Shader : public Common::AsyncHandle') + ';\n'
    text = header.read_text()
    members = text[text.index('    struct ReadyFragmentKey {'):
                   text.index('    std::unordered_map<size_t, Shader> fixed_geometry_shaders;')]
    write(out / 'shader.inc', shader_decl)
    write(out / 'members.inc', members)
    write(out / 'candidate-source.cpp', cpp.read_text())
    write(out / 'harness.cpp', (D / 'test_optional_fragment_worker.cpp').read_text())

    # CodexAstraLocal: Each negative control changes only one real boundary and
    # must fail for its intended reason, not a compiler error or timeout.
    standard = extract(function, 'catch (const std::exception& error)')
    following = function[function.index(standard) + len(standard):]
    if not re.match(r'\s*catch\s*\(\.\.\.\)\s*\{', following):
        raise ValueError('Expected opaque failure handler immediately after standard handler')
    unknown = extract(following, 'catch (...)')
    logger_pattern = re.compile(r'try\s*\{\s*(LOG_ERROR\(Render_Vulkan, "Uberhar optional GPU fragment failed: \{\}", error.what\(\)\);)\s*\}\s*catch\s*\(\.\.\.\)\s*\{\s*\}')
    unsafe_log, replacements = logger_pattern.subn(r'\1', function)
    if replacements != 1:
        raise ValueError('Expected exactly one guarded standard-failure logger')
    queue_token = '} catch (...) {\n        entry->shader.MarkFailed();\n        ready_fragment_failures.fetch_add(1);'
    if function.count(queue_token) != 1:
        raise ValueError('Expected exactly one terminal queue-failure boundary')
    lookup = extract(function, 'if (!allow_build)')
    profile_token = ' || !(entry.key.profile == parent.profile)'
    if function.count(profile_token) != 1:
        raise ValueError('Expected exactly one complete-profile equality guard')
    variants = {
        'normal': (function, 'all', 'pass'),
        'opaque-escape': (function.replace(unknown, ''), 'opaque', 'abort'),
        'logger-escape': (unsafe_log, 'logger', 'abort'),
        'queue-pending': (function.replace(queue_token, '} catch (...) {\n        ready_fragment_failures.fetch_add(1);'), 'queue', 'queue failure terminal'),
        'lookup-warms': (function.replace(lookup, ''), 'lookup', 'lookup-only'),
        'profile-alias': (function.replace(profile_token, ''), 'profile', 'profile mismatch'),
    }
    inputs = [D / 'test_optional_fragment_worker.cpp', Path(__file__), cpp, header, shader,
              ROOT / 'src/common/thread_worker.h', ROOT / 'src/common/async_handle.h',
              ROOT / 'src/common/unique_function.h',
              ROOT / 'src/video_core/renderer_vulkan/uberhar_fragment_policy.h',
              ROOT / 'src/video_core/renderer_vulkan/uberhar_shader_compile_policy.h',
              ROOT / 'src/video_core/shader/generator/pica_fs_config.cpp',
              ROOT / 'src/video_core/shader/generator/pica_fs_config.h',
              ROOT / 'src/video_core/shader/generator/profile.h']
    provenance = {'author': 'CodexAstraLocal', 'utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'scope': 'Exact candidate optional-FS function and owned declarations; real ThreadWorker, AsyncHandle, UniqueFunction, FSConfig/Profile and DemandGate. Generator/compiler/driver/log endpoint controls, no GPU execution or device. Allocation countdown affects owner thread only.',
        'inputs': {str(p): sha(p) for p in inputs}, 'cases': [],
        'compiler': subprocess.run([args.cxx, '--version'], capture_output=True, text=True,
                                   check=True, timeout=10).stdout}
    includes = [out, ROOT / 'src', ROOT / 'externals/fmt/include', ROOT / 'externals/boost',
                ROOT / 'externals/xxHash', ROOT / 'externals/nihstro/include',
                ROOT / 'externals/vulkan-headers/include']
    for name, (body, selected, expected) in variants.items():
        write(out / f'{name}.inc', body + '\n')
        binary = out / name
        command = [args.cxx, '-std=c++20', '-O2', '-pthread', '-DFMT_HEADER_ONLY', '-DXXH_INLINE_ALL',
                   f'-DEXTRACTED_FUNCTION="{name}.inc"', *[f'-I{p}' for p in includes],
                   str(out / 'harness.cpp'),
                   str(ROOT / 'src/video_core/shader/generator/pica_fs_config.cpp'), '-o', str(binary)]
        compile_result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=90)
        write(out / f'{name}-compile.log', compile_result.stdout + compile_result.stderr)
        case = {'name': name, 'compile_argv': command, 'compile_returncode': compile_result.returncode,
                'expected': expected, 'selected': selected, 'function_sha256': sha(out / f'{name}.inc')}
        provenance['cases'].append(case)
        if compile_result.returncode:
            write(out / 'provenance.json', json.dumps(provenance, indent=2) + '\n')
            print(compile_result.stderr[-14000:]); return compile_result.returncode
        result = subprocess.run([str(binary), selected], cwd=ROOT, capture_output=True, text=True,
                                timeout=20, preexec_fn=child_limits)
        write(out / f'{name}-run.log', result.stdout + result.stderr)
        case.update(binary_sha256=sha(binary), run_returncode=result.returncode,
                    stdout=result.stdout, stderr=result.stderr)
        if expected == 'pass':
            passed = result.returncode == 0 and re.fullmatch(
                r'PASS checks=498 actual_thread_worker=true gpu_execution=false\n', result.stdout) is not None
        elif expected == 'abort':
            passed = result.returncode == -signal.SIGABRT
        else:
            passed = result.returncode == 1 and expected in result.stderr
        case['expected_outcome_observed'] = passed
        print(name, result.returncode, result.stdout.strip() or result.stderr.strip(), flush=True)
        if not passed:
            write(out / 'provenance.json', json.dumps(provenance, indent=2) + '\n')
            return 1
    provenance['passed'] = all(case['expected_outcome_observed'] for case in provenance['cases'])
    write(out / 'provenance.json', json.dumps(provenance, indent=2) + '\n')
    print('PASS all six real-worker controls:', out)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
