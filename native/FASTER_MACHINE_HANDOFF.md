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

## Combo Reuse-Map Full-Window Audit

Artifact: `native-test-runs/20260620-015720-c4b04caf`

This run adds `mul-large-toom-cmb-reuse-map`, a benchmark-only full-window
audit for the same mapped route but with reusable recursive Toom workspace
temporaries. It keeps the full campaign window:
`4096`, `5639`, `8192`, `11717`, `16384`, `24103`, `32768`, `52163`, and
`65536`.

Aggregate signal:

| Row | Sizes | Reuse / Non-Reuse Max | Reuse / Current Max | Reuse / GMP Max | Non-Reuse / GMP Max | Current / GMP Max | Worst Pair | Safe Sizes | Hash Safe | Decision |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `mul-large-toom-cmb-reuse-map` | `4096..65536` | `0.996` | `0.829` | `1.132` | `1.128` | `1.878` | `1.192` | `0/9` | `162/162` | observe only |

Per-size signal:

| Digits | Active Candidate | Reuse / Non-Reuse | Reuse / Current | Reuse / GMP | Non-Reuse / GMP | Current / GMP | Worst Pair | GMP Stable | Status |
| ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `4096` | `full-ws-combo-l64d2` | `0.875` | `0.807` | `0.752` | `0.847` | `0.907` | `1.091` | `9/9` | reuse-baseline-regression |
| `5639` | `full-ws-combo-l64d2` | `0.949` | `0.829` | `0.737` | `0.790` | `0.889` | `1.001` | `9/9` | reuse-baseline-regression |
| `8192` | `full-ws-combo-l64d2` | `0.979` | `0.817` | `0.855` | `0.879` | `1.048` | `1.025` | `9/9` | reuse-baseline-regression |
| `11717` | `full-ws-combo-l64d2` | `0.938` | `0.673` | `0.880` | `0.942` | `1.315` | `0.997` | `9/9` | reuse-baseline-regression |
| `16384` | `full-ws-combo-l64d2` | `0.996` | `0.689` | `0.928` | `0.940` | `1.340` | `1.008` | `9/9` | reuse-baseline-regression |
| `24103` | `full-ws-combo-l48d4` | `0.967` | `0.629` | `0.956` | `0.985` | `1.517` | `0.989` | `9/9` | reuse-baseline-regression |
| `32768` | `full-ws-combo-l48d4` | `0.976` | `0.639` | `1.014` | `1.030` | `1.585` | `1.020` | `2/9` | reuse-baseline-regression |
| `52163` | `full-ws-combo-l48d3` | `0.965` | `0.576` | `1.017` | `1.050` | `1.763` | `1.030` | `1/9` | backend-regression |
| `65536` | `full-ws-combo-l48d3` | `0.980` | `0.603` | `1.132` | `1.128` | `1.878` | `1.192` | `1/9` | reuse-baseline-regression |

Decision: reusable workspace materially helps the mapped route and keeps exact
parity/hash across the full random-spot window, but it still fails promotion.
The route beats current production multiply everywhere, but GMP/MPIR and
stable-pair gates fail from `32768` upward and worst-pair safety peaks at
`1.192`. Keep it observe-only and move toward arithmetic shape work instead of
another workspace-only route promotion.

## Combo Leaf32 Depth4 Upper Scout

Artifact: `native-test-runs/20260620-021539-c4b04caf`

This run adds `mul-large-toom-cmb-l32d4-scout`, a benchmark-only upper-window
arithmetic-shape scout. It compares `full-ws-combo-l32d4` against
`full-ws-combo-l48d4` on the same operand fingerprints at `24103`, `32768`,
`52163`, and `65536`.

Aggregate signal:

| Row | Sizes | Candidate / L48D4 Max | Candidate / Current Max | Candidate / GMP Max | L48D4 / GMP Max | Current / GMP Max | Worst Pair | Safe Sizes | Hash Safe | Decision |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `mul-large-toom-cmb-l32d4-scout` | `24103,32768,52163,65536` | `1.098` | `0.693` | `1.188` | `1.167` | `1.900` | `1.228` | `0/4` | `72/72` | observe only |

Per-size signal:

| Digits | L32D4 / L48D4 | L32D4 / Current | L32D4 / GMP | L48D4 / GMP | Current / GMP | Worst Pair | Stable vs L48D4 | GMP Stable | Status |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `24103` | `0.976` | `0.651` | `0.979` | `1.001` | `1.508` | `1.014` | `5/9` | `8/9` | combo-l48d4-regression |
| `32768` | `1.070` | `0.693` | `1.079` | `1.035` | `1.585` | `1.166` | `0/9` | `0/9` | combo-l48d4-regression |
| `52163` | `1.098` | `0.645` | `1.146` | `1.052` | `1.776` | `1.165` | `0/9` | `0/9` | combo-l48d4-regression |
| `65536` | `1.011` | `0.626` | `1.188` | `1.167` | `1.900` | `1.228` | `1/9` | `0/9` | combo-l48d4-regression |

Decision: reject leaf32/depth4 for this campaign. It only nudges the `24103`
random spot on median and regresses the larger rows against `l48d4`, with
stable-pair and worst-pair failures throughout. The next shape scout should
try a different handoff or interpolation/evaluation structure, not a smaller
Toom leaf at the same depth.

## 2026-06-20: Combo In-Place Interpolation Upper Scout

Artifact: `native-test-runs/20260620-024037-c4b04caf`

This run adds `mul-large-toom-cmb-ipdiv`, a benchmark-only upper-window scout
that keeps the `full-ws-combo-l48d4` arithmetic shape but performs the exact
division-by-three and division-by-two interpolation updates in place. It
compares `full-ws-combo-inplace-div-l48d4` against the regular
`full-ws-combo-l48d4` path on identical operand fingerprints at `24103`,
`32768`, `52163`, and `65536`.

Summary:

| Row | Sizes | Candidate / Baseline Max | Candidate / Current Max | Candidate / GMP Max | Baseline / GMP Max | Current / GMP Max | Worst Pair Max | Safe Sizes | Hash Safe | Decision |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `mul-large-toom-cmb-ipdiv` | `24103,32768,52163,65536` | `1.025` | `0.658` | `1.182` | `1.182` | `1.923` | `1.230` | `0/4` | `72/72` | observe only |

