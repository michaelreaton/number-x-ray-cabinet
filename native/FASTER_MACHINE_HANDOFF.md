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

## Full-Workspace Div2+Div3 Scout

Follow-up run:

- Validation: `native/build-codex-large-mul-campaign/Release/xray_native_tests.exe`
  printed `native xray tests passed`
- Artifact:
  `native-test-runs/20260619-122328-c4b04caf/benchmark.tsv`

This run adds `mul-large-toom-div2-div3-scout` plus per-size
`mul-large-toom-div2-div3-point` rows. The candidate combines the checked
division-by-two shifts with the exact division-by-three interpolation shortcut
inside the leaf64/depth2 full-workspace Toom probe. The active window remains
`11717`, `16384`, `24103`, `32768`, `52163`, and `65536`.

| Row | Sizes | Leaf64 Max | Current Max | GMP Max | Worst Pair | Safe Sizes | Hash | Decision |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `mul-large-toom-div2-div3-scout` | `11717..65536` | `0.958` | `0.719` | `1.292` | `1.416` | `1/6` | `108/108` | observe only |

The combined shortcut is the strongest interpolation scout so far: exact,
median-positive against leaf64 at every active-window size, and current-route
faster throughout. It is still not promotion-ready because only `16384` clears
the full safe-size bar, GMP remains ahead on the larger rows, and stable-pair
counts miss `8/9` at `32768` and `52163`.

## Full-Workspace Combo Leaf48 Scout

Follow-up run:

- Validation: `native/build-codex-large-mul-campaign/Release/xray_native_tests.exe`
  printed `native xray tests passed`
- Artifact:
  `native-test-runs/20260619-135039-c4b04caf/benchmark.tsv`

This run adds `mul-large-toom-cmb-leaf48-scout` plus per-size
`mul-large-toom-cmb-leaf48-point` rows. The candidate keeps the combined
division-by-two plus division-by-three interpolation shortcut, lowers the leaf
handoff to 48, and compares against the same combined shortcut at leaf64. The
active window remains `11717`, `16384`, `24103`, `32768`, `52163`, and
`65536`, so the deterministic random spots stay in the gate.

| Row | Sizes | Combo64 Max | Current Max | GMP Max | Worst Pair | Safe Sizes | Hash | Decision |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `mul-large-toom-cmb-leaf48-scout` | `11717..65536` | `1.017` | `0.719` | `1.257` | `1.334` | `0/6` | `108/108` | observe only |

Lowering the handoff after the combined interpolation shortcut is exact and
still faster than current production multiply, but it does not improve the
combo leaf64 baseline. The random spots remain decision-changing:
`11717` regresses to `1.017`, `24103` is only `0.986`, and `52163` regresses
to `1.008`. Do not route combo leaf48; keep combo leaf64 as the stronger
active-window scout and move next toward backend/GMP gap work or broader Toom
structure.

## Full-Workspace Combo Depth3 Scout

Follow-up run:

- Validation: `native/build-codex-large-mul-campaign/Release/xray_native_tests.exe`
  printed `native xray tests passed`
- Artifact:
  `native-test-runs/20260619-142015-c4b04caf/benchmark.tsv`

This run adds `mul-large-toom-cmb-depth3-scout` plus per-size
`mul-large-toom-cmb-depth3-point` rows. The candidate keeps the combined
division-by-two plus division-by-three interpolation shortcut and raises the
recursive Toom depth limit from 2 to 3 at leaf64. The active window remains
`11717`, `16384`, `24103`, `32768`, `52163`, and `65536`, preserving the
deterministic random spots between the power-of-two anchors.

| Row | Sizes | ComboDepth2 Max | Current Max | GMP Max | Worst Pair | Safe Sizes | Hash | Decision |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `mul-large-toom-cmb-depth3-scout` | `11717..65536` | `1.014` | `0.729` | `1.220` | `1.277` | `0/6` | `108/108` | observe only |

Deeper recursion is exact and still beats current production multiply, but it
does not clear the combo depth2 baseline gate. The random spots are again
useful: `11717` and `24103` regress against combo depth2, while the larger
`52163` random spot improves on median but still misses GMP and worst-pair
safety. Keep depth3 observe-only; the combo depth2/leaf64 path remains the
stronger active-window baseline.

## Full-Workspace Combo Lower-Window Scout

Follow-up run:

- Validation: `native/build-codex-large-mul-campaign/Release/xray_native_tests.exe`
  printed `native xray tests passed`
