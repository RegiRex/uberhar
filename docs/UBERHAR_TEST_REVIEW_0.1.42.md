# Uberhar 0.1.42 device review

<!-- CodexAstraLocal: Retain one test per mode and distinguish runtime failures from missing performance measurements. -->
The qualified .42 APK was upgraded in place with compatible signing and its
installed checksum verified. One Dark Moon opening each in Native and Combo
completed through the ghost/moon sequence and later Luigi/TV scenes, followed by
normal game-list returns. Vulkan 2x, normal 100%, GPU timings on, File 1 reset,
and empty application shader caches were verified. Private game evidence stays
outside the public repository. The owner assesses noticeable graphics artifacts;
a few stills do not qualify consecutive-frame correctness.

| Mode | Complete five-second windows | Mean speed | Best / slowest speed | App CPU, logical-core equivalents |
| --- | ---: | ---: | ---: | ---: |
| Native | 48 | 69.061% | 92.931% / 53.536% | 2.013 |
| Combo | 47 | 64.865% | 93.699% / 40.852% | 1.909 |
| CPU Software 1x | None: startup stopped | Unavailable | Unavailable | Unavailable |

The Native speed population covers 240.658 s; Combo 235.664 s. Intervals crossing
start or exit input are excluded. Telemetry follows its own measured brackets;
whole-app CPU includes worker threads and is not a capacity-weighted utilization
percentage. Per-core system counters cannot establish emulator core residency.
Unlike scene weighting prevents an exact causal comparison with earlier builds.
Neither successful run demonstrates sustained 99% speed.

<!-- CodexAstraLocal: GPU points and nearby CPU intervals have different integration windows; do not manufacture synchronized per-window averages. -->
| Mode/window | GPU busy sample | GPU clock | App CPU in overlapping intervals | RSS / KGSL allocation MiB |
| --- | ---: | ---: | ---: | ---: |
| Native best | 56.58% | 401MHz | 1.666–1.988 cores | 890.96 /1099.05 |
| Native slowest | 98.50% | 680MHz | 1.800–1.804 cores | 879.45 /1097.16 |
| Combo best | 71.26% | 401MHz | 1.978–2.094 cores | 1005.46 /3389.32 |
| Combo slowest | 95.35% | 680MHz | 0.982–1.193 cores | 1004.52 /3317.47 |

GPU samples are device-wide with an opaque accumulation period. CPU intervals
overlap, rather than fit entirely within, these five-second speed windows. RSS
and KGSL are different gauges and must not be added. Battery remained 80%,
25–26 °C for Native and 27 °C for Combo, with AC connected and reported thermal
status 0. Battery temperature is not a validated SoC temperature; thermal HAL,
measured power, DDR bandwidth and GPU-per-core counters remain unavailable.

The slow windows coincide with high GPU activity, making GPU work a continuing
optimization priority. This is evidence of pressure, not attribution to a
specific shader or proof that every dip has the same cause. Native's queue now
executes auxiliary work: 86,819 completed packets, 45,968 waves and 24,374 auxiliary
packets in interior reports; cumulative maximum wave 12 and participants 6.
Observed PICA-pool stable-thread-pair CPU is only 0.042 core-equivalents. More
parallel admission has not yet produced a demonstrated large frame-rate gain.

<!-- CodexAstraLocal: A failed startup is a real test result, never a zero-speed gameplay run. -->
CPU Software stopped before the title menu with `invalid LCD framebuffer span`,
after eight system frames and zero game submissions. The shader cache had been
cleared; File 1 reset could not be reached. No Software opening or speed result
is claimed. The error dialog failed to respond to OK, and app-only restart was
required after retaining final logs. No app-data clear or reinstall was used.
A .43 scanout correction passes focused host checks; those checks cannot
replace owner confirmation on Thor. The protected original save-state checksum
is unchanged. .42 remains installed in Native 2x, with idle displays put to sleep.
