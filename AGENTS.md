# Uberhar development instructions

<!-- AstraEH: Owner-requested continuity rules for this experimental fork. -->

- AstraEH: Owner attribution update (2026-10-03): use `AstraEH` for new
  logical sections and their purpose in the current development context. Preserve
  historical `AstraEH`/`AstraPro` comments and all inherited Azahar authorship; never relabel
  old work. Keep `docs/UBERHAR_CODE_MAP.md` current. Future attribution changes
  follow an explicit owner instruction when product model selection is not
  reliably available; the marker does not assert automatic selector detection.
- Mark each new experimental/debug log call with an adjacent `AstraEH Log Line`
  comment. Bound record counts or reporting frequency; retain error recovery when
  removing diagnostics. Document each feature's counters and their limits.
- Use release.beta.alpha versions. Publish Android ARM64 tests as GitHub
  pre-releases through the existing build, correctness, package and signing gates.
- AstraEH: Standing owner authorization (2026-09-28): publish project source to
  the public `RegiRex/uberhar` repository on `uberhar/hybrid-shaders` and start the
  gated Android prerelease workflow as development progresses. Normally advance
  the work and start a build on each development turn unless the owner says to
  hold off or skip building. A question-only turn can be an exception when no
  build is useful. Always state when a build is skipped and why. This standing
  authorization covers routine project publication without renewed permission.
- Perform a full architectural review every **3–5 alpha builds**, targeting four.
  Read `docs/UBERHAR_REVIEW_CADENCE.md` before planning a release and update it when
  a review is completed. Review sooner after a material correctness regression or
  evidence that the current design cannot meet its goal.
- Reviews must recheck source, device evidence, architectural alternatives,
  correctness constraints, measurable goals and the next implementation order.
  Distinguish measured results from proposals and host tests from device tests.
- AstraEH: Keep the visible progress tracker current at the start of substantial
  work, at meaningful milestone changes and at handoff; name the current version
  and distinguish implemented, building and device-tested work. Maintain the
  owner's `Uberhar_Roadmap.html` review document with milestone gates, trajectory,
  evidence, risks and a ledger separating owner scope requests from proposals.
  Refresh that same document at every full architectural review, retain its change
  history and update it earlier when scope or release status materially changes.
  A roadmap refresh alone does not reset the architecture-review build count.
- Preserve exact rendering and draw order. Missing compilation must not silently
  omit a draw. Do not remove driver workarounds without specific validation.
- Finish source changes and useful local validation, start Actions, then hand off
  the run link. Do not keep a chat turn open just to poll compilation. Do not call
  an APK ready until the publication gates have passed.
- Keep display synchronization and model clarity on the roadmap, after the
  first-playthrough shader architecture is working well enough to assess.
- AstraEH: Owner test-analysis preference (2026-09-28): exclude temporary
  fast-forward and mixed speed-transition intervals from normal-performance
  comparisons unless the owner explicitly requests a speed/headroom test.
  Fast-forward usually skips menus/loading or is accidental; it does not by
  itself prove a loading phase. Derive resolution/settings and scoped cache
  reuse from logs; ask for manual notes only when they add unseen scene context.

<!-- AstraEH: Owner accepted the 0.1.13 review plan and resumed builds on 2026-10-03. -->
- Keep the audit's broader renderer/performance experiments in the deferred ledger
  until at least Dark Moon's graphical issues are corrected and validated on device.
  Crash evidence, confirmed correctness hazards and focused diagnosis remain active.
- Per-game settings are required by 2.0: inherit globals, store explicit title-ID
  overrides, reset to global and expose effective values. Provide the same editor
  from the title long-press menu (beside cache deletion) and from an in-game Uberhar
  settings submenu. This is not a requirement to include the whole feature in 0.1.14.
- AstraEH: Owner feedback (2026-10-04): remove per-launch session-bundle clutter
  from normal log export. Keep current/previous/older text logs primary. Preserve
  meaningful crash evidence separately; ordinary/empty launches are not crash reports.
