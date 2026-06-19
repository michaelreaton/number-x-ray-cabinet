# Faster Machine Handoff

This handoff captures the current bigint benchmark state and the next
experiments to run on a faster CPU. Treat these notes as a reproducibility
checklist, not as proof that a route should be promoted.

## Current Baseline

Latest merged bigint benchmark PR:

- PR: `#193`, `benchmark: audit karatsuba split views`
- Merge commit: `dff16fc0d626ba8eb4c9325f2afe24e0d6997e1b`
- Local artifact:
  `native/build-codex-exact-estimate/native-test-runs/20260618-030505-c4b04caf/benchmark.tsv`
- Local validation:
  `ctest --test-dir native\build-codex-exact-estimate -C Release --output-on-failure`
  passed `4/4`.

Key local rows from the laptop:

| Row | Digits | Ratio | Worst Pair | Stable Pairs | Decision |
| --- | ---: | ---: | ---: | ---: | --- |
| `mul-karatsuba-view-vs-copy` | 4096 | `0.940` | `1.131` | `4/5` | observe only |
| `mul-karatsuba-view-vs-copy` | 8192 | `0.996` | `1.088` | `2/5` | observe only |
| `mul-karatsuba-view-vs-copy` | 16384 | `0.956` | `1.000` | `3/5` | observe only |
| `mul-dense-leaf-vs-scan` | 4096 | `0.971` | `1.042` | `3/5` | observe only |
| `mul-dense-leaf-vs-scan` | 8192 | `0.964` | `1.038` | `3/5` | observe only |
| `mul-dense-leaf-vs-scan` | 16384 | `0.991` | `1.001` | `1/5` | observe only |

Lower ratio is better for candidate-vs-baseline rows. These rows are exact by
parity checks, but they are not production-ready because stability is mixed and
the 8192-digit split-view row is flat.

Latest local diagnostic workspace campaign:

- Build folder: `native/build-codex-large-mul-campaign`
- Build: Visual Studio 16 2019 x64 Release, `CMAKE_C_FLAGS_RELEASE=/O2 /Ob2 /DNDEBUG`, IPO `/GL=false`
- CPU: Intel(R) Core(TM) i7-6820HQ CPU @ 2.70GHz, flags `SSE2 SSE4.2 POPCNT AES FMA AVX AVX2 BMI1 BMI2 ADX`
- Validation: `native/build-codex-large-mul-campaign/Release/xray_native_tests.exe`
  printed `native xray tests passed`
- Artifact:
  `native-test-runs/20260618-233248-c4b04caf/benchmark.tsv`

This run adds `mul-large-cpu-campaign`, a diagnostic `kernel-probe` row that
compares current production multiply, split-view Karatsuba, split-view plus
workspace reuse, and `mpz_mul` on the same operand fingerprints. It covers the
power-of-two anchors plus deterministic in-between spots:
`4096`, `5639`, `8192`, `11717`, `16384`, `24103`, `32768`, `52163`, `65536`.
All rows are `noAutoRoute=1`, `replacementReady=false`, and `observe-only`.
On MSVC x64, the same run also emits `mul-large-cpu-toom-branch` rows for a
recursive Toom-3 plus unroll4 leaf branch (`leafThreshold=64`, `depthLimit=2`)
and `mul-large-cpu-toom-view-branch` rows that isolate Toom split-copy tax by
timing read-only operand-third split views against the copied Toom branch.

