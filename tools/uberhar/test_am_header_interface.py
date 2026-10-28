#!/usr/bin/env python3
"""CodexAstraLocal: Compile the public AM ownership interface without private Crypto++."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ndk', type=Path, default=os.environ.get('ANDROID_NDK_HOME'))
    args = parser.parse_args()
    if args.ndk is None:
        parser.error('provide --ndk or ANDROID_NDK_HOME (Android NDK 27.3.13750724)')
    ndk = args.ndk.absolute()
    if 'Pkg.Revision = 27.3.13750724' not in (ndk / 'source.properties').read_text().splitlines():
        parser.error('Android NDK 27.3.13750724 is required, matching the app')
    root = Path(__file__).resolve().parents[2]
    parent = root / 'build/uberhar-probe/am-header-interface'
    parent.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix='run-', dir=parent))
    proof = {'passed': False, 'commands': []}
    print(f'AM header interface artifacts: {out}', flush=True)

    # CodexAstraLocal: Ambient host include paths must not widen the frontend contract.
    env = dict(os.environ)
    for name in ('CPATH', 'CPLUS_INCLUDE_PATH', 'C_INCLUDE_PATH', 'OBJC_INCLUDE_PATH'):
        env.pop(name, None)

    def run(label, command, expected_error=None):
        result = subprocess.run(command, cwd=root, env=env, capture_output=True,
                                text=True, timeout=120)
        log = result.stdout + result.stderr
        (out / f'{label}.log').write_text(log)
        proof['commands'].append({'label': label, 'argv': command, 'exit_code': result.returncode})
        passed = result.returncode == 0 if expected_error is None else (
            result.returncode != 0 and expected_error in result.stderr)
        if not passed:
            print(log)
            raise RuntimeError(f'{label} failed; see {out}')

    try:
        # CodexAstraLocal: Run only the existing settings generator, without an emulator build.
        source = out / 'setting-source'
        for relative in ('common/setting_keys.h.in',
                         'android/app/src/main/jni/jni_setting_keys.cpp.in'):
            target = source / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes((root / 'src' / relative).read_bytes())
        (source / 'CMakeLists.txt').write_text(
            'cmake_minimum_required(VERSION 3.22)\nproject(AMHeaderInterface NONE)\n'
            'set(ANDROID TRUE)\n'
            f'include("{root}/CMakeModules/GenerateSettingKeys.cmake")\n')
        run('settings', ['cmake', '-S', str(source), '-B', str(out / 'settings')])

        # CodexAstraLocal: Reject the original leak even if host Crypto++ headers exist.
        marker = 'AM_PUBLIC_HEADER_MUST_NOT_INCLUDE_PRIVATE_CRYPTOPP'
        poison = out / 'poison'
        (poison / 'cryptopp').mkdir(parents=True)
        (poison / 'cryptopp/sha.h').write_text(f'#error {marker}\n')
        fixture = out / 'am_header_interface.cpp'
        fixture.write_text('''// CodexAstraLocal: Instantiate frontend destruction of both AM owners.
#include "core/hle/service/am/am.h"
void DestroyNCCH(Service::AM::NCCHCryptoFile* file) { delete file; }
void DestroyCIA(Service::AM::CIAFile* file) { delete file; }
''')
        compiler = ndk / 'toolchains/llvm/prebuilt/linux-x86_64/bin/clang++'
        flags = [str(compiler), '--target=aarch64-none-linux-android29', '-std=c++20',
                 # CodexAstraLocal: Match the enabled Android renderer branches in shared settings.
                 '-DANDROID', '-DENABLE_VULKAN', '-DENABLE_OPENGL', '-DENABLE_SOFTWARE_RENDERER',
                 '-I' + str(poison), '-I' + str(out / 'settings')]
        public_includes = ['src', 'externals/boost', 'externals/fmt/include', 'externals/xxHash']
        flags += ['-I' + str(root / name) for name in public_includes]
        run('candidate', flags + ['-c', str(fixture), '-o', str(out / 'candidate.o')])

        # CodexAstraLocal: Restore the defective dependency only in an isolated header overlay.
        overlay = out / 'original-dependency'
        header = overlay / 'core/hle/service/am/am.h'
        header.parent.mkdir(parents=True)
        header.write_text('#include "core/file_sys/ncch_install_verifier.h"\n' +
                          (root / 'src/core/hle/service/am/am.h').read_text())
        run('original-dependency', flags[:1] + ['-I' + str(overlay)] + flags[1:] +
            ['-c', str(fixture), '-o', str(out / 'original-dependency.o')],
            expected_error=f'error: {marker}')
        proof['passed'] = True
        print('PASS Android AM header ownership; original private Crypto++ dependency rejected')
    finally:
        (out / 'provenance.json').write_text(json.dumps(proof, indent=2) + '\n')


if __name__ == '__main__':
    main()