- Artifact:
  `native-test-runs/20260619-145709-c4b04caf/benchmark.tsv`

This run adds `mul-large-toom-cmb-lower-scout` plus per-size
`mul-large-toom-cmb-lower-point` rows. It checks the current strongest
combined interpolation candidate at the lower promotion blockers:
`4096`, `5639`, and `8192`, with `5639` as the deterministic random spot
between the power-of-two anchors. Production multiply remains unchanged.

| Row | Sizes | Leaf64 Max | Current Max | GMP Max | Worst Pair | Safe Sizes | Hash | Decision |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `mul-large-toom-cmb-lower-scout` | `4096,5639,8192` | `0.979` | `0.911` | `0.893` | `1.012` | `1/3` | `54/54` | observe only |

The combo candidate is median-positive against current production and
GMP/MPIR at every lower-window size, so the lower band is no longer an obvious
median blocker. It is still not promotion-ready: `5639` and `8192` miss the
`8/9` baseline-stability bar, and the aggregate worst pair reaches `1.012`.
Keep this as a lower-window scout and require a repeat/route audit before any
default route change.

## Full-Window Combo Route Audit

Follow-up run:

- Validation: `native/build-codex-large-mul-campaign/Release/xray_native_tests.exe`
  printed `native xray tests passed`
- Artifact:
  `native-test-runs/20260619-153624-c4b04caf/benchmark.tsv`

This run adds `mul-large-toom-cmb-route-audit` plus per-size
`mul-large-toom-cmb-route-point` rows over the complete promotion window:
`4096`, `5639`, `8192`, `11717`, `16384`, `24103`, `32768`, `52163`, and
`65536`. The audit compares `full-ws-combo-depth2` directly against current
production multiply and GMP/MPIR with no diagnostic leaf64 baseline in the
gate. Production multiply remains unchanged.

| Row | Sizes | Current Max | GMP Max | Current/GMP Max | Worst Pair | Safe Sizes | Hash | Decision |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `mul-large-toom-cmb-route-audit` | `4096..65536` + spots | `0.924` | `1.298` | `1.947` | `1.378` | `5/9` | `162/162` | observe only |

The combo route beats current production multiply at every measured size,
including all deterministic random spots. It is not promotion-ready because
the GMP/MPIR gate fails starting at `24103`; the upper random spot `52163`
also remains a backend regression. Keep the route audit as evidence that the
app-shaped route is now better than current production, but continue attacking
the backend-facing gap before any default multiply route change.

## Combo Leaf48 Depth3 Upper Scout

Follow-up run:

- Validation: `native/build-codex-large-mul-campaign/Release/xray_native_tests.exe`
  printed `native xray tests passed`
- Artifact:
  `native-test-runs/20260619-160707-c4b04caf/benchmark.tsv`

This run adds `mul-large-toom-cmb-l48d3-scout` plus per-size
`mul-large-toom-cmb-l48d3-point` rows for the upper failure band:
`24103`, `32768`, `52163`, and `65536`. The candidate combines the two
stronger individual scouts, using combo interpolation with leaf48 and depth3,
then compares it against the current combo leaf64/depth2 shape, current
production multiply, and GMP/MPIR in the same rotating run.

| Row | Sizes | Base Max | Current Max | GMP Max | Current/GMP Max | Worst Pair | Safe Sizes | Hash | Decision |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `mul-large-toom-cmb-l48d3-scout` | `24103,32768,52163,65536` | `0.980` | `0.633` | `1.180` | `2.041` | `1.208` | `0/4` | `72/72` | observe only |

Per-size signal:

| Digits | L48D3 / L64D2 | L48D3 / Current | L48D3 / GMP | L64D2 / GMP | Current / GMP | Worst Pair | Stable Base | Stable GMP | Status |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `24103` | `0.980` | `0.633` | `0.973` | `0.988` | `1.545` | `1.055` | `4/9` | `7/9` | combo-l64d2-regression |
| `32768` | `0.969` | `0.626` | `1.048` | `1.089` | `1.682` | `1.065` | `7/9` | `0/9` | combo-l64d2-regression |
| `52163` | `0.920` | `0.568` | `1.049` | `1.136` | `1.844` | `1.070` | `9/9` | `0/9` | backend-regression |
| `65536` | `0.917` | `0.571` | `1.180` | `1.279` | `2.041` | `1.208` | `9/9` | `0/9` | backend-regression |

