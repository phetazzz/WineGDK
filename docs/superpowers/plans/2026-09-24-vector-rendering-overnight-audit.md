# Vector Rendering Overnight Audit Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Audit every Direct2D rendering path exercised by Vector, fix only test-proven WineGDK defects, and leave reproducible evidence without modifying Vector, KKE, DXVK, or installed game files.

**Architecture:** Vector and KKE are read-only API-usage references. All execution uses an isolated GDK-Proton runner and prefix under `/tmp/opencode`; each effect test is run independently so failures and hangs are attributable to one case. Production changes are limited to `WineGDK/dlls/d2d1` and require a failing regression before implementation.

**Tech Stack:** WineGDK Direct2D, C, Wine tests, GDK-Proton 10-32, WineD3D, DXVK, Xvfb, umu SDK container.

**Spec:** Approved conversation scope: inspect all Vector rendering paths, modify only Wine source, require root-cause evidence, and require no user interaction during execution.

## Global Constraints

- Modify only `/home/peti/Projects/BedrockProjects/WineGDK` source files.
- Treat `/home/peti/Projects/BedrockProjects/client-feat-keyboard-gui` and `/tmp/opencode/kke` as read-only references.
- Do not install or replace BedrockOnLinux, Lutris, Proton, DXVK, Vector, or prefix files.
- Use isolated test runners and prefixes below `/tmp/opencode`.
- Do not discard or reset existing WineGDK changes.
- Add production fixes only after a regression test fails for the expected reason.
- Run targeted tests during development, one d2d1 regression, and x86_64/i386 builds.
- Do not push, publish, package, deploy, or modify CI/CD.

## Review Focus

- Deferred Direct2D errors must be reported by `Flush` with the drawing tags that caused them.
- Command-list playback must preserve pixels and drawing state after nested lists, layers, and failures.
- Blur/effect bounds must remain finite where required and must not allocate from unbounded logical bounds.
- Rounded geometry and projected line rendering must not write outside requested geometry bounds.
- A single failing or hanging effect case must not prevent identifying the rest of the suite.

---

### Task 1: Isolated per-effect test matrix

**Files:**
- Create: `/tmp/opencode/vector-render-audit/run-effect-matrix.py`
- Create: `/tmp/opencode/vector-render-audit/wined3d-results.json`
- Create: `/tmp/opencode/vector-render-audit/wined3d-summary.md`

**Interfaces:**
- Consumes: `D2D1_EFFECT_TEST=<test_name>` and `d2d1_test.exe effects`.
- Produces: one result record per `RUN_EFFECT_TEST`, including exit code, elapsed time, assertion summary, and timeout status.

- [ ] Extract every `RUN_EFFECT_TEST(test_name)` from `dlls/d2d1/tests/effects.c`.
- [ ] Run each test independently under the isolated GDK-Proton runner with `WINEDEBUG=-all`, Xvfb, and a 120-second timeout.
- [ ] Exclude `test_animated_blur_timing` from correctness totals and record it separately as a benchmark.
- [ ] Save complete per-case logs and machine-readable results.
- [ ] Re-run failed or timed-out cases once to distinguish deterministic failures from environmental instability.

### Task 2: DXVK comparison matrix

**Files:**
- Create: `/tmp/opencode/vector-render-audit/dxvk-results.json`
- Create: `/tmp/opencode/vector-render-audit/dxvk-summary.md`

**Interfaces:**
- Consumes: the Task 1 test list and the isolated GDK-Proton DXVK DLLs.
- Produces: the same result schema as Task 1, permitting case-by-case backend comparison.

- [ ] Clone only the isolated prefix used by Task 1.
- [ ] Place the runner's DXVK `d3d11.dll` and `dxgi.dll` into the isolated DXVK prefix and enable native overrides there.
- [ ] Run every non-benchmark effect test independently with the same timeout and environment controls.
- [ ] Re-run failures once and classify WineD3D-only, DXVK-only, shared, or infrastructure failures.

### Task 3: Root-cause and fix confirmed Wine defects

**Files:**
- Modify as required: `dlls/d2d1/device.c`
- Modify as required: `dlls/d2d1/command_list.c`
- Modify as required: `dlls/d2d1/effect_render.c`
- Modify as required: `dlls/d2d1/geometry.c`
- Test: `dlls/d2d1/tests/effects.c`
- Test: `dlls/d2d1/tests/d2d1.c`

**Interfaces:**
- Consumes: deterministic failures shared by isolated reruns and attributable to Wine source.
- Produces: one regression and one minimal Wine fix per confirmed defect.

- [ ] For each confirmed Wine defect, add the smallest test that reproduces observable incorrect output or HRESULT behavior.
- [ ] Run the test against the pre-fix implementation and preserve the expected RED output.
- [ ] Implement one root-cause fix without executable-name checks, DXVK changes, or application-specific behavior.
- [ ] Run the focused test to GREEN, then run related rendering cases.
- [ ] Leave Vector-side or KKE-side defects unfixed and document their source evidence instead.

### Task 4: Regression, architecture builds, and report

**Files:**
- Create: `/tmp/opencode/vector-render-audit/final-report.md`
- Update: `/home/peti/Projects/BedrockProjects/GDK-Proton-d2d-build/vector-effects-progress.md`

**Interfaces:**
- Consumes: Tasks 1-3 results and the final WineGDK source tree.
- Produces: verified build/test evidence and a distinction between fixed, unresolved, and non-Wine findings.

- [ ] Run focused Vector-relevant effect tests: command lists, layers, bounds, blur, composite, transforms, rounded geometry, and `Flush` error propagation.
- [ ] Run the full d2d1 entry point once under Xvfb and record known versus new failures.
- [ ] Build `dlls/d2d1/all` for x86_64 and i386 using the pinned umu SDK image.
- [ ] Run `git diff --check` and inspect only the final task-owned diff without reverting pre-existing work.
- [ ] Write the final report with exact commands, counts, hashes, limitations, and no claim that the in-game issue is fixed without game evidence.