| Row | Digits | Ratio | Worst Pair | Stable Pairs | Decision |
| --- | ---: | ---: | ---: | ---: | --- |
| `mul-large-cpu-campaign` | 4096 | `0.810` | `1.092` | `4/5` | observe only |
| `mul-large-cpu-campaign` | 5639 | `0.849` | `1.370` | `4/5` | observe only |
| `mul-large-cpu-campaign` | 8192 | `0.940` | `1.104` | `4/5` | observe only |
| `mul-large-cpu-campaign` | 11717 | `0.834` | `1.059` | `1/5` | observe only |
| `mul-large-cpu-campaign` | 16384 | `0.837` | `1.455` | `1/5` | observe only |
| `mul-large-cpu-campaign` | 24103 | `0.767` | `1.349` | `0/5` | observe only |
| `mul-large-cpu-campaign` | 32768 | `0.842` | `1.684` | `1/5` | observe only |
| `mul-large-cpu-campaign` | 52163 | `0.801` | `1.651` | `0/5` | observe only |
| `mul-large-cpu-campaign` | 65536 | `0.828` | `2.464` | `0/5` | observe only |

Toom branch rows from the same run:

| Row | Digits | Ratio | Worst Pair | Stable Pairs | Decision |
| --- | ---: | ---: | ---: | ---: | --- |
| `mul-large-cpu-toom-branch` | 4096 | `1.067` | `2.606` | `0/5` | observe only |
| `mul-large-cpu-toom-branch` | 5639 | `1.165` | `1.891` | `1/5` | observe only |
| `mul-large-cpu-toom-branch` | 8192 | `0.861` | `1.165` | `1/5` | observe only |
| `mul-large-cpu-toom-branch` | 11717 | `0.969` | `1.415` | `0/5` | observe only |
| `mul-large-cpu-toom-branch` | 16384 | `0.834` | `1.268` | `2/5` | observe only |
| `mul-large-cpu-toom-branch` | 24103 | `0.833` | `1.584` | `0/5` | observe only |
| `mul-large-cpu-toom-branch` | 32768 | `0.815` | `1.481` | `1/5` | observe only |
| `mul-large-cpu-toom-branch` | 52163 | `0.740` | `1.545` | `0/5` | observe only |
| `mul-large-cpu-toom-branch` | 65536 | `0.663` | `2.157` | `0/5` | observe only |

Toom split-view rows from the same run:

| Row | Digits | Ratio | Worst Pair | Stable Pairs | Decision |
| --- | ---: | ---: | ---: | ---: | --- |
| `mul-large-cpu-toom-view-branch` | 4096 | `0.978` | `1.303` | `0/5` | observe only |
| `mul-large-cpu-toom-view-branch` | 5639 | `0.805` | `1.451` | `2/5` | observe only |
| `mul-large-cpu-toom-view-branch` | 8192 | `1.193` | `1.513` | `0/5` | observe only |
| `mul-large-cpu-toom-view-branch` | 11717 | `0.999` | `1.498` | `0/5` | observe only |
| `mul-large-cpu-toom-view-branch` | 16384 | `0.945` | `1.356` | `1/5` | observe only |
| `mul-large-cpu-toom-view-branch` | 24103 | `0.956` | `1.328` | `1/5` | observe only |
| `mul-large-cpu-toom-view-branch` | 32768 | `0.990` | `1.610` | `1/5` | observe only |
| `mul-large-cpu-toom-view-branch` | 52163 | `0.990` | `1.800` | `0/5` | observe only |
| `mul-large-cpu-toom-view-branch` | 65536 | `0.977` | `2.108` | `0/5` | observe only |

The stacked Toom workspace probe run:

- Validation: `native/build-codex-large-mul-campaign/Release/xray_native_tests.exe`
  printed `native xray tests passed`
- Artifact:
  `native-test-runs/20260619-001030-c4b04caf/benchmark.tsv`

This run adds `mul-large-cpu-toom-ws-branch`, which compares recursive Toom-3
plus unroll4 with read-only operand-third views and reusable per-depth Toom
temporaries against current, Karatsuba workspace, copied Toom, split-view Toom,
and `mpz_mul`.

