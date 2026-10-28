#!/usr/bin/env python3
"""CodexAstraLocal: Real Z3DS finalization and metadata-save failure regression fixture."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

from test_installer_security import definition, replace_once, require

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=ROOT)
    parser.add_argument("--output", type=Path, default=ROOT / "build/uberhar-probe/z3ds-close")
    parser.add_argument("--seekable-library", type=Path, required=True)
    parser.add_argument("--seekable-include", type=Path, required=True)
    parser.add_argument("--cryptopp-library", type=Path, required=True)
    parser.add_argument("--cryptopp-include", type=Path, required=True)
    parser.add_argument("--cxx", default=os.environ.get("CXX", "g++"))
    parser.add_argument("--linker")
    parser.add_argument("--no-mutants", action="store_true")
    args = parser.parse_args()
    repo = args.source_root.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix="run-", dir=args.output.resolve()))
    proof = {"author": "CodexAstraLocal", "status": "INCOMPLETE", "inputs": {}, "variants": []}

    def read(relative):
        data = (repo / relative).read_bytes()
        proof["inputs"][relative] = hashlib.sha256(data).hexdigest()
        saved = output / "sources" / relative
        saved.parent.mkdir(parents=True, exist_ok=True)
        saved.write_bytes(data)
        return data.decode()

    def run(command, label):
        result = subprocess.run(command, cwd=repo, capture_output=True, timeout=60,
                                env=dict(os.environ, ASAN_OPTIONS="detect_leaks=0"))
        (output / (label + ".stdout")).write_bytes(result.stdout)
        (output / (label + ".stderr")).write_bytes(result.stderr)
        return result

    try:
        read("tools/uberhar/test_z3ds_close.py")
        read("tools/uberhar/test_installer_security.py")
        fixture = read("tools/uberhar/test_z3ds_close.cpp")
        source = read("src/common/file_derived.cpp")
        header = read("src/common/file_derived.h")
        compression = read("src/common/zstd_compression.cpp")
        ticket = read("src/core/file_sys/ticket.cpp")
        metadata = read("src/core/file_sys/title_metadata.cpp")
        signature = read("src/core/file_sys/signature.h")
        am = read("src/core/hle/service/am/am.cpp")
        read("src/core/file_sys/ticket.h")
        read("src/core/file_sys/title_metadata.h")
        for name in ("alignment", "common_types", "common_funcs", "swap"):
            read("src/common/" + name + ".h")
        writer_classes = "\n".join(definition(header, text) + ";" for text in (
            "struct Z3DSFileHeader {", "class Z3DSMetadata {", "class Z3DSWriteIOFile : public IOFileBase {"))
        impl = definition(source, "struct Z3DSWriteIOFile::Z3DSWriteIOFileImpl {") + ";"
        start = source.index("Z3DSWriteIOFile::Z3DSWriteIOFile()")
        end = source.index("template <class Archive>", start)
        writer = source[start:end]
        metadata_writer = ("template <typename T>\n" + definition(compression, "void WriteToOStream(") +
                           "\n" + definition(compression, "std::vector<u8> Z3DSMetadata::AsBinary()"))
        sig_enum = re.search(r"enum SignatureType : u32 \{.*?\};", signature, re.S)
        require(sig_enum, "signature enum missing")
        parser_code = sig_enum[0] + "\n" + "\n".join((
            definition(signature, "inline u32 GetSignatureSize("),
            definition(ticket, "Loader::ResultStatus Ticket::Load(std::span<const u8>"),
            definition(ticket, "std::vector<u8> Ticket::Serialize() const"),
            definition(ticket, "Loader::ResultStatus Ticket::Save("),
            definition(metadata, "Loader::ResultStatus TitleMetadata::Load(std::span<const u8>"),
            definition(metadata, "Loader::ResultStatus TitleMetadata::Save("),
            definition(metadata, "size_t TitleMetadata::GetContentCount()")))
        install = definition(am, "InstallStatus InstallCIA(")
        gate_start = install.index("        const bool needs_title_key =")
        gate_stop = install.index("        installFile.EnableExternalInstall(needs_title_key);", gate_start)
        gate = install[gate_start:gate_stop + len("        installFile.EnableExternalInstall(needs_title_key);")]
        variants = [("candidate", writer, parser_code, gate, None)]
        if not args.no_mutants:
            variants.append(("repeat-close", replace_once(writer, "    if (closed) {", "    if (false) {"),
                             parser_code, gate, b"FAIL repeated Close preserves success\n"))
            variants.append(("ignore-stream-result", replace_once(writer,
                "close_result = stream_result && child_result;", "close_result = child_result;"),
                parser_code, gate, b"FAIL writer I/O failure is retained\n"))
            variants.append(("accept-ticket-short-write", writer, replace_once(parser_code,
                "file.WriteBytes(serialized.data(), serialized.size()) != serialized.size()",
                "file.WriteBytes(serialized.data(), serialized.size()) == 0"), gate,
                b"FAIL ticket short write rejected\n"))
            variants.append(("require-unused-title-key", writer, parser_code,
                replace_once(gate, "if (needs_title_key && container.GetTicket().DoTitlekeyFixup()",
                             "if (container.GetTicket().DoTitlekeyFixup()"),
                b"FAIL plaintext personalized CIA bypasses unused key fixup\n"))
        libraries = [args.seekable_library.resolve(), args.cryptopp_library.resolve()]
        proof["libraries"] = [{"path": str(path), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
                              for path in libraries]
        for label, current_writer, current_parser, current_gate, expected in variants:
            case = output / label
            case.mkdir()
            generated = fixture
            for marker, code in (("WRITER_CLASSES", writer_classes), ("WRITER_IMPL", impl),
                                 ("WRITER_METHODS", current_writer), ("METADATA_WRITER", metadata_writer),
                                 ("PARSER_METHODS", current_parser), ("TITLE_KEY_GATE", current_gate)):
                generated = generated.replace("// @" + marker + "@", code)
            cpp, executable = case / "fixture.cpp", case / "fixture"
            cpp.write_text(generated)
            command = [args.cxx, "-std=c++20", "-O1", "-g", "-fno-omit-frame-pointer", "-pthread",
                       "-fsanitize=address", "-fno-pie", "-no-pie", "-I" + str(repo / "src"),
                       "-I" + str(args.cryptopp_include.resolve()),
                       "-I" + str(args.seekable_include.resolve()), str(cpp)]
            command += (["-c", "-o", str(case / "fixture.o")] if args.linker else
                        [*[str(path) for path in libraries], "-lzstd", "-o", str(executable)])
            compiled = run(command, label + "-compile")
            require(compiled.returncode == 0, label + " failed to compile")
            link_command = None
            if args.linker:
                link_command = [args.linker, "-fsanitize=address", "-no-pie", "-pthread",
                                str(case / "fixture.o"), *[str(path) for path in libraries],
                                "-lzstd", "-o", str(executable)]
                linked = run(link_command, label + "-link")
                require(linked.returncode == 0, label + " failed to link")
            executed = run([str(executable)], label + "-run")
            if expected is None:
                require(executed.returncode == 0 and b"PASS Z3DS close/save" in executed.stdout,
                        "candidate close/save assertions failed")
            else:
                require(executed.returncode == 1 and executed.stderr == expected,
                        label + " did not reject intended defect")
            proof["variants"].append({"name": label, "compile_command": command,
                                      "link_command": link_command, "compile_exit": compiled.returncode,
                                      "run_exit": executed.returncode,
                                      "expected_error": expected.decode() if expected else None})
        proof["status"] = "PASS"
        proof["scope"] = "Actual Z3DS writer/impl/metadata serialization with real seekable zstd and independent decompression; actual ticket/TMD saves and frontend key gate with controlled IO/ticket endpoints. ASan plus counted real compressor allocations/frees; LSan unavailable under sandbox ptrace. No full filesystem/SAF or device qualification."
    except Exception as error:
        proof["status"] = "FAIL"
        proof["error"] = str(error)
        raise
    finally:
        (output / "provenance.json").write_text(json.dumps(proof, indent=2) + "\n")
        print(output, flush=True)
    print("PASS Z3DS close/save: candidate and " + str(len(proof["variants"]) - 1) + " negative controls")


if __name__ == "__main__":
    main()