Per-size signal:

| Digits | In-Place / L48D4 | In-Place / Current | In-Place / GMP | L48D4 / GMP | Current / GMP | Worst Pair | Stable vs L48D4 | GMP Stable | Status |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `24103` | `1.025` | `0.628` | `1.015` | `1.004` | `1.617` | `1.167` | `3/9` | `4/9` | combo-l48d4-regression |
| `32768` | `0.995` | `0.658` | `1.054` | `1.060` | `1.609` | `1.084` | `1/9` | `0/9` | combo-l48d4-regression |
| `52163` | `1.000` | `0.568` | `1.055` | `1.054` | `1.858` | `1.070` | `0/9` | `0/9` | combo-l48d4-regression |
| `65536` | `1.009` | `0.617` | `1.182` | `1.182` | `1.923` | `1.230` | `0/9` | `0/9` | combo-l48d4-regression |

Decision: reject in-place interpolation division as a promotion direction. It
preserves exact parity but does not beat the regular `l48d4` baseline under the
strict gate, remains behind GMP/MPIR at every upper-window size, and keeps the
worst-pair/stability failures. Treat the result as evidence that temporary-copy
removal inside interpolation is not the high-end blocker by itself.

## 2026-06-20: Combo Leaf64 Depth3 Upper Scout

Artifact: `native-test-runs/20260620-025945-c4b04caf`

This run adds `mul-large-toom-cmb-l64d3-scout`, a benchmark-only upper-window
leaf/depth scout. It compares `full-ws-combo-l64d3` against
`full-ws-combo-l48d4` on identical operand fingerprints at `24103`, `32768`,
`52163`, and `65536`.

Summary:

| Row | Sizes | Candidate / Baseline Max | Candidate / Current Max | Candidate / GMP Max | Baseline / GMP Max | Current / GMP Max | Worst Pair Max | Safe Sizes | Hash Safe | Decision |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `mul-large-toom-cmb-l64d3-scout` | `24103,32768,52163,65536` | `1.059` | `0.696` | `1.201` | `1.170` | `1.900` | `1.224` | `0/4` | `72/72` | observe only |

Per-size signal:

| Digits | L64D3 / L48D4 | L64D3 / Current | L64D3 / GMP | L48D4 / GMP | Current / GMP | Worst Pair | Stable vs L48D4 | GMP Stable | Status |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `24103` | `0.983` | `0.642` | `0.978` | `0.996` | `1.548` | `1.017` | `4/9` | `8/9` | combo-l48d4-regression |
| `32768` | `1.059` | `0.696` | `1.113` | `1.047` | `1.598` | `1.185` | `0/9` | `0/9` | combo-l48d4-regression |
| `52163` | `1.050` | `0.624` | `1.105` | `1.048` | `1.751` | `1.122` | `0/9` | `0/9` | combo-l48d4-regression |
| `65536` | `1.026` | `0.638` | `1.201` | `1.170` | `1.900` | `1.224` | `0/9` | `0/9` | combo-l48d4-regression |

Decision: reject leaf64/depth3 as a route direction. It is only near-useful at
the `24103` random spot and still misses the strict 0.98 baseline gate there;
the larger rows regress against `l48d4`, fail GMP/MPIR, and fail stability.
Future scouts should move away from simple leaf/depth reshaping.

## 2026-06-20: Combo Reuse plus In-Place Interpolation Map

Artifact: `native-test-runs/20260620-033959-c4b04caf`

This run adds `mul-large-toom-cmb-ripdiv`, a benchmark-only full-window audit
that combines reusable recursive workspaces with in-place exact interpolation
division. It compares the combined route against the regular reusable map on
identical operand fingerprints across `4096`, `5639`, `8192`, `11717`,
`16384`, `24103`, `32768`, `52163`, and `65536`.

Summary:

| Row | Sizes | Candidate / Baseline Max | Candidate / Current Max | Candidate / GMP Max | Baseline / GMP Max | Current / GMP Max | Worst Pair Max | Safe Sizes | Hash Safe | Decision |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `mul-large-toom-cmb-ripdiv` | `4096..65536` | `1.017` | `0.842` | `1.158` | `1.158` | `1.967` | `1.297` | `0/9` | `162/162` | observe only |

Per-size signal:

| Digits | Reuse+IPDiv / Reuse | Reuse+IPDiv / Current | Reuse+IPDiv / GMP | Reuse / GMP | Current / GMP | Worst Pair | Stable vs Reuse | GMP Stable | Status |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `4096` | `1.004` | `0.830` | `0.763` | `0.755` | `0.913` | `1.065` | `4/9` | `9/9` | reuse-baseline-regression |
| `5639` | `1.017` | `0.842` | `0.763` | `0.746` | `0.897` | `1.078` | `1/9` | `9/9` | reuse-baseline-regression |
| `8192` | `1.004` | `0.811` | `0.860` | `0.856` | `1.061` | `1.034` | `1/9` | `9/9` | reuse-baseline-regression |
| `11717` | `1.014` | `0.693` | `0.911` | `0.902` | `1.320` | `1.035` | `0/9` | `9/9` | reuse-baseline-regression |
| `16384` | `0.993` | `0.682` | `0.922` | `0.929` | `1.351` | `1.018` | `3/9` | `9/9` | reuse-baseline-regression |
| `24103` | `1.001` | `0.628` | `0.967` | `0.971` | `1.557` | `1.019` | `3/9` | `9/9` | reuse-baseline-regression |
| `32768` | `0.983` | `0.625` | `1.008` | `1.022` | `1.622` | `1.092` | `4/9` | `2/9` | reuse-baseline-regression |
| `52163` | `0.999` | `0.573` | `1.041` | `1.040` | `1.815` | `1.065` | `0/9` | `0/9` | reuse-baseline-regression |
| `65536` | `0.999` | `0.591` | `1.158` | `1.158` | `1.967` | `1.297` | `2/9` | `0/9` | reuse-baseline-regression |

Decision: reject reuse plus in-place interpolation as a promotion direction.
The combined path beats current production multiply, but it does not beat the
regular reusable map under the strict gate and still fails GMP/MPIR and
worst-pair gates at the high end.

## 2026-06-20: Top-Level Toom-4 Scout

Artifact: `native-test-runs/20260620-040903-c4b04caf`

