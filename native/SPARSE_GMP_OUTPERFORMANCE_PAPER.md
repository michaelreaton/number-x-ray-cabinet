# Outperforming GMP/MPIR On App-Shaped Sparse Bigint Multiplication

Draft date: 2026-06-21

## Abstract

Number X-Ray outperforms the GMP-compatible `mpz_mul` baseline on a proven
class of app-shaped sparse multiplication workloads. The current evidence does
not show a general dense-multiply replacement for GMP/MPIR. It does show that
when the operands have very few live limbs, such as `2^n + 1` style inputs or
fixed sparse pair-product shapes, Number X-Ray's scratch bigint route can skip
most zero-limb work and win across the full measured sparse campaign window.

The strongest reproduced result is the production sparse multiply row:
`sparse-production-mul` remained replacement-ready over the campaign grid
`4096, 5639, 8192, 11717, 16384, 24103, 32768, 52163, 65536` bits. Two local
two-repeat matrices found a repeat-stable safe chunk from `4096` through
`65536`, including deterministic interior points between powers of two. The two
repeats showed paired-median ratios of `0.068182` and `0.050847` for this row,
with every paired sample stable (`45/45`) and worst-pair ratios at or below
`0.222222`.

## Claim Scope

The claim is intentionally narrow:

- Number X-Ray beats `mpz_mul` on the measured sparse multiplication shapes
  recorded below.
- The claim is based on exact product parity, same-run paired samples,
  repeat-stable safe chunks, and worst-pair gates.
- The broad dense multiply route is not solved. Current dense Toom, Karatsuba
  split-view, backend unroll, and handoff-boundary probes remain observe-only or
  rejected by worst-pair and repeat-stability evidence.
- Windows builds use MPIR through the GMP-compatible API, while Linux/macOS
  builds typically use GMP. The paper therefore says GMP/MPIR unless a specific
  artifact says only `mpz_mul`.

## Why A Specialized Route Can Win

Generic big integer multiplication has to be excellent for dense and unknown
operands. Number X-Ray's sparse route is allowed to be more selective. It first
counts live limbs, checks density and product-count gates, and only dispatches
to sparse multiplication when the estimated nonzero product work is much smaller
than the dense row work.

For a dense `m` by `n` limb product, schoolbook-style work touches the full
`m * n` product grid. For sparse operands, the useful product grid is closer to
`nnz(a) * nnz(b)`, where `nnz` is the count of nonzero limbs. The production
route uses this difference directly:

- It counts nonzero limbs in both operands.
- It rejects the sparse route if the operands are too short or too dense.
- It rejects tiny sparse cases unless the product count is within a tight cap.
- It collects the nonzero limb indices and only emits products for those pairs.
- It normalizes the exact product and validates benchmark results against
  `mpz_mul`.

That is not an asymptotic breakthrough over GMP for dense random inputs. It is
a shape win: Number X-Ray avoids doing work that is provably zero for the
app-shaped operands we care about.

## Workloads

The sparse campaign uses a power-of-two ladder plus deterministic interior
points so wins cannot hide only at neat powers of two:

`4096, 5639, 8192, 11717, 16384, 24103, 32768, 52163, 65536`

These values are bit points in the current sparse focus, not decimal digit
counts. Each raw benchmark row also records the decimal digit length for the
specific operand.

The key sparse multiply families are:

| Row | Shape | Route Type | Baseline |
| --- | --- | --- | --- |
| `sparse-production-mul` | `2^n + 1` style product | current production sparse route | `mpz_mul-sparse-product` |
| `sparse-zero-mul` | same sparse shape, diagnostic row | observe-only probe | `mpz_mul-sparse-product` |
| `sparse-production-pair-mul` | eight live limbs per operand | current production sparse route | `mpz_mul-sparse-product` |
| `sparse-pair-mul` | eight live limbs per operand | observe-only probe | `mpz_mul-sparse-product` |

The diagnostic rows are useful for explaining the mechanism. The production
rows are the stronger evidence because they measure the route a product call
actually takes for those shapes.

The same `mul-sparse` focus also emits `mul-dense-control` at `4096`, `11717`,
and `65536` bits. Those rows use deterministic dense bit patterns and compare
the current production multiply route with `mpz_mul`. They are marked
`controlSafety=paper-dense-control` and `replacementReady=false`; their purpose
is to keep the sparse paper honest, not to claim dense-route superiority.

## Benchmark Method

The benchmark policy is designed to avoid false wins:

- Exact parity is required against the GMP-compatible oracle.
- Lower `speedRatio` means Number X-Ray is faster.
- Ratios are same-run paired medians, not independent best-of runs.
- `worstPairRatio <= 1.0` is required for replacement-ready rows.
- Stable-pair counts must show the candidate winning consistently.
- Repeat helpers intersect safe chunks across runs and report contiguous safe
  spans.
