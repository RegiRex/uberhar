<!-- CodexAstraLocal: Final performance report, sanitized for the owner-authorized public archive. -->
> Public report copy, archived November 20, 2026. Measurements and historical qualifications are retained. Private paths, device identifiers and operational closeout details are omitted; raw captures were deleted during the authorized storage cleanup. References to retained raw evidence describe the original analysis, not its present availability. This archive does not authorize device operations or settings restoration.

**Uberhar 0.1.57 — completed additive testing and matched-build report**

CodexAstraLocal · November 7, 2026, Eastern time. Hardware collection ended November 8 at 00:12 UTC.

**The matched .56-r2/.57 baselines are broadly stable. Several combinations hurt performance, and the strongest apparent Xenoblade low did not survive confirmation.** Xenoblade's three-control package did reproduce its stronger battle average. Fire Emblem's Optimize generic shaders benefit reproduced almost exactly in both builds. Dark Moon favors the lean selected profile over the tested new-feature combinations.

**What was covered**

The final assembly contains **182 qualified rows: 176 newly completed normal runs and six reused stock references**. One additional v27 Native Dark Moon run completed gameplay but stalled during shutdown; it remains an explicitly qualified gameplay appendix. The assembly's single “pending” entry refers to that excluded shutdown row, not missing scheduled gameplay. Sixteen existing stock-three-renderer/historical-profile build comparisons remain indexed separately.

- Dark Moon: clean alternate-driver screening, seven eligible single controls, all 21 pairs, optimizer/timing corners, and fresh .56/.57 matched controls. No broadly beneficial new-work set survived; no higher combinations were needed.
- Xenoblade: driver screening, seven singles/all 21 pairs, all 63 nonempty combinations of the six retained candidates, selected-profile checks, stock/historical-profile transfers, fresh .56/.57 controls and one decision-changing confirmation.
- Fire Emblem: stock renderer baselines, alternate-driver screening, six singles/all 15 pairs, optimizer/timing controls, full-profile 0%/1% Guest 3D comparisons, and fresh clean/optimizer-ON .56/.57 controls. The final typed-append candidate failed retention.

Report tables group Native, Combo CPU Vertices OFF and Combo CPU Vertices ON consistently. Valid earlier evidence was reused. Tests used Standard mode, cold application shader caches, normal cap 100%/turbo cap 400%, Wi-Fi off and actual driver/settings verification. Closing controls used external power, charging separation OFF and the fixed Standard fan mode. Battery temperatures were approximately 28–30°C; CPU/GPU temperatures and changing clocks are retained per measured window.

**Matched build results — shared settings fixed, new work controls OFF**

Values are **slowest complete five seconds / recorded scene mean / fastest complete five seconds**, in emulation-speed percent, not FPS. Detailed GPU pass timing remains ON in both versions because .56 had the same observer implicitly enabled; .57 observer-OFF tests are separate. These are matched scene extents, not exact guest-frame rebinning.

| Game / scene | .56-r2 | .57 closing control |
|---|---:|---:|
| Dark Moon — opening moon | 78.69 / 78.69 / 78.69 | 80.11 / 80.11 / 80.11 |
| Dark Moon — first lab | 85.80 / 95.68 / 101.46 | 82.84 / 95.77 / 102.22 |
| Dark Moon — ghost/moon | 79.79 / 84.22 / 88.64 | 77.54 / 84.81 / 92.10 |
| Dark Moon — second lab | 89.85 / 89.85 / 89.85 | 89.63 / 89.63 / 89.63 |
| Xenoblade — titan opening | 103.89 / 177.97 / 231.27 | 103.50 / 178.42 / 244.39 |
| Xenoblade — battle cinematic | 49.51 / 103.45 / 162.89 | 49.00 / 104.16 / 162.17 |
| Xenoblade — stationary gameplay | 113.08 / 116.35 / 117.84 | 112.88 / 114.90 / 116.40 |

Both games use Turnip v27 and Combo CPU Vertices OFF at 2x. Dark Moon has Linear Filtering ON; Xenoblade OFF. Both keep Optimize generic shaders OFF, Disable Right Eye Render OFF, Render-thread delay 0 and Guest 3D 0%. Simulate 3DS GPU timings and Turnip pass-end submission are ON. The opening/second-lab Dark Moon cells each contain one qualifying window, hence equal low/mean/high.