This run adds `mul-large-toom4-top`, a benchmark-only top-level Toom-4 scout
for the upper window. It splits each operand into four slices, evaluates at
`0`, `1`, `-1`, `2`, `-2`, `3`, and infinity, and reuses the existing
full-workspace Toom-3 combo route for point products. The probe is diagnostic
only: `noAutoRoute=1`, `replacementReady=false`, and production multiply is
unchanged.

Summary:

| Row | Sizes | Candidate / Baseline Max | Candidate / Current Max | Candidate / GMP Max | Baseline / GMP Max | Current / GMP Max | Worst Pair Max | Safe Sizes | Hash Safe | Decision |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `mul-large-toom4-top` | `24103,32768,52163,65536` | `1.054` | `0.665` | `1.141` | `1.176` | `1.884` | `1.359` | `0/4` | `72/72` | observe only |

Per-size signal:

| Digits | Toom-4 / Combo L48D3 | Toom-4 / Current | Toom-4 / GMP | Combo L48D3 / GMP | Current / GMP | Worst Pair | Stable vs Combo | GMP Stable | Status |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `24103` | `1.003` | `0.654` | `1.002` | `1.008` | `1.534` | `1.063` | `3/9` | `4/9` | combo-l48d3-regression |
| `32768` | `0.985` | `0.665` | `1.061` | `1.077` | `1.611` | `1.137` | `4/9` | `0/9` | combo-l48d3-regression |
| `52163` | `1.054` | `0.625` | `1.141` | `1.078` | `1.831` | `1.359` | `0/9` | `1/9` | combo-l48d3-regression |
| `65536` | `0.954` | `0.595` | `1.125` | `1.176` | `1.884` | `1.156` | `8/9` | `0/9` | combo-l48d3-regression |

Decision: reject top-level Toom-4 as a promotion direction for this window.
It shows useful median wins at the power-of-two anchors, especially `65536`,
but the deterministic random spots expose the real shape: `24103` is slightly
slower than combo L48D3, `52163` regresses by 5.4 percent, and worst-pair
safety peaks at 1.359. Exact parity/hash passed, so the algorithm is a clean
diagnostic result, but not a route candidate.

## 2026-06-20: Top-Level Toom-4 Workspace Reuse Scout

Artifact: `native-test-runs/20260620-045841-c4b04caf`

This run adds `mul-large-toom4-top-reuse`, a benchmark-only no-realloc variant
of the top-level Toom-4 scout. It reuses caller-owned recursive Toom-3 and
Karatsuba workspaces for point products and compares against the same Toom-4
shape without reuse, current production multiply, and `mpz_mul` on identical
operand fingerprints. The probe is diagnostic only: `noAutoRoute=1`,
`replacementReady=false`, and production multiply is unchanged.

Summary:

| Row | Sizes | Candidate / Baseline Max | Candidate / Current Max | Candidate / GMP Max | Baseline / GMP Max | Current / GMP Max | Worst Pair Max | Safe Sizes | Hash Safe | Decision |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `mul-large-toom4-top-reuse` | `24103,32768,52163,65536` | `0.976` | `0.640` | `1.071` | `1.101` | `1.920` | `1.112` | `0/4` | `72/72` | observe only |

Per-size signal:

| Digits | Reuse / Non-Reuse Toom-4 | Reuse / Current | Reuse / GMP | Non-Reuse / GMP | Current / GMP | Worst Pair | Stable vs Non-Reuse | GMP Stable | Status |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `24103` | `0.976` | `0.630` | `0.954` | `0.978` | `1.513` | `1.013` | `5/9` | `8/9` | toom4-reuse-baseline-regression |
| `32768` | `0.960` | `0.640` | `1.040` | `1.091` | `1.640` | `1.112` | `7/9` | `0/9` | toom4-reuse-baseline-regression |
| `52163` | `0.958` | `0.580` | `1.043` | `1.086` | `1.796` | `1.052` | `9/9` | `0/9` | backend-regression |
| `65536` | `0.970` | `0.552` | `1.071` | `1.101` | `1.920` | `1.105` | `5/9` | `3/9` | toom4-reuse-baseline-regression |

Decision: keep the Toom-4 reuse path as a diagnostic result, not a promotion
candidate. Workspace reuse is real and improves the same Toom-4 shape at every
upper-window size, including the deterministic random spots, but the aggregate
still has `safeSizes=0/4`: `24103` is too thin and worst-pair unsafe, `32768`
and `65536` miss stable-pair gates, and the high end remains slower than
GMP/MPIR. Allocation cost was part of the Toom-4 problem, but it was not the
whole blocker.

## 2026-06-20: Top-Level Toom-4 Inner-Handoff Scout

Artifact: `native-test-runs/20260620-054314-c4b04caf`

This run adds `mul-large-toom4-top-handoff`, a benchmark-only handoff scout for
the reusable top-level Toom-4 route. The candidate keeps top-level Toom-4 and
workspace reuse but uses `leaf64/depth2` for recursive point products; the
baseline is the prior reusable `leaf48/depth3` Toom-4 point-product handoff.
It covers the same upper window and deterministic random spots and remains
diagnostic only: `noAutoRoute=1`, `replacementReady=false`, production multiply
unchanged.

Summary:

| Row | Sizes | Candidate / Baseline Max | Candidate / Current Max | Candidate / GMP Max | Baseline / GMP Max | Current / GMP Max | Worst Pair Max | Safe Sizes | Hash Safe | Decision |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `mul-large-toom4-top-handoff` | `24103,32768,52163,65536` | `0.995` | `0.650` | `1.134` | `1.159` | `1.881` | `1.255` | `0/4` | `72/72` | observe only |

Per-size signal:

| Digits | L64D2 / L48D3 Toom-4 | L64D2 / Current | L64D2 / GMP | L48D3 / GMP | Current / GMP | Worst Pair | Stable vs L48D3 | GMP Stable | Status |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `24103` | `0.961` | `0.650` | `0.987` | `1.040` | `1.519` | `1.103` | `6/9` | `5/9` | toom4-handoff-baseline-regression |
| `32768` | `0.946` | `0.630` | `1.068` | `1.107` | `1.720` | `1.221` | `6/9` | `1/9` | toom4-handoff-baseline-regression |
| `52163` | `0.967` | `0.584` | `1.053` | `1.088` | `1.826` | `1.255` | `6/9` | `2/9` | toom4-handoff-baseline-regression |
| `65536` | `0.995` | `0.601` | `1.134` | `1.159` | `1.881` | `1.198` | `1/9` | `0/9` | toom4-handoff-baseline-regression |