- Focus rows preserve raw `benchmark.tsv`, progress TSV, and matrix summaries.

Contiguous chunks in these reports mean contiguous over the measured campaign
grid. The interior points reduce the risk of a power-of-two-only illusion, but
they do not prove every intervening bit length without a separate route audit.

## Threats To Validity

This draft is intentionally conservative about what the current artifacts can
prove:

- The strongest table is from a local Windows MPIR-compatible build. A
  non-Windows GMP rerun is still required before presenting the result as
  cross-platform GMP evidence.
- `4096-65536` means one contiguous chunk over the measured campaign grid:
  `4096, 5639, 8192, 11717, 16384, 24103, 32768, 52163, 65536`. It is not a
  proof that every unmeasured bit length in that interval is faster.
- The benchmark compares Number X-Ray's scratch bigint route with the
  GMP-compatible `mpz_mul` oracle on identical operand shapes. It is not an
  end-to-end application throughput result.
- Dense random control rows are control evidence only. They are meant to stop
  the sparse result from being overgeneralized into a dense multiply claim.
- Timing ratios are local machine evidence. The durable claim is the
  shape-selective mechanism plus exact parity, paired samples, worst-pair
  safety, stable-pair counts, and repeat-stable chunks.

## Results

Primary sparse artifact:

`native-test-runs/20260621-100500-sparse-upper-repeat2/matrix.tsv`

| Operation | Runs With Safe Chunks | Repeat-Stable Chunk | Worst Pair Max | Status |
| --- | ---: | --- | ---: | --- |
| `sparse-production-mul` | `2/2` | `4096-65536` | `0.222222` | `replacement-ready` |
| `sparse-zero-mul` | `2/2` | `4096-65536` | `0.222222` | `candidate-faster` |
| `sparse-production-pair-mul` | `2/2` | `4096-65536` | `0.875000` | `replacement-ready` |
| `sparse-pair-mul` | `2/2` | `4096-65536` | `0.875000` | `candidate-faster` |

The raw summary for the same artifact shows the magnitude of the main
production sparse win:

| Run | Operation | Speed Ratio | Worst Pair Ratio | Stable Pairs | Safe Size Chunk |
| ---: | --- | ---: | ---: | ---: | --- |
| `1` | `sparse-production-mul` | `0.068182` | `0.222222` | `45/45` | `4096-65536` |
| `2` | `sparse-production-mul` | `0.050847` | `0.203704` | `45/45` | `4096-65536` |
| `1` | `sparse-production-pair-mul` | `0.457895` | `0.777778` | `45/45` | `4096-65536` |
| `2` | `sparse-production-pair-mul` | `0.375000` | `0.875000` | `45/45` | `4096-65536` |

Per-size rows from the preserved raw benchmark files make the size coverage
auditable. Timings are local median microseconds from `run01.benchmark.tsv` and
`run02.benchmark.tsv`; read them as same-machine evidence, not as portable
absolute timings.

`sparse-production-mul`:

| Bits | Digits | Run 1 Scratch/GMP us | Run 1 Ratio | Run 2 Scratch/GMP us | Run 2 Ratio |
| ---: | ---: | ---: | ---: | ---: | ---: |
| `4096` | `1234` | `3/44` | `0.068182` | `2/45` | `0.044444` |
| `5639` | `1698` | `2/40` | `0.050000` | `3/59` | `0.050847` |
| `8192` | `2467` | `2/69` | `0.028986` | `2/68` | `0.029412` |
| `11717` | `3528` | `5/224` | `0.021834` | `3/126` | `0.023810` |
| `16384` | `4933` | `4/217` | `0.020000` | `4/194` | `0.020725` |
| `24103` | `7256` | `10/522` | `0.014045` | `5/370` | `0.013477` |
| `32768` | `9865` | `12/1006` | `0.011928` | `7/559` | `0.012389` |
| `52163` | `15703` | `12/1040` | `0.011538` | `12/1025` | `0.011707` |
| `65536` | `19729` | `17/1498` | `0.011773` | `20/1409` | `0.014347` |

`sparse-production-pair-mul`:

