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

The strongest current clue is Karatsuba copy tax, not sparse scan removal by
itself. On the faster machine, start by combining split views with a no-realloc
workspace plan for recursive temporaries, then run a deeper route audit at
4096, 8192, 16384, and at least one larger size.
