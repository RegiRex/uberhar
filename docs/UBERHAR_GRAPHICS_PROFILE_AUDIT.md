# Native/Combo inherited graphics settings audit

<!-- CodexAstraLocal: .55 source audit prompted by the owner's Sonic comparison; observations are not device qualification. -->

Native and Combo apply `src/common/uberhar_test_profile.h` after loading ordinary
settings. Android displays corresponding effective values through
`UberharGraphicsProfile`. The saved global/title values are preserved.

| Forced setting group | Current effective behavior | Why / follow-up |
|---|---|---|
| Graphics API | Vulkan | Current renderer implementation; OpenGL is a separate Custom path. |
| Hardware shaders / shader JIT | Hardware shader route OFF, CPU shader JIT ON | Native/Combo vertex-routing contract. Enabling inherited hardware shaders would change the method, not merely optimize it. |
| Async shader compilation / hybrid / forced TEV | No skipped asynchronous draws; hybrid ON; force TEV depends on Native vs Combo | Preserve complete cold output and current route ownership. |
| Accurate multiplication | ON | Preserve graphics correctness; no blanket relaxation. |
| Disk shader cache | ON | Useful generic and compatible driver cache persistence. Optional specialization persistence extends this separately. |
| Generic optimizer baseline | Legacy disable-optimizer setting forced ON | First-use compilation latency tradeoff, not a correctness requirement. .55 adds **Optimize generic shaders** as an independent launch-only comparison; it does not silently flip full-shader generation. |
| Texture filter / sampling | No texture filter / game-controlled sampling | Avoid added filtering work and preserve game sampling semantics. |
| Render-thread delay | Zero | Do not add an artificial delay. Simulate 3DS GPU Timings remains separately selectable. |
| Stereo / right-eye suppression | Stereo display OFF; **Disable Right Eye Rendering OFF** | Right-eye commands continue. This already matches the owner's newly successful Sonic Azahar configuration. |
| Presentation filtering | Linear filtering ON | Host screen scaling; not guest texture filtering. A future controlled output-quality/performance comparison is possible. |
| Texture replacement/dumping | OFF | Benchmark/native-output policy. These are optional content features, not required rendering optimizations. |
| Duplicate-frame skipping | OFF | Preserve complete output. |

Resolution, integer scaling, full-fragment SPIR-V/GLSL choice and explicit Uberhar
comparison controls remain independently selectable. .55's Turnip pass-end
submission and generic optimizer controls are outside the inherited-settings lock.

## Sonic right-eye finding

`video_core/right_eye_disabler.cpp` identifies likely duplicate top-screen command
queues/display transfers using guessed command properties and frame history, then
can suppress them. This is broader than hiding a second presentation image. The
owner reports that enabling **Disable Right Eye Rendering** harmed Sonic in base
Azahar; disabling that setting restored strong performance. That is a valuable
compatibility observation, but no captured measurement identifies its exact cause.
Native/Combo already force the suppression option OFF. Do not claim .55 fixes Sonic
by changing it, or unlock suppression globally without validating guest-visible work.

Next owner test: Sonic's third track in base Azahar with suppression OFF and Uberhar
.55, matching driver, resolution, turbo and gameplay route. Keep warm Azahar and cold
Uberhar compiler behavior distinct. Capture lap/section frame pacing, lowest five-second
speed, shader admission/selection, CPU owner/worker time, GPU intervals, waits and clocks.
The first-track benchmark remains useful but does not qualify later tracks.