| Row | Digits | Ratio | Worst Pair | Stable Pairs | Decision |
| --- | ---: | ---: | ---: | ---: | --- |
| `mul-large-cpu-toom-ws-branch` | 4096 | `0.855` | `1.120` | `1/5` | observe only |
| `mul-large-cpu-toom-ws-branch` | 5639 | `0.957` | `1.127` | `0/5` | observe only |
| `mul-large-cpu-toom-ws-branch` | 8192 | `0.924` | `1.171` | `2/5` | observe only |
| `mul-large-cpu-toom-ws-branch` | 11717 | `0.838` | `1.134` | `0/5` | observe only |
| `mul-large-cpu-toom-ws-branch` | 16384 | `0.940` | `1.265` | `2/5` | observe only |
| `mul-large-cpu-toom-ws-branch` | 24103 | `1.015` | `1.644` | `0/5` | observe only |
| `mul-large-cpu-toom-ws-branch` | 32768 | `1.047` | `1.380` | `0/5` | observe only |
| `mul-large-cpu-toom-ws-branch` | 52163 | `0.914` | `1.371` | `0/5` | observe only |
| `mul-large-cpu-toom-ws-branch` | 65536 | `0.901` | `2.585` | `1/5` | observe only |

The workspace, copied Toom, split-view Toom, and Toom workspace probes are exact
and sometimes median-positive, including several in-between sizes, but none
clears the promotion bar. Treat them as opt-in probes and faster-machine targets,
not production routes.

The stacked full workspace probe run:

- Validation: `native/build-codex-large-mul-campaign/Release/xray_native_tests.exe`
  printed `native xray tests passed`
- Artifact:
  `native-test-runs/20260619-004040-c4b04caf/benchmark.tsv`

This run adds `mul-large-cpu-toom-full-ws`, which keeps production multiply
unchanged and reuses Karatsuba temporaries on the recursive Toom handoff path in
addition to the per-depth Toom temporaries.

| Row | Digits | Ratio | Worst Pair | Stable Pairs | Decision |
| --- | ---: | ---: | ---: | ---: | --- |
| `mul-large-cpu-toom-full-ws` | 4096 | `0.913` | `1.495` | `1/5` | observe only |
| `mul-large-cpu-toom-full-ws` | 5639 | `0.911` | `1.500` | `2/5` | observe only |
| `mul-large-cpu-toom-full-ws` | 8192 | `0.954` | `1.379` | `3/5` | observe only |
| `mul-large-cpu-toom-full-ws` | 11717 | `0.975` | `1.240` | `1/5` | observe only |
| `mul-large-cpu-toom-full-ws` | 16384 | `0.947` | `1.169` | `2/5` | observe only |
| `mul-large-cpu-toom-full-ws` | 24103 | `0.945` | `1.459` | `0/5` | observe only |
| `mul-large-cpu-toom-full-ws` | 32768 | `0.960` | `1.496` | `1/5` | observe only |
| `mul-large-cpu-toom-full-ws` | 52163 | `0.937` | `1.369` | `0/5` | observe only |
| `mul-large-cpu-toom-full-ws` | 65536 | `1.052` | `1.769` | `0/5` | observe only |

The full workspace row is exact and median-positive against the prior Toom
workspace baseline at eight of nine campaign sizes, including all deterministic
in-between sizes, but still fails the promotion bar on worst-pair safety and
stable-pair counts. Treat it as the next faster-machine route-audit candidate,
not as a default route.

The stacked full workspace route audit run:

- Validation: `native/build-codex-large-mul-campaign/Release/xray_native_tests.exe`
  printed `native xray tests passed`
- Artifact:
  `native-test-runs/20260619-080250-c4b04caf/benchmark.tsv`

This run adds `mul-large-cpu-toom-full-audit`, which compares the full
workspace probe directly against current production multiply in the same large
multiply CPU campaign. It remains `noAutoRoute=1` and
`replacementReady=false`.

