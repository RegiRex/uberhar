#!/usr/bin/env python3
"""CodexAstraLocal: Test actual parallel-memory admission predicates in modeled owners."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile


# CodexAstraLocal: Extract complete production definitions, rejecting ambiguous
# seams. The fixture supplies dependency shells rather than reimplementing gates.
def definition(text, signature):
    if text.count(signature) != 1:
        raise RuntimeError(f"Production extraction boundary changed: {signature}")
    start = text.index(signature)
    end = text.index("{", start) + 1
    depth = 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutants", action="store_true")
    parser.add_argument("--output", type=Path,
                        default=Path("build/uberhar-probe/parallel-memory"))
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    args.output.mkdir(parents=True, exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix="run-", dir=args.output.resolve()))
    paths = {
        "dsp": "src/audio_core/dsp_interface.h",
        "hle": "src/audio_core/hle/hle.h",
        "lle_h": "src/audio_core/lle/lle.h",
        "lle": "src/audio_core/lle/lle.cpp",
        "core_h": "src/core/core.h",
        "core": "src/core/core.cpp",
        "kernel": "src/core/hle/kernel/kernel.h",
        "ipc": "src/core/hle/kernel/hle_ipc.h",
        "directory": "src/core/hle/service/fs/directory.cpp",
        "script": "tools/uberhar/test_parallel_memory.py",
        "fixture": "tools/uberhar/test_parallel_memory.cpp",
    }

    def hashes():
        return {value: hashlib.sha256((root / value).read_bytes()).hexdigest()
                for value in paths.values()}

    proof = {
        "author": "CodexAstraLocal", "sources_before": hashes(), "variants": [],
        "scope": "Actual getters and atomic counter methods in modeled owner/dependency shells; "
                 "no full DSP/core/target execution or scheduling-cost measurement",
    }
    try:
        sources = {key: (root / value).read_text() for key, value in paths.items()}
        blocks = {
            "dsp": definition(sources["dsp"], "virtual bool MayWriteMemoryConcurrently() const"),
            "hle": definition(sources["hle"], "bool MayWriteMemoryConcurrently() const override"),
            "lle": definition(sources["lle"], "bool DspLle::MayWriteMemoryConcurrently() const"),
            "core": definition(sources["core"], "bool System::HasConcurrentGuestMemoryWriters() const"),
            "begin_end": definition(sources["kernel"], "void ReportAsyncState(bool state)"),
            "pending": definition(sources["kernel"], "bool AreAsyncOperationsPending()"),
        }
        # CodexAstraLocal: Bind the source scheduling facts outside the modeled
        # owners: increment before dispatch and decrement after owner completion.
        # A real asynchronous directory write makes this guard materially needed.
        async_def = definition(sources["ipc"], "void RunAsync(AsyncFunctor async_section,")
        worker_def = definition(sources["ipc"], "void RunOnThreadWorker(Common::ThreadWorker& worker,")
        wakeup = definition(sources["ipc"], "void WakeUp(std::shared_ptr<Kernel::Thread> thread,")
        directory = definition(sources["directory"], "void Directory::Read(")
        checks = {
            "count_before_async": async_def.index("kernel.ReportAsyncState(true)") < async_def.index("std::async("),
            "count_before_queue": worker_def.index("kernel.ReportAsyncState(true)") < worker_def.index("worker.QueueWork("),
            "decrement_after_owner_result": wakeup.index("functor(ctx)") < wakeup.index("kernel.ReportAsyncState(false)"),
            "atomic_zero_initialized": "std::atomic<int> pending_async_operations{};" in sources["kernel"],
            "immutable_lle_mode": "const bool multithread;" in sources["lle"],
            "constructed_lle_mode": "multithread(multithread)" in sources["lle"],
            "directory_worker_writes": directory.index("async_data->buffer->Write(entries.data(), 0,") < directory.index("IPC::RequestBuilder"),
            "actual_runtime_not_settings": "Settings::" not in blocks["core"] + blocks["lle"],
        }
        proof["source_checks"] = checks
        if not all(checks.values()):
            raise RuntimeError(f"Source contract changed: {checks}")

        source = sources["fixture"]
        for key, value in blocks.items():
            token = "@" + key.upper() + "@"
            if source.count(token) != 1:
                raise RuntimeError(f"Fixture extraction token changed: {token}")
            source = source.replace(token, value)
        if "@" in source:
            raise RuntimeError("Unexpanded fixture token")
        proof["extracted"] = blocks
        variants = [("baseline-no-rpc", source, False, None),
                    ("baseline-rpc", source, True, None)]
        # CodexAstraLocal: Every deliberate unsafe predicate must compile and fail
        # its named runtime assertion; a parser/compiler error is not a passed mutant.
        mutations = [
            ("unknown-dsp", blocks["dsp"], blocks["dsp"].replace("return true;", "return false;"), "unknown DSP must refuse"),
            ("lle-mode", blocks["lle"], blocks["lle"].replace("return impl->multithread;", "return false;"), "parallel LLE must refuse"),
            ("pending-jobs", "kernel->AreAsyncOperationsPending()", "false", "active jobs must refuse"),
            ("count-boolean", "pending_async_operations++;", "pending_async_operations = 1;", "one remaining job must refuse"),
            ("rpc", "if (rpc_server)", "if (false)", "actual RPC must refuse"),
        ] if args.mutants else []
        for name, old, new, message in mutations:
            if source.count(old) != 1:
                raise RuntimeError(f"Nonunique mutation: {name}")
            variants.append((name, source.replace(old, new), True, message))

        # CodexAstraLocal: This is a predicate/lifetime oracle, not a microbenchmark.
        # Avoid optimizer-specific speculative devirtualization warnings in minimal
        # dependency shells; full production translation units use normal build flags.
        flags = shlex.split(os.environ.get("CXX", "c++")) + [
            "-std=c++20", "-O0", "-pthread", "-Wall", "-Wextra", "-Werror"]
        for name, text, scripting, expected_error in variants:
            cpp, binary = output / (name + ".cpp"), output / name
            cpp.write_text(text)
            command = flags + (["-DENABLE_SCRIPTING=1"] if scripting else [])
            command += [str(cpp), "-o", str(binary)]
            compiled = subprocess.run(command, capture_output=True, text=True, timeout=60)
            (output / (name + ".compile.log")).write_text(compiled.stdout + compiled.stderr)
            compiled.check_returncode()
            run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=15)
            result = run.stdout + run.stderr
            (output / (name + ".run.log")).write_text(result)
            passed = (run.returncode == 0 and "runtime-query controls" in result) if expected_error is None else (
                run.returncode == 1 and "parallel memory FAIL: " + expected_error in result)
            proof["variants"].append({"name": name, "argv": command, "exit": run.returncode,
                                      "output": result, "passed": passed})
            print(f'{name}: {"PASS" if passed else "FAIL"}', flush=True)
            if not passed:
                raise RuntimeError(f"Unexpected oracle outcome: {name}; {output}")
    finally:
        proof["sources_after"] = hashes()
        proof["sources_unchanged"] = proof["sources_before"] == proof["sources_after"]
        proof["artifacts"] = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                              for p in output.iterdir() if p.is_file()}
        (output / "provenance.json").write_text(json.dumps(proof, indent=2) + "\n")
        print(f"Proof: {output}", flush=True)
    if not proof["sources_unchanged"]:
        raise RuntimeError("Production source changed during test")


if __name__ == "__main__":
    main()
