# Architecture audit before the next Dark Moon candidate

<!-- CodexAstraLocal: Reconstruct the lost local session from Git, retained files and evidence without relabeling historical authorship or treating this audit as device qualification. -->

Reviewed October 7, 2026 UTC, in the resumed October 6 local session. Released
source anchor: **0.1.23 / `99fe4a8851ff696219dddda58fb154fbf8ce1149`**. Guidance
anchor: **`814de4d60`**, including the owner's correction that Native has no
observed moon flashing. Change-inventory baseline: **0.1.22 /
`d742cd5e9e90b7ba3f67367460ef2c7d3f27b5bc`**. The previous full architecture
review used **0.1.20 / `c75e544f7`**; this review rechecks its decisions after
the three successors 0.1.21, 0.1.22 and 0.1.23.

**Continue correctness isolation before widening GPU admission or claiming a
throughput milestone.** The retained mode-4 experiment reports moon flashing
even with zero optional fragment modules. The separately observed ghost defect
is not cleared by a different camera pose. A reproduced CPU/GPU output-W
disagreement justifies narrow containment, but it has not been tied to a captured
Dark Moon draw. The published opening measurements remain far below 100% speed.

This is a full architecture review across the active renderer routes, ownership,
diagnostics, Android lifecycle, delivery gates and alternatives. It is not a
claim that every inherited line, guest instruction, Android provider or driver
failure has been exhaustively verified. Native shader arithmetic and the
inherited JIT implementations were not rewritten. The output-contract candidate
has a separate implementation/review owner; the coordinator owns all device
inputs, installation, publication and final integration.

## Changes since 0.1.22

<!-- CodexAstraLocal: Inventory tracked release/guidance changes separately from restored local work and this audit's new changes; ordinary Git diff does not include untracked files. -->

The fetched history contains four commits after the requested baseline:

| Commit | Actual scope |
| --- | --- |
| `99fe4a885` | 0.1.23 optional shader optimization, its queued-job regression, device-readiness tooling, memory analysis, version and release guidance. |
| `7a1b2717d` | Published 0.1.23 build/gate status in progress and roadmap records. |
| `0dabb2f09` | Local autonomous takeover, archived device report and initial bounded-batch guidance. |
| `814de4d60` | Owner-corrected Combo-only moon flashing; temporal comparison and separate ghost diagnosis. |

The complete logical inventory at review start is below. Paths grouped in a row
share a purpose; none of these rows assigns old code to the current reviewer.