| Row | Digits | Ratio | Worst Pair | Stable Pairs | Decision |
| --- | ---: | ---: | ---: | ---: | --- |
| `mul-large-cpu-toom-full-audit` | 4096 | `1.003` | `1.100` | `2/5` | observe only |
| `mul-large-cpu-toom-full-audit` | 5639 | `0.954` | `1.065` | `3/5` | observe only |
| `mul-large-cpu-toom-full-audit` | 8192 | `0.947` | `1.020` | `4/5` | observe only |
| `mul-large-cpu-toom-full-audit` | 11717 | `0.791` | `0.851` | `5/5` | observe only |
| `mul-large-cpu-toom-full-audit` | 16384 | `0.815` | `0.860` | `5/5` | observe only |
| `mul-large-cpu-toom-full-audit` | 24103 | `0.831` | `0.868` | `5/5` | observe only |
| `mul-large-cpu-toom-full-audit` | 32768 | `0.902` | `0.919` | `5/5` | observe only |
| `mul-large-cpu-toom-full-audit` | 52163 | `0.750` | `0.770` | `5/5` | observe only |
| `mul-large-cpu-toom-full-audit` | 65536 | `0.769` | `0.870` | `5/5` | observe only |

The audit clears current-route median, worst-pair, and stable-pair gates from
`11717` through `65536`, including the deterministic random spots, but the full
`4096` through `65536` window is still blocked by `4096`, `5639`, and `8192`.
Use faster-machine reruns to decide whether the low-end misses are hardware
noise or whether promotion needs a thresholded handoff above `8192`.

The stacked active-window deep audit run:

- Validation: `native/build-codex-large-mul-campaign/Release/xray_native_tests.exe`
  printed `native xray tests passed`
- Artifact:
  `native-test-runs/20260619-092659-c4b04caf/benchmark.tsv`

This run adds `mul-large-toom-full-deep-audit`, an aggregate 9-sample route
audit for only the high-size handoff window: `11717`, `16384`, `24103`,
`32768`, `52163`, and `65536`. It remains `noAutoRoute=1` and
`replacementReady=false`.

| Row | Sizes | Current Max | GMP Max | Worst Pair | Safe Sizes | Hash | Decision |
| --- | --- | ---: | ---: | ---: | ---: | ---: | --- |
| `mul-large-toom-full-deep-audit` | `11717..65536` | `0.792` | `1.321` | `1.389` | `0/6` | `108/108` | observe only |

The deeper active-window audit is exact and still faster than current
production multiply on median, but it fails the GMP-facing and worst-pair gates.
Do not promote a thresholded high-size handoff from the current evidence.

Per-size point rows explain the aggregate rejection:

| Digits | Current Ratio | GMP Ratio | Worst Pair | Current Stable | GMP Stable |
| ---: | ---: | ---: | ---: | ---: | ---: |
| `11717` | `0.764` | `1.004` | `1.048` | `9/9` | `4/9` |
| `16384` | `0.762` | `1.021` | `1.121` | `9/9` | `2/9` |
| `24103` | `0.792` | `1.225` | `1.334` | `9/9` | `0/9` |
| `32768` | `0.743` | `1.157` | `1.293` | `9/9` | `0/9` |
| `52163` | `0.693` | `1.216` | `1.267` | `9/9` | `0/9` |
| `65536` | `0.689` | `1.321` | `1.389` | `9/9` | `0/9` |

The follow-up depth-3 scout run:

- Validation: `native/build-codex-large-mul-campaign/Release/xray_native_tests.exe`
  printed `native xray tests passed`
- Artifact:
  `native-test-runs/20260619-100600-c4b04caf/benchmark.tsv`

This run adds `mul-large-toom-depth-scout` plus per-size
`mul-large-toom-depth-point` rows. It compares depth-3 full workspace against
the depth-2 full-workspace baseline, current production multiply, and `mpz_mul`
on the same active window and keeps `noAutoRoute=1`, `replacementReady=false`.

| Row | Sizes | Depth2 Max | Current Max | GMP Max | Worst Pair | Safe Sizes | Hash | Decision |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `mul-large-toom-depth-scout` | `11717..65536` | `1.045` | `0.786` | `1.342` | `1.443` | `0/6` | `108/108` | observe only |

