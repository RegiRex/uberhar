#!/usr/bin/env python3
"""CodexAstraLocal: Execute actual NCCH/CIA crypto boundaries with generated synthetic data."""
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
    parser.add_argument("--output", type=Path, default=ROOT / "build/uberhar-probe/ncch-crypto")
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
        target = output / "sources" / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        return data.decode()

    def run(command, label):
        result = subprocess.run(command, cwd=repo, capture_output=True, timeout=60,
                                env=dict(os.environ, ASAN_OPTIONS="detect_leaks=0"))
        (output / (label + ".stdout")).write_bytes(result.stdout)
        (output / (label + ".stderr")).write_bytes(result.stderr)
        return result

    try:
        read("tools/uberhar/test_ncch_crypto.py")
        read("tools/uberhar/test_installer_security.py")
        fixture = read("tools/uberhar/test_ncch_crypto.cpp")
        source = read("src/core/file_sys/ncch_container.cpp")
        header = read("src/core/file_sys/ncch_container.h")
        metadata = read("src/core/file_sys/title_metadata.cpp")
        read("src/core/file_sys/title_metadata.h")
        cia = read("src/core/file_sys/cia_container.cpp")
        cia_header = read("src/core/file_sys/cia_container.h")
        am = read("src/core/hle/service/am/am.cpp")
        for name in ("bit_field", "swap", "alignment", "common_types"):
            read("src/common/" + name + ".h")
        structures = "\n".join(definition(header, signature) + ";" for signature in (
            "struct NCCH_Header {", "struct ExeFs_SectionHeader {", "struct ExeFs_Header {"))
        structures += "\n" + definition(cia_header, "struct CIAHeader {") + ";"
        load = definition(source, "Loader::ResultStatus NCCHContainer::Load()")
        start = load.index("        bool failed_to_decrypt = false;")
        end = load.index("        if (ncch_header.content_size", start)
        # CodexAstraLocal: Execute the complete key/counter setup and the later
        # no-exheader rejection. Exheader tolerance and filesystem loading remain
        # outside this bounded fixture, rather than being imitated by stubs.
        setup = ("Loader::ResultStatus NCCHContainer::SetupCrypto() {\n" + load[start:end] +
                 definition(load, "if (is_encrypted && failed_to_decrypt)") +
                 "\nreturn Loader::ResultStatus::Success;\n}")
        section = definition(source, "Loader::ResultStatus NCCHContainer::LoadSectionExeFS(")
        lzss = "\n".join(definition(source, signature) for signature in (
            "static std::size_t LZSS_GetDecompressedSize(", "static bool LZSS_Decompress("))
        metadata_code = "\n".join(definition(metadata, signature) for signature in (
            "size_t TitleMetadata::GetContentCount()",
            "u16 TitleMetadata::GetContentIndexByIndex(",
            "const std::array<u8, 0x20>& TitleMetadata::GetContentHashByIndex(",
            "u16 TitleMetadata::GetContentTypeByIndex(",
            "u64 TitleMetadata::GetContentSizeByIndex(",
            "std::array<u8, 16> TitleMetadata::GetContentCTRByIndex(",
            "void TitleMetadata::AddContentChunk("))
        metadata_code += "\n" + definition(cia, "u64 CIAContainer::GetContentSize(")
        prepare = definition(am, "Result CIAFile::ApplyContentPreparation(")
        init_start = prepare.index("                decryption_state->content.resize(content_count);")
        init_loop = definition(prepare, "for (std::size_t i = 0; i < content_count; ++i)")
        init_end = prepare.index(init_loop, init_start) + len(init_loop)
        initialize = prepare[init_start:init_end]
        write = definition(am, "ResultVal<std::size_t> CIAFile::WriteContentData(")
        block_guard = definition(write, "if (temp.size() % HW::AES::AES_BLOCK_SIZE != 0)")
        process = "decryption_state->content[i].ProcessData(temp.data(), temp.data(), temp.size());"
        require(write.count(process) == 1, "changed CBC process seam")
        hash_update = definition(write, "if (external_transaction && i < decryption_state->content_hashes.size()")
        chunk = ("int CIAFile::ProcessChunk(std::size_t i, std::vector<u8>& temp) {\n" +
                 block_guard + "\n" + process + "\n" + hash_update + "\nreturn 0;\n}")
        finish = definition(am, "bool CIAFile::FinishCurrentContent()")
        variants = [("candidate", setup, section, metadata_code, chunk, finish, None)]
        if not args.no_mutants:
            variants.append(("missing-material-accepted", replace_once(setup,
                "if (is_encrypted && failed_to_decrypt)", "if (false)"), section, metadata_code, chunk, finish,
                b"FAIL missing primary key rejected without exheader\n"))
            variants.append(("wrong-icon-key", setup, replace_once(section,
                'if (section_name == "icon" || section_name == "banner")', 'if (false)'),
                metadata_code, chunk, finish, b"FAIL ExeFS section plaintext matches\n"))
            variants.append(("position-bitmap", setup, section, replace_once(metadata_code,
                "cia_header.IsContentPresent(cia_tmd.GetContentIndexByIndex(index))",
                "cia_header.IsContentPresent(index)"), chunk, finish,
                b"FAIL sparse CIA uses encoded presence index\n"))
            variants.append(("skip-content-integrity", setup, section, metadata_code, chunk,
                replace_once(finish,
                    "if (digest != container.GetTitleMetadata().GetContentHashByIndex(current_content_index))",
                    "if (false)"), b"FAIL wrong CBC key rejected by content hash\n"))
        library = args.cryptopp_library.resolve()
        proof["cryptopp"] = {"library": str(library), "sha256": hashlib.sha256(library.read_bytes()).hexdigest(),
                              "headers": str(args.cryptopp_include.resolve())}
        for label, current_setup, current_section, current_metadata, current_chunk, current_finish, expected in variants:
            case = output / label
            case.mkdir()
            generated = fixture
            for marker, code in (("STRUCTURES", structures), ("SETUP", current_setup),
                                 ("SECTION", current_section), ("LZSS", lzss),
                                 ("METADATA", current_metadata), ("CBC_INIT", initialize),
                                 ("CBC_CHUNK", current_chunk), ("CIA_FINISH", current_finish)):
                generated = generated.replace("// @" + marker + "@", code)
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
                require(executed.returncode == 0 and b"PASS NCCH/CIA crypto" in executed.stdout,
                        "candidate NCCH/CIA assertions failed")
            else:
                require(executed.returncode == 1 and executed.stderr == expected,
                        label + " did not reject intended defect")
            proof["variants"].append({"name": label, "compile_command": command,
                                      "link_command": link_command, "compile_exit": compiled.returncode,
                                      "run_exit": executed.returncode,
                                      "expected_error": expected.decode() if expected else None})
        proof["status"] = "PASS"
        proof["scope"] = "Actual NCCH setup/section reader and CIA CTR/sparse-index/CBC/hash sections, real CryptoPP and ASan, controlled supplied-slot/seed/IO/finalizer endpoints. Synthetic data only; does not validate console key scrambling, full installer/loader, plaintext-exheader tolerance or device behavior."
    except Exception as error:
        proof["status"] = "FAIL"
        proof["error"] = str(error)
        raise
    finally:
        (output / "provenance.json").write_text(json.dumps(proof, indent=2) + "\n")
        print(output, flush=True)
    print("PASS NCCH/CIA crypto: candidate and " + str(len(proof["variants"]) - 1) + " negative controls")


if __name__ == "__main__":
    main()
