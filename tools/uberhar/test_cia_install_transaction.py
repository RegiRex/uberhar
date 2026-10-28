#!/usr/bin/env python3
"""CodexAstraLocal: Exercise actual CIA transaction code against temporary real files."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def require(condition, reason):
    if not condition:
        raise RuntimeError(reason)


def replace_once(source, old, new):
    require(source.count(old) == 1, "changed mutation seam: " + old)
    return source.replace(old, new, 1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=ROOT)
    parser.add_argument("--output", type=Path,
                        default=ROOT / "build/uberhar-probe/cia-install-transaction")
    parser.add_argument("--cxx", default=os.environ.get("CXX", "g++"))
    parser.add_argument("--linker", help="Optional compiler driver supplying sanitizer runtimes")
    parser.add_argument("--no-mutants", action="store_true")
    args = parser.parse_args()
    repo = args.source_root.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix="run-", dir=args.output.resolve()))
    proof = {"author": "CodexAstraLocal", "status": "INCOMPLETE", "inputs": {}, "variants": []}

    def read(relative):
        data = (repo / relative).read_bytes()
        proof["inputs"][relative] = hashlib.sha256(data).hexdigest()
        target = output / "sources" / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        return data.decode()

    def run(command, label):
        result = subprocess.run(command, capture_output=True, cwd=repo, timeout=60,
                                env=dict(os.environ, ASAN_OPTIONS="detect_leaks=0",
                                         UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1"))
        (output / (label + ".stdout")).write_bytes(result.stdout)
        (output / (label + ".stderr")).write_bytes(result.stderr)
        return result

    try:
        read("tools/uberhar/test_cia_install_transaction.py")
        fixture = read("tools/uberhar/test_cia_install_transaction.cpp")
        transaction = read("src/core/file_sys/cia_install_transaction.h")
        util = read("src/common/file_util.h")
        # CodexAstraLocal: Keep endpoint declarations bound to production while
        # replacing only their bodies with actual filesystem operations and faults.
        for declaration in ("bool Exists(const std::string& filename);",
                            "bool Delete(const std::string& filename);",
                            "bool Rename(const std::string& srcFullPath, const std::string& destFullPath);"):
            require(declaration in util, "changed filesystem endpoint: " + declaration)
        variants = [("candidate", transaction, None)]
        if not args.no_mutants:
            variants.append(("metadata-first", replace_once(transaction,
                "return a.metadata < b.metadata;", "return a.metadata > b.metadata;"),
                b"FAIL metadata published last\n"))
            variants.append(("discard-recovery-backup", replace_once(transaction,
                "    void Rollback() {", "    void Rollback() {\n"
                "        for (const auto& entry : entries) { FileUtil::Delete(entry.backup); }"),
                b"FAIL existing file restored after rename failure\n"))
        for label, header, expected in variants:
            case = output / label
            (case / "core/file_sys").mkdir(parents=True)
            (case / "common/logging").mkdir(parents=True)
            (case / "core/file_sys/cia_install_transaction.h").write_text(header)
            (case / "common/file_util.h").write_text(
                "#pragma once\n#include <string>\nnamespace FileUtil {\n"
                "bool Exists(const std::string& filename);\n"
                "bool Delete(const std::string& filename);\n"
                "bool Rename(const std::string& srcFullPath, const std::string& destFullPath);\n}\n")
            (case / "common/logging/log.h").write_text(
                "#pragma once\n#define LOG_ERROR(...) ((void)0)\n#define LOG_WARNING(...) ((void)0)\n")
            source, executable = case / "fixture.cpp", case / "fixture"
            source.write_text(fixture)
            command = [args.cxx, "-std=c++20", "-O1", "-g", "-fno-omit-frame-pointer",
                       "-fsanitize=address,undefined", "-fno-pie", "-no-pie",
                       "-I" + str(case), str(source)]
            command += (["-c", "-o", str(case / "fixture.o")] if args.linker else
                        ["-o", str(executable)])
            compiled = run(command, label + "-compile")
            require(compiled.returncode == 0, label + " failed to compile")
            link_command = None
            if args.linker:
                link_command = [args.linker, "-fsanitize=address,undefined", "-no-pie",
                                str(case / "fixture.o"), "-o", str(executable)]
                linked = run(link_command, label + "-link")
                require(linked.returncode == 0, label + " failed to link sanitizer runtimes")
            executed = run([str(executable), str(case / "files")], label + "-run")
            if expected is None:
                require(executed.returncode == 0 and b"PASS CIA transaction" in executed.stdout,
                        "candidate transaction assertions failed")
            else:
                # CodexAstraLocal: Negative controls must compile and fail the
                # exact data/order assertion, never a timeout or sanitizer crash.
                require(executed.returncode == 1 and executed.stderr == expected,
                        label + " did not reject intended defect")
            proof["variants"].append({"name": label, "compile_command": command,
                                      "link_command": link_command, "compile_exit": compiled.returncode,
                                      "run_exit": executed.returncode,
                                      "expected_error": expected.decode() if expected else None})
        proof["status"] = "PASS"
        proof["scope"] = "Actual CIAInstallTransaction with real temporary std::filesystem operations and injected rename failures; ASan and UBSan. No full installer, power-loss atomicity, SAF or device qualification."
    except Exception as error:
        proof["status"] = "FAIL"
        proof["error"] = str(error)
        raise
    finally:
        (output / "provenance.json").write_text(json.dumps(proof, indent=2) + "\n")
        print(output, flush=True)
    print("PASS CIA transaction: candidate and " + str(len(proof["variants"]) - 1) + " negative controls")


if __name__ == "__main__":
    main()
