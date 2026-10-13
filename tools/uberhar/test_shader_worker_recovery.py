#!/usr/bin/env python3
"""CodexAstraLocal: Execute the actual worker terminal boundary, not a GPU queue.

The scheduler's exact submission/allocation seam and real ShaderFailureState are
used. Chunk commands, allocation and completion are controlled fault adapters.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time


FIXTURE = r'''
#include <cstdio>
#include <exception>
#include <memory>
#include <stdexcept>
#include <thread>
#include "video_core/shader_build_failure.h"
unsigned checks{};
void Check(bool value, const char* reason) {
    ++checks;
    if (!value) throw std::runtime_error{reason};
}
template<typename F> bool Typed(F&& function) {
    try { function(); } catch (const VideoCore::ShaderRecoveryError&) { return true; }
    return false;
}
// CodexAstraLocal: Only command execution/allocation are modeled. The exact
// production boundary decides whether failures latch and later work is canceled.
struct Harness {
    struct Chunk {
        VideoCore::ShaderFailureState& state;
        bool submit{};
        unsigned executed{}, discarded{};
        bool HasSubmit() const { return submit; }
        void ExecuteAll(int) { ++executed; if (submit) state.Submitted(1); }
        void Discard() { ++discarded; }
    };
    VideoCore::ShaderFailureState shader_failure;
    std::unique_ptr<Chunk> work;
    int current_cmdbuf{7}, fault{};
    unsigned allocations{};
    Harness(bool submit, int fault_) : work{std::make_unique<Chunk>(shader_failure, submit)}, fault{fault_} {}
    void AllocateWorkerCommandBuffers() {
        ++allocations;
        if (fault == 1) throw VideoCore::ShaderRecoveryError{"next command-buffer allocation failed"};
        if (fault == 2) throw std::runtime_error{"unrelated driver failure"};
    }
    void Run() {
        // UBERHAR_WORKER_SEAM
    }
};
int main() {
    try {
        for (bool submit : {false, true}) {
            Harness owner{submit, 0};
            owner.Run();
            Check(owner.work->executed == 1 && owner.work->discarded == 0,
                  "healthy chunk executes once");
            Check(owner.allocations == unsigned(submit), "only submission allocates a next buffer");
            Check(!owner.shader_failure.Failed(), "healthy worker remains usable");
        }
        Harness owner{true, 1};
        std::exception_ptr escaped;
        std::jthread worker{[&] {
            try { owner.Run(); } catch (...) { escaped = std::current_exception(); }
        }};
        worker.join();
        Check(!escaped, "typed next-buffer failure must not escape worker thread");
        Check(owner.shader_failure.Failed() && owner.work->executed == 1 &&
              owner.work->discarded == 1 && owner.allocations == 1,
              "post-submit failure latches once and discards remaining captures");
        Check(owner.shader_failure.TrySubmittedTick() == 1, "already submitted tick stays published");
        Check(Typed([&] { owner.shader_failure.Check(); }), "owner observes typed terminal failure");
        Check(Typed([&] { owner.shader_failure.WaitSubmitted(2); }), "unsignaled wait fails without hanging");
        owner.work = std::make_unique<Harness::Chunk>(owner.shader_failure, true);
        owner.Run();
        Check(owner.work->executed == 0 && owner.work->discarded == 1 && owner.allocations == 1,
              "no command or allocation executes after terminal latch");
        owner.shader_failure.BeginShutdown();
        Check(owner.shader_failure.WaitSubmitted(1), "shutdown drains actual prior submission");
        Check(!owner.shader_failure.WaitSubmitted(2), "shutdown never waits for canceled work");
        Harness unrelated{true, 2};
        bool propagated = false;
        try { unrelated.Run(); } catch (const std::runtime_error&) { propagated = true; }
        Check(propagated && !unrelated.shader_failure.Failed(), "unrelated failures are not mislabeled recoverable");
        std::printf("Worker allocation recovery: %u checks; exact production boundary, controlled operations\n", checks);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL after %u checks: %s\n", checks, error.what());
        return 1;
    }
}
'''


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=Path("build/uberhar-probe/shader-worker-recovery"))
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    source = root / "src/video_core/renderer_vulkan/vk_scheduler.cpp"
    text = source.read_text()
    start = text.index("const bool has_submit = work->HasSubmit();")
    end = text.index("\n        }\n\n        {\n            std::scoped_lock rl", start)
    seam = text[start:end]
    assert seam.count("AllocateWorkerCommandBuffers();") == 1
    assert seam.count("VideoCore::ExecuteShaderCommands(") == 1
    mutant = seam.replace("AllocateWorkerCommandBuffers();", "/* defect: allocation escapes the boundary */")
    mutant += "\nif (has_submit && !shader_failure.Failed()) AllocateWorkerCommandBuffers();\n"
    args.output.mkdir(parents=True, exist_ok=True)
    run = Path(tempfile.mkdtemp(prefix="run-", dir=args.output.resolve()))
    report = {"scope": "exact worker seam and real terminal latch; modeled commands/allocation",
              "inputs": {str(p.relative_to(root)): sha(p) for p in
                         (source, Path(__file__).resolve(), root / "src/video_core/shader_build_failure.h",
                          root / "src/video_core/shader_recovery_error.h")}, "results": []}
    started = time.monotonic()
    status = {"state": "running"}
    (run / "status.json").write_text(json.dumps(status) + "\n")
    print(f"Worker recovery proof: {run}", flush=True)
    try:
        for name, body in (("baseline", seam), ("outside-boundary", mutant)):
            cpp, exe = run / f"{name}.cpp", run / name
            cpp.write_text(FIXTURE.replace("// UBERHAR_WORKER_SEAM", body))
            command = [os.environ.get("CXX", "c++"), "-std=c++20", "-O2", "-pthread", "-Isrc", str(cpp), "-o", str(exe)]
            with (run / f"{name}.compile.stdout").open("w") as out, (run / f"{name}.compile.stderr").open("w") as err:
                compiled = subprocess.run(command, cwd=root, stdout=out, stderr=err, timeout=60)
            row = {"name": name, "command": command, "compile_returncode": compiled.returncode}
            report["results"].append(row)
            if compiled.returncode: raise RuntimeError(f"{name} compilation failed")
            with (run / f"{name}.stdout").open("w") as out, (run / f"{name}.stderr").open("w") as err:
                executed = subprocess.run([str(exe)], cwd=root, stdout=out, stderr=err, timeout=10)
            row["returncode"] = executed.returncode
            if name == "baseline" and executed.returncode: raise RuntimeError("positive worker check failed")
            if name != "baseline" and (executed.returncode != 1 or "typed next-buffer failure" not in (run / f"{name}.stderr").read_text()):
                raise RuntimeError("outside-boundary control missed the intended failure")
        report["passed"] = True
        status.update(state="complete", exit_code=0)
    except BaseException as error:
        status.update(state="failed", exit_code=1, error=repr(error))
        raise
    finally:
        status["elapsed_seconds"] = time.monotonic() - started
        report["artifacts"] = {p.name: sha(p) for p in sorted(run.iterdir()) if p.is_file() and p.name != "status.json"}
        (run / "provenance.json").write_text(json.dumps(report, indent=2) + "\n")
        (run / "status.json").write_text(json.dumps(status, indent=2) + "\n")


if __name__ == "__main__":
    main()
