#!/usr/bin/env python3
"""AstraEH: Fail closed on missing emulator code or a non-ARM64 package."""

import hashlib
from pathlib import Path
import shutil
import struct
import sys
import zipfile

source, destination = map(Path, sys.argv[1:])
apks = list(source.rglob("*.apk"))
# AstraEH: AGP may publish a redirect to an intermediate APK instead of copying it into
# outputs/apk. Accept identical copies, but reject ambiguous different builds.
unique = {hashlib.sha256(path.read_bytes()).hexdigest(): path for path in apks}
if len(unique) != 1:
    raise SystemExit(f"Expected one unique APK in {source}; found {apks}")
apk = next(iter(unique.values()))
with zipfile.ZipFile(apk) as archive:
    libraries = [name for name in archive.namelist() if name.startswith("lib/")]
    print("\n".join(libraries))
    required = "lib/arm64-v8a/libcitra-android.so"
    if required not in libraries or archive.getinfo(required).file_size < 1_000_000:
        raise SystemExit("ARM64 emulator library missing or unexpectedly small")
    if any(name.split("/")[1] != "arm64-v8a" for name in libraries):
        raise SystemExit("Package contains native code for another ABI")
    # AstraEH: ABI directory names alone do not prove the libraries contain AArch64 machine code.
    for name in libraries:
        if not name.endswith(".so"):
            continue
        with archive.open(name) as library:
            header = library.read(64)
        if (
            len(header) != 64
            or header[:6] != b"\x7fELF\x02\x01"
            or struct.unpack_from("<H", header, 18)[0] != 183
        ):
            raise SystemExit(f"Native library is not a little-endian AArch64 ELF: {name}")
    if archive.testzip() is not None:
        raise SystemExit("APK failed ZIP integrity check")
# AstraEH: Publish one stable filename and a checksum only after the package passes every gate.
destination.mkdir(parents=True, exist_ok=True)
target = destination / "uberhar-alpha1a-arm64.apk"
shutil.copyfile(apk, target)
digest = hashlib.sha256(target.read_bytes()).hexdigest()
(destination / "apk-sha256.txt").write_text(f"{digest}  {target.name}\n")
print(f"Validated {apk}; SHA256 {digest}")