Decision: reject the `leaf64/depth2` inner handoff as a promotion direction.
It improves median time over `leaf48/depth3` at every measured upper size, but
the gains are not strict-gate safe, `65536` is nearly flat, and worst-pair/GMP
stability remains blocked. This suggests the reusable Toom-4 path needs a
different arithmetic shape, not just a shallower inner Toom-3 handoff.

## 2026-06-20: Top-Level Toom-4 Factored-Division Scout

Artifact: `native-test-runs/20260620-063633-c4b04caf`

This run adds `mul-large-toom4-top-fdiv`, a benchmark-only interpolation scout
for the reusable top-level Toom-4 route. The candidate keeps `leaf48/depth3`
recursive point products and replaces generic exact division by `24`, `60`, and
`120` with factored exact division by powers of two, `3`, and `5`. The baseline
is the prior reusable top-level Toom-4 route. It covers `24103`, `32768`,
`52163`, and `65536`, preserving deterministic random spots between the
power-of-two anchors. It remains diagnostic only: `noAutoRoute=1`,
`replacementReady=false`, production multiply unchanged.

Summary:

| Row | Sizes | Candidate / Baseline Max | Candidate / Current Max | Candidate / GMP Max | Baseline / GMP Max | Current / GMP Max | Worst Pair Max | Safe Sizes | Hash Safe | Decision |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `mul-large-toom4-top-fdiv` | `24103,32768,52163,65536` | `1.017` | `0.661` | `1.094` | `1.110` | `1.986` | `1.162` | `0/4` | `72/72` | observe only |

Per-size signal:

| Digits | Factored Div / Reuse Toom-4 | Factored Div / Current | Factored Div / GMP | Reuse Toom-4 / GMP | Current / GMP | Worst Pair | Stable vs Reuse | GMP Stable | Status |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `24103` | `1.017` | `0.661` | `0.948` | `0.936` | `1.481` | `1.162` | `2/9` | `7/9` | toom4-fdiv-baseline-reg |
| `32768` | `0.979` | `0.624` | `1.025` | `1.044` | `1.645` | `1.051` | `5/9` | `3/9` | toom4-fdiv-baseline-reg |
| `52163` | `0.977` | `0.561` | `1.022` | `1.050` | `1.809` | `1.044` | `6/9` | `1/9` | toom4-fdiv-baseline-reg |
| `65536` | `0.984` | `0.553` | `1.094` | `1.110` | `1.986` | `1.131` | `3/9` | `0/9` | toom4-fdiv-baseline-reg |

Decision: reject factored exact division as a promotion direction. It is exact
and faster than current production multiply, but it regresses the reusable
Toom-4 baseline at `24103`, stays too thin at the high end, and does not fix
GMP/MPIR, stable-pair, or worst-pair safety. The Toom-4 blocker is not generic
small-divisor cost by itself.

## 2026-06-20: Top-Level Toom-4 Reuse Versus Combo Reuse Audit

Artifact: `native-test-runs/20260620-072031-c4b04caf`

This run adds `mul-large-toom4-top-vs-cmb`, a benchmark-only same-run audit
that compares reusable top-level Toom-4 (`full-ws-toom4-top-reuse-l48d3`)
against reusable Toom-3 combo (`full-ws-combo-reuse-l48d3`) on the upper mixed
window: `24103`, `32768`, `52163`, and `65536`. It preserves deterministic
random spots between the power-of-two anchors and keeps current production
multiply plus `mpz_mul` in the same timing run. It remains diagnostic only:
`noAutoRoute=1`, `replacementReady=false`, production multiply unchanged.

Summary:

| Row | Sizes | Candidate / Baseline Max | Candidate / Current Max | Candidate / GMP Max | Baseline / GMP Max | Current / GMP Max | Worst Pair Max | Safe Sizes | Hash Safe | Decision |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `mul-large-toom4-top-vs-cmb` | `24103,32768,52163,65536` | `1.036` | `0.606` | `1.115` | `1.131` | `2.124` | `1.279` | `0/4` | `72/72` | observe only |

Per-size signal:

| Digits | Toom-4 Reuse / Combo Reuse | Toom-4 Reuse / Current | Toom-4 Reuse / GMP | Combo Reuse / GMP | Current / GMP | Worst Pair | Stable vs Combo | GMP Stable | Status |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `24103` | `1.035` | `0.591` | `1.014` | `0.980` | `1.660` | `1.164` | `1/9` | `3/9` | toom4-combo-baseline-re |
| `32768` | `1.036` | `0.606` | `0.984` | `0.985` | `1.647` | `1.246` | `1/9` | `5/9` | toom4-combo-baseline-re |
| `52163` | `0.975` | `0.520` | `1.006` | `1.059` | `1.929` | `1.086` | `6/9` | `3/9` | toom4-combo-baseline-re |
| `65536` | `1.012` | `0.537` | `1.115` | `1.131` | `2.124` | `1.279` | `4/9` | `1/9` | toom4-combo-baseline-re |

Decision: reject reusable top-level Toom-4 as a broad upper-window promotion
candidate against reusable combo. It is exact and still faster than current
production multiply, but it loses to combo reuse at `24103`, `32768`, and
`65536`, only wins the deterministic `52163` spot, and misses stable-pair,
worst-pair, safe-size, and GMP/MPIR gates. Keep it as evidence that top-level
Toom-4 is not the next default route shape.

## 2026-06-20: Combo Reuse GMP Duplicate-Control Audit

Artifact: `native-test-runs/20260620-091748-c4b04caf`

This run adds `mul-large-toom-cmb-gmpctrl`, a benchmark-only duplicate-control
audit for the reusable combo map on the upper mixed window: `24103`, `32768`,
`52163`, and `65536`. The candidate is
`full-ws-combo-reuse-map-l64d2-l48d4-l48d3`; it is compared against current
production multiply, a primary `mpz_mul` oracle, and an independent duplicate
`mpz_mul` lane in the same rotating batch. It preserves both deterministic
random spots and power-of-two anchors and remains diagnostic only:
`noAutoRoute=1`, `replacementReady=false`, production multiply unchanged.