This is a useful improvement over the previous full-window route candidate:
the upper-band `candGmpMax` improves from about `1.278` to `1.180`, and
`24103` now beats GMP/MPIR on median. It is still not promotion-ready:
`32768`, `52163`, and `65536` remain backend regressions, and worst-pair plus
stable-pair gates fail. The next upper-band scout should look beyond simple
leaf/depth combination, likely at deeper structure or a handoff strategy for
the high end.

## Combo Leaf48 Depth4 Upper Scout

Follow-up run:

- Validation: `native/build-codex-large-mul-campaign/Release/xray_native_tests.exe`
  printed `native xray tests passed`
- Artifact:
  `native-test-runs/20260619-163518-c4b04caf/benchmark.tsv`

This run adds `mul-large-toom-cmb-l48d4-scout` plus per-size
`mul-large-toom-cmb-l48d4-point` rows for the same upper window:
`24103`, `32768`, `52163`, and `65536`. The candidate keeps combo
interpolation and leaf48, then raises the recursion cap from depth3 to depth4.
It compares directly against the l48d3 scout shape, current production multiply,
and GMP/MPIR in the same rotating run.

| Row | Sizes | Base Max | Current Max | GMP Max | Base/GMP Max | Current/GMP Max | Worst Pair | Safe Sizes | Hash | Decision |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `mul-large-toom-cmb-l48d4-scout` | `24103,32768,52163,65536` | `1.030` | `0.632` | `1.221` | `1.185` | `2.117` | `1.300` | `0/4` | `72/72` | observe only |

Per-size signal:

| Digits | L48D4 / L48D3 | L48D4 / Current | L48D4 / GMP | L48D3 / GMP | Current / GMP | Worst Pair | Status |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `24103` | `1.030` | `0.602` | `0.995` | `0.967` | `1.654` | `1.090` | combo-l48d3-regression |
| `32768` | `1.018` | `0.632` | `1.029` | `1.085` | `1.757` | `1.225` | combo-l48d3-regression |
| `52163` | `0.958` | `0.543` | `1.106` | `1.125` | `1.944` | `1.199` | combo-l48d3-regression |
| `65536` | `0.991` | `0.558` | `1.221` | `1.185` | `2.117` | `1.300` | combo-l48d3-regression |

Depth4 does not beat the l48d3 upper scout overall. It improves some middle
upper medians, but loses at `24103` and `65536`, worsens the aggregate
`candGmpMax`, and leaves all promotion gates closed. Treat this as a negative
result for simply increasing Toom recursion depth; the next high-end slice
should test a different structure or handoff rather than depth5.

## Combo Leaf48 Depth3 Full-Window Audit

Follow-up run:

- Validation: `native/build-codex-large-mul-campaign/Release/xray_native_tests.exe`
  printed `native xray tests passed`
- Artifact:
  `native-test-runs/20260619-184951-c4b04caf/benchmark.tsv`

This run adds `mul-large-toom-cmb-l48d3-full` plus per-size
`mul-large-toom-cmb-l48d3-fpt` rows across the complete CPU multiply campaign
window: `4096`, `5639`, `8192`, `11717`, `16384`, `24103`, `32768`, `52163`,
and `65536`. The row compares the best recent upper-window shape
(`full-ws-combo-l48d3`) against the prior combo depth2 baseline, current
production multiply, and GMP/MPIR in the same rotating run.

| Row | Sizes | Base Max | Current Max | GMP Max | Base/GMP Max | Current/GMP Max | Worst Pair | Safe Sizes | Hash | Decision |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `mul-large-toom-cmb-l48d3-full` | `4096,5639,8192,11717,16384,24103,32768,52163,65536` | `1.051` | `0.981` | `1.179` | `1.294` | `1.899` | `1.216` | `0/9` | `162/162` | observe only |

Per-size signal:

| Digits | L48D3 / L64D2 | L48D3 / Current | L48D3 / GMP | L64D2 / GMP | Current / GMP | Worst Pair | Status |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `4096` | `1.051` | `0.981` | `0.872` | `0.811` | `0.897` | `1.126` | combo-l64d2-regression |
| `5639` | `0.987` | `0.875` | `0.771` | `0.759` | `0.878` | `1.112` | combo-l64d2-regression |
| `8192` | `1.014` | `0.876` | `0.901` | `0.901` | `1.039` | `1.136` | combo-l64d2-regression |
| `11717` | `1.042` | `0.735` | `0.923` | `0.887` | `1.225` | `1.203` | combo-l64d2-regression |
| `16384` | `0.991` | `0.705` | `0.937` | `0.952` | `1.336` | `1.132` | combo-l64d2-regression |
| `24103` | `0.997` | `0.667` | `0.999` | `1.014` | `1.510` | `1.118` | combo-l64d2-regression |
| `32768` | `0.964` | `0.665` | `1.068` | `1.107` | `1.601` | `1.216` | combo-l64d2-regression |
| `52163` | `0.921` | `0.600` | `1.055` | `1.141` | `1.762` | `1.065` | backend-regression |
| `65536` | `0.915` | `0.625` | `1.179` | `1.294` | `1.899` | `1.205` | backend-regression |

The full-window l48d3 audit is exact and beats current production multiply at
every measured size, including every deterministic random spot. It is still not
promotion-ready: it regresses against the l64d2 baseline at lower anchors, loses
to GMP/MPIR from `32768` upward, and fails worst-pair/stable-pair safety. Keep
it as an observe-only probe and use it as evidence that the next CPU multiply
slice needs a different high-end structure or a stricter thresholded handoff.

## Combo L64D2 To L48D3 Handoff Audit

Follow-up run:

- Validation: `native/build-codex-large-mul-campaign/Release/xray_native_tests.exe`
  printed `native xray tests passed`
- Artifact:
  `native-test-runs/20260619-190919-c4b04caf/benchmark.tsv`

This run adds `mul-large-toom-cmb-hand`, a benchmark-only handoff audit. The
candidate keeps `full-ws-combo-l64d2` below `32768` decimal digits and switches
to `full-ws-combo-l48d3` at `32768` and above. It covers the same full campaign
window, including all deterministic random spots, and compares the mixed route
against current production multiply and GMP/MPIR in each point row.

| Row | Sizes | Current Max | GMP Max | Current/GMP Max | Worst Pair | Safe Sizes | Hash | Decision |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `mul-large-toom-cmb-hand` | `4096,5639,8192,11717,16384,24103,32768,52163,65536` | `0.887` | `1.208` | `1.934` | `1.357` | `3/9` | `162/162` | observe only |

Per-size signal:

| Digits | Active Candidate | Candidate / Current | Candidate / GMP | Current / GMP | Worst Pair | Status |
| ---: | --- | ---: | ---: | ---: | ---: | --- |
| `4096` | `full-ws-combo-l64d2` | `0.887` | `0.813` | `0.947` | `0.935` | combo-handoff-clean |
| `5639` | `full-ws-combo-l64d2` | `0.884` | `0.799` | `0.915` | `0.966` | combo-handoff-clean |
| `8192` | `full-ws-combo-l64d2` | `0.810` | `0.900` | `1.104` | `1.357` | current-regression |
| `11717` | `full-ws-combo-l64d2` | `0.674` | `0.952` | `1.326` | `0.986` | combo-handoff-clean |
| `16384` | `full-ws-combo-l64d2` | `0.787` | `0.984` | `1.319` | `1.088` | backend-regression |
| `24103` | `full-ws-combo-l64d2` | `0.645` | `0.981` | `1.530` | `1.066` | backend-regression |
| `32768` | `full-ws-combo-l48d3` | `0.657` | `1.059` | `1.624` | `1.114` | backend-regression |
| `52163` | `full-ws-combo-l48d3` | `0.578` | `1.112` | `1.871` | `1.174` | backend-regression |
| `65536` | `full-ws-combo-l48d3` | `0.612` | `1.208` | `1.934` | `1.243` | backend-regression |

The thresholded handoff is exact and materially stronger than a full-window
l48d3 policy against current production multiply, but it still cannot be
promoted. It leaves GMP/MPIR regressions from `32768` upward and still fails
worst-pair/stable-pair gates. Keep it as the best current handoff clue, not a
route. The next CPU multiply slice should target the high-end arithmetic gap
itself, likely with a different multiplication structure rather than another
l64/l48 threshold tweak.

## 2026-06-19: Combo Upper Same-Input Tournament

Run:

- Release:
  `native-test-runs/20260619-193550-c4b04caf`

This run adds `mul-large-toom-cmb-tourn`, a benchmark-only upper-window
tournament over the four strongest current combo shapes on identical operand
fingerprints in the same run:

