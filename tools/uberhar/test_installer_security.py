#!/usr/bin/env python3
"""CodexAstraLocal: Execute the production AM/parser security boundaries with synthetic bytes.

The actual parser layouts and selected production methods run under ASan/UBSan
(except alignment, because inherited packed endian-wrapper access is unaligned);
only AM result transport and logging are doubles. No keys, titles or device access
are required. This does not exercise the full asynchronous AM service lifecycle.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def definition(source, signature):
    # CodexAstraLocal: Bind extraction to one exact overload and balance lexical
    # braces so comments and diagnostic format strings cannot move the boundary.
    require(source.count(signature) == 1, "ambiguous extraction: " + signature)
    start = source.index(signature)
    begin = source.index("{", start)
    depth, state, at = 0, "code", begin
    while at < len(source):
        char, pair = source[at], source[at:at + 2]
        if state == "line":
            if char == "\n":
                state = "code"
        elif state == "block":
            if pair == "*/":
                state = "code"
                at += 1
        elif state in ('"', "'"):
            if char == "\\":
                at += 1
            elif char == state:
                state = "code"
        elif pair == "//":
            state = "line"
            at += 1
        elif pair == "/*":
            state = "block"
            at += 1
        elif char in ('"', "'"):
            state = char
        elif char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[start:at + 1]
        at += 1
    raise RuntimeError("unterminated extraction: " + signature)


def replace_once(source, old, new):
    require(source.count(old) == 1, "changed negative-control seam: " + old)
    return source.replace(old, new, 1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=ROOT)
    parser.add_argument("--output", type=Path,
                        default=ROOT / "build/uberhar-probe/installer-security")
    parser.add_argument("--cxx", default=os.environ.get("CXX", "g++"))
    parser.add_argument("--linker", help="Optional compiler driver providing sanitizer runtimes")
    parser.add_argument("--no-mutants", action="store_true")
    args = parser.parse_args()
    repo = args.source_root.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix="run-", dir=args.output.resolve()))
    proof = {"author": "CodexAstraLocal", "status": "INCOMPLETE", "inputs": {}, "variants": []}

    def read(relative):
        path = repo / relative
        data = path.read_bytes()
        proof["inputs"][relative] = hashlib.sha256(data).hexdigest()
        saved = output / "sources" / relative
        saved.parent.mkdir(parents=True, exist_ok=True)
        saved.write_bytes(data)
        return data.decode()

    def run(command, label, timeout=60):
        result = subprocess.run(command, cwd=repo, capture_output=True, timeout=timeout,
                                env=dict(os.environ, ASAN_OPTIONS="detect_leaks=0",
                                         UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1"))
        (output / (label + ".stdout")).write_bytes(result.stdout)
        (output / (label + ".stderr")).write_bytes(result.stderr)
        return result

    try:
        read("tools/uberhar/test_installer_security.py")
        fixture = read("tools/uberhar/test_installer_security.cpp")
        am = read("src/core/hle/service/am/am.cpp")
        ncch = read("src/core/file_sys/ncch_container.cpp")
        ticket = read("src/core/file_sys/ticket.cpp")
        metadata = read("src/core/file_sys/title_metadata.cpp")
        ticket_header = read("src/core/file_sys/ticket.h")
        read("src/core/file_sys/title_metadata.h")
        signature = read("src/core/file_sys/signature.h")
        for header in ("common_types", "common_funcs", "swap", "alignment"):
            read("src/common/" + header + ".h")

        maximum = re.search(r"constexpr u64 MAX_IMPORT_FILE_SIZE = [^;]+;", am)
        require(maximum is not None, "missing production import bound")
        am_code = maximum[0] + "\n" + "\n\n".join(
            definition(am, "ResultVal<std::size_t> " + name + "::Read(")
            for name in ("CIAFile", "TicketFile", "TMDFile", "ContentFile"))
        am_code += "\n\n" + "\n\n".join(
            definition(am, "ResultVal<std::size_t> " + name + "::Write(")
            for name in ("TicketFile", "TMDFile"))
        # CodexAstraLocal: Exercise the real CIA overflow check and metadata
        # buffering lambda before the separate installer state-machine boundary.
        cia_write = definition(am, "ResultVal<std::size_t> CIAFile::Write(")
        cia_boundary = "    if (install_state == CIAInstallState::InstallStarted)"
        require(cia_write.count(cia_boundary) == 2, "changed CIA write state boundary")
        cia_prefix = cia_write[cia_write.index("{") + 1:cia_write.index(cia_boundary)]
        am_code += ("\nResultVal<std::size_t> CIAFile::BufferMetadataFixture(u64 offset, "
                    "std::size_t length, const u8* buffer, u64 limit) {" + cia_prefix +
                    "\n    buffer_data(limit);\n    return length;\n}\n")
        lzss_code = "\n\n".join(definition(ncch, sig) for sig in (
            "static std::size_t LZSS_GetDecompressedSize(", "static bool LZSS_Decompress("))
        parser_code = "\n\n".join((
            definition(signature, "inline u32 GetSignatureSize("),
            definition(ticket, "Loader::ResultStatus Ticket::Load(std::span<const u8>"),
            definition(ticket, "void Ticket::ContentIndex::Initialize()"),
            definition(ticket, "bool Ticket::ContentIndex::HasRights("),
            definition(metadata, "Loader::ResultStatus TitleMetadata::Load(std::span<const u8>"),
            definition(metadata, "size_t TitleMetadata::GetContentCount()"),
            definition(metadata, "u32 TitleMetadata::GetContentIDByIndex(")))
        # CodexAstraLocal: Preserve real signature enum values with the real
        # sizing function; logging remains a silent controlled endpoint.
        signature_enum = re.search(r"enum SignatureType : u32 \{.*?\};", signature, re.S)
        require(signature_enum is not None, "missing production signature enum")
        parser_code = signature_enum[0] + "\n" + parser_code
        variants = [("candidate", am_code, parser_code, ticket_header, None)]
        if not args.no_mutants:
            # CodexAstraLocal: Each deliberate defect must compile and fail the
            # named behavioral assertion; compile failure is never a rejection.
            bad_read = replace_once(am_code,
                definition(am_code, "ResultVal<std::size_t> TicketFile::Read("),
                "ResultVal<std::size_t> TicketFile::Read(u64, std::size_t length, u8*) const { return length; }")
            variants.append(("read-reports-success", bad_read, parser_code, ticket_header,
                             b"FAIL TicketFile rejects reads\n"))
            variants.append(("retain-old-rights", am_code, parser_code,
                             replace_once(replace_once(ticket_header,
                                 "            initialized = false;", ""),
                                 "            rights.clear();", ""),
                             b"FAIL rights reset on reload\n"))
            variants.append(("append-old-tmd-chunks", am_code,
                             replace_once(parser_code, "    tmd_chunks.clear();", ""), ticket_header,
                             b"FAIL TMD reload replaces chunks\n"))
            variants.append(("omit-cia-wrap-check", replace_once(am_code,
                             "    if (length > std::numeric_limits<u64>::max() - offset) {\n"
                             "        return FileSys::ResultWriteBeyondEnd;\n    }", ""),
                             parser_code, ticket_header,
                             b"FAIL CIA wrapped metadata write rejected\n"))
        for label, am_variant, parser_variant, ticket_variant, expected in variants:
            case = output / label
            case.mkdir()
            # CodexAstraLocal: The override contains the exact saved header,
            # except the explicitly recorded negative-control change.
            header_path = case / "core/file_sys/ticket.h"
            header_path.parent.mkdir(parents=True)
            header_path.write_text(ticket_variant)
            generated = fixture.replace("// @AM_METHODS@", am_variant)
            generated = generated.replace("// @LZSS_METHODS@", lzss_code)
            generated = generated.replace("// @PARSER_METHODS@", parser_variant)
            source = case / "fixture.cpp"
            source.write_text(generated)
            executable = case / "fixture"
            # CodexAstraLocal: Existing packed endian-wrapper member access is
            # intentionally unaligned. Report that coverage exclusion explicitly;
            # bounds, lifetime and all other undefined-behavior checks stay active.
            command = [args.cxx, "-std=c++20", "-O1", "-g", "-fno-omit-frame-pointer",
                       "-fsanitize=address,undefined", "-fno-sanitize=alignment", "-fno-pie", "-no-pie",
                       "-I" + str(case), "-I" + str(repo / "src"), str(source)]
            # CodexAstraLocal: A separate driver can supply compatible sanitizer
            # runtimes when the host compiler package lacks its runtime libraries.
            # Both exact commands remain evidence; this never disables sanitizers.
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
            executed = run([str(executable)], label + "-run")
            if expected is None:
                require(executed.returncode == 0, "candidate security assertions failed")
                require(b"PASS installer security boundaries" in executed.stdout,
                        "missing candidate completion marker")
            else:
                require(executed.returncode == 1 and executed.stderr == expected,
                        label + " did not reject the intended defect")
            proof["variants"].append({"name": label, "compile_command": command,
                                      "link_command": link_command,
                                      "compile_exit": compiled.returncode,
                                      "run_exit": executed.returncode,
                                      "expected_error": expected.decode() if expected else None})
        proof["status"] = "PASS"
        proof["scope"] = "Production AM read/write, CIA metadata buffering prefix and LZSS/ticket/TMD methods; synthetic bytes; ASan/UBSan except alignment of inherited packed endian wrappers. No async service, crypto, APK or device qualification."
    except Exception as error:
        proof["status"] = "FAIL"
        proof["error"] = str(error)
        raise
    finally:
        (output / "provenance.json").write_text(json.dumps(proof, indent=2) + "\n")
        print(output, flush=True)
    print("PASS installer security: candidate and " + str(len(proof["variants"]) - 1) + " negative controls")


if __name__ == "__main__":
    main()