Summary:

| Row | Sizes | Candidate / GMP Max | Candidate / GMP Duplicate Max | Current / GMP Max | GMP Control Max | GMP Control Worst | Worst Pair Max | Safe Sizes | Hash Safe | Decision |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `mul-large-toom-cmb-gmpctrl` | `24103,32768,52163,65536` | `1.106` | `1.112` | `1.893` | `1.014` | `1.129` | `1.237` | `0/4` | `72/72` | observe only |

Per-size signal:

| Digits | Candidate / GMP | Candidate / GMP Duplicate | Current / GMP | GMP Control | GMP Control Worst | Worst Pair | GMP Stable | Control Stable | Status |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `24103` | `0.962` | `0.961` | `1.523` | `0.981` | `1.055` | `1.055` | `7/9` | `9/9` | backend-regression |
| `32768` | `1.031` | `1.049` | `1.566` | `0.996` | `1.129` | `1.129` | `3/9` | `8/9` | backend-regression |
| `52163` | `1.033` | `1.035` | `1.782` | `0.987` | `1.091` | `1.093` | `2/9` | `9/9` | backend-regression |
| `65536` | `1.106` | `1.112` | `1.893` | `1.014` | `1.098` | `1.237` | `0/9` | `9/9` | backend-regression |

Decision: keep the reusable combo map as the strongest known diagnostic route,
but do not promote it. The duplicate `mpz_mul` control lane is stable in this
run, so the upper-window GMP/MPIR miss is a real backend/stability gap rather
than just oracle-lane noise. `24103` is median-positive but still one stable
pair short of the `8/9` gate; `32768`, `52163`, and `65536` remain slower than
GMP/MPIR. The next CPU multiply work should use this control-aware methodology
and move to a different arithmetic structure or broader handoff, not another
simple Toom-4/top-level variant.

## 2026-06-20: Toom-3 Neg2 Arithmetic-Shape Scout

Artifact:
`native/build-codex-neg2-msvc142-nmake/native-test-runs/20260620-104543-c4b04caf`

Validation: `native/build-codex-neg2-msvc142-nmake/xray_native_tests.exe`
printed `native xray tests passed` with the Release MSVC 14.29 NMake build.

This run adds `mul-large-toom-cmb-neg2`, a benchmark-only reusable Toom-3
combo scout that evaluates the non-unit Toom-3 point at `-2` instead of `+2`
and interpolates back to the normal coefficient order. It compares
`full-ws-combo-reuse-neg2-l64d2-l48d4-l48d3` against the current reusable
combo map on the same upper mixed window: `24103`, `32768`, `52163`, and
`65536`. It preserves the deterministic random spots and power-of-two anchors
and remains diagnostic only: `noAutoRoute=1`, `replacementReady=false`,
production multiply unchanged.

Summary:

| Row | Sizes | Neg2 / Reuse Map Max | Neg2 / Current Max | Neg2 / GMP Max | Reuse Map / GMP Max | Current / GMP Max | Worst Pair Max | Safe Sizes | Hash Safe | Decision |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `mul-large-toom-cmb-neg2` | `24103,32768,52163,65536` | `1.065` | `0.614` | `1.111` | `1.080` | `1.980` | `1.199` | `0/4` | `72/72` | observe only |

Per-size signal:

| Digits | Neg2 / Reuse Map | Neg2 / Current | Neg2 / GMP | Reuse Map / GMP | Current / GMP | Worst Pair | Stable vs Reuse | GMP Stable | Status |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `24103` | `0.980` | `0.556` | `0.950` | `0.996` | `1.764` | `1.199` | `4/9` | `8/9` | reuse-neg2-baseline-regression |
| `32768` | `1.065` | `0.614` | `1.059` | `1.018` | `1.673` | `1.108` | `1/9` | `1/9` | reuse-neg2-baseline-regression |
| `52163` | `1.020` | `0.544` | `1.015` | `1.006` | `1.865` | `1.180` | `4/9` | `3/9` | reuse-neg2-baseline-regression |
| `65536` | `1.006` | `0.531` | `1.111` | `1.080` | `1.980` | `1.168` | `3/9` | `1/9` | reuse-neg2-baseline-regression |

Decision: reject the neg2 Toom-3 arithmetic shape as a promotion direction.
It is exact and still much faster than current production multiply, but it
does not beat the reusable combo map at any upper-window size under the strict
baseline, stability, worst-pair, and GMP/MPIR gates. The result usefully rules
out a signed evaluation/interpolation reshuffle as the next fix for the
backend gap.

## 2026-06-20: Top-Level Toom-5 Smoke Scout

Artifact:
`native-test-runs/20260620-130732-c4b04caf`

Validation: `native/build-codex-neg2-msvc142-nmake/xray_native_tests.exe`
printed `native xray tests passed` with the Release MSVC 14.29 NMake build.

This run adds `mul-large-toom5-top-reuse`, a benchmark-only smoke scout for a
top-level Toom-5 split with reusable recursive Toom-3/Karatsuba point products.
The tiny window deliberately includes the deterministic in-between spot `5639`
and the power-of-two anchor `8192`. The route is diagnostic only:
`noAutoRoute=1`, `replacementReady=false`, production multiply unchanged.

Summary:

| Row | Sizes | Toom-5 / Combo Reuse Max | Toom-5 / Current Max | Toom-5 / GMP Max | Combo / GMP Max | Current / GMP Max | Worst Pair Max | Safe Sizes | Hash Safe | Decision |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `mul-large-toom5-top-reuse` | `5639,8192` | `0.767` | `1.031` | `0.980` | `2.601` | `1.091` | `1.167` | `0/2` | `12/12` | observe only |

Per-size signal:

| Digits | Toom-5 / Combo Reuse | Toom-5 / Current | Toom-5 / GMP | Combo / GMP | Current / GMP | Worst Pair | Stable vs Combo | GMP Stable | Status |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `5639` | `0.767` | `1.031` | `0.778` | `1.015` | `0.673` | `1.167` | `2/3` | `3/3` | toom5-combo-baseline-re |
| `8192` | `0.447` | `0.898` | `0.980` | `2.601` | `1.091` | `1.163` | `3/3` | `2/3` | current-regression |

