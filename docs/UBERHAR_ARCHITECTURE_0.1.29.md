# Uberhar architecture and cleanliness review through 0.1.29

<!-- CodexAstraLocal: Close the three-successor architecture and independent cleanliness review after integrating both delivered controls; product qualification remains separate. -->

The architecture and independent code-cleanliness reviews cover released
`0136f89d429eab2b25f626703b2158b9ebe5091d` against the completed 0.1.26 review.
Versions 0.1.27, 0.1.28 and 0.1.29 are the three successful successors. No new
blocking correctness defect was found in their reviewed changes. Both cold
controls and their independent consecutive-frame reviews are integrated; the
coordinator closes this audit at **0.1.29**, resetting the successor count to zero.
This is a changed-section and active-contract audit, not a claim that every line
of inherited emulator and dependency code was re-audited.

The current product gap is substantial: cold 2x Dark Moon observations remain
far below sustained 99% normal speed. Passing source/build checks does not qualify
0.2.0. Longer gameplay, 4x and the retained 2x Fire Emblem Awakening baseline are
still open under the [beta evidence gate](UBERHAR_ARCHITECTURE_0.1.6.md#roadmap-and-version-gates).

<!-- CodexAstraLocal: Connect each delivered change to the behavior its regression actually exercises. -->

| Successor | Change and evidence | Material limit |
| --- | --- | --- |
| 0.1.27 | Bounded opt-in CPU vertex timing preserves one FIFO, shader unit and assembler across prefix/chunk/suffix. Actual producer, runner, reader and failure checks pass. | Tiny detailed intervals are heavily perturbed; selected chunks cannot establish workload-wide function shares. |
| 0.1.28 | Exact final-mapping output copies and view-owned Android overlay callbacks pass production conversion and extracted lifecycle regressions. | Host conversion gains are not Thor gains; callback repair does not eliminate all presentation/recording perturbation. |
| 0.1.29 | Draw-local typed program/uniform/entry binding passes real JIT/interpreter, FIFO/output/assembly and extracted call-site checks with profiling off/on. | Generated ABI and arithmetic are unchanged; x64 label-lookup savings do not predict ARM64 stored-offset savings. Associated host x64 native text grows about 19.9%. |

All release shader, Android, package, signing and publication gates passed.
Exact-source asset verification and the compatible data-preserving installation
passed. Local certificate parsing checks signing identity; cryptographic signing
verification is supplied by CI. Native integration and 67 CTest entries pass or
retain five inherited firmware-dependent skips. The aggregate test entry is not
an additional set of independent tests.

<!-- CodexAstraLocal: Retain correctness boundaries even when a particular title sample shows little coverage or a private prototype looks faster. -->

CPU execution retains live uniforms, register carry, exact FIFO miss order and
owned primitive snapshots. A borrowed FIFO output can be overwritten by a later
miss within the same triangle; its ownership cannot be removed casually.
Grouped output copies preserve final alias order, defaults, bank mappings and
color conversion. The draw-local context borrows existing objects only for the
synchronous draw, with inherited empty/interpreter/profiler/unsupported fallbacks.

GPU admission retains topology, bounds, output and quaternion guards. Exact
program/swizzle equality prevents stale memo reuse; a zero observed rejection
count is not permission to remove that guard. A private carry-independence proof
handles restricted straight-line code but rejects retained title cases at CALL.
It is not production admission. Quaternion GPU expansion has a finite subnormal
sign counterexample, so a simple arithmetic rewrite cannot establish parity.

Required generic compilation and mandatory unsupported-state recovery remain
complete-draw fallbacks. Optional shaders and pipelines require successful
completion, exact execution/layout/attachment keys and stable shader owners.
Descriptor bindings and uniform offsets are captured per command; uploaded bytes
retain their stream-buffer lifetime. Scheduler/compiler drains protect ownership
on title/reload; profile mismatch alone rejects incompatible
entries rather than inventing an immediate drain. Count caps do not bound opaque
driver bytes, and short teardown observations do not qualify long-play residency.

<!-- CodexAstraLocal: Record bounded observations and their denominators without converting unmatched scene mixtures or wall brackets into causal performance claims. -->

The 0.1.29 cold Native and Combo openings pass exact settings, empty application
cache, zero skipped-draw and failure checks; saved settings match their respective
0.1.28 controls. The limiter remains at 100% and resolution at 2x.

| Mode | Complete timing windows | Mean normal speed | Wall-weighted fifth percentile of window-average speed | Worst reported interval |
| --- | ---: | ---: | ---: | ---: |
| Native | 40 | 27.869% | 20.781% | 208.762 ms |
| Full Combo | 40 | 21.892% | 16.628% | 222.201 ms |

Native covers 201.323753 seconds and Combo 201.708616 seconds of their respective
approximately 206.04-second scopes. Every selected window is below 99%.
Different scene mixtures, capture intervals
and asynchronous readiness prevent a matched version/mode speedup claim. Direct
all-batch reporting windows put 83.81% of Native and 54.90% of Combo elapsed wall
inside CPU vertex loading/execution/assembly. These are different window
populations, not thread-CPU shares; the remainder is not measured GPU time.
Action-window containment uses ordinary clock and log-emission assumptions;
the underlying all-batch duration sums are direct.

The owner-requested 0.1.28 calculated baseline averaged 27.337%, but all considered
draws took Native fallback: eligible rectangles and computed draws were zero.
The implemented compute path does not yet cover general 3D rendering. This useful
baseline prevents interpreting a mode label as measured acceleration.

Native's bounded 0.1.29 moon review found five isolated single-frame events, each
clearing immediately, within the owner's reference-only threshold. A separate
one-frame rear-ghost disturbance remains; reviewed primary ghost cores lack the
older dense patches. Combo's 417 reviewed moon frames contain no comparable
event or prior polygon-local flashing. Its bounded near, foreground and book-holder
ghost cores lack the older dense patches. Its 511-frame ghost recording ends
before the later Native rear-ghost phase and cannot clear that separate fault.
Decoded recording frames are not guest-frame counts, and unobserved scenes remain
unqualified. The earlier calculated repeat with text hidden had no comparable moon
event but retained brief rear-ghost disturbances; it establishes neither cause
nor cure. Battery readings are not SoC thermal-headroom measurements.

<!-- CodexAstraLocal: Close purpose coverage and trace values to consumers while keeping rejected cleanup and unmeasured costs visible. -->

The separate cleanliness review covers 62 post-0.1.22 code/tool/gate files,
including 32 production files. It checks changed logical sections and actual
value consumers. The final 17-file, 74-line purpose-comment patch is integrated
with preserved historical authorship and identical C++ noncomment tokens, Python
ASTs and existing workflow bytes. Source-derived cache fingerprints still require
fresh configuration when a later candidate is built.

Three activity counters are loaded but unused by enabled vertex-timing consumers;
the default-off path avoids them. This is a bounded observer cleanup opportunity,
not an established gameplay bottleneck. Skipping overwritten output defaults
proved slower in several host cases and was rejected. Input-layout preparation,
exact memo equality, fallback checks and delivered diagnostics have real consumers;
they are not dead work merely because their costs are not isolated.

Inherited review debts remain: unexpected descriptor-allocation failure handling,
semaphore worker teardown ordering, repeated texture/resource replacement and a
diagnostic recovery-route label need focused exercised proofs. These source
concerns are not newly observed crash/leak causes in the current controls.

<!-- CodexAstraLocal: Select a substantial next proof without promising throughput or relaxing cold first-run rendering. -->

The next private candidate separates optional specialized fragment selection
from GPU vertex admission. CPU-generated vertices may use a completed pipeline
with the actual software layout and trivial vertex shader while retaining all
CPU arithmetic, carry, FIFO, quaternion handling and complete generic fallback.
An initial eight CPU pipelines stay within the existing combined optional limit;
module limits and a single pending optional pipeline are retained.

Production selection requires actual CPU-output/trivial-shader ABI parity,
the fragment corpus, alternating descriptor/constant state, exact-key tests,
failure completion and ownership/drain checks. Draw counts establish plausible
scope, not fragment cost. Menu states may consume the small initial bank; lit and
unlit coverage must be reported. Driver compilation and residency may offset any
benefit. Host primitive-submission alternatives showed mixed small gains and
code growth, so they are not being promoted solely on a microbenchmark.

Cold Native/Combo controls and a generic-only discriminator remain required for
relevant shared changes. Ownership changes warrant renewed memory cleanup and
longer-growth checks. Routine development continues toward measured correct
99%+ play; a version number does not substitute for that evidence.
