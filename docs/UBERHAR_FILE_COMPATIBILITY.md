# Installer safety and encrypted file compatibility

<!-- CodexAstraLocal: Pin the narrowly selected upstream ports and their validation boundary. -->
The development base is submitted Uberhar 0.1.55 commit
`db72da0a3704510d66c943d33fabd097928af845`, tree
`869d878985b3ae4e0e1f596949b710c27dab0695`. Its local backup sibling and all
pre-existing worktree changes remain preserved.

The installer security port applies the complete functional nine-file change from
[Azahar PR 2648](https://github.com/azahar-emu/azahar/pull/2648), head
`8b1e42b97598685d7f2e023580dc37825e51dba2`, merged as
`2536381fe4a160964f26130e0088e43f49e2a394`. Upstream authorship remains in the source.
The nine donor files retain their GPLv3-or-later notices; the upstream license
text is included verbatim at `misc/licenses/gplv3.txt` (upstream blob
`e7fc5225c02e97b2563f9342b3c667c223f47fd1`). The Plus RomFS reader
files retain their GPLv2-or-later notices. The port
includes AM input bounds and async ownership, ticket rights parsing, metadata
reload bounds, NCCH decompression validation, and the FS initialized-read clamp.
A local subtraction-first CIA metadata-buffer clamp additionally prevents integer
wrap before copying whole-CIA input.

The following compatibility step uses only the relevant file-loading and installer
paths from [Azahar Plus](https://github.com/AzaharPlus/AzaharPlus/tree/cf9068844820f819a6eabbeeeb6a70d546e3e85a)
commit `cf9068844820f819a6eabbeeeb6a70d546e3e85a`. Existing user-supplied AES keys
and seed facilities are the only sources of decryption material. No keys or seeds
are distributed. Existing console-unique installed storage remains supported.
Rendering, Android minimum version, cheats and network features are outside scope.

Validation is finite and tied to the changed paths: source-extracted executable
malformed-input and lifetime fixtures, synthetic encrypted/decrypted reader data,
the real changed translation units with the pinned Android NDK, and focused
extension/filter checks if changed. Compilation and synthetic fixtures do not
establish real-game installation or gameplay qualification. APK generation,
publication and device testing are separate owner-directed steps.


<!-- CodexAstraLocal: Record local safety adaptations required by the newly accepted input paths. -->
The compatibility port keeps nested console-unique/Z3DS input and the existing
installed-output wrappers. External imports deliberately enable supplied-key
crypto without changing guest HLE authorization. Ticket, TMD and provided content
files stage beside their final paths; only finalized outputs are promoted, with
rename rollback and retained recovery backups on recovery failure. Ordinary
unencrypted installs keep plaintext output when their initial header identifies
it. Missing optional contents remain untouched. Encrypted CIA content hashes
reject wrong common keys and corruption before promotion; sparse presence masks
use the TMD's encoded content indices.

Missing seeds/keys, unsupported crypto slots/versions, malformed block sizes,
short reads/writes and failed output finalization return errors. Decrypted NCCH
exheader, ExeFS superblock/section and RomFS hash regions are verified before
promotion, so a present but wrong inner key cannot silently replace installed
content. Compressor finalization is idempotent and propagates footer/header errors;
ticket and TMD saves propagate final close errors. Plain personalized tickets do
not require unused title-key/console material. NCCH/CCI and
ExeFS/RomFS ranges use widened arithmetic and bounded names/allocations. Existing
compression wrappers handle compatible `.zcci`, `.zcxi` and `.zcia` input; generic
ZIP/7z archives are outside this feature. RomFS save-state crypto fields are
versioned so the old plaintext-reader layout remains readable.

Focused executable fixtures are `test_installer_security.py`,
`test_cia_install_transaction.py`, `test_romfs_crypto_reader.py` and
`test_ncch_crypto.py`, `test_ncch_install_integrity.py` and
`test_z3ds_close.py` under `tools/uberhar`. They use synthetic bytes and real
filesystem/AES endpoints where applicable, with explicit doubles for service
transport and supplied-key lookup. The Z3DS fixture also decodes real compressed bytes after finalization. These
checks do not replace real Android SAF, complete AM async scheduling or handheld
installation/gameplay qualification. A process/power interruption during staged
promotion can leave recovery backups; the transaction is not a crash journal.


<!-- CodexAstraLocal: Report host evidence separately from device/release acceptance. -->
The source candidate passed all six focused fixtures (including compiled negative
controls), eight affected real C++ units with pinned Android r27d ARM64/libc++, and
Android resource compilation. The independent correction review found no remaining
blocker in its stated scope. Runtime permission/storage-provider behavior, real
handheld installation and gameplay, and the existing gated release workflow remain
separate qualification; no APK was generated or installed by this source task.