Decision: keep top-level Toom-5 as an exact diagnostic clue, not a promotion
candidate. The shape beats the reusable combo baseline on median in this smoke
window, but the random spot regresses against current production, the aggregate
has `safeSizes=0/2`, and the worst-pair gate is not safe. If revisited, it
needs a cheaper, broader route audit before any production-route discussion.

## 2026-06-20: Top-Level Toom-5 Leaf48 Handoff Smoke

Artifact:
`native-test-runs/20260620-134707-c4b04caf`

Validation: `native/build-codex-neg2-msvc142-nmake/xray_native_tests.exe`
printed `native xray tests passed` with the Release MSVC 14.29 NMake build.

This run adds `mul-large-toom5-top-handoff`, a second benchmark-only Toom-5
smoke scout using a leaf48/depth2 handoff shape. The tiny window moves upward
to deterministic random spot `11717` plus power-of-two anchor `16384`.
Production multiply remains unchanged.

Summary:

| Row | Sizes | Toom-5 / Combo Reuse Max | Toom-5 / Current Max | Toom-5 / GMP Max | Combo / GMP Max | Current / GMP Max | Worst Pair Max | Safe Sizes | Hash Safe | Decision |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `mul-large-toom5-top-handoff` | `11717,16384` | `1.252` | `0.795` | `1.156` | `0.875` | `1.335` | `1.566` | `0/2` | `12/12` | observe only |

Per-size signal:

| Digits | Toom-5 / Combo Reuse | Toom-5 / Current | Toom-5 / GMP | Combo / GMP | Current / GMP | Worst Pair | Stable vs Combo | GMP Stable | Status |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `11717` | `1.173` | `0.753` | `0.832` | `0.698` | `1.087` | `1.275` | `0/3` | `2/3` | toom5-combo-baseline-re |
| `16384` | `1.252` | `0.795` | `1.156` | `0.875` | `1.335` | `1.566` | `0/3` | `1/3` | toom5-combo-baseline-re |

Decision: reject the leaf48/depth2 Toom-5 handoff shape as a promotion path.
It is exact and faster than current production multiply, but it loses to the
combo reuse baseline at both tested sizes and worsens the `16384` GMP/MPIR
gap. The next multiply slice should leave top-level Toom-5 handoff tuning and
move to a different lower-level backend or multiplication structure.

## 2026-06-20: Combo Reuse Transition GMP Duplicate-Control Audit

Artifact:
`native-test-runs/20260620-143501-c4b04caf`

Validation: `native/build-codex-neg2-msvc142-nmake/xray_native_tests.exe`
printed `native xray tests passed` with the Release MSVC 14.29 NMake build.

This run adds `mul-large-toom-cmb-gmptrans`, a benchmark-only duplicate-control
audit for the reusable combo map in the transition window. It covers the
deterministic random spot `11717` and the power-of-two anchor `16384`, with
current production multiply, primary `mpz_mul`, and duplicate `mpz_mul` in the
same rotating run. Production multiply remains unchanged.

Summary:

| Row | Sizes | Candidate / GMP Max | Candidate / GMP Duplicate Max | Current / GMP Max | GMP Control Max | GMP Control Worst | Worst Pair Max | Safe Sizes | Hash Safe | Decision |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `mul-large-toom-cmb-gmptrans` | `11717,16384` | `0.924` | `0.908` | `1.401` | `1.025` | `1.073` | `1.080` | `2/2` | `36/36` | observe only |

Per-size signal:

| Digits | Candidate / GMP | Candidate / GMP Duplicate | Current / GMP | GMP Control | GMP Control Worst | Worst Pair | GMP Stable | Control Stable | Status |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `11717` | `0.924` | `0.903` | `1.347` | `1.025` | `1.073` | `1.073` | `9/9` | `9/9` | combo-gmp-control-clean |
| `16384` | `0.906` | `0.908` | `1.401` | `0.991` | `1.058` | `1.080` | `9/9` | `9/9` | combo-gmp-control-clean |

Decision: keep this as strong transition-window methodology evidence, not a
production promotion. The same-run duplicate GMP/MPIR lane is stable, and the
reusable combo map beats both GMP lanes plus current production at the random
spot and anchor. That supports a later forced route audit for this transition
pocket, but the broader upper-window GMP miss still blocks any default-route
change.

## 2026-06-20: Combo Reuse Transition Forced Route Audit

Artifact:
`native-test-runs/20260620-162931-c4b04caf`

Validation: `native/build-codex-neg2-msvc142-nmake/xray_native_tests.exe`
printed `native xray tests passed` with the Release MSVC 14.29 NMake build.

This run adds `mul-large-toom-cmb-troute`, a benchmark-only forced route audit
for the reusable combo map in the transition window. It covers the deterministic
random spot `11717` and the power-of-two anchor `16384`, comparing the forced
candidate against the current production route, `mpz_mul`, and the nonreuse
combo baseline in the same rotating run. Production multiply remains unchanged.

Summary:

| Row | Sizes | Candidate / Baseline Max | Candidate / Current Max | Candidate / GMP Max | Baseline / GMP Max | Current / GMP Max | Worst Pair Max | Safe Sizes | Hash Safe | Decision |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `mul-large-toom-cmb-troute` | `11717,16384` | `1.041` | `0.660` | `0.971` | `0.923` | `1.388` | `1.642` | `0/2` | `36/36` | observe only |

Per-size signal:

| Digits | Candidate / Baseline | Candidate / Current | Candidate / GMP | Baseline / GMP | Current / GMP | Worst Pair | Baseline Stable | GMP Stable | Status |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `11717` | `0.987` | `0.598` | `0.861` | `0.794` | `1.316` | `1.115` | `4/9` | `8/9` | reuse-baseline-regression |
| `16384` | `1.041` | `0.660` | `0.971` | `0.923` | `1.388` | `1.642` | `2/9` | `5/9` | reuse-baseline-regression |

Decision: do not promote the transition route. The candidate is exact and still
beats current production multiply plus GMP/MPIR in this window, but it does not
beat the nonreuse combo baseline at every size, stable-pair counts are weak
against that baseline, and worst-pair safety is too noisy. Keep the route audit
as diagnostic evidence and look for a stronger backend or interpolation shape
before any production default change.

## Combo Reuse Transition Self-Control Audit