Dark Moon's first-lab/ghost lows fell **3.45%/2.81%**, while scene means stayed close. Xenoblade's battle mean changed **+0.68%** and stationary mean **−1.24%**. Earlier apparent late-lab and stationary build gains did not reproduce in these closing controls and are withdrawn as dependable build improvements. Dark Moon comparison (source not included in this compact archive) · Xenoblade comparison (source not included in this compact archive).

Fire Emblem's stock/Combo CPU Vertices OFF/4x comparison holds the same final-turn-two map view:

| Optimize generic shaders | .56-r2 | Closing .57 | Build change |
|---|---:|---:|---:|
| OFF | 158.625% | 158.539% | −0.05% |
| ON | 173.016% | 172.806% | −0.12% |
| ON gain over OFF | +9.07% | +9.00% | — |

This is a useful, repeated settings benefit that predates .57. The entire battle was captured, but short attack intervals do not always contain a full five-second sample wholly inside matching visual bounds. Closing clean .57 has one strict 84.49% battle window; fresh .56 optimizer-ON has 87.62%; the other fresh battle counterparts are boundary-qualified rather than strict. These different attack portions cannot support a whole-battle build verdict. Their raw evidence and sensitivity windows were available for the original analysis. Fire Emblem comparison and all phase resources (source not included in this compact archive).

**Which complete profiles to use now**

| Game | Practical profile | Decision |
|---|---|---|
| Dark Moon | Combo CPU Vertices OFF; v27; 2x; Linear Filtering ON; Optimize generic shaders OFF; Detailed GPU pass timing ON; new work controls OFF | Retain the lean profile. Some settings improve one scene while worsening the labs; none earned balanced retention. |
| Xenoblade | Combo CPU Vertices OFF; Gen8 V38; 2x; Disable Right Eye Render ON; Linear Filtering/Optimize generic shaders OFF; delay 0; Guest 3D 0%; Detailed GPU pass timing ON; new work controls OFF | The historical complete profile remains the current low-focused candidate. Its recorded floor is condition-qualified, not guaranteed on every replay. |
| Fire Emblem | Combo CPU Vertices OFF; stock; 4x; Linear Filtering/Optimize generic shaders ON; Disable Right Eye Render OFF; delay 0; Guest 3D 0%; new work controls OFF | Optimizer ON is the clearest useful choice. Detailed GPU pass timing OFF remains the practical candidate tested earlier; it has no demonstrated isolated overhead saving. |

These recommendations were **not written over your saved preferences**. Testing and comparison profiles remain fully documented. The Fire Emblem complete historical 1% Guest 3D profile remains a useful map-throughput alternative: its matched final-map throughput was about **28% above the otherwise matching complete 0% profile**. Its battle-low tradeoff remains important; that result should not be reduced to “1% is always better.”

Xenoblade's three-control package is **Reuse contained render passes + Typed primitive append + Adaptive CPU workers**, with Optimize generic shaders OFF and Detailed GPU pass timing ON:

| Complete profile | Battle low / mean / high | Stationary low / mean / high |
|---|---:|---:|
| v27 clean closing control | 49.00 / 104.16 / 162.17 | 112.88 / 114.90 / 116.40 |
| v27 package, original selected result | 68.75 / 114.80 / 157.88 | 123.85 / 126.47 / 130.19 |
| v27 package, confirmation | 47.36 / 113.46 / 164.83 | 113.93 / 115.68 / 119.27 |
| V38 historical profile, new work OFF | 66.99 / 112.56 / 164.11 | 118.52 / 121.17 / 123.28 |
| V38 historical profile + package | 53.06 / 106.42 / 162.22 | 131.28 / 134.35 / 137.68 |
| Stock + package | 42.91 / 90.62 / 144.82 | 123.04 / 126.70 / 129.53 |

The v27 confirmation supports **+8.93% battle-average direction** over the nearby clean control, while its low is 3.36% worse. It does not support the original exceptional floor. V38's package improves stationary throughput but lowers the battle result. Keep the package as an average-performance experiment, not a broad default or proven low fix. Low shots, clock differences and chronology are retained in the confirmation report (source not included in this compact archive).

**CPU/GPU demand and All-Core charts**

