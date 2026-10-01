# Uberhar 0.1.10 — local validation record

<!-- AstraPro: Distinguish source candidates, host checks, CI publication and device acceptance. -->

**Status: implemented and validated locally; NOT committed to GitHub, NOT submitted to Actions, NOT built or signed as an Android APK.**

Intended public parent: `1bce59f62b595da60f7f386e654bffbdeea5c876` (released 0.1.9). The repository was read through the connected GitHub integration. This session exposed no repository-write or workflow-dispatch action; no authenticated GitHub CLI was available. The supplied patch is a normal source diff against that parent, not a published commit. The local reconstruction's synthetic Git commit is not an upstream commit and must not be published as history.

## Completed checks

| Check | Observed result | Record |
| --- | --- | --- |
| Complete existing host suite plus candidate tests | Exit 0 | `review-package/validation/host-suite010.txt` |
| New optional-fragment policy and transport admission | 278 cases passed; full-config collision/slot-replacement checks, reset and 1,000,000 saturated demand observations | Host suite and `review-package/validation/focused010.txt` |
| New optional-fragment and pipeline-error policy tests under ASan/UBSan | Exit 0 | `review-package/validation/focused010.txt` |
| Frame-end setting-range tests under ASan/UBSan | Exit 0; crossing windows, pause/re-anchor, auto-resolution zero and unknown context; raw timing retained | `review-package/validation/frame-context010.txt` |
| Inherited GPU admission and actual assembler tests | 4,992 admission cases, 200,000 routing decisions and 10,000 production-assembler differential sequences passed | Host suite |
| Inherited vertex/input checks | 998,282 output-conversion comparisons; 36,864 fused-input bitwise comparisons; 4,099 index-domain checks passed | Host suite |
| Integration syntax, final source revision | All six translation units passed after the explicit-optimizer overload was added | `review-package/validation/syntax010.txt` |
| Vulkan shader module validation | 1,066 modules in each of unoptimized and optimized modes; SPIR-V validation, DontUnroll and 128-byte transport ABI checks passed | `review-package/validation/shader-modules010.txt` |
| Full fragment color, depth and discard oracle | 1,081,344 exact RGBA8 pixel comparisons and 1,081,344 depth/discard comparisons across all 1,056 states passed | `review-package/validation/pixels-full010.txt` |
| TEV oracle | 208,896 exact RGBA8 comparisons across 816 six-stage programs and 208,896 texture-use checks passed | `review-package/validation/pixels-tev010.txt` |
| Compute versus Native rectangle oracle | 256 pixel comparisons and associated Vulkan module checks passed | `review-package/validation/pixels-rect010.txt` |
| Compiler-helper source invariant | Old three-argument API preserved; the implementation differs only by explicit optimizer input and the forwarding wrapper | `review-package/validation/compiler-overload010.txt` |

The six syntax-checked integration units are PICA core, Vulkan graphics pipeline, Vulkan pipeline cache, Vulkan shader disk cache, Vulkan rasterizer and core performance statistics. These checks include the new compiler-overload declaration and its caller, but do not link it. Inherited warnings remain; this is not a warning-free-build claim.

## Important outstanding checks

The complete `vk_shader_util.cpp` translation unit could not be compiled locally because pinned glslang development headers were unavailable. Its new four-argument overload uses the existing compiler body and explicitly supplied optimizer setting. A source-invariant test verifies that narrow change, but **does not substitute for compiling/linking the helper**. Android CI must compile and link the entire program, including this helper.

The pixel tests execute generated shaders on **Mesa llvmpipe (LLVM 19.1.7)**. They support the covered fragment semantics on that host implementation; they do not prove Adreno numerical parity, new GPU vertex precision, Android synchronization/lifecycle correctness, performance, power use or physical input latency. The specialized fragment module worker and ready-pipeline route require on-device validation. Host policy tests exercise admission/recovery rules, not a complete Android driver integration.

The full-fragment oracle's resident program/VAO cache was bounded to 16 entries to complete the corpus without unbounded object retention. Every original case, pixel comparison and tolerance remains. The comparison was completed, not sampled or truncated.

There is no measured 0.1.10 speedup. Four-times resolution remains unqualified for the stress suite. No shader accuracy or driver guard is removed, and pending/failed optional preparation still executes the complete CPU draw.

## Publication and acceptance sequence

1. Check the supplied patch and manifest against a clean clone at the exact parent. The included `apply_candidate.py` defaults to check-only; `--apply` modifies a working tree without publishing. `--publish` additionally commits and pushes to the authorized branch, using the user's existing Git authentication.
2. Preserve the existing shader/pixel/Vulkan, Android/unit, package, signing and publication gates. A successful push starts the existing workflow only if its normal trigger is enabled; inspect its real result rather than assuming success.
3. Only after successful publication use `TEST_CARD_0.1.10.md`. Validate Dark Moon's short matching 2× scene in Native and Combo and protect FEA/LEGO. Stop a clearly regressed scene instead of repeating a long cutscene.
4. Record actual specialized-ready use, exact output observations, scene speed and first-use/pacing regressions. Keep the public 0.1.9 release until the candidate passes acceptance.

The recorded early full 0.1.9 review is local until this patch is published. No new published successor has been counted in this turn.