| Bits | Digits | Run 1 Scratch/GMP us | Run 1 Ratio | Run 2 Scratch/GMP us | Run 2 Ratio |
| ---: | ---: | ---: | ---: | ---: | ---: |
| `4096` | `1234` | `3/8` | `0.375000` | `3/8` | `0.375000` |
| `5639` | `1696` | `4/18` | `0.250000` | `6/20` | `0.300000` |
| `8192` | `2467` | `9/26` | `0.346154` | `9/25` | `0.360000` |
| `11717` | `3526` | `20/54` | `0.250000` | `10/45` | `0.222222` |
| `16384` | `4933` | `23/80` | `0.291139` | `40/116` | `0.355932` |
| `24103` | `7244` | `69/231` | `0.291304` | `33/133` | `0.248120` |
| `32768` | `9865` | `87/193` | `0.457895` | `64/181` | `0.320442` |
| `52163` | `15702` | `76/397` | `0.196891` | `78/411` | `0.200514` |
| `65536` | `19729` | `135/508` | `0.273279` | `174/511` | `0.321569` |

Every row in both tables has exact parity and `5/5` stable paired samples in
both preserved runs.

Independent post-pocket novelty matrix:

`native-test-runs/20260621-103409-post-pocket-novelty-repeat2/matrix.tsv`

| Operation | Repeat-Stable Chunk | Worst Pair Max | Status |
| --- | --- | ---: | --- |
| `sparse-production-mul` | `4096-65536` | `0.230769` | `replacement-ready` |
| `sparse-zero-mul` | `4096-65536` | `0.230769` | `candidate-faster` |
| `sparse-production-pair-mul` | `4096-65536` | `1.000000` | `replacement-ready` |
| `sparse-pair-mul` | `5639-65536` | `1.000000` | `candidate-faster` |

This second matrix matters because it was not only a sparse rerun. It swept
several post-pocket novelty lanes and still found sparse multiplication as the
only broad audit-ready multiply lane.

Refreshed sparse-paper control artifact:

`native-test-runs/20260621-121430-sparse-paper-detail-repeat2/matrix.tsv`

This refreshed Windows MPIR-compatible run used `--keep-all-progress-rows`, so
the artifact preserves both aggregate contiguous safe chunks and the
per-size dense-control rows. The primary `sparse-production-mul` row remained
repeat-stable over the full measured grid:

| Operation | Runs With Safe Chunks | Repeat-Stable Chunk | Worst Pair Max | Status |
| --- | ---: | --- | ---: | --- |
| `sparse-production-mul` | `2/2` | `4096-65536` | `0.247059` | `replacement-ready` |
| `sparse-zero-mul` | `2/2` | `4096-65536` | `0.247059` | `candidate-faster` |
| `sparse-production-pair-mul` | `2/2` | `5639-65536` | `1.076923` | `replacement-ready/parity` |
| `mul-dense-control` | `0/2` | none | `1.876424` | `control-parity` |

Dense-control rows from the same preserved artifact:

| Bits | Digits | Run 1 Scratch/GMP us | Run 1 Ratio | Run 1 Stable Pairs | Run 2 Scratch/GMP us | Run 2 Ratio | Run 2 Stable Pairs |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `4096` | `1234` | `19/24` | `0.760000` | `3/3` | `10/12` | `0.833333` | `3/3` |
| `11717` | `3528` | `111/144` | `0.770833` | `3/3` | `60/72` | `0.845070` | `3/3` |
| `65536` | `19729` | `1306/814` | `1.610825` | `0/3` | `1214/784` | `1.554847` | `0/3` |

All dense-control rows were exact against `mpz_mul`, marked
`controlSafety=paper-dense-control`, and kept `replacementReady=false`. The
upper anchor losing to MPIR is useful negative evidence: the sparse paper can
claim a shape-selective sparse win without implying dense-route superiority.

Linux GMP cross-check artifact:

