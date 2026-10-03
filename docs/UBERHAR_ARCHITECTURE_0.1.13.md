# 0.1.13 consolidated architecture review

<!-- AstraEH: Owner accepted this review's order on October 3; no new speed claim follows. -->

Reviewed source: `f7b01cc6aceb2ffb4c604f4a2a05219aa484e3d0`.
Date: October 3, 2026. Evidence: the two October 2 Thor exports, the earlier
`uberhar-review.md`, the additional `UBERHAR_AUDIT_0.1.13.md`, and direct inspection
of pipeline/cache/profile lifetime, logging, Android export/settings and release
paths. External audit findings are not treated as independently reproduced device
results. Ten completed title runs and one partial run are counted once.

## Evidence and constraints

| Recorded run | Normal band | Additional evidence |
| --- | --- | --- |
| FEA Native 2x | 99.521%, 72.131 s | Warm file inventory; no physical latency measurement. |
| Dark Moon Native 2x | 29.121%, 213.906 s | CPU vertex stage 170.551 s, approximately 79.7% of observed time. |
| Dark Moon Combo 2x | 13.092%, 342.896 s | CPU vertex stage 40.599 s, approximately 11.8%; owner reports corruption and crashes. |
| Sonic Combo 2x, three runs | 93.311%, 93.413%, 94.594% | Unmixed normal-window lows 78.510%, 80.521%, 83.369%. |
| LEGO Combo 2x | 99.882%, 51.940 s | Separate accelerated band 385.722%; scene/camera not logged; optional cache reaches 256. |

FEA 4x accelerated means are 110.074% Native and 235.685% Combo, but scene/frame
coverage differs. All turbo bands and transitions are excluded from ordinary-play
comparisons. Menus/loading/race boundaries are not inferred from speed.

Dark Moon's 81 optional pipelines total 109.402 s of background driver creation;
54 optional fragment module compiles total only 0.312 s. The last optional build
ends at process time 366.425 s; eleven later complete windows span 55.887 s and
average 12.202%. Creation cost alone does not explain persistent slow execution.
All 81 pipelines have stage mask 3 (vertex+fragment); a guest geometry hash is not
an active geometry stage. Host timings do not identify GPU execution cost or
certify output. Fixed attributes peak at 16 bytes, with zero over-legacy events;
the previous reservation fix is not established as the reported crash's cause.

The first export ends at a partial Dark Moon pause record. The second contains
orderly title-run endings. Neither identifies a fatal native stack/signal. The
owner's report of lost logs therefore remains a requirement, not a diagnosed
filesystem mechanism. Cache inventories distinguish empty application files from
present files; internal driver state remains unknown.

## Accepted next order

1. Independent session evidence that normal rotation cannot erase; export even
   when the live logger/provider cannot flush; attach available OS exit records.
2. Snapshot compiler-worker inputs; handle the specifically identified terminal
   shader-recovery failure by stopping the title, preserving complete draw policy.
3. Localize Dark Moon's graphical failure with focused route/output evidence.
4. Resume broader performance experiments only after the graphical issues are
   corrected and device validation supports that decision.

0.1.14 implements the first two steps and lifecycle hardening, not a Dark Moon
graphics cure. Preserve Native as a first-class route, Sonic's working Combo
behavior, exact output/draw order, support guards and driver workarounds.

## Audit decisions and deferred experiments

| Item | Decision / later gate |
| --- | --- |
| Shared mutable generic-worker profile/options | Source-confirmed hazard; capture immutable profile, optimizer, persistence, title and directory inputs together. Keep profile selection before new-title cache generation. |
| Terminal recovery throw | Handle the specific typed error at the Android emulation loop; never skip a draw and continue. Unrelated native failures remain OS crash evidence, not broadly swallowed exceptions. |
| Non-Combo generic module ownership | Drain generic borrowers before repeated disk-cache replacement, as for optional GPU users. |
| Demand-gate collisions | B1 has 20,417 replacements. Associative admission is deferred; measure which keys starve before claiming a speed gain. |
| Cheaper failed GPU attempts / shared scans / FSConfig reuse | Deferred. New failed-host samples average about 4.5 us (241 samples); highest-impact ranking is unproven. Any early cap check must still serve existing ready hits. |
| Diagnostics gating / push-constant reuse | Deferred paired measurements; neither source-only overhead estimates nor avoided-byte counts prove speed changes. Keep essential crash context. |
| Shader sampling hoist / specialization constants / GPU PICA interpreter | Deferred isolated prototypes with actual pixel and GPU-cost checks. Removing glslang work alone does not remove driver pipeline optimization. |
| Graphics pipeline libraries / shader objects | Deferred capability-dependent work. Both are unadvertised on the recorded Thor driver; no immediate target-device fix promised. |
| VS/GS persistence / loading-screen preparation | Deferred compatible-cache design. Move work into genuinely noninteractive periods; retain unknown phase when evidence is insufficient. |
| Strip/fan expansion / broader coverage | Deferred until correctness; preserve winding, continuation, restart, attributes and fix-ups. Guest-GS limitations are not waived. |
| Cache eviction / weaker-device presets | Deferred measurement and in-flight ownership design. Driver allocation estimates are not measurements. |
| GPU timing / scene and draw comparison | Focused diagnostic candidates for Dark Moon; replay must include relevant texture/uniform/LUT/attachment state and history. |

The earlier audit's FastInterp proposal concerns ARM guest-CPU interpretation,
not the existing ARM64 PICA shader JIT. The simple f24-versus-fp32 explanation is
not established. Controlled stock-Azahar baselines and real-device graphics
acceptance remain needed. Existing host shader/pixel gates remain mandatory.

## Scope and cadence

New owner requirements: new comments use AstraEH; crash evidence is protected from
rotation; per-game settings are required by **2.0**, accessible from **title
long-press** and **an in-game Uberhar settings submenu**. Global inheritance and
explicit title-ID overrides must not mutate global defaults. Show profile-enforced
effective values; support resetting one or all overrides. This feature is planned,
not implemented by 0.1.14.

The owner accepted this consolidated review and lifted the build hold on October 3.
The full-review anchor advances from 0.1.9 to **0.1.13**. 0.1.14 is successor one
only after publication; default next review after 0.1.17 evidence, allowed after
0.1.16–0.1.18, before starting 0.1.19. Review earlier for continued correctness
failures or new evidence invalidating the plan. A failed same-version build retry
does not increment the interval. Earlier 0.4 latency, 0.8 scaling, 1.0 qualification
and separate 2x/400%, 4x/200% capacity goals remain in scope.
