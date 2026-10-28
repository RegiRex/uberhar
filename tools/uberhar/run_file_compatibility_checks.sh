#!/usr/bin/env bash
# CodexAstraLocal: Gate the four dependency-backed synthetic file-compatibility
# fixtures. build_probe.sh separately owns installer security and CIA transaction.
set -euo pipefail

compat_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
cd -- "$compat_root"
compat_build="$compat_root/build/uberhar-compat-deps"
compat_dependencies_only=false
compat_linker=()
while [ "$#" -gt 0 ]; do
    case "$1" in
        --build-dir)
            compat_build="${2:?--build-dir requires a directory}"
            shift 2
            ;;
        --dependencies-only)
            compat_dependencies_only=true
            shift
            ;;
        --linker)
            # CodexAstraLocal: Local hosts may supply sanitizer runtimes through
            # another driver; hosted Linux uses each fixture's ordinary g++ path.
            compat_linker=(--linker "${2:?--linker requires a compiler driver}")
            shift 2
            ;;
        *)
            printf 'Unknown argument: %s\n' "$1" >&2
            exit 2
            ;;
    esac
done
mkdir -p -- "$compat_build"
compat_build="$(cd -- "$compat_build" && pwd)"
compat_logs="$compat_root/build/uberhar-probe/file-compatibility-gate"
mkdir -p -- "$compat_logs"

# CodexAstraLocal: Require the checkout's gitlink revisions; never fetch moving
# dependency heads or silently use unversioned system Crypto++/seekable headers.
for compat_dep in cryptopp zstd; do
    compat_expected="$(git rev-parse "HEAD:externals/$compat_dep")"
    compat_actual="$(git -C "externals/$compat_dep" rev-parse HEAD)"
    test "$compat_actual" = "$compat_expected"
    printf '%s %s\n' "$compat_dep" "$compat_actual"
done | tee "$compat_logs/dependency-revisions.log"

# CodexAstraLocal: One reusable static Release build serves all four fixtures.
# Keep dependency compiler output, including warnings, available on gate failure.
timeout 120s cmake -S externals/cryptopp -B "$compat_build/cryptopp" \
    -DCMAKE_BUILD_TYPE=Release -DCRYPTOPP_BUILD_TESTING=OFF \
    -DCRYPTOPP_BUILD_SHARED=OFF -DCRYPTOPP_INSTALL=OFF \
    2>&1 | tee "$compat_logs/cryptopp-configure.log"
timeout 600s cmake --build "$compat_build/cryptopp" --config Release \
    --target cryptopp --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-2}" \
    2>&1 | tee "$compat_logs/cryptopp-build.log"

# CodexAstraLocal: Inline pinned xxhash so the seekable archive never depends on
# private ZSTD_XXH symbols hidden by the system libzstd used by the Z3DS fixture.
compat_zstd="$compat_root/externals/zstd"
compat_seekable="$compat_zstd/contrib/seekable_format"
for compat_unit in zstdseek_compress zstdseek_decompress; do
    timeout 60s "${CC:-cc}" -O2 -DXXH_INLINE_ALL \
        -I"$compat_zstd/lib" -I"$compat_zstd/lib/common" -I"$compat_seekable" \
        -c "$compat_seekable/$compat_unit.c" -o "$compat_build/$compat_unit.o" \
        2>&1 | tee "$compat_logs/$compat_unit.log"
done
"${AR:-ar}" rcs "$compat_build/libzstd_seekable.a" \
    "$compat_build/zstdseek_compress.o" "$compat_build/zstdseek_decompress.o"
compat_crypto=(--cryptopp-library "$compat_build/cryptopp/libcryptopp.a"
               --cryptopp-include "$compat_root/externals/cryptopp/include")
sha256sum "$compat_build/cryptopp/libcryptopp.a" "$compat_build/libzstd_seekable.a" \
    > "$compat_logs/dependency-libraries.sha256"
if "$compat_dependencies_only"; then
    exit 0
fi

# CodexAstraLocal: Preserve every default negative control. No console keys or
# game content are inputs; each fixture produces its own synthetic data/proof.
for compat_fixture in romfs_crypto_reader ncch_crypto ncch_install_integrity; do
    timeout 600s python3 "tools/uberhar/test_$compat_fixture.py" \
        "${compat_crypto[@]}" "${compat_linker[@]}" \
        2>&1 | tee "$compat_logs/$compat_fixture.log"
done
timeout 600s python3 tools/uberhar/test_z3ds_close.py \
    "${compat_crypto[@]}" "${compat_linker[@]}" \
    --seekable-library "$compat_build/libzstd_seekable.a" \
    --seekable-include "$compat_seekable" \
    2>&1 | tee "$compat_logs/z3ds_close.log"
