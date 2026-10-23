# Required pre-submission validation

<!-- CodexAstraLocal: The .48–.53 failure review identified incomplete final-tree fixture execution, not a reason to weaken release gates. -->
Run executable checks before submitting an emulator candidate. Source review,
Python syntax, XML parsing and an Android compilation are useful but cannot prove
that source-extracted C++ fixtures still compile or target the correct behavior.
The release workflow remains the final shader/Android/signing/publication gate.

## Choose checks from the production change

<!-- CodexAstraLocal: Tests that read changed production files are affected even when their own files have no diff. -->
For every changed production class/function, search `tools/uberhar` and `src/tests`
for its file name, class, method, constructor and setting names. Inspect extractors,
recording doubles, include requirements and deliberate-defect anchors. Run every
affected ordinary/static variant and its negative controls. A compilation error,
missing anchor or timeout never counts as successfully rejecting a deliberate bug.

Current shader-cache consumers include:

| Changed production boundary | Required fixture commands |
| --- | --- |
| Queued VS/GS/ready-full fragment work | `python3 tools/uberhar/test_shader_compile_policy.py` |
| Optional shader cache/worker | `python3 tools/uberhar/test_optional_fragment_worker.py` and `--static-tev` |
| Ready fragment selection and binding | `python3 tools/uberhar/test_ready_cpu_fragments.py --mutants` and `--static-tev --mutants` |
| Failure/teardown and cache ownership | Existing shader-worker recovery, adaptive-cache, descriptor and lifetime commands in `build_probe.sh` |
| Full GLSL generation | Existing module validation, TEV, full fragment color/depth and CPU-fragment ABI pixel gates in `uberhar-shaders.yml` |
| Android title/Testing configuration layers | `python3 tools/uberhar/test_android_settings_layers.py` plus actual pinned Android NDK checks for changed JNI translation units |
| Android settings persistence and inheritance | `python3 tools/uberhar/test_android_settings_scope.py --kotlinc <compiler> --ini4j <jar>` with compatible `JAVA_HOME`; full Android build remains separate |

This table identifies current consumers, not an exhaustive future allowlist. A
changed renderer, queue or constructor requires its own dependency search. Real
Android C++ changes require the pinned NDK/compiler check; a desktop GCC pass does
not establish libc++ or ARM64 compatibility. Retain prior executable evidence only
when its exact sources, headers, configuration and tools still match.

## Validate the final integration once

<!-- CodexAstraLocal: Broad source rebases and many-agent integrations cannot rely only on each agent's selected standalone checks. -->
For a broad integration such as .53, run **`bash tools/uberhar/build_probe.sh`**
against the complete candidate and pinned dependencies before release submission.
Its early shader extraction block is the same ordinary/static check set used by
CI and is not duplicated later in that script. Run changed shader output/module
gates as well. Record the commands, exit codes, source identity and output paths.
Keep private logs/artifacts out of public commits.

If a check fails, fix it and run that check, then continue through the remaining
unreached commands. Do not resubmit after repairing only the first failure. Passed
unaffected checks need not be repeated; changing a shared production dependency
invalidates the affected earlier results. Track the reached boundary explicitly.
After the last fix, the collected passes must cover the final integrated source,
not a mixture of obsolete intermediate versions. The integrator verifies this
coverage and the architect reviews the dependency map before submission.

## Keep evidence honest and useful

<!-- CodexAstraLocal: Executable integration assurance is not hardware performance qualification. -->
A recording endpoint establishes routing and lifecycle behavior; it does not prove
real GPU pixels. Shader compilers and host pixel comparisons cover different
claims. Android build success, signing, publication and owner gameplay acceptance
are separate results. Report a failed gate by its actual error, and distinguish
stale harnesses from production defects or external infrastructure failures.
Do not call a candidate ready until its required release gates pass.
