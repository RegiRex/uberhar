# Private vertex capture and offline replay

<!-- CodexAstraLocal: Document the finite, opt-in diagnostic and its operational limits without presenting it as a Dark Moon graphics fix. -->
The 0.1.25 candidate's capture is disabled unless the exact title has a valid
`config/uberhar_vertex_capture.json` sidecar. It observes optional-ready Combo
draws. It does not alter draw admission, shader math, or Native execution. Its
timing can still affect asynchronous shader readiness, so captured runs are
diagnostic runs, not qualification measurements.

Only the root device operator installs the sidecar and operates the Thor. The
sidecar is read once at title initialization; editing it during play does not
rearm a session. Every opening still requires the authorized File 1 reset and
title-cache deletion. Warm runs cannot qualify first-run performance.

## Discover, then select using the same APK

<!-- CodexAstraLocal: Use stable identities plus a global per-swap ordinal, never a process-local pipeline pointer or a filtered occurrence counter. -->
Create an ignored, private working directory and prepare a discovery sidecar:

```sh
python3 tools/uberhar/vertex_capture.py discover \
  --title-id TITLE_ID_FROM_ORDINARY_LOG \
  --capture-id moon-discovery-01 --window 8 --per-swap 1 \
  --output build/device-testing/moon-discovery-01.json
```

Replace `TITLE_ID_FROM_ORDINARY_LOG` with its exact 16 hexadecimal digits. The
device operator puts this file at the sidecar location before launch. At the
target scene, use the existing Test phase menu: select Automatic, allow a swap,
then select Gameplay. This explicit edge arms one finite window. Selecting
Gameplay when it is already selected does not arm it. Startup transitions alone
do not arm it. Normal return to the game list drains the evidence to
`dump/uberhar_vertex_capture/<capture_id>.uvc`; an existing artifact is never
overwritten. An abrupt process kill may lose the capture.

After the device operator retrieves the private artifact:

```sh
python3 tools/uberhar/vertex_capture.py inspect build/device-testing/discovery.uvc
python3 tools/uberhar/vertex_capture.py select build/device-testing/discovery.uvc \
  --row 3 --capture-id moon-packets-01 --window 8 --per-swap 1 \
  --output build/device-testing/moon-packets-01.json
```

Selection retains the row's **global optional-request ordinal**, shader/swizzle
identities, entry, vertex count and target guard. A mismatch is censored; another
draw is never silently substituted. Scene pacing and asynchronous readiness can
change ordinals between cold runs, so zero matches and caps are meaningful
results. Inspect the counters before interpreting a capture. Swap intervals are
not necessarily consecutive guest or video frames; pair them with the root
operator's temporal visual evidence and ordinary logs. Track moon flashing and
ghost corruption separately.

## Replay and interpret

<!-- CodexAstraLocal: Keep immutable recorded inputs and actual bound uniforms distinct from current/intended state and counterfactual controls. -->
Use the matching APK source checkout and the existing ModernGL environment:

```sh
build/uberhar-render-env/bin/python tools/uberhar/replay_vertex_capture.py \
  build/device-testing/moon-packets-01.uvc \
  --output build/device-testing/moon-packets-01-replay
```

This builds an isolated host worker. `--cpu-only` avoids Mesa;
`--reuse-binary` explicitly reuses an already-built worker. The output directory
must be new. Each report retains source context and the worker's SHA-256; current
checkout identity alone does not attest a reused binary's provenance. CPU and
Mesa workers each have a finite timeout.

- CPU replay uses the production input plan, interpreter, semantic conversion,
  draw-local register carry and 64-entry FIFO. Original indices remain FIFO keys;
  only accesses into the compact copied upload are rebased. The report explicitly
  names `production_interpreter`; neither CPU JIT is executed by this worker.
- Mesa replays generated GLSL with actual typed raw uploads, fixed attributes and
  actual bound vertex UBO semantic bytes. The production trivial shader applies
  the same clip/viewport transport to CPU output before comparison.
- `cpu_vs_gpu_inputs` separates input-fetch differences.
  `actual_vs_intended_uniforms` records stale/different bound PICA boolean,
  integer and float bytes. A second GPU run with intended uniforms is a labeled
  counterfactual. Actual clip/viewport uniforms are applied to both routes;
  their intended values and fragment uniforms are not compared.
- `cpu_carry_vs_fresh_unit` measures a labeled reset-per-miss counterfactual. The
  fresh-unit result is never the Native oracle.
- Bit differences, finite absolute/relative tolerance and nonfinite pairs are
  reported separately. Geometry-shader paths, ambiguous loader mappings,
  missing semantic UBO ranges, incorrect shader identity and fetches requiring
  uncaptured padding/cross-row bytes are rejected, not approximated.

An accepted packet proves the draw command reached a successful queue submission
in the same scheduler lifetime. `completed` is separate. The replay ends before
primitive quaternion correction, fragment lighting, textures and rasterization;
it neither emulates Adreno nor proves a visible defect's cause. Host agreement
does not qualify Dark Moon, speed, memory, temperature or correct pixels.

Keep `.uvc`, raw shader/data files, replay outputs and scene evidence under
ignored `build/device-testing/`. They may contain private guest content and must
not be uploaded with public source or release validation artifacts. Normal
diagnostic summaries may describe results without including those payloads.

## Regression gates

<!-- CodexAstraLocal: Synthetic fixtures make the diagnostic independently testable without distributing game content. -->
```sh
python3 tools/uberhar/test_vertex_capture_replay.py
build/uberhar-render-env/bin/python tools/uberhar/test_vertex_capture_replay.py \
  --render --reuse-build
```

The synthetic cases exercise actual production CPU/GLSL code, FIFO eviction and
high indices, cleared trailing shader words, conditional carry, output defaults,
u8 index widening, signed/unsigned/scaled/fixed/padded inputs, stale bound
uniforms, strict parser limits and the discovery ordinal contract. These are
host correctness checks, not device performance measurements.

<!-- CodexAstraLocal: Preserve old shader identity while making the new optional arithmetic contract explicit in private captures and replay interpretation. -->
From 0.1.26, packet `extra.precise_jit_dot` records whether Combo generated
precise JIT-ordered DP4/DPH additions. It is a strict JSON boolean; an absent
field in a 0.1.25 artifact means the legacy generator. The reader does not
retrofit old captures to new arithmetic. Replay still verifies generated source
identity, so removing a true flag from a new packet is rejected. The host CPU
side remains the interpreter: this flag changes reconstructed GPU generation,
not the replay engine or its interpretation as device evidence.