- `full-ws-combo-l64d2`
- `full-ws-combo-l48d3`
- `full-ws-combo-l48d4`
- `full-ws-combo-handoff-l64d2-l48d3`

It covers `24103`, `32768`, `52163`, and `65536`, preserving the deterministic
random spots between the power-of-two anchors.

Observed aggregate:

| Operation | Sizes | Winner / Current Max | Winner / GMP Max | Current / GMP Max | Worst Pair Max | Safe Sizes | Hash Gate | Decision |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `mul-large-toom-cmb-tourn` | `24103,32768,52163,65536` | `0.657` | `1.197` | `1.906` | `1.302` | `0/4` | `288/288` | observe only |

Per-size winners:

| Digits | Winner | Winner / Current | Winner / GMP | Current / GMP | Worst Pair | Status |
| ---: | --- | ---: | ---: | ---: | ---: | --- |
| `24103` | `full-ws-combo-l48d4` | `0.587` | `0.967` | `1.587` | `1.172` | backend-regression |
| `32768` | `full-ws-combo-l48d4` | `0.657` | `1.069` | `1.627` | `1.221` | backend-regression |
| `52163` | `full-ws-combo-l48d3` | `0.574` | `1.066` | `1.813` | `1.098` | backend-regression |
| `65536` | `full-ws-combo-l48d3` | `0.612` | `1.197` | `1.906` | `1.302` | backend-regression |

Decision: keep the tournament observe-only. It proves the prior row-to-row
comparisons were not hiding a simple threshold answer: l48d4 wins the lower
half of the upper window, l48d3 wins the two largest sizes, and every winner
still fails GMP/MPIR or worst-pair safety. The next implementation slice should
target a structural high-end multiply improvement, not another promotion audit.

## 2026-06-19: Combo Reusable Workspace Scout

Run:

- Release:
  `native-test-runs/20260619-200105-c4b04caf`

This run adds `mul-large-toom-cmb-reuse`, a benchmark-only no-realloc scout.
It uses the same winner-by-size route from the upper tournament:
`full-ws-combo-l48d4` at `24103` and `32768`, then
`full-ws-combo-l48d3` at `52163` and `65536`. The candidate keeps
caller-owned Toom/Karatsuba recursive workspaces across repeated multiply
calls; the baseline uses the same route without workspace reuse.

Observed aggregate:

| Operation | Sizes | Reuse / Non-Reuse Max | Reuse / Current Max | Reuse / GMP Max | Non-Reuse / GMP Max | Worst Pair Max | Safe Sizes | Hash Gate | Decision |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `mul-large-toom-cmb-reuse` | `24103,32768,52163,65536` | `0.990` | `0.621` | `1.177` | `1.191` | `1.209` | `0/4` | `72/72` | observe only |

Per-size signal:

| Digits | Active Candidate | Reuse / Non-Reuse | Reuse / Current | Reuse / GMP | Non-Reuse / GMP | Current / GMP | Worst Pair | Status |
| ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `24103` | `full-ws-combo-l48d4` | `0.893` | `0.613` | `0.926` | `1.014` | `1.583` | `1.138` | reuse-baseline-regression |
| `32768` | `full-ws-combo-l48d4` | `0.958` | `0.621` | `1.001` | `1.097` | `1.600` | `1.181` | reuse-baseline-regression |
| `52163` | `full-ws-combo-l48d3` | `0.948` | `0.594` | `1.079` | `1.137` | `1.819` | `1.125` | backend-regression |
| `65536` | `full-ws-combo-l48d3` | `0.990` | `0.611` | `1.177` | `1.191` | `1.916` | `1.209` | reuse-baseline-regression |

Decision: keep the reusable workspace probe observe-only. It proves allocator
and workspace-preparation overhead are real on the upper window, but the
improvement is not stable enough for a route gate and still leaves the GMP/MPIR
gap open. The next structural work should keep caller-owned workspaces in mind,
but also attack the arithmetic shape itself.

## 2026-06-19: Combo Best-Map Full-Window Audit

Run:

- Release:
  `native-test-runs/20260619-202535-c4b04caf`

This run adds `mul-large-toom-cmb-map`, a benchmark-only full-window audit of
the strongest known fixed map from the recent lower, handoff, tournament, and
reuse scouts. It keeps `full-ws-combo-l64d2` below `24103`, switches to
`full-ws-combo-l48d4` for `24103` and `32768`, then switches to
`full-ws-combo-l48d3` for `52163` and `65536`. The row covers the full campaign
window, including every deterministic in-between size.