| Paths | Change and present interpretation |
| --- | --- |
| `src/video_core/renderer_vulkan/uberhar_shader_compile_policy.h`, `vk_shader_disk_cache.cpp` | Optional ready-only VS/GS/FS jobs enable frontend optimization. VS/GS queue immutable optimizer options; GS also snapshots clip support. Required/generic and Custom settings remain separate. A bounded policy field identifies the effective option. No shader arithmetic, draw ordering, cache eviction or GPU byte budget was added. |
| `tools/uberhar/test_shader_compile_policy.py`, `tools/uberhar/build_probe.sh` | Execute extracted production queued jobs with recording compiler doubles and require the policy fixture in the host gate. This verifies policy/snapshot/completion behavior, not Adreno rendering. |
| `tools/uberhar/device_testing/device_probe.py`, `test_device_probe.py` | Bounded read-only local ADB readiness, explicit selection for multiple devices, authorization state, installed package/version and exclusive report creation; fake-server coverage. The probe does not launch/replay a scene or prove APK signing compatibility. |
| `tools/uberhar/device_testing/README.md`, `LOCAL_CODEX_HANDOFF.md` | Local USB, settings, capture, install/signing and single-device-owner operating instructions. |
| `UBERHAR_VERSION`, `docs/releases/0.1.23.md` | Version 0.1.23 and scoped release/validation notes. Numeric machine-readable version data remains comment-free. |
| `docs/UBERHAR_LOG_ANALYSIS_0.1.22.md`, `docs/assets/darkmoon-022-memory-pipelines.svg` | Derived process/KGSL/pipeline evidence, normal-exit recovery and explicitly correlated allocation models. The generated SVG is evidence, not runtime code. |
| `docs/device_reports/UBERHAR_DEVICE_TEST_2026-10-06.md` | Distinct archived report, followed by the owner's temporal correction; the original local report is preserved separately. |
| `AGENTS.md`, `UBERHAR.md`, `docs/UBERHAR_LOCAL_AUTONOMOUS_HANDOFF.md` | Development ownership, authorized fork workflow, corrected symptoms and local protocol. The newer direct owner instruction supersedes the historical four-hour/three-build stop. |
| `docs/UBERHAR_PROGRESS.md`, `docs/UBERHAR_CODE_MAP.md`, `docs/UBERHAR_REVIEW_CADENCE.md`, `docs/Uberhar_Roadmap.html` | Release/evidence reconciliation and resumed-work checkpoints, including restored staged/unstaged changes. Their historical statements retain their original attribution. |
| Local `vk_rasterizer.cpp/.h`, new `uberhar_gpu_output_policy.h` | Uncommitted output-W admission containment recovered from the prior session, now under focused independent review. It was not part of the released 0.1.23 APK. Its final tests and revision must be recorded with the candidate. |
| Current `tools/uberhar/test_gpu_output_guard.cpp`, `test_gpu_output_guard.py`, `test_gpu_input_parity.py`, `build_probe.sh` | Output-guard semantic/write-decoder/memo and production-route regressions, including the existing input-parity fixture's support for the added early guard. The complete host probe gate passes; device qualification remains separate. |
| Current `.github/workflows/uberhar-shaders.yml` | Execute the new generated output-W render regression in shader CI after the production CPU/admission probe. Local llvmpipe execution passes; the new candidate's CI result is still pending. |
| Current `UBERHAR_VERSION`, `docs/releases/0.1.24.md`, `docs/UBERHAR_DIAGNOSTICS.md` | Coordinator selects the next candidate number and records output-guard scope, bounded diagnostics and remaining qualification gates. This version edit does not assert Android publication or a device cure. |
| Current `docs/UBERHAR_MODE4_ANALYSIS_2026-10-07.md`, `UBERHAR.md`, `docs/Uberhar_Roadmap.html` | Retained experiment analysis and refreshed current status distinguish completed mode-4/host evidence from the pending 0.1.24 device test, preserving historical snapshots, current cold-cache scope and every-three-build review cadence. |
| Local `docs/UBERHAR_DEVICE_TEST_2026-10-06.md`, `docs/UBERHAR_LOCAL_BATCH_2026-10-06.md` | Original report and durable existing-build experiment ledger. Private logs, captures and snapshots remain under ignored `build/device-testing/`. |
| Current `.github/workflows/uberhar-alpha.yml`, `tools/uberhar/test_release_publication.py`, this audit | Correct unconditional prerelease classification, exercise the actual publication shell offline, and record this architecture review. The output-guard owner also wires the publication regression into `build_probe.sh`. |

Most post-0.1.22 additions already have purpose comments marked
`CodexAstraUlt`. Those historical markers stay intact. This review adds the
missing `CodexAstraLocal` readiness-contract clarification above `probe()`;
new publication logic, regression sections and this audit use the current
owner-requested marker. It does not add one comment per line, edit private game
payloads, or modify the original device report. Parallel output-guard edits and
coordinator documentation updates are additional current work, not retroactive
0.1.23 changes. The coordinator's pinned submodule initialization is build setup;
dependency pointer changes must not enter a candidate accidentally.

## Architecture and correctness contracts

<!-- CodexAstraLocal: Recheck actual production paths against the 0.1.20 review rather than inferring behavior from the labels Native, Compute or Combo. -->

