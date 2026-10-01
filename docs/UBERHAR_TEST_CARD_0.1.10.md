# Uberhar 0.1.10 — focused test card

<!-- AstraPro: Use only after an APK passes the normal publication gates. -->

**Use after publication:** verify the 0.1.10 ARM64 release and its completed gates. This card does not assert APK availability.

| Priority | Scene | Renderer / resolution | Speed | Check |
|---|---|---|---|---|
| 1 | Familiar FEA battle | Native / 2× | 100% | No lost geometry, changed effects or new recurring slowdown. One run is sufficient. |
| 2 | Same short Dark Moon opening shots | Native, then Combo / fixed 2× | 100% | Correct image and materially improved slow section. Do not require the complete cutscene. Abort a clearly bad run. |
| 3 | LEGO dense-city-facing view, then a separate coast-facing control | Combo / fixed 2× | Hold 400% | Raise the demanding inland result; the prior near-400% result was coast-facing only. Keep scene identities independent of observed speed. |
| 4, after the above | Same Sonic race section | Native then Combo / fixed 2× | 100%, with an optional separate fixed400 pass | Raise sustained lows; short loading/menu peaks do not qualify racing. |
| Optional later | A section already correct and substantially improved at 2× | Combo / fixed 4× | 100%; separate 200% usability test | Resolution qualification, not required before 2× behavior is sound. |

Keep guest CPU clock 100%, the same host performance/fan/charging setup and a cooled device. Restart the title when changing renderer. Keep caches for throughput comparisons and record deliberate cold tests separately. Use the same in-game start/end landmarks, not just equal real-world durations. A much slower renderer reaches a different scene after the same elapsed time.

Do not switch resolution or the speed limit repeatedly during a measured section. The new context fields help identify sampled changes but do not turn an uncontrolled run into a matched benchmark. A short pause/resume and title switch checks lifetime behavior. Report missing/flipped geometry, lighting/animation changes, incorrect shadows/effects and new hangs.

No Compute-mode matrix, full Kid Icarus replay or MH4U character creation/intro replay is requested for this candidate. MH4U's strip/guest-GS blocker is unchanged; it remains a performance milestone, not a claimed beneficiary of this change.

The log should identify `specialized_ready_v1`, nonzero ready fragment hits and `optimized_gpu_draws`. Misses must produce CPU draws. Pipeline/module timing, host attempt samples, cache caps, slow windows and visual output all matter; GPU promotion count alone does not pass the test.

<!-- AstraPro: Follow-up owner landmarks, not guessed automatic scene detection. -->
See [scene annotations](UBERHAR_SCENE_NOTES_0.1.9.md). For Dark Moon, retain the
ghost-electrification cue as the repeatable 2x-to-4x boundary when a 4x check is
useful; compare the same side of the cue across runs, not 2x against a different
4x scene. There is no need to repeat a prolonged bad 4x sequence. For Sonic, leave
fast-forward off before the countdown, enable it at countdown start, and keep
it steady during the race. A toggle is a user marker, not automatic scene recognition.
