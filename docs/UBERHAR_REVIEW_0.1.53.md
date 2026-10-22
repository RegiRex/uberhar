# 0.1.53 integration review

<!-- CodexAstraLocal: Keep the selected source baseline and reviewed scope explicit; this is a focused integration review, not a new whole-project audit. -->
The implementation worktree starts at original .48, commit
`946a1bc4bcc023b768902aaebfb5200cd34e4015`. The release preserves subsequent public
history/documentation and current saved-mode migrations without inheriting the
later rendering framework wholesale. Local owner changes and private test
artifacts remain outside the release.

| Area | Included | Deliberately retained from .48 |
| --- | --- | --- |
| CPU queue | Exact proof reuse, owner counters, lazy worker start | Cheap small-work rejection, grouping, admission, one-entry mapping cache |
| Worker census | Default-off execution/peak counting, immutable process choice | Safety reservations and shared capacity arbitration |
| Full shaders | Compile-time GLSL liveness, compatible direct SPIR-V selection | Generic/static programs, quotas, fallback output, asynchronous readiness |
| Vulkan safety | Old-pass attachment ordering, terminal wait propagation | Command ordering, cancellation, completion and resource lifetime rules |
| Product | Current supported-mode migration and release signing | App identity and in-place data compatibility |

<!-- CodexAstraLocal: Exclude mechanisms together with their coupled instrumentation so source rollback cannot leave misleading controls. -->
No four-entry mapping cache, expanded standalone seeds, adaptive workload history,
new generic/static liveness framework, shared texture memoization, constant or
directional preparation cache, new lighting quotas, contained-area reuse or
blit-to-copy optimization is imported. Historical evidence for these experiments
is retained. Reintroduction requires a specific comparison, not an assumption
that every later change is an improvement.

The architect checked key/profile/ABI compatibility, single-owner bookkeeping,
proof generations and leases, worker-start failure, settings migration and
shader-cache identity. The shared TEV-liveness dependency is included in the
source-derived shader-cache version. Mandatory CI remains the executable gate.
No gameplay testing was requested. This review does not establish a measured CPU
or GPU improvement, and no per-feature timing is invented.

<!-- CodexAstraLocal: Distinguish the existing direct route from its sentinel fixture's limited evidence. -->
The direct ready-fragment route reuses the existing SPIR-V generator. Route tests
exercise dispatch and compatibility decisions with recording endpoints; full
CPU-input/direct-generator pixel output is not established by those sentinels.
The stock/direct interface has been source-reviewed and the owner will validate
actual output on hardware. Existing shared pixel and shader-compiler gates remain.

Next device comparison: match original .48 renderer, internal resolution, CPU
vertices, scene and game fast-forward. Compare complete five-second lows first,
then app core-equivalents, per-core distribution and GPU work per frame. Judge
shader-compilation activity separately from steady gameplay. Compare **Enable
SPIR-V shader generation** on/off with other settings held fixed before expanding
another settings matrix. Keep **Collect detailed CPU worker counts** off for throughput
unless the diagnostic question requires it and the process has been restarted.