| Area inspected | Contract and assessment |
| --- | --- |
| `common/uberhar_test_profile.h`, `pica/pica_core.cpp`, `renderer_vulkan/uberhar_gpu_vertex_policy.h` | Native selects CPU JIT vertices and generic covered fragments; it is not the inherited default Azahar hardware-vertex mode. Both Combo presets attempt eligible complete GPU draws and preserve the complete CPU draw on rejection. Mode 4 removes optional fragment specialization but can promote a different population because dependencies differ. Geometry/partial assembly/winding, input-layout and quaternion guards remain necessary. |
| `pica/uberhar_vertex_input.h`, `uberhar_vertex_output.h`, `uberhar_vertex_plan_cache.h` and `PicaCore::LoadVertices` | Fused live input transport, exact-value mapping-plan reuse and the within-draw 64-entry final-vertex FIFO remain useful. Register state belongs to a CPU batch, and transformed vertices do not persist across draws. Unsupported/debug/geometry paths retain recovery. No basis was found to remove these optimizations or infer that CPU arithmetic is now cheap. |
| `vk_rasterizer.cpp`, `vk_pipeline_cache.cpp`, `vk_graphics_pipeline.*`, `vk_shader_disk_cache.*` | GPU preflight, dependency publication and final execution-state matching remain separate gates. Optional modules/pipelines have stable owners and failed completion; no pending/failed candidate may replace the CPU draw. Full specialized recovery still handles unsupported generic fragment states. A ready pipeline is not evidence of numerical parity or lower execution cost. |
| `glsl_shader_gen.cpp`, `pica/shader_unit.h`, output transport | GPU output vectors start with W=1 each invocation; the CPU ShaderUnit starts at zero and persists within a batch. The recovered guard addresses a proven never-written consumed-W case. It does not establish general partial-component, temporary-register or flow-dependent parity. Do not replace all GPU initial values with zero and claim the persistence contract is solved. |
| `glsl_fs_shader_gen.cpp`, `uberhar_tev_preparation.h`, `UseFragmentShader` and `BindPipeline` | Generic runtime TEV/ordered lighting and specialized fragments share accurate recovery, but require different transport. Successful specialized GPU promotion may bypass dynamic constants only because any rejected attempt re-enters CPU preparation. Fragment fixtures cover synthetic color/depth/discard; they do not identify which guest draw caused a temporal title defect. |
| `uberhar_shader_compile_policy.h`, queued VS/GS/FS jobs | The 0.1.23 policy is correctly scoped to optional background compilation. Profile/options are captured before workers run, and `ShaderDiskCache::Init` keeps preset optional modules out of Custom transferable caches. No new shader-generator source was introduced by that policy; changed generator sources still require fresh CMake configuration for cache identity. |
| `vk_resource_pool.*`, `vk_stream_buffer.*`, `vk_memory_diagnostics.h`, `vk_instance.*` | The refreshed completion-tick repair and common partial/full stream cleanup are retained and reproduced by host tests. Accounting follows successful allocations and frees. Pool/set/command capacity counts and explicit Vulkan bytes do not account for all opaque driver allocations. |
| `vk_pipeline_cache.cpp`, `rasterizer_cache/rasterizer_cache.h`, `vk_texture_runtime.*` | Cache replacement drains command/compiler users before destroying borrowed optional pipelines/modules. Normal surface retirement respects completion ticks. Existing save-state `ClearAll` and texture move-assignment debts remain separate from the opening diagnosis. No speculative in-flight eviction is selected. |
| Android `UberharDeviceDiagnostics`, `UberharProcessMemory`, `UberharKernelMemory`, `CrashSessionLogs` | Existing 30-second health sampling and before/after native-run records keep units/scopes separate and tolerate unavailable data. Ordinary logging retains one writer and incident-only recovery; there is no new per-frame Android service or capture worker. Missing thermal services leave headroom unknown. |
| `.github/workflows/uberhar-alpha.yml`, `uberhar-shaders.yml`, APK/manifest validators | Android, shader, package, alignment, signature pin, source SHA, artifact checksum and existing-version/tag gates remain required. Compilation jobs are read-only; only publication writes a release. The audit found and corrected unconditional prerelease classification, described below. |