[GitHub Actions run 27910603343](https://github.com/michaelreaton/number-x-ray-cabinet/actions/runs/27910603343)
uploaded `native-test-runs/27910603343-linux-sparse-paper/matrix.tsv`.
The runner was Ubuntu 24.04 on an AMD EPYC 9V74 virtual CPU, GCC `13.3.0`,
Release build, IPO disabled, with `libgmp10`/`libgmp-dev`
`2:6.3.0+dfsg-2ubuntu6.1`.

| Operation | Runs With Safe Chunks | Repeat-Stable Chunk | Longest Span | Worst Pair Max | Status |
| --- | ---: | --- | ---: | ---: | --- |
| `sparse-production-mul` | `2/2` | `4096-65536` | `61441` | `0.190476` | `replacement-ready` |
| `sparse-zero-mul` | `2/2` | `4096-65536` | `61441` | `0.190476` | `candidate-faster` |
| `sparse-production-pair-mul` | `2/2` | `4096-5639,11717,24103-65536` | `41434` | `1.833333` | `replacement-ready/parity` |
| `mul-dense-control` | `0/2` | none | `0` | `3.984733` | `control-parity` |

The Linux dense-control rows are a useful stronger negative control than the
Windows MPIR run: dense random multiply lost to GMP at all three control
points, with median scratch/GMP ratios of `2.000`, `2.900-3.000`, and
`3.885-3.907` at `4096`, `11717`, and `65536` bits. That keeps the portable
claim narrow: the sparse app-shaped route beats GMP on the measured sparse
window; dense random multiply does not.

## Dense Multiply Negative Results

The sparse result should not be blurred into a generic "we beat GMP" claim.
Dense multiply remains unresolved:

| Artifact | Row | Repeat-Stable Chunk | Worst Pair Max | Decision |
| --- | --- | --- | ---: | --- |
| `20260621-101950-mul-large-upper-baselines-repeat2` | `mul-dense-leaf-vs-scan` | none | `1.347154` | baseline-only |
| `20260621-101950-mul-large-upper-baselines-repeat2` | `mul-karatsuba-view-vs-copy` | none | `1.522843` | baseline-only |
| `20260621-101950-mul-large-upper-baselines-repeat2` | `mul-large-cpu-campaign` | none | `2.410811` | reject full-window route |
| `20260621-102952-full-audit-pocket-repeat3` | `mul-large-cpu-toom-full-audit` | `16384` | `1.615254` | reject route audit |
| `20260621-103933-backend-gap-repeat5` | `muladd-unroll4` | none | `1.255976` | retire primitive hint |

These rows are useful because they keep the paper honest. Number X-Ray has a
strong sparse route and a useful methodology, not a universal dense replacement
for GMP/MPIR.

## Interpretation

The sparse wins come from matching the algorithm to the operand shape:

- `2^n + 1` style products have only a tiny number of live limbs. The sparse
  production route can reduce the useful multiplication work to a handful of
  nonzero products plus normalization.
- The eight-live-limb pair-product shape stays safe across the whole measured
  campaign grid, but its margin is naturally smaller than the two-live-limb
  shape because it performs more nonzero pair products.
- Dense Toom and Karatsuba probes often look promising on median, but they keep
  failing worst-pair or repeat-stable gates. This is exactly why the benchmark
  policy requires paired medians, worst-pair safety, and repeat intersections.

The practical conclusion is piecemeal adoption. Sparse app-shaped multiply is
already useful where its route gates match the operands. Dense large multiply
should continue as a separate research campaign, with contiguous safe chunks
used to choose the next experiment rather than forcing a single full-window
route.

## Reproducibility Checklist

Recommended local commands:

```powershell
cmake --build native\build-stack-v142 --config Release
$cli = "native\build-stack-v142\xray_cli.exe"
python native\tools\bench_focus_matrix.py --cli $cli --runs 2 --preset sparse-paper --out native-test-runs\<stamp>-sparse-repeat2
python native\tools\bench_focus_matrix.py --cli $cli --runs 2 --preset post-pocket-novelty --out native-test-runs\<stamp>-novelty-matrix
```

CMake copies discovered GMP/MPIR runtime DLLs beside the Windows tools. If
configuration prints a runtime DLL warning, set `PATH` manually or point CMake
at the expected GMP/MPIR prefix before rerunning the commands.

For the non-Windows GMP side of the evidence package, run the manual GitHub
Actions workflow `Sparse Paper Benchmark`. It configures a Linux Release build
with `libgmp-dev`, runs the same `--preset sparse-paper
--keep-all-progress-rows` matrix, records runner/compiler/CPU/GMP metadata, and
uploads the full `native-test-runs/<run_id>-linux-sparse-paper` artifact. The
workflow is `workflow_dispatch` only so normal PR CI stays fast.

Artifacts to preserve:

- `matrix.tsv`
- `matrix_ranked.tsv`
- `matrix_audit_candidates.tsv`
- per-focus `summary.tsv`
- per-focus `repeat_stable_chunks.tsv`
- raw per-run `benchmark.tsv`
- raw per-run `progress.tsv`

Any external-facing paper or benchmark note should also record compiler,
Release/LTCG state, CPU model, CPU features, baseline backend name, baseline
backend version, and baseline backend library.

## Next Paper-Quality Evidence

The next evidence step is not a broader claim. It is a tighter sparse paper
package:

- Preserve the completed Linux GMP artifact from run `27910603343` and refresh
  it with a higher-repeat or physical-host run before external submission; the
  refreshed Windows MPIR-compatible dense-control artifact is also preserved
  locally.
- Keep dense-control rows in the per-size evidence table whenever the sparse
  paper artifact is refreshed.
- Keep extending the benchmark visibility contract if new paper rows are added,
  so TSV, JSON/frontier/progress text, and GUI-visible summaries stay aligned.
- Keep tracking contiguous safe chunks, including interior points between
  powers of two, so piecemeal opportunities stay visible.
