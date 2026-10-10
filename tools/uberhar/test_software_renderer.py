#!/usr/bin/env python3
"""CodexAstraLocal: Compile actual CPU frame/sampling helpers and shared Catch controls.

Native CTest additionally exercises actual textured triangles, total worker
budgets and live-memory alias refusal. This small CI hook has no GPU dependency.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import time
import xml.etree.ElementTree as ET


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--output-root', type=Path)
    args = parser.parse_args()
    root = args.root.resolve()
    parent = args.output_root or root / 'build/uberhar-probe/software-renderer'
    parent.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix='run-', dir=parent)).resolve()
    sources = [Path(__file__).with_suffix('.cpp').resolve(),
               root / 'src/video_core/renderer_software/sw_frame.cpp',
               root / 'src/video_core/renderer_software/sw_sampler.cpp',
               root / 'src/video_core/renderer_software/sw_texturing.cpp',
               root / 'src/video_core/texture/texture_decode.cpp',
               root / 'src/video_core/texture/etc1.cpp',
               root / 'externals/catch2/extras/catch_amalgamated.cpp']
    includes = ['src', 'externals/fmt/include', 'externals/boost',
                'externals/catch2/extras', 'externals/nihstro/include']
    compiler = shlex.split(os.environ.get('CXX', 'c++'))
    # CodexAstraLocal: Compile real settings with the renderer under test enabled.
    flags = ['-std=c++20', '-O2', '-pthread', '-DFMT_HEADER_ONLY', '-DENABLE_SOFTWARE_RENDERER']
    flags += ['-I' + str(root / name) for name in includes]
    flags += ['-I' + str(out / 'settings')]
    proof = {'passed': False, 'commands': [], 'started_unix': time.time(),
             'scope': 'actual capture and sampling helpers; native CTest owns triangle/worker cases'}
    inputs = {Path(__file__).resolve(), root / 'src/tests/video_core/software_renderer.cpp',
              root / 'CMakeModules/GenerateSettingKeys.cmake', root / 'src/common/setting_keys.h.in'}

    def run(label, command):
        with (out / (label + '.stdout')).open('w') as stdout, \
             (out / (label + '.stderr')).open('w') as stderr:
            result = subprocess.run(command, cwd=root, stdout=stdout, stderr=stderr,
                                    timeout=180)
        proof['commands'].append({'label': label, 'argv': command, 'exit_code': result.returncode})
        if result.returncode:
            print((out / (label + '.stderr')).read_text()[-12000:])
            raise RuntimeError(label + ' failed')

    try:
        # CodexAstraLocal: This hook runs before the older profile gate. Generate
        # its real settings keys locally instead of depending on build order or
        # a full native configuration left behind on the developer's machine.
        setting_source = out / 'setting-source'
        (setting_source / 'common').mkdir(parents=True)
        (setting_source / 'common/setting_keys.h.in').write_bytes(
            (root / 'src/common/setting_keys.h.in').read_bytes())
        (setting_source / 'CMakeLists.txt').write_text(
            'cmake_minimum_required(VERSION 3.22)\nproject(UberharSoftwareProbe NONE)\n'
            + f'include("{root}/CMakeModules/GenerateSettingKeys.cmake")\n')
        run('settings', ['cmake', '-S', str(setting_source), '-B', str(out / 'settings')])
        objects = []
        for index, source in enumerate(sources):
            obj = out / f'{index}.o'
            dep = out / f'{index}.d'
            run(f'compile-{index}', compiler + flags + ['-MMD', '-MF', str(dep),
                '-c', str(source), '-o', str(obj)])
            inputs.add(source)
            # CodexAstraLocal: Bind headers actually selected by the compiler,
            # including the shared fixture, without guessing a dependency list.
            dependency_text = dep.read_text().replace('\\\n', '')
            inputs.update((root / name).resolve() for name in
                          shlex.split(dependency_text.split(':', 1)[1]))
            objects.append(str(obj))
        binary = out / 'software-renderer'
        run('link', compiler + objects + ['-pthread', '-o', str(binary)])
        report = out / 'results.xml'
        run('test', [str(binary), '[software]', '--reporter', 'xml', '--out', str(report)])
        tree = ET.parse(report).getroot()
        totals = tree.find('OverallResultsCases')
        if totals is None or int(totals.attrib['successes']) != 4 or int(totals.attrib['failures']):
            raise RuntimeError('Four complete helper cases were not executed')
        proof['cases'] = 4
        proof['assertions'] = tree.find('OverallResults').attrib
        proof['binary_sha256'] = sha(binary)
        proof['passed'] = True
    finally:
        proof['ended_unix'] = time.time()
        proof['inputs'] = [{'path': str(path), 'sha256': sha(path)} for path in sorted(inputs)]
        (out / 'provenance.json').write_text(json.dumps(proof, indent=2) + '\n')
        print(json.dumps({'passed': proof['passed'], 'output': str(out)}))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
