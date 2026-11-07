# Retired .56-r2 same-version publication mapping

<!-- CodexAstraLocal: Archive the exact retired release exception and its matching fixture before normal version-tag publication resumes. This directory is never compiled or packaged. -->

Origin/base commit: `0723b003d92d20df45778b046672c77739e78599`. Sources: `.github/workflows/uberhar-alpha.yml` and `tools/uberhar/test_release_publication.py`. `removed.patch` records the exact removal and fixture update; `MANIFEST.json` records its SHA-256. Original repository contributors' copyright and GPLv2-or-later license remain applicable; the license is copied here.

The owner requested a corrected settings-precedence APK while retaining app version `0.1.56`. The exception mapped that version to the separate `0.1.56-r2` release tag, preserving the original `.56` and `.56-r1` assets. It explicitly expired at the next alpha. Version `0.1.57` now publishes to its own version tag; existing-release rejection and exact-source/tag checks remain in the workflow and fixture. Published historical tags/assets are not changed by this cleanup.

Restoration requires auditing both the workflow and executable publication fixture against the then-current version/revision policy. Reverse the patch only in an isolated review checkout to inspect the old behavior. Do not re-enable a mapping to an already published tag, overwrite historical releases, or redirect a future alpha to `.56-r2`. A newly authorized same-version repair needs a new unused revision tag and matched tests.
