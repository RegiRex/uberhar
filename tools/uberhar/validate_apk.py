#!/usr/bin/env python3
"""Fail closed on missing emulator code or a non-ARM64 package."""

import hashlib
from pathlib import Path
import shutil
import sys
import zipfile

source, destination = map(Path, sys.argv[1:])
apks = list(source.rglob("*.apk"))
if len(apks) != 1:
    raise SystemExit(f"Expected one APK in {source}; found {apks}")
apk = apks[0]
with zipfile.ZipFile(apk) as archive:
    libraries = [name for name in archive.namelist() if name.startswith("lib/")]
    print("\n".join(libraries))
    required = "lib/arm64-v8a/libcitra-android.so"
    if required not in libraries or archive.getinfo(required).file_size < 1_000_000:
        raise SystemExit("ARM64 emulator library missing or unexpectedly small")
    if any(name.split("/")[1] != "arm64-v8a" for name in libraries):
        raise SystemExit("Package contains native code for another ABI")
    if archive.testzip() is not None:
        raise SystemExit("APK failed ZIP integrity check")
destination.mkdir(parents=True, exist_ok=True)
target = destination / "uberhar-alpha1-arm64.apk"
shutil.copyfile(apk, target)
digest = hashlib.sha256(target.read_bytes()).hexdigest()
(destination / "apk-sha256.txt").write_text(f"{digest}  {target.name}\n")
print(f"Validated {apk}; SHA256 {digest}")
