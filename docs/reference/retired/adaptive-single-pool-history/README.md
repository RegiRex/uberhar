# Retired single-pool adaptive history wiring

<!-- CodexAstraLocal: Record the superseded admission wiring outside built sources; the static worker pool and safe comparison policy remain live. -->

Source commit: `0723b003d92d20df45778b046672c77739e78599`. Source: `src/common/uberhar_parallel_work.h`, `ParallelWork::RunImpl`.
Reference excerpt SHA-256: `bb6b03dbd77223405d1697bb0f997239dee9bdeeb0a0f424f3aa48f51c977c49` (excerpt before added archival header).
License/authorship: Uberhar contributors, GPLv2-or-later; preserve original purpose/author notices.

The former callsite used one exact-shape history for each pool. Interleaved shapes reset its comparison windows; inspected .56 cohorts reported zero samples/trials despite hundreds of thousands of unstable exclusions. .57 keeps exact work identities in a fixed two-way cohort table and allows adaptive OFF to bypass the table while retaining static worker execution.

This file is historical evidence, not an alternative implementation to compile. Restoring the old callsite alone is unsafe after the API change: restore matching `AdaptiveParticipation`, pool signatures, owners, diagnostic fields and fixtures from this commit together. Prefer the supported adaptive-OFF setting for a static comparison.

Cleanup audit of the owned worker/primitive/append files found no inactive commented-out function bodies to delete. Explanatory arithmetic, state ownership comments and live fallback paths were retained. The newly obsolete inner capacity-adjustment helper was removed; capacity invalidation belongs to the cohort owner. No performance gain is claimed from documentation cleanup.