Observed aggregate:

| Operation | Sizes | Candidate / Current Max | Candidate / GMP Max | Current / GMP Max | Worst Pair Max | Safe Sizes | Hash Gate | Decision |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `mul-large-toom-cmb-map` | `4096,5639,8192,11717,16384,24103,32768,52163,65536` | `0.930` | `1.204` | `2.063` | `2.869` | `0/9` | `162/162` | observe only |

Per-size signal:

| Digits | Active Candidate | Candidate / Current | Candidate / GMP | Current / GMP | Worst Pair | Status |
| ---: | --- | ---: | ---: | ---: | ---: | --- |
| `4096` | `full-ws-combo-l64d2` | `0.921` | `0.860` | `0.945` | `1.195` | current-regression |
| `5639` | `full-ws-combo-l64d2` | `0.919` | `0.809` | `0.838` | `1.206` | current-regression |
| `8192` | `full-ws-combo-l64d2` | `0.930` | `0.953` | `1.147` | `1.560` | current-regression |
| `11717` | `full-ws-combo-l64d2` | `0.741` | `1.021` | `1.432` | `1.170` | backend-regression |
| `16384` | `full-ws-combo-l64d2` | `0.667` | `0.972` | `1.495` | `1.152` | backend-regression |
| `24103` | `full-ws-combo-l48d4` | `0.739` | `1.088` | `1.385` | `2.204` | current-regression |
| `32768` | `full-ws-combo-l48d4` | `0.634` | `1.119` | `1.758` | `2.869` | current-regression |
| `52163` | `full-ws-combo-l48d3` | `0.567` | `1.092` | `1.927` | `1.158` | backend-regression |
| `65536` | `full-ws-combo-l48d3` | `0.591` | `1.204` | `2.063` | `1.232` | backend-regression |

Decision: keep the best-map audit observe-only. It gives the clearest
full-window summary of the current multiply campaign and beats current
production multiply at every measured size, but it still misses the strict
proof bar badly: `safeSizes=0/9`, GMP/MPIR regressions start at `11717`, and
worst-pair safety fails at both random spots and power-of-two anchors. This is
useful route-map evidence, not a production route.

## 2026-06-20: Combo Best-Map Duplicate-Control Audit

Run:

- Release:
  `native-test-runs/20260620-013252-c4b04caf`

This run adds `mul-large-toom-cmb-map-ctrl`, a focused duplicate-control audit
for the two worst-pair outlier sizes from the best-map route: deterministic
random spot `24103` and power-of-two anchor `32768`. It times the exact same
best-map route against a duplicate of itself, current production multiply, and
`mpz_mul` on the same operand fingerprints in the same run.

Observed aggregate:

| Operation | Sizes | Control Ratio Max | Control Worst Max | Candidate / Current Max | Candidate / GMP Max | Current / GMP Max | Worst Pair Max | Safe Sizes | Hash Gate | Decision |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `mul-large-toom-cmb-map-ctrl` | `24103,32768` | `0.997` | `1.077` | `0.638` | `1.040` | `1.633` | `1.091` | `0/2` | `36/36` | observe only |

Per-size signal:

| Digits | Control Ratio | Control Worst | Candidate / Current | Candidate / GMP | Current / GMP | Product Worst | Control Stable | GMP Stable | Status |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `24103` | `0.991` | `1.077` | `0.638` | `0.979` | `1.540` | `1.058` | `9/9` | `7/9` | backend-regression |
| `32768` | `0.997` | `1.044` | `0.637` | `1.040` | `1.633` | `1.091` | `9/9` | `0/9` | backend-regression |

Decision: treat the previous best-map outlier signal as real enough to block
promotion, not just harness noise. The self-duplicate control is stable at both
sizes, while the same candidate still misses GMP/MPIR and stable-pair gates.
Keep the route observe-only and move the next multiply work toward a deeper
arithmetic-structure change.

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
cost: the combined `/2` plus `/3` shortcut finally beats the leaf64 baseline on
median across the active window, but it still misses GMP and stable-pair gates.
Keep future scouts on the mixed window with deterministic in-between sizes, not
only power-of-two anchors. If a candidate passes exact parity, worst-pair
safety, and stable-pair gates across the full window, run a forced route audit
against current production multiply. If the larger rows remain unstable, keep
these probes opt-in and move to a broader interpolation/evaluation rewrite,
GMP-facing backend improvement, or a different handoff design.