## Findings and selected work

<!-- CodexAstraLocal: Prioritize observed contract/evidence gaps, distinguish inherited debt from new regressions, and avoid presenting proposals as measured improvements. -->

| Priority / state | Finding, source and required action |
| --- | --- |
| P1 / correctness gate open | GPU output initialization at [`glsl_shader_gen.cpp:295`](../src/video_core/shader/generator/glsl_shader_gen.cpp#L295) differs from the CPU batch lifetime at [`pica_core.cpp:1339`](../src/video_core/pica/pica_core.cpp#L1339). Retain narrow proven containment, require an old-code-failing regression and measure its actual title incidence. A captured identical draw replay is needed before attributing moon or ghost corruption to this specific defect. |
| P1 / memory gate open | [`uberhar_gpu_vertex_policy.h:19`](../src/video_core/renderer_vulkan/uberhar_gpu_vertex_policy.h#L19) limits optional pipelines to 256 objects, not bytes. The 0.1.22 and retained local reports show multi-GiB live KGSL scope associated with pipeline populations and recovery on normal exit. Optimized shaders may reduce a population's cost, but neither one peak nor a successful exit demonstrates bounded full-game working memory. Measure repeated content and new-state admission before selecting a conservative budget; do not invent per-object allocation ownership. |
| P1 / throughput gate open | The published 0.1.22 analysis reports 22.061% Combo and 29.065% Native normal bands; local 0.1.23 opening observations are also far below 100%. Runs are unmatched. CPU stage time remains substantial while optional GPU promotion adds work/residency. No host fixture or reduced compiler module size establishes the needed device throughput. Correctness, then matched slow-window profiling, must select the next optimization. |
| P1 for beta / fixed locally | Publication formerly passed unconditional `--prerelease` at both create and edit. The owner requires `Full Release.Beta.Alpha`: positive alpha is a prerelease; zero alpha is a full release. [`uberhar-alpha.yml`](../.github/workflows/uberhar-alpha.yml) now uses the same derived boolean at both steps and retains `--latest=false`, draft-first upload, exact target and every preceding gate. The offline extracted-shell fixture passes; no release was published by this audit. |
| P2 / inherited failure debt | [`vk_resource_pool.cpp:188`](../src/video_core/renderer_vulkan/vk_resource_pool.cpp#L188) retries descriptor allocation indefinitely on unexpected errors without selecting another pool. The fence destructor at [`vk_master_semaphore.cpp:115`](../src/video_core/renderer_vulkan/vk_master_semaphore.cpp#L115) destroys free fences before automatic `jthread` stop/join, while the wait thread can still own or enqueue fences. The timeline wait also repeats unsuccessful waits. These require failure injection and a complete cancellation/join/ownership contract; they are not an established flashing or normal-exit leak cause. |
| P2 / inherited lifecycle debt | [`rasterizer_cache.h:1243`](../src/video_core/rasterizer_cache/rasterizer_cache.h#L1243) clears lookup maps without retiring slot-owned surfaces; [`vk_texture_runtime.h:65`](../src/video_core/renderer_vulkan/vk_texture_runtime.h#L65) move assignment overwrites live ownership without destroying it. The prior review found no active texture-handle assignment trigger. Preserve these findings, reproduce a triggering path before broad repair, and do not use save-state loading for this comparison. |
| P2 / performance proposals deferred | [`UseFragmentShader:1079`](../src/video_core/renderer_vulkan/vk_pipeline_cache.cpp#L1079) reconstructs fragment config for transport/recovery; [`PrepareTev:23`](../src/video_core/renderer_vulkan/uberhar_tev_preparation.h#L23) retains a 16-dimension historical census; [`pica_core.cpp:1266`](../src/video_core/pica/pica_core.cpp#L1266) takes CPU-stage clocks on all experimental CPU draws. These are candidates for exact snapshot reuse or bounded diagnostic simplification after measured attribution. No speed saving has been established. |
| P2 / candidate cost to measure | The new output-write memo in [`uberhar_gpu_output_policy.h`](../src/video_core/renderer_vulkan/uberhar_gpu_output_policy.h) keeps at most 128 immutable program/swizzle snapshots, approximately 4 MiB plus container metadata. A hit can compare 32 KiB and scan up to 128 hashes. Exact comparisons avoid collision/stale-state errors, but the bounded cache is not free: measure admission coverage and host preparation alongside the added CPU fallback cost. `ConsumedOutputW` tracks emitted semantic slots rather than proven live fragment inputs; an unlit quaternion mismatch may be invisible, and conservative fallback can therefore reject work whose differing output is unused. No device slowdown, saving or visible-image cure has been established. |
| P2 / validation reproducibility | Released 0.1.23 notes record 42 vertex and 75 representative fragment SPIR-V execution cases from its prior local validation. The checked-in release gate validates both fragment optimizer modes and executes GLSL-based pixel/vertex fixtures, while the new policy test uses compiler doubles. Preserve the distinction; do not relabel these as a new execution of proprietary Adreno SPIR-V. |

The documented `--prerelease` and draft controls remain standard GitHub CLI
release operations ([create](https://cli.github.com/manual/gh_release_create),
[edit](https://cli.github.com/manual/gh_release_edit)); the new regression
intercepts `gh` locally and makes no network requests.

## Device evidence and next experiment

<!-- CodexAstraLocal: Treat the original report and retained experiment ledger as evidence, preserve the owner's corrected temporal interpretation, and keep all device operations with the coordinator. -->

The original report remains unchanged. Its earlier inference from static moon
facets is superseded by the owner's observation: **Native has no observed moon
glitch; Combo has flashing segments**. The [retained local experiment
ledger](UBERHAR_LOCAL_BATCH_2026-10-06.md) records an existing 0.1.23 mode-4 run
at Vulkan/2x/100% with effective generic-control fragments, zero optional fragment
requests/modules and persisting temporal moon changes. Thus optional fragment
specialization is not necessary for that observed moon instability. Mode 4 still
changes dependency readiness and draw promotion, so this is route isolation,
not proof of a particular vertex fault.

The mode-4 ghost sequence appears more coherent than the prior wide full-Combo
sample, but camera/animation phases differ. Ghost corruption remains a separate
target requiring a matched phase; it must not be marked fixed because the moon
result is understood, or vice versa. This audit did not operate the Thor or
claim a new independent raw-frame measurement; the coordinator and evidence
reviewer own the current capture analysis.

The owner directly reconfirmed **Dark Moon File 1 reset before every opening**
and **deletion of each tested game's shader cache before testing that game**.
Preserve other saves, unrelated app data and the existing 0.1.22 emulator state;
cache preservation is not a constraint for another title when it is tested.
The goal is first-run near-perfect graphics, good normal performance and zero
draw skipping with **zero saved application shader cache available**. Warm-cache
results cannot qualify that target. Record deletion and the effective empty-cache
namespace, not just a restart. Private driver-cache state remains unknown.
Do not load or create save states to shortcut this run.

For the next focused candidate, verify cold **full Combo, Vulkan, 2x, 100%**,
capture consecutive moon frames and a matched ghost phase, then exit normally.
Record output-guard incidence, CPU/GPU routes, required/optional waits, skipped
draws, memory before/during/after, power/charging/fan mode and available thermal
context. Unknown thermal HAL values remain unknown. Separate brief recordings
from timing windows when their overhead matters. Qualify repeated 2x content
before 4x or broader play; a short clean opening is not full-game qualification.

## Alternatives and implementation order

<!-- CodexAstraLocal: Keep the next architecture decision measurable rather than growing route exceptions or pipeline populations because the target remains distant. -->

1. The recovered output-W containment has passed independent review and meaningful
   production-path regressions. Preserve all existing fallbacks and draw order;
   finish Android/signing/publication gates, then verify incidence and each visual
   symptom in the same cold opening. A title result can reject this hypothesis
   even when the synthetic defect is real.
2. If necessary, capture one bounded, default-off, successfully submitted draw
   with immutable guest inputs/state and exact invocation order, retained only in
   ignored private artifacts. Replay through actual CPU/GPU paths; the ordinary
   PICA debugger disables optional promotion and cannot stand in for this capture.
3. Keep the 0.1.23 optimized optional policy while comparing matched pipeline
   populations, attachment groups and memory timelines. If admission must be
   bounded further, stop optional warming conservatively while retaining complete
   CPU rendering; do not raise caps or evict in-flight owners speculatively.
4. After image correctness, use matched cold normal-speed slow windows to choose
   CPU transport/JIT, fragment execution or command-preparation work. Prefer one
   immutable draw snapshot with exact equality over duplicate state preparation.
   Independent fragment promotion, wider topology admission, a general compute
   renderer and removal of driver workarounds remain deferred without supporting
   correctness, memory and timing evidence.

Two consecutive candidates with neither an improvement nor new discriminating
evidence require immediate reassessment. Every three completed successor builds
require the next full audit, with earlier review if a core assumption fails.
The old four-hour/three-candidate stopping boundary is superseded; checkpoints
and actual account/device/build blockers remain meaningful stopping conditions.

## Validation performed by this audit

<!-- CodexAstraLocal: Record actual commands/results and environment limits separately from prior release validation and the parallel candidate's remaining gates. -->

| Check | Result |
| --- | --- |
| `test_shader_compile_policy.py --output build/uberhar-audit-20261007/shader-compile-policy` | Passed 48 production queued-job cases. |
| `test_resource_pool_reuse.py --output build/uberhar-audit-20261007/resource-pool-reuse` | Passed 22 checks using production methods and modeled completion. |
| `test_stream_buffer_ownership.py --output build/uberhar-audit-20261007/stream-buffer-ownership` | Passed 21 production ownership scenarios, including partial/constructor failure and exhaustion. |
| `test_combo_generic_route.py --output build/uberhar-audit-20261007/combo-generic-route` | Passed extracted fragment gate, CPU retry transport and mandatory recovery checks. |
| Compiled/executed `test_memory_diagnostics.cpp` | Passed concurrent allocation balance, capacity batches, reset and explicit heap statistics. |
| Compiled/executed `test_graphics_profile.cpp` with existing generated setting keys | Passed four native profiles, stable IDs, route matrix, guards, resolution/custom preservation and invalid-mode recovery. |
| `tools/uberhar/device_testing/test_device_probe.py` | Passed 16 fake-ADB cases. Initial sandbox socket denial was an environment restriction; an authorized ephemeral-loopback-only rerun passed without contacting the real ADB server or Thor. |
| `test_release_publication.py` | Passed 11 executions of the actual publication shell with fake `gh`, covering alpha/beta/full release, existing versions, same/wrong tags and failed draft creation. The unchanged prior workflow fails the 0.2.0 assertion. |
| Complete `bash tools/uberhar/build_probe.sh` | Passed all required host probes and generated the 816-case TEV, 64-family and 1,056-state full-fragment corpora. Includes the new publication and output-guard tests; existing recovery/logging/input/output/ownership gates remain intact. |
| `validate_shaders.py` with the pinned local validators | Passed 1,066 Vulkan fragment modules in each optimizer mode; compact-loop `DontUnroll` and 128-byte ABI checks passed. |
| `compare_tev.py` on the render venv | Passed 208,896 exact RGBA8 comparisons across 816 six-stage programs and 208,896 texture-use checks. |
| `compare_fragment_state.py` on the render venv | Passed 1,081,344 exact RGBA8 pixels and 1,081,344 depth/discard comparisons across 1,056 states on llvmpipe. |
| Quaternion/attribute `--render-only`; `compare_compute_rect.py --require-vulkan` | Passed three quaternion controls, 21 attribute-padding shaders and 256 compute/native pixel comparisons, with required compute SPIR-V validation. |
| Native core/Vulkan/tests/room build and CTest, separate `build/uberhar-native` tree | Build passed. CTest has 67 entries including an aggregate: 61 distinct Catch cases passed with 1,096 assertions, five existing firmware-dependent DSP/audio cases skipped, zero failures. The aggregate is not another distinct case. |
| `git diff --check` | Passed for the current shared-tree snapshot. |

The two lifetime fixtures were also attempted with `--sanitize`; this host
could not link `/usr/lib64/libasan.so.8.0.0`. Their ordinary tests pass, but this
audit makes no new ASan/UBSan success claim. Artifacts are under ignored
`build/uberhar-audit-20261007/`; complete host-gate logs and reviewed source hashes
are under `build/device-testing/20261007-recovery/host-validation/`. Tests are host evidence, not device graphics,
throughput, memory bounds or signing validation. Independent inspection of the
stable output-guard source and its new regressions found no blocker to the narrow
containment scope; the emitted-versus-live semantic and persistence limits above
remain explicit. Android/package/signing CI, gated publication and the candidate
device run remain separate integration results before qualification.

<!-- CodexAstraLocal: Incorporate the coordinator's completed native gate without counting the aggregate twice or hiding firmware-dependent skips and local dependency setup. -->

The native build and CTest were run by the parallel integration owner; their
logs are retained in `build/device-testing/20261007-recovery/` as
`native-build-recovered.log`, `native-ctest.log` and
`native-ctest-inventory.json`. `build/uberhar-native/Testing/Temporary/LastTest.log`
records the five skips: DSP LLE versus HLE, two last-buffer-ID cases, DSP LLE
sanity and the audio biquad filter lack `dspfirm.cdc`/`dspaudio.cdc`. These skips
do not represent verified firmware/audio behavior. Missing X11 headers were
supplied by the signed matching `libX11-devel-1.8.13-3.fc44` RPM, extracted only
under ignored `build/native-deps/root`, with command-local `CPLUS_INCLUDE_PATH`;
no OS package or CMake policy was changed. Its verified SHA-256 is
`c50738939024df57367c19e6cdebf7b26acb404a3a61daad0544c92613769197`.

## Beta gate and review checkpoint

<!-- CodexAstraLocal: Keep 0.2.0 earned by the retained architecture/evidence gate, and make this completed source audit distinct from a published or qualified candidate. -->

The [0.1.6 architecture gate](UBERHAR_ARCHITECTURE_0.1.6.md#roadmap-and-version-gates)
still requires measurably better demanding matched scenes, exact output, a
retained **2x FEA baseline**, and profiling that supports the chosen CPU/GPU/command
architecture. The current owner scope prioritizes Combo Dark Moon; repeat Native
only if the Native process changes. That narrowed test scope does not silently
waive the FEA beta gate. Request any required scope decision when it becomes the
actual remaining promotion blocker, not as a reason to stop routine Dark Moon
work now.

This completes the architecture audit of the 0.1.23 source and retained local
0.1.24 candidate direction before the next build. The coordinated review ledger
records 0.1.23 as the new anchor; count 0.1.24 as successor one only after its
gates pass. Do not count test retries or this documentation as another build.
No 2x correctness, 100% normal-speed, cold first-run, repeated-content memory,
4x or broader-gameplay qualification is asserted here. Publishing x.y.0 as a full
release is now technically supported, but the evidence gate remains open.