| Closing .57 measured section | App CPU core equivalents | CPU7 busy | GPU busy |
|---|---:|---:|---:|
| Dark Moon opening | 2.36 | 95.81% | 47.90% |
| Dark Moon first lab | 1.98 | 91.57% | 80.12% |
| Dark Moon ghost/moon | 2.17 | 93.40% | 70.45% |
| Xenoblade clean battle cinematic | 0.95 | N/A¹ | 94.52% |
| Xenoblade confirmed package, battle cinematic | 1.03 | 71.65% | 91.55% |
| Fire Emblem matched map, optimizer ON | 0.84 | 72.64% | 99.20% |

¹ One CPU7 accounting interval exceeded available elapsed CPU time and remains masked. Valid low/high endpoints are retained. Whole-device CPU0–7 readings include all processes; app core equivalents come from separate process accounting and are not a sum of the displayed cores.

Dark Moon still shows strong CPU7 pressure, with cores3–6 doing useful work. Xenoblade/Fire Emblem's demanding sections show substantial GPU pressure despite lower app CPU demand. More CPU activity alone will not fix their fragment cost. At the confirmed Xenoblade battle low, CPU7 was45.75% busy at1.8432GHz and GPU95.58% at680MHz; at its high, CPU7 was93.17% and GPU71.83%. The limiting balance changes within a scene.

Dark Moon All-Core utilization (source not included in this compact archive)

Xenoblade All-Core utilization (source not included in this compact archive)

Fire Emblem All-Core utilization (source not included in this compact archive)

Dark Moon scene-speed chart (source not included in this compact archive) · Xenoblade scene-speed chart (source not included in this compact archive) · Fire Emblem scene-speed chart (source not included in this compact archive). FEA's broad plotted map means contain differing views; the matched-view table above is the direct comparison.

**What the controls actually did, and the next engineering priorities**

1. **Improve predictable shader readiness and reduce GPU shader work.** The bad Dark Moon Typed primitive append + V38 static state path combination dropped ghost mean to54.54%, versus roughly85% for its parents; GPU busy was99.5% at its low. Audit admission/replacement in both partial and full CPU-layout pipeline banks, and reuse existing dead-work analysis in the direct SPIR-V fragment path actually used. Keep ownership/retirement safeguards. This is distinct from the earlier lifetime-credit leak.
2. **Reduce measured CPU transport cost for Dark Moon.** Existing profiles identify output conversion and input recipes at5.75% and4.80% of sampled user CPU respectively. Fixed-layout conversion, shared-stride recipes and profitable owned upload work on the existing recording worker are concrete candidates. Those CPU shares are not predicted FPS gains. Preserve ordering and avoid introducing another copy or queue drain.
3. **Make adaptive bookkeeping earn its cost.** The confirmed Xenoblade package recorded5,030 samples and zero trials/accepted changes. Its average-speed improvement cannot be credited to learned scheduling. Keep static parallel work independent; repair cohort/trial formation or bypass uneconomic bookkeeping. An earlier stock CPU-Vertices-ON case did accept a trial, so universal removal is not justified solely by this profile.
4. **Join workload identity to existing sampled GPU timings.** Current draw shares and sampled pass times cannot identify the exact shader/target/rectangle responsible for each low. Add bounded route/fragment identity and target metadata to existing samples, including mixed-pass markers. Avoid an expensive new per-draw timing/hash system. Track the observer's own cost and logger omissions.
5. **Retain only demonstrated per-game work reductions.** Containment executed around50–52 accepted boundaries in Xenoblade's measured battle interiors and zero in stationary gameplay; it had zero accepted work in the selected Dark Moon tests. Narrow included scissor reduced FEA map bounding area but did not earn a general speed recommendation. Prepare displayed eye only removes one of three presentation preparations, not guest-eye rendering. The early-right-eye probes did not demonstrate omitted draws; expose the failing prerequisite before spending another run on the same inactive path. V38 static forcing did not earn retention.
6. **Address the recorded exit stall and diagnostic gaps alongside real fixes.** Preserve the v27 Native Dark Moon shutdown evidence. Ordinary logs were retained in full, while some optional events were dropped inside the application; do not describe these as complete event-level attribution. Keep invalid counters and short unscored attack phases explicit. Useful low-cost guest-progress/phase markers would improve future alignment beyond current five-second aggregates.

The report provides actionable directions without inventing confidence intervals from correlated windows inside one run. The fresh controls show no broad default-performance collapse, and the interaction search gives concrete combinations to avoid. No production code or new build was changed during this testing task. Existing unrelated checkout modifications remain untouched.

