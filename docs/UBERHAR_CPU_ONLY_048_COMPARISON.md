# Uberhar .48 CPU-only comparison

<!-- CodexAstraLocal: Define the source boundary and interpretation of the isolated
CPU experiment; this is not a replacement release or a measured performance claim. -->

This branch measures CPU preparation improvements against the original 0.1.48
graphics implementation. Its baseline is commit
`946a1bc4bcc023b768902aaebfb5200cd34e4015`. The app version remains **0.1.48**.
Use the artifact's `uberhar-commit.txt` and the `Uberhar CPU048 preparation` log
family to identify the comparison build rather than relying on its version alone.

## Included changes

<!-- CodexAstraLocal: List only implemented CPU mechanisms, keeping unchanged
admission/ordering rules explicit so the experiment remains interpretable. -->

| CPU mechanism | Changed behavior | Preserved contract |
|---|---|---|
| Exact vertex mapping cache | Retains four recurring register-to-semantic mappings, checking the last hit first. | Complete exact keys; no vertex values, guest memory or shader results are cached. |
| Shader eligibility lookup | Reuses a retained code/entry/Boolean proof before repeating CPU loader/setup preparation. | The original renderer eligibility check runs first; unknown or stale code follows the original preparation path. |
| Proof reuse during capture | Validates the accepted proof's slot generation, code owner, entry point and Boolean state. | Replacement or changed inputs use a fresh matching proof; actual FIFO misses, copies and credits are still checked. |
| Lazy worker startup | Creates the optional CPU pool and reserves its progress lane only for an accepted owned packet. | Startup precedes publication. Resource failure retains synchronous forward progress. |
| Owner bookkeeping | Avoids queue-lock traffic for counters written exclusively by the emulation owner. | Worker publication and resident-credit synchronization remain locked. |

The .48 standalone floors, pending-seed grouping, dispatcher partition, ordered
primitive output, shared CPU worker budget and GPU vertex-routing policy remain
unchanged. Later broader grouping and adaptive-history changes are excluded.
Shader generation, specialization/admission, Vulkan/OpenGL renderers, texture
caches, GPU transfers, scheduler, resolution and presentation source are unchanged.

Faster CPU preparation can naturally feed more frames or alter asynchronous
readiness. Unchanged GPU source therefore does not promise identical GPU busy
percentages; compare useful game speed and complete output alongside workload
counters. GPU utilization alone is not a measure of shader cost per frame.

## Diagnostics and validation

<!-- CodexAstraLocal TEST-ONLY LOG: The bounded runtime attribution below may be
removed after the isolated CPU mechanisms are qualified on hardware. -->

The existing progress/final reporting cadence includes exact mapping hits,
history hits/builds/evictions, proof hits/reuse/refusals, optional startup outcomes,
and sparse lookup/eligibility/proof wall times. These are actual host costs,
not estimated savings or GPU execution time. Broader capture timing overlaps
these components and must not be added to them.

Local validation passed 168,438 mapping checks, the full positive CPU packet
fixture on x64 and Android ARM64 under QEMU, and selected deliberate-defect
controls. The packet checks cover exact input/FIFO/output bytes, retained tails,
FP state, proof changes, pending-group loss, terminal failures, lazy startup and
thread-resource refusal. PICA integration passes the production compiler's syntax
check. A source audit confirms the remaining 1,878 tracked source files are
byte-identical to the .48 baseline. These checks establish the port's bounded
contracts; hardware testing determines throughput and pacing.

## Build and download

<!-- CodexAstraLocal: The owner-repository PR restores the existing base signing
cache while retaining immutable source checkout and fail-closed certificate checks. -->

Open the same-repository `uberhar/cpu-only-048-comparison` branch against
`uberhar/hybrid-shaders`. Only this exact branch is additionally permitted by
the shader and Android job guards. The comparison shader job calls the historical
branch's reusable workflow with the event's immutable head SHA as `checkout_ref`,
so newer main shader steps are not applied to historical source.

Both shader correctness and Android package validation must pass before the
comparison is ready to install. Android retains the existing signing-key restore,
certificate pin, manifest/ABI/alignment checks and source provenance. A missing or
mismatched key fails; the comparison never generates a replacement key.

Download **`uberhar-0.1.48-cpu-only-comparison-arm64`** from that Actions run. It
contains the signed APK, `apk-sha256.txt`, signature/validation evidence, exact
source commit and matching native symbols. No GitHub release or tag is created,
and the published 0.1.48 assets are preserved. The APK uses the existing package
identity; preserve app data when installing the authorized comparison.
