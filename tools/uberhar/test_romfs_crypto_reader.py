#!/usr/bin/env python3
"""CodexAstraLocal: Test actual RomFS read/cache crypto with synthetic bytes and real AES."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

from test_installer_security import definition, replace_once, require

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=ROOT)
    parser.add_argument("--output", type=Path, default=ROOT / "build/uberhar-probe/romfs-crypto-reader")
    parser.add_argument("--cryptopp-include", type=Path, required=True)
    parser.add_argument("--cryptopp-library", type=Path, required=True)
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
        path = output / "sources" / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        return data.decode()

    def run(command, label):
        result = subprocess.run(command, cwd=repo, capture_output=True, timeout=60,
                                env=dict(os.environ, ASAN_OPTIONS="detect_leaks=0"))
        (output / (label + ".stdout")).write_bytes(result.stdout)
        (output / (label + ".stderr")).write_bytes(result.stderr)
        return result

    try:
        read("tools/uberhar/test_romfs_crypto_reader.py")
        read("tools/uberhar/test_installer_security.py")
        fixture = read("tools/uberhar/test_romfs_crypto_reader.cpp")
        source = read("src/core/file_sys/romfs_reader.cpp")
        header = read("src/core/file_sys/romfs_reader.h")
        for name in ("static_lru_cache", "alignment", "common_types"):
            read("src/common/" + name + ".h")
        require("BOOST_CLASS_VERSION(FileSys::DirectRomFSReader, 1)" in header,
                "reader serialization version not registered")
        classes = "\n".join(definition(header, signature) + ";" for signature in (
            "class RomFSReader {", "class DirectRomFSReader : public RomFSReader {"))
        methods = "\n\n".join(definition(source, signature) for signature in (
            "std::size_t DirectRomFSReader::ReadFile(",
            "bool DirectRomFSReader::AllowsCachedReads() const",
            "bool DirectRomFSReader::CacheReady(",
            "std::vector<std::pair<std::size_t, std::size_t>> DirectRomFSReader::BreakupRead("))
        variants = [("candidate", methods, None)]
        if not args.no_mutants:
            # CodexAstraLocal: Exact output comparisons reject missing CTR bias
            # independently on cache-miss and direct large-read paths.
            variants.append(("wrong-cache-ctr", replace_once(methods,
                "d.Seek(crypto_offset + page);", "d.Seek(page);"),
                b"FAIL unaligned encrypted bytes\n"))
            variants.append(("wrong-large-ctr", replace_once(methods,
                "d.Seek(crypto_offset + offset);", "d.Seek(offset);"),
                b"FAIL large encrypted bytes\n"))
            variants.append(("cache-truncated-page", replace_once(methods,
                "            cache.clear();", ""),
                b"FAIL short final page is not reused\n"))
        library = args.cryptopp_library.resolve()
        proof["cryptopp"] = {"library": str(library), "sha256": hashlib.sha256(library.read_bytes()).hexdigest(),
                              "headers": str(args.cryptopp_include.resolve())}
        for label, current_methods, expected in variants:
            case = output / label
            case.mkdir()
            generated = fixture.replace("// @READER_CLASSES@", classes)
            generated = generated.replace("// @READ_METHODS@", current_methods)
            cpp, executable = case / "fixture.cpp", case / "fixture"
            cpp.write_text(generated)
            command = [args.cxx, "-std=c++20", "-O1", "-g", "-fno-omit-frame-pointer",
                       "-fsanitize=address", "-fno-pie", "-no-pie", "-pthread",
                       "-I" + str(repo / "src"), "-I" + str(args.cryptopp_include.resolve()), str(cpp)]
            command += (["-c", "-o", str(case / "fixture.o")] if args.linker else
                        [str(library), "-o", str(executable)])
            compiled = run(command, label + "-compile")
            require(compiled.returncode == 0, label + " failed to compile")
            link_command = None
            if args.linker:
                link_command = [args.linker, "-fsanitize=address", "-no-pie", "-pthread",
                                str(case / "fixture.o"), str(library), "-o", str(executable)]
                linked = run(link_command, label + "-link")
                require(linked.returncode == 0, label + " failed to link")
            executed = run([str(executable)], label + "-run")
            if expected is None:
                require(executed.returncode == 0 and b"PASS RomFS crypto reader" in executed.stdout,
                        "candidate RomFS assertions failed")
            else:
                require(executed.returncode == 1 and executed.stderr == expected,
                        label + " did not reject intended defect")
            proof["variants"].append({"name": label, "compile_command": command,
                                      "link_command": link_command, "compile_exit": compiled.returncode,
                                      "run_exit": executed.returncode,
                                      "expected_error": expected.decode() if expected else None})
        proof["status"] = "PASS"
        proof["scope"] = "Actual reader classes/methods and StaticLRUCache, real CryptoPP, synthetic transient key/IV and bytes, ASan; recording versioned archive fields. No real content, full Boost archive roundtrip or device qualification."
    except Exception as error:
        proof["status"] = "FAIL"
        proof["error"] = str(error)
        raise
    finally:
        (output / "provenance.json").write_text(json.dumps(proof, indent=2) + "\n")
        print(output, flush=True)
    print("PASS RomFS crypto reader: candidate and " + str(len(proof["variants"]) - 1) + " negative controls")


if __name__ == "__main__":
    main()