Depth 3 is exact and still faster than current production multiply, but it is
not a stable improvement over depth 2 and remains GMP/worst-pair unsafe. Do not
continue by merely increasing Toom recursion depth; tune leaf/handoff shape or
the lower-level Toom arithmetic next.

## Leaf96 Full-Workspace Scout

Follow-up run:

- Validation: `native/build-codex-large-mul-campaign/Release/xray_native_tests.exe`
  printed `native xray tests passed`
- Artifact:
  `native-test-runs/20260619-103207-c4b04caf/benchmark.tsv`

This run adds `mul-large-toom-leaf-scout` plus per-size
`mul-large-toom-leaf-point` rows. It compares leaf96 depth-2 full workspace
against the leaf64 depth-2 full-workspace baseline, current production multiply,
and `mpz_mul` on the active window, including deterministic random spots
between the powers of two. It remains `noAutoRoute=1`,
`replacementReady=false`.

| Row | Sizes | Leaf64 Max | Current Max | GMP Max | Worst Pair | Safe Sizes | Hash | Decision |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `mul-large-toom-leaf-scout` | `11717..65536` | `1.137` | `0.827` | `1.515` | `1.578` | `0/6` | `108/108` | observe only |

Leaf96 is exact and still faster than current production multiply across the
active window, but it loses to leaf64 at every measured size and remains
GMP/worst-pair unsafe. Do not promote leaf96; continue with lower-level Toom
arithmetic or a different handoff shape.

## Leaf48 Full-Workspace Scout

Follow-up run:

- Validation: `native/build-codex-large-mul-campaign/Release/xray_native_tests.exe`
  printed `native xray tests passed`
- Artifact:
  `native-test-runs/20260619-105456-c4b04caf/benchmark.tsv`

This run adds `mul-large-toom-leaf48-scout` plus per-size
`mul-large-toom-leaf48-point` rows over the same active window, including
deterministic random spots between powers of two.

| Row | Sizes | Leaf64 Max | Current Max | GMP Max | Worst Pair | Safe Sizes | Hash | Decision |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `mul-large-toom-leaf48-scout` | `11717..65536` | `1.030` | `0.804` | `1.340` | `1.457` | `0/6` | `108/108` | observe only |

Leaf48 is closer than leaf96 and wins median against leaf64 at `16384`,
`32768`, and `65536`, but it loses at `11717`, `24103`, and `52163`; those
deterministic random spots keep the gate honest. Do not promote leaf48; move
next into Toom interpolation/evaluation cost or another structural arithmetic
improvement below the handoff threshold.

## Full-Workspace Div2 Scout

Follow-up run:

- Validation: `native/build-codex-large-mul-campaign/Release/xray_native_tests.exe`
  printed `native xray tests passed`
- Artifact:
  `native-test-runs/20260619-112537-c4b04caf/benchmark.tsv`

This run adds `mul-large-toom-div2-scout` plus per-size
`mul-large-toom-div2-point` rows. The candidate keeps leaf64/depth2 full
workspace Toom but replaces the two exact division-by-two interpolation steps
with checked bit shifts. The active window remains
`11717`, `16384`, `24103`, `32768`, `52163`, and `65536`, so deterministic
random spots are measured between the power-of-two anchors.

| Row | Sizes | Leaf64 Max | Current Max | GMP Max | Worst Pair | Safe Sizes | Hash | Decision |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `mul-large-toom-div2-scout` | `11717..65536` | `1.008` | `0.726` | `1.282` | `1.408` | `0/6` | `108/108` | observe only |

The div2 shortcut is exact and beats current production multiply throughout
the active window, but it is not a promotion candidate. It loses to leaf64 at
`32768` and `52163`, never reaches the `8/9` stable-pair bar, and worst-pair
safety fails at every measured size. Keep it as a lower-level Toom clue and
continue with interpolation/evaluation structure rather than routing it.

