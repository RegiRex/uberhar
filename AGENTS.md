# Uberhar development instructions

<!-- AstraEH: Owner-requested continuity rules for this experimental fork. -->

- Attribute new logical implementation sections and their purpose with `AstraEH`
  comments. Keep the file-level map in `docs/UBERHAR_CODE_MAP.md` current. Do not
  imply that inherited Azahar code is AstraEH work.
- Use release.beta.alpha versions. Publish Android ARM64 tests as GitHub
  pre-releases through the existing build, correctness, package and signing gates.
- Perform a full architectural review every **3–5 alpha builds**, targeting four.
  Read `docs/UBERHAR_REVIEW_CADENCE.md` before planning a release and update it when
  a review is completed. Review sooner after a material correctness regression or
  evidence that the current design cannot meet its goal.
- Reviews must recheck source, device evidence, architectural alternatives,
  correctness constraints, measurable goals and the next implementation order.
  Distinguish measured results from proposals and host tests from device tests.
- Preserve exact rendering and draw order. Missing compilation must not silently
  omit a draw. Do not remove driver workarounds without specific validation.
- Finish source changes and useful local validation, start Actions, then hand off
  the run link. Do not keep a chat turn open just to poll compilation. Do not call
  an APK ready until the publication gates have passed.
- Keep display synchronization and model clarity on the roadmap, after the
  first-playthrough shader architecture is working well enough to assess.
