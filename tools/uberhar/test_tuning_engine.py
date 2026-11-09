#!/usr/bin/env python3
"""CodexAstraLocal: Compile/run the Android-free production tuning engine and host cases."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "src/android/app/src/main/java/org/citra/citra_emu/features/settings/model/UberharTuningEngine.kt"
TEST = ROOT / "tools/uberhar/tests/UberharTuningEngineHostTest.kt"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kotlinc", default=shutil.which("kotlinc"))
    parser.add_argument("--java", default=shutil.which("java"))
    parser.add_argument("--output", type=Path, default=ROOT / "build/uberhar-probe/tuning-engine")
    args = parser.parse_args()
    if not args.kotlinc or not args.java:
        parser.error("Supply --kotlinc and --java when absent from PATH")
    args.output.mkdir(parents=True, exist_ok=True)
    folder = Path(tempfile.mkdtemp(prefix="run-", dir=args.output))
    jar = folder / "tuning-engine.jar"
    env = dict(os.environ, JAVA_HOME=str(Path(args.java).resolve().parent.parent))
    compiler = Path(args.kotlinc).resolve()
    # CodexAstraLocal: IDE-bundled compiler scripts can be readable but non-executable.
    command = ([str(compiler)] if os.access(compiler, os.X_OK) else ["bash", str(compiler)])
    compile_command = command + [str(SOURCE), str(TEST), "-include-runtime", "-d", str(jar)]
    run_command = [str(Path(args.java).resolve()), "-jar", str(jar)]
    proof = {"author": "CodexAstraLocal", "production_source": str(SOURCE),
             "source_sha256": hashlib.sha256(SOURCE.read_bytes()).hexdigest(),
             "test_sha256": hashlib.sha256(TEST.read_bytes()).hexdigest(),
             "compile_command": compile_command, "run_command": run_command,
             "scope": "actual pure production Kotlin; synthetic host scenarios only"}
    print(f"Evidence: {folder}", flush=True)
    for name, command in (("compile", compile_command), ("run", run_command)):
        result = subprocess.run(command, env=env, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, timeout=120, text=True)
        (folder / f"{name}.log").write_text(result.stdout)
        proof[f"{name}_returncode"] = result.returncode
        (folder / "RESULT.json").write_text(json.dumps(proof, indent=2) + "\n")
        if result.returncode:
            print(result.stdout)
            raise SystemExit(result.returncode)
        if name == "run":
            print(result.stdout, end="")


if __name__ == "__main__":
    main()