Local focused run:

- Command:
  `native/build-stack-v142/xray_cli.exe --bench-focus mul-combo-transition --bench-tsv`
- Artifact:
  `native-test-runs/20260621-063109-transition-self-control/benchmark.tsv`

This run adds `mul-large-toom-cmb-tctrl` plus per-size
`mul-large-toom-cmb-tctrl-pt` rows for the transition pocket:
`11717` and `16384`. Candidate and baseline are the same reusable combo map,
measured through independent reusable workspaces, while current production
multiply and `mpz_mul` remain in the same rotating run. It remains diagnostic
only: `noAutoRoute=1`, `replacementReady=false`, production multiply
unchanged.

| Row | Sizes | Control Max | Control Worst | Current Max | GMP Max | Current/GMP Max | Safe Sizes | Safe Chunk | Hash | Decision |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | --- | ---: | --- |
| `mul-large-toom-cmb-tctrl` | `11717,16384` | `0.997` | `1.140` | `0.676` | `0.925` | `1.378` | `1/2` | `11717` | `36/36` | observe only |

Per-size signal:

| Digits | Control Ratio | Control Worst | Candidate / Current | Candidate / GMP | Current / GMP | Control Stable | GMP Stable | Status |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `11717` | `0.982` | `1.018` | `0.635` | `0.879` | `1.378` | `9/9` | `9/9` | self-control-clean |
| `16384` | `0.997` | `1.140` | `0.676` | `0.925` | `1.340` | `7/9` | `9/9` | duplicate-control-noise |

Decision: use the row as a fast noise classifier before wider transition or
promotion sweeps. The candidate still beats current production multiply and
GMP/MPIR in this focused run, but the duplicate-route control is noisy at
`16384`, so the transition pocket remains observe-only. The reported
`safeSizeChunks=11717` is a single adjacent measured-point chunk in this run,
not proof of every unmeasured size around `11717`.

### Transition Tournament Scout

Latest focused command:

```powershell
native/build-stack-v142/xray_cli.exe --bench-focus mul-combo-transition --bench-tsv
```

Artifact:
`native-test-runs/20260621-064718-transition-tournament/benchmark.tsv`

This run adds `mul-large-toom-cmb-ttourn` plus per-route
`mul-large-toom-cmb-ttourn-pt` rows over `11717`, `16384`, and `24103`.
It compares `l64d2`, `l48d3`, `l48d4`, and a `16384` handoff route on
identical operands in the same rotating run. It remains diagnostic only:
`noAutoRoute=1`, `replacementReady=false`, production multiply unchanged.

| Row | Sizes | Winner | Current Max | GMP Max | Current/GMP Max | Worst Pair | Safe Sizes | Safe Chunk | Hash | Decision |
| --- | --- | --- | ---: | ---: | ---: | ---: | ---: | --- | ---: | --- |
| `mul-large-toom-cmb-ttourn` | `11717,16384,24103` | handoff all sizes | `0.663` | `1.060` | `1.598` | `1.307` | `1/3` | `11717` | `216/216` | observe only |

Per-size winner signal:

| Digits | Winner Active Route | Winner / Current | Winner / GMP | Current / GMP | Worst Pair | GMP Stable | Status |
| ---: | --- | ---: | ---: | ---: | ---: | ---: | --- |
| `11717` | `l64d2` | `0.614` | `0.904` | `1.490` | `0.989` | `9/9` | tournament-winner |
| `16384` | `l48d3` | `0.649` | `0.884` | `1.386` | `1.035` | `8/9` | backend-regression |
| `24103` | `l48d3` | `0.663` | `1.060` | `1.598` | `1.307` | `2/9` | backend-regression |

Decision: use this as a fast ladder scout for novelty. The handoff shape is
the local winner, but only the `11717` measured point is a clean chunk in this
run. Treat that as a piecemeal follow-up target, not a default-route candidate.

### Handoff Pocket Scout

Latest focused command:

```powershell
native/build-stack-v142/xray_cli.exe --bench-focus mul-combo-transition --bench-tsv
```

Artifact:
`native-test-runs/20260621-065905-handoff-pocket/benchmark.tsv`

This run adds `mul-large-toom-cmb-hpocket` plus
`mul-large-toom-cmb-hpocket-pt` rows over seven transition sizes:
`10007`, `10733`, `11717`, `12553`, `13649`, `14831`, and `16384`.
It tests the handoff shape from the transition tournament more densely, with
`l64d2` below `16384` and `l48d3` at `16384`. It remains diagnostic only:
`noAutoRoute=1`, `replacementReady=false`, production multiply unchanged.

| Row | Sizes | Current Max | GMP Max | Current/GMP Max | Worst Pair | Safe Sizes | Safe Chunk | Hash | Decision |
| --- | --- | ---: | ---: | ---: | ---: | ---: | --- | ---: | --- |
| `mul-large-toom-cmb-hpocket` | `10007..16384` | `0.771` | `1.015` | `1.427` | `1.531` | `0/7` | none | `126/126` | observe only |

Per-size signal:

| Digits | Active Route | Candidate / Current | Candidate / GMP | Current / GMP | Worst Pair | GMP Stable | Status |
| ---: | --- | ---: | ---: | ---: | ---: | ---: | --- |
| `10007` | `l64d2` | `0.771` | `0.971` | `1.288` | `1.092` | `6/9` | backend-regression |
| `10733` | `l64d2` | `0.729` | `1.015` | `1.373` | `1.531` | `3/9` | current-regression |
| `11717` | `l64d2` | `0.721` | `0.946` | `1.335` | `1.001` | `8/9` | backend-regression |
| `12553` | `l64d2` | `0.674` | `0.947` | `1.418` | `1.029` | `8/9` | backend-regression |
| `13649` | `l64d2` | `0.707` | `0.928` | `1.348` | `1.002` | `8/9` | backend-regression |
| `14831` | `l64d2` | `0.697` | `0.906` | `1.393` | `1.333` | `8/9` | backend-regression |
| `16384` | `l48d3` | `0.743` | `0.994` | `1.427` | `1.406` | `5/9` | backend-regression |

Decision: treat this as a fast negative scout. The handoff route beats current
production multiply at every measured size and mostly beats GMP/MPIR by median,
but it does not produce a strict contiguous safe chunk. Do not widen this route
blindly; rerun with duplicate controls or repeated focused runs before spending
full-suite CI time on this pocket again.