## Full-Workspace Div3 Scout

Follow-up run:

- Validation: `native/build-codex-large-mul-campaign/Release/xray_native_tests.exe`
  printed `native xray tests passed`
- Artifact:
  `native-test-runs/20260619-115618-c4b04caf/benchmark.tsv`

This run adds `mul-large-toom-div3-scout` plus per-size
`mul-large-toom-div3-point` rows. The candidate keeps leaf64/depth2 full
workspace Toom but replaces the exact division-by-three interpolation step with
an odd-limb exact division shortcut. The active window is unchanged:
`11717`, `16384`, `24103`, `32768`, `52163`, and `65536`, preserving the
deterministic random spots between the power-of-two anchors.

| Row | Sizes | Leaf64 Max | Current Max | GMP Max | Worst Pair | Safe Sizes | Hash | Decision |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `mul-large-toom-div3-scout` | `11717..65536` | `0.999` | `0.720` | `1.324` | `1.364` | `0/6` | `108/108` | observe only |

The div3 shortcut is exact and beats current production multiply throughout
the active window. It is closer to the leaf64 full-workspace baseline than
div2 on median (`candBaseMax=0.999`), including the random spots, but it still
does not clear the strict promotion bar: several sizes miss the `<=0.98`
leaf64 threshold, no size reaches `8/9` stable pairs, and the GMP comparison
still trails on the larger rows. Keep it observe-only.

## Rebuild And Validate

Use a fresh build folder on the faster machine so compiler and processor
features are easy to compare.

```powershell
cmake -S native -B native/build-fast -DCMAKE_BUILD_TYPE=Release
cmake --build native/build-fast --config Release
ctest --test-dir native/build-fast -C Release --output-on-failure
```

For Visual Studio generators, prefer a clean `vcvars64.bat` environment and
record compiler version, `/GL` or IPO state, and detected CPU features from the
benchmark report.

## First Experiments

Run these in order.

1. Confirm existing rows on the faster machine.
   - Compare `mul-karatsuba-view-vs-copy`.
   - Compare `mul-dense-leaf-vs-scan`.
   - Compare scratch `mul` rows against `mpz_mul`.

2. Increase samples only for promising rows.
   - Do not globally lengthen every benchmark.
   - Require paired medians and same-run ratios.
   - Keep raw `benchmark.tsv`, `frontier.txt`, and report JSON.

3. Test split views plus allocation/workspace reduction.
   - Keep production multiply unchanged until a route audit passes.
   - Measure copied slices, split views, and split views with reusable
     temporaries in the same run.
   - Use the same operand families and leaf threshold first, then sweep
     thresholds only if the route is stable.

4. Revisit AVX/BMI2/ADX only with local proof.
   - Record CPU feature detection in the report.
   - Compare compiler flags in same-run or adjacent clean builds.
   - Do not trust prior AVX claims without local rows from this machine.

## Promotion Rules

Do not promote a bigint route unless all are true:

- Candidate outputs match the current route and GMP/MPIR oracle where present.
- Same-run paired median ratio is better at every target size in the policy
  window.
- Worst-pair ratio stays inside the policy threshold.
- Stable-pair count meets the benchmark gate.
- A forced route audit beats the current production route, not only GMP or an
  independent best row.
- The PR description includes raw artifact paths and the exact command line.

## Current Next Best Step

The strongest current clue remains lower-level Toom interpolation/evaluation
cost: checked `/2` and exact `/3` shortcuts improved the active full-workspace
family but did not clear the leaf64, stable-pair, or GMP gates. Keep future
scouts on the mixed window with deterministic in-between sizes, not only
power-of-two anchors. If a candidate passes exact parity, worst-pair safety,
and stable-pair gates across the full window, run a forced route audit against
current production multiply. If the larger rows remain unstable, keep these
probes opt-in and move to a broader interpolation/evaluation rewrite, a
combined interpolation shortcut, or a different handoff design.
