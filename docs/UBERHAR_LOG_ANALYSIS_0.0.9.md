# Uberhar 0.0.9 device analysis

<!-- AstraEH: Measured evidence from the owner's four sessions; no raw logs or game data committed. -->

Source: `71657e2b58292690fd24185703197365bdab57bc`, reported by the attached
`uberhar_log_9_24_1546_FEA.txt`. Device remains Adreno 740 / Qualcomm 512.676.53,
Vulkan 1.3.128. Sessions are separated at renderer initialization, not at the
single Android logger initialization. Cache labels below follow the owner's test
sequence; deleting emulator cache files does not prove every driver cache is cold.

| Session | Force TEV | Reported cache | Draws | Pipeline waits | Total wait | Longest wait | Waits >= 50 ms |
| --- | --- | --- | ---: | ---: | ---: | ---: | ---: |
| 1 | Off | Cold | 184,158 | 118 | 20.821 s | 1.124 s | 70 |
| 2 | Off | Warm | 183,385 | 0 | 0 | 0 | 0 |
| 3 | On | Warm, brief accidental run | 3,187 | 11 | 1.120 s | 0.521 s | 3 |
| 4 | On | Cold | 193,315 | 130 | 50.410 s | 1.446 s | 89 |

The third session is excluded from full-section performance comparisons. Its
specialized cache was warm, but forced fallback configurations were new: eleven
fallback pipelines were built. A warm specialized cache does not make every
generic configuration ready.

## What changed from 0.0.8

| Normal cold session | 0.0.8 | 0.0.9 |
| --- | ---: | ---: |
| Draws | 182,345 | 184,158 |
| Scheduler pipeline waits | 127 | 118 |
| Total scheduler wait | 20.790 s | 20.821 s |
| Longest wait | 1.337 s | 1.124 s |
| Waits >= 50 ms | 79 | 70 |
| CPU bridge draws | 0 | 890 |
| Total fallback draws | 419 | 932 |
| Fallback driver-call time | 13.387 s | 6.526 s |

The bridge now works. It emitted 529,314 bridge vertices
across 890 recorded batches, spending 284.572 ms in CPU preparation with a
4.874 ms maximum. No bridge key mismatches, fallback failures or skipped draws
were reported. Of 163,565 eligibility observations, 156,570 were eligible and
6,995 exceeded the input limit. All observed topology entries were shader-list.

Total waiting did not improve: the change was +31.211 ms, about +0.15%. The longest
wait fell about 15.9%, and fewer waits exceeded 50 ms, but these are separately
played sessions rather than a controlled replay. Worker compilation durations
overlap, so the reduced fallback driver time cannot be subtracted from total
gameplay stalls. The normal warm run again had no pipeline waits.

Forced cold execution built 141 generic pipelines in 37 fragment families and
still built 285 specialized pipelines. All 141 generic pipelines served draws.
Its generic driver calls totaled 49.992 s, while the specialized worker driver
calls totaled 89.586 s. These overlapping durations identify considerable work,
not an additive wall-clock delay. It also rendered 14,258 draws outside the TEV
fallback's supported set. Force TEV disables the bridge by design in 0.0.9.

## Decision

The source and device evidence justify an early architectural review. Ready-only
bridging can improve selected encounters, but racing on-demand generic and
specialized builds cannot guarantee first-playthrough readiness. A new experiment
must measure generic execution as the primary route, suppress unnecessary rival
compilation and clearly expose incomplete coverage.

0.0.10 adds explicit test profiles, a primary generic native route, an initial
startup-compiled compute subset and sampled automatic routing. Its scope and
unproven parts are recorded in [the architectural review](UBERHAR_ARCHITECTURE_0.0.9.md).
It is not evidence that a full GPU interpreter or full compute rasterizer is fast
enough on Thor; neither is implemented by this prototype.