### Focus Repeat Triage

Latest repeat artifacts:

- `native-test-runs/20260621-072833-handoff-pocket-repeat3/summary.tsv`
- `native-test-runs/20260621-072833-transition-controls-repeat3/summary.tsv`

The handoff-pocket helper run produced safe chunks `none`, `14831`, and `none`
across three repeats. The transition-control helper run also stayed noisy:
duplicate-route control reported `16384`, then `11717-16384`, then `none`,
while the duplicate-GMP control reported `none` in every repeat. Decision: do
not promote or widen this exact handoff pocket next. Use the helper to filter
future pockets, and move the next novelty slice toward a different route shape
or lower-level Toom arithmetic.

### Toom Div Transition Focus

Artifact:
`native-test-runs/20260621-073742-mul-toom-div-transition-focus/benchmark.tsv`

The `mul-toom-div-transition` focus runs only the existing div2, div3, and
div2+div3 scouts over `11717`, `16384`, and `24103`. This local run rejected all
three against the leaf64 baseline: `mul-large-toom-div2-scout` ratio `1.025`,
`mul-large-toom-div3-scout` ratio `1.010`, and
`mul-large-toom-div2-div3-scout` ratio `1.000`, with `safeSizes=0/3` for each.
Keep the focus for quick reruns, but do not spend the next novelty slice on
div2/div3 arithmetic unless a future CPU/build changes the signal.

### Toom-5 Smoke Focus

Artifact:
`native-test-runs/20260621-075205-mul-toom5-smoke-focus/benchmark.tsv`

The `mul-toom5-smoke` focus runs only the existing Toom-5 top-level smoke rows
over `5639`, `8192`, `11717`, and `16384`. This local run was exact but
rejected both aggregates: `mul-large-toom5-top-reuse` had combo-baseline ratio
`1.201`, GMP ratio `1.050`, worst pair `1.213`, and `safeSizes=0/2`;
`mul-large-toom5-top-handoff` had combo-baseline ratio `1.110`, GMP ratio
`1.013`, worst pair `1.171`, and `safeSizes=0/2`. Keep the focus label because
it answers the Toom-5 topology question quickly, but do not widen Toom-5 top
work unless a future CPU/build flips the smoke signal.

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

2. Scout novelty with focused runs before spending full-suite time.
   - Use `xray_cli.exe --bench-focus mul-novelty --bench-tsv` to cover the
     mixed `4096` through `65536` multiply window, including deterministic
     in-between sizes, without running unrelated benchmark families.
   - Use `mul-toom5-smoke` when the next question is whether the existing
     Toom-5 top-level smoke rows deserve more attention. It runs only
     `5639,8192,11717,16384`, so it is a cheap topology check before any wider
     Toom-5 work.
   - Use `mul-toom-div-transition` when the next question is lower-level Toom
     arithmetic in the transition window: it runs only the existing div2, div3,
     and div2+div3 scouts over `11717,16384,24103`. Use `mul-toom-div` for the
     same scouts over the full active window.
   - Use narrower focus labels such as `mul-combo-lower`,
     `mul-combo-transition`, `mul-combo-upper`, and `mul-combo-reuse` when a
     candidate pocket is already known.
   - `mul-combo-transition` now includes forced route, duplicate-route
     self-control, duplicate GMP/MPIR control rows for `11717` and `16384`,
     plus a `11717,16384,24103` transition tournament scout, so use it before
     launching a broader transition sweep.
   - Use `mul-combo-transition-controls` when you only need the duplicate-route
     and duplicate-GMP transition checks before deciding whether a pocket is
     stable enough to inspect more deeply.
   - Use `mul-combo-handoff-pocket` when you only need the dense
     `10007,10733,11717,12553,13649,14831,16384` handoff-pocket scout. It is
     the cheaper first check before rerunning the full transition bundle.
   - Treat focus output as triage only: keep the raw TSV, but do not promote or
     publish a route until the full parity, route-audit, worst-pair, and
     stable-pair gates pass.
   - Use `python native/tools/bench_focus_repeat.py --cli <xray_cli> --focus
     mul-combo-handoff-pocket --runs 3` to repeat a fast focus, save every raw
     TSV/progress TSV artifact, and print the safe-chunk summary table. The
     helper also writes `repeat_stable_chunks.tsv`, an operation-level
     intersection of chunks that stayed safe in every repeat.
   - Read `safeSizeChunks` and `longestSafeSizeChunk*` as contiguous measured
     benchmark points only. They are useful for piecemeal follow-up audits over
     promising pockets, but they do not prove every unmeasured digit between the
     endpoints is safe.
   - Prefer `--bench-progress-tsv` for repeated focus runs because it exposes
     `safeSizes`, `safeSizeChunks`, `longestSafeSizeChunk`, and
     `longestSafeSizeChunkCount` as import columns.

3. Increase samples only for promising rows.
   - Do not globally lengthen every benchmark.
   - Require paired medians and same-run ratios.
   - Keep raw `benchmark.tsv`, `frontier.txt`, and report JSON.

4. Test split views plus allocation/workspace reduction.
   - Keep production multiply unchanged until a route audit passes.
   - Measure copied slices, split views, and split views with reusable
     temporaries in the same run.
   - Use the same operand families and leaf threshold first, then sweep
     thresholds only if the route is stable.

5. Revisit AVX/BMI2/ADX only with local proof.
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

The strongest current clue is now structural: reusable workspace removes a real
amount of route overhead and beats current production multiply everywhere, but
the high end still cannot consistently beat GMP/MPIR, and factored exact
division, top-level Toom-4-vs-combo reuse, Toom-5 top smoke, and the Toom-3
neg2 arithmetic-shape scout did not move the strict gates. Transition-pocket
runs now have
same-route self-control, and the first local self-control pass found noise at
`16384`; use that row to avoid spending broader benchmark time on unstable
pockets. Keep future scouts on the mixed window with deterministic in-between
sizes, not only power-of-two anchors. The next multiply work should try a
broader handoff or a deeper multiplication structure below or beyond these
Toom-3/Toom-4 reshuffles, then use the same exact parity, worst-pair,
stable-pair, same-run route, and self-control gates before considering
production routing.
