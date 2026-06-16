# Native Performance Decision Log

This file records measured bigint performance decisions for the native workbench.
Benchmarks are local and noisy, so a result is actionable only when it has exact
parity plus a stable same-run paired win.

## Current Rules

- `replacementReady` requires exact parity, paired-median speed within the row
  limit, and at least 4 of 5 stable paired samples.
- `kernel-probe` rows are evidence only. They do not change production routing.
- A primitive-level win is not enough. The candidate must also survive the full
  operation shape before it can replace the current path.

## 2026-06-15: Toom-3 Full-Shape Gate

Run: `runs/20260615-041302-c4b04caf`

`mul-toom3-vs-scratch` compared one-level Toom-3 against current scratch multiply:

- 4096 digits, leaf 32: ratio `1.207`, stable `0/5`, `observe-only`
- 4096 digits, leaf 64: ratio `1.011`, stable `1/5`, `observe-only`
- 8192 digits, leaf 32: ratio `1.113`, stable `0/5`, `observe-only`
- 8192 digits, leaf 64: ratio `0.955`, stable `3/5`, `observe-only`

Decision: do not route Toom-3 into production multiply yet. The 8192 digit leaf
64 median was interesting, but it did not clear the 4 of 5 stability gate.

## 2026-06-15: Karatsuba Threshold Scout

Run: `runs/20260615-042112-c4b04caf`

A guarded dynamic threshold tried 32-limb Karatsuba leaves only for large inputs.
Production multiply stayed oracle-only:

- 4096 digits: ratio `1.03`
- 8192 digits: ratio `1.11`

Decision: rejected. The change did not improve the large rows and made the 4096
row noisier.

## 2026-06-15: BMI2/ADX Primitive Probe

Run: `runs/20260615-042614-c4b04caf`

`muladd-bmi2-adx` compared `_mulx_u64`/`_addcarryx_u64` with the existing
`_umul128`/`_addcarry_u64` multiply-accumulate primitive on this laptop:

- 617 digits: ratio `0.87`, stable `5/5`, `candidate-faster`
- 4933 digits: ratio `0.84`, stable `4/5`, `candidate-faster`

Decision: keep the probe. BMI2/ADX is locally real enough to investigate, and it
is a better bigint lead than a broad AVX2 build flag.

## 2026-06-15: BMI2/ADX Full-Route Scout

Run: `runs/20260615-044154-c4b04caf`

An unmerged scout threaded the single-chain BMI2/ADX primitive through the full
scratch multiply shape and compared it directly with current scratch multiply:

- 1000 digits: ratio `1.06`, baseline faster
- 4096 digits: ratio `1.16`, baseline faster
- 8192 digits: ratio `1.03`, baseline faster

Decision: rejected and not merged. The primitive win did not survive the current
Karatsuba/schoolbook operation shape. Future BMI2/ADX work should not be a simple
single-chain replacement; it should test a GMP-style addmul design with better
carry scheduling before touching production routing.

## 2026-06-15: Muladd Unroll Scheduling Probe

Run: `runs/20260615-045717-c4b04caf`

`muladd-unroll4` compared a four-limb unrolled `_umul128`/`_addcarry_u64`
multiply-accumulate loop with the scalar `_umul128`/`_addcarry_u64` loop:

- 617 digits: ratio `0.93`, stable `3/5`, `observe-only`
- 4933 digits: ratio `0.64`, stable `4/5`, `candidate-faster`

Decision: keep the probe. The larger row suggests loop scheduling can matter
more than swapping to BMI2/ADX alone. Production multiply should still remain
unchanged until a full multiply route proves exact parity and a stable same-run
win.

## 2026-06-15: Muladd Unroll Width Probe

Run: `runs/20260615-050656-c4b04caf`

`muladd-unroll8` was added beside `muladd-unroll4` to check whether wider loop
scheduling helps:

- unroll4, 617 digits: ratio `0.79`, stable `5/5`, `candidate-faster`
- unroll8, 617 digits: ratio `0.90`, stable `3/5`, `observe-only`
- unroll4, 4933 digits: ratio `0.79`, stable `5/5`, `candidate-faster`
- unroll8, 4933 digits: ratio `0.87`, stable `5/5`, `candidate-faster`

Decision: keep the width probe, but prefer unroll4 as the current scheduling
candidate. Wider unroll did not improve the small row and lost to unroll4 on
both measured sizes in this run.

## 2026-06-15: Unroll4 Full-Shape Probe

Run: `runs/20260615-052029-c4b04caf`

`mul-unroll4-vs-scratch` threaded the four-limb `_umul128`/`_addcarry_u64`
schedule through the full scratch multiply path and compared it directly with
current scratch multiply:

- 1000 digits: ratio `0.85`, stable `5/5`, `candidate-faster`
- 4096 digits: ratio `0.84`, stable `4/5`, `candidate-faster`
- 8192 digits: ratio `0.90`, stable `3/5`, `observe-only`

Decision: keep the probe, but do not route production multiply through it yet.
The 1000 and 4096 digit rows cleared the same-run gate, but the 8192 digit row
missed the 4 of 5 stability requirement. The same run also showed the raw
muladd primitive rows as noisy or slower, so the next step is confirmation and
carry-scheduling work rather than adoption.

## 2026-06-15: Unroll4 GMP Gate

Run: `runs/20260615-053253-c4b04caf`

`mul-unroll4-vs-gmp` compared the full unroll4 multiply candidate directly
against `mpz_mul`:

- 1000 digits: ratio `0.667`, stable `5/5`, `candidate-faster`
- 4096 digits: ratio `0.844`, stable `5/5`, `candidate-faster`
- 8192 digits: ratio `1.009`, stable `2/5`, `observe-only`

Decision: keep the direct GMP gate and do not adopt the route globally. The
candidate is locally safe-looking through 4096 digits in this run, but 8192
digits still fails the GMP gate. A future production route must be bounded to
the proven size window or collect stronger 8192+ evidence before replacing the
current path.

## 2026-06-15: Unroll4 Full Ladder Gate

Run: `runs/20260615-053942-c4b04caf`

The direct unroll4 GMP gate was expanded to the full scratch multiply ladder:

- 40 digits: vs scratch ratio `1.154`, vs GMP ratio `0.941`, stable `3/5`,
  `observe-only`
- 150 digits: vs scratch ratio `1.020`, vs GMP ratio `0.654`, stable `5/5`,
  `candidate-faster`
- 1000 digits: vs scratch ratio `0.833`, vs GMP ratio `0.658`, stable `5/5`,
  `candidate-faster`
- 4096 digits: vs scratch ratio `0.820`, vs GMP ratio `0.688`, stable `5/5`,
  `candidate-faster`
- 8192 digits: vs scratch ratio `0.921`, vs GMP ratio `1.185`, stable `1/5`,
  `observe-only`

Decision: do not use unroll4 globally. The locally safe-looking window starts
above tiny inputs and ends before the 8192 digit row. A later route scout may
try an MSVC-only bounded path for approximately 150 to 4096 digit operands, but
40 digit and 8192+ operands must stay on the current production path unless new
evidence changes those rows.

## 2026-06-15: Bounded Unroll4 Production Route

Final corrected run: `runs/20260615-055050-c4b04caf`

The production `mul` path was routed through the unroll4 leaf only on MSVC x64
when operands are not tiny, not too large, and not strongly unbalanced
(`8 <= min limbs`, `max limbs <= 256`, and `min * 3 >= max * 2`).

Production `mul` rows:

- 40 digits: ratio `0.792`, stable `5/5`, `replacement-ready`
- 150 digits: ratio `0.636`, stable `5/5`, `replacement-ready`
- 1000 digits: ratio `0.649`, stable `5/5`, `replacement-ready`
- 4096 digits: ratio `0.825`, stable `5/5`, `replacement-ready`
- 8192 digits: ratio `1.156`, stable `0/5`, `oracle-only`

Route probe rows against the scalar threshold baseline:

- 40 digits: ratio `1.203`, stable `1/5`, `observe-only`
- 150 digits: ratio `0.979`, stable `4/5`, `candidate-faster`
- 1000 digits: ratio `0.722`, stable `5/5`, `candidate-faster`
- 4096 digits: ratio `0.862`, stable `5/5`, `candidate-faster`
- 8192 digits: ratio `0.787`, stable `3/5`, `observe-only`

Decision: keep the bounded route. It improves the 4096 digit production
adoption row without routing the 8192 digit case, and it preserves the small
40 digit row by keeping it outside the route. The scalar-threshold probe remains
in the benchmark so future changes can still compare the route against the
non-routed path.

## 2026-06-15: Unroll4 Deep GMP Gate

Runs: `runs/20260615-060503-c4b04caf`, `runs/20260615-061507-c4b04caf`

The 8192 digit unroll4 rows were noisy across five-sample runs, so the
benchmark now emits a stricter `mul-unroll4-deep-vs-gmp` gate for the 4096 and
8192 digit rows using nine paired samples. Deep rows require 8 of 9 paired
samples inside the speed gate before they can be marked `promote-candidate`.

Initial gate run:

- 4096 digits: ratio `0.791`, stable `9/9`, worst pair ratio `0.925`,
  `candidate-faster`
- 8192 digits: ratio `1.017`, stable `3/9`, worst pair ratio `1.328`,
  `gmp-faster`

Strict-threshold confirmation run:

- 4096 digits: ratio `0.905`, stable `7/9`, worst pair ratio `1.063`,
  `observe-only`
- 8192 digits: ratio `1.012`, stable `3/9`, worst pair ratio `1.263`,
  `observe-only`

Decision: keep the production route bounded below the 8192 digit row. The deep
gate supports the existing 4096 production window, but it is not stable enough
to justify widening the route, and the 8192 digit result is still too noisy and
sometimes slower than GMP. A future 8192+ route needs a different algorithmic
primitive, not just a wider unroll4 switch.

## 2026-06-15: Add/Sub Measurement Floor

Run: `runs/20260615-062215-c4b04caf`

The add/sub scratch rows were sometimes flipping adoption at 4096 and 8192
digits because the sample windows were only a few hundred microseconds. The
benchmark now uses a higher add/sub iteration floor for 1000+ digit operands.
This does not change the scratch bigint implementation; it only lengthens the
measurement window.

- 4096 digit add: ratio `0.745`, stable `5/5`, `replacement-ready`
- 4096 digit sub: ratio `0.743`, stable `5/5`, `replacement-ready`
- 8192 digit add: ratio `0.799`, stable `5/5`, `replacement-ready`
- 8192 digit sub: ratio `0.818`, stable `5/5`, `replacement-ready`

Decision: keep the higher iteration floor. The benchmark still finishes in
about ten seconds locally while removing noisy large add/sub oracle-only
flips. Remaining scratch oracle-only rows are parse at 4096/8192 digits and
multiply at 8192 digits.

## 2026-06-15: 16384 Digit Multiply Discovery Tier

Run: `runs/20260615-063047-c4b04caf`

The next multiply frontier is larger than the current 8192 digit scratch gate,
but parse is being worked separately. The benchmark now adds 16384 digit rows
only to multiply-specific surfaces: scratch `mul`, runtime threshold probes,
Toom-3 probes, and unroll4 GMP gates. This is evidence collection only; no
production route changes are made by this tier.

- 16384 digit scratch `mul`: ratio `1.307`, stable `1/5`, `oracle-only`
- 16384 digit unroll4 vs scratch: ratio `0.693`, stable `5/5`,
  `candidate-faster`
- 16384 digit unroll4 vs GMP: ratio `1.040`, stable `2/5`, `gmp-faster`
- 16384 digit deep unroll4 vs GMP: ratio `1.143`, stable `3/9`,
  `gmp-faster`
- Best 16384 digit Toom-3 vs scratch row: ratio `0.940`, stable `3/5`,
  `candidate-no-margin`

Decision: do not widen the unroll4 route. The 16384 rows show that unroll4
improves our scalar scratch path but does not catch GMP at this size; GMP is
winning from higher-level multiplication strategy, not just leaf scheduling.
The next larger-number multiply work should target algorithm structure above
the leaf loop.

## 2026-06-15: Toom-3 + Unroll4 Leaf Probe

Run: `runs/20260615-070407-c4b04caf`

The benchmark now combines the one-level Toom-3 split with the bounded MSVC x64
unroll4 leaf schedule. This is an evidence-only probe for the larger-number
frontier: the candidate must still pass exact GMP parity, and the production
route is unchanged unless the same-run stability gate is met.

- 8192 digits, leaf 32 vs scratch: ratio `1.357`, stable `1/5`,
  `observe-only`
- 8192 digits, leaf 32 vs GMP: ratio `0.994`, stable `2/5`, `observe-only`
- 8192 digits, leaf 64 vs scratch: ratio `0.900`, stable `5/5`,
  `promote-candidate`
- 8192 digits, leaf 64 vs GMP: ratio `0.973`, stable `3/5`, `observe-only`
- 16384 digits, leaf 32 vs scratch: ratio `1.007`, stable `2/5`,
  `observe-only`
- 16384 digits, leaf 32 vs GMP: ratio `1.303`, stable `0/5`, `observe-only`
- 16384 digits, leaf 64 vs scratch: ratio `0.851`, stable `5/5`,
  `promote-candidate`
- 16384 digits, leaf 64 vs GMP: ratio `1.092`, stable `1/5`, `observe-only`

Decision: keep the probe, but do not route production multiplication through it
yet. Leaf 64 is a clear improvement over the current scratch path at 8192 and
16384 digits, which validates combining algorithm structure with leaf
scheduling. It still does not beat GMP with enough stability, so the next
larger-number step should explore deeper Toom thresholds or Karatsuba/Toom
handoff policy before any replacement claim.

## 2026-06-15: Toom-3 + Unroll4 Deep GMP Gate

Run: `runs/20260615-071642-c4b04caf`

The leaf 64 Toom-3+unroll4 row sometimes looks like a near GMP win in the
ordinary five-sample benchmark, so the benchmark now emits
`mul-toom3-unroll4-deep-vs-gmp` rows for 8192 and 16384 digits with nine paired
samples. Deep rows need 8 of 9 paired samples inside the speed gate before they
can become promotion candidates.

Five-sample rows from this run:

- 8192 digits, leaf 64 vs scratch: ratio `0.862`, stable `5/5`,
  `promote-candidate`
- 8192 digits, leaf 64 vs GMP: ratio `1.000`, stable `2/5`,
  `observe-only`
- 16384 digits, leaf 64 vs scratch: ratio `0.748`, stable `5/5`,
  `promote-candidate`
- 16384 digits, leaf 64 vs GMP: ratio `1.052`, stable `2/5`,
  `observe-only`

Deep GMP rows:

- 8192 digits, leaf 64 vs GMP: ratio `0.986`, stable `4/9`, worst pair ratio
  `1.047`, `observe-only`
- 16384 digits, leaf 64 vs GMP: ratio `1.125`, stable `3/9`, worst pair ratio
  `1.188`, `observe-only`

Decision: do not route production multiplication through Toom-3+unroll4 yet.
The combined candidate is a real improvement over the current scratch path, but
the stricter GMP gate shows it is not a stable GMP replacement on this laptop.
Next work should test a more GMP-like handoff ladder, not simply reuse the same
one-level Toom split with more confidence.

## 2026-06-15: Toom-3 + Unroll4 Handoff Scout

Run: `runs/20260615-072856-c4b04caf`

The benchmark now adds GMP-facing Toom-3+unroll4 scout rows at leaf thresholds
`24`, `48`, and `96` for 8192 and 16384 digit operands. The goal is to learn
whether a GMP-like handoff threshold changes the picture before any production
route is touched. Leaf 96 also gets the nine-sample deep GMP gate because it
looked promising in local scout rows.

Five-sample GMP rows:

- 8192 digits, leaf 24: ratio `1.239`, stable `0/5`, `observe-only`
- 8192 digits, leaf 48: ratio `1.004`, stable `1/5`, `observe-only`
- 8192 digits, leaf 64: ratio `0.998`, stable `0/5`, `observe-only`
- 8192 digits, leaf 96: ratio `1.048`, stable `2/5`, `observe-only`
- 16384 digits, leaf 24: ratio `1.389`, stable `0/5`, `observe-only`
- 16384 digits, leaf 48: ratio `0.954`, stable `3/5`, `observe-only`
- 16384 digits, leaf 64: ratio `1.184`, stable `0/5`, `observe-only`
- 16384 digits, leaf 96: ratio `0.859`, stable `3/5`, `observe-only`

Deep GMP rows:

- 8192 digits, leaf 64: ratio `1.016`, stable `3/9`, worst pair ratio
  `1.622`, `observe-only`
- 8192 digits, leaf 96: ratio `0.868`, stable `9/9`, worst pair ratio
  `0.929`, `promote-candidate`
- 16384 digits, leaf 64: ratio `1.090`, stable `2/9`, worst pair ratio
  `1.349`, `observe-only`
- 16384 digits, leaf 96: ratio `0.923`, stable `5/9`, worst pair ratio
  `1.336`, `observe-only`

Decision: keep leaf 96 as the next bounded route candidate for the 8192 digit
frontier only. It survived the deep GMP gate at 8192, but not at 16384. The
next route experiment should be explicitly bounded by size and should keep the
16384 row as a guardrail, not as an adoption target.

## 2026-06-15: Bounded Toom-3 + Unroll4 Route Rejected

Runs: `native/build-codex-fresh/native-test-runs/20260615-073725-c4b04caf`,
`runs/20260615-073822-c4b04caf`

A temporary production scratch route sent balanced `384..512` limb operands
through Toom-3+unroll4 with a 96-limb leaf. The intended adoption target was
the 8192 digit multiply row, while 16384 digits remained the guardrail.

CTest benchmark with the route:

- 8192 digit scratch `mul`: ratio `1.314`, stable `2/5`, worst pair ratio
  `1.693`, `oracle-only`
- 16384 digit scratch `mul`: ratio `1.326`, stable `0/5`, worst pair ratio
  `1.479`, `oracle-only`
- 8192 digit deep leaf 96 vs GMP: ratio `1.006`, stable `4/9`,
  `observe-only`

CLI benchmark with the same route:

- 8192 digit scratch `mul`: ratio `0.903`, stable `4/5`, worst pair ratio
  `1.006`, `replacement-ready`
- 16384 digit scratch `mul`: ratio `1.240`, stable `2/5`, worst pair ratio
  `1.352`, `oracle-only`
- 8192 digit deep leaf 96 vs GMP: ratio `1.005`, stable `4/9`,
  `observe-only`

Decision: reject the route and leave production multiplication unchanged. The
ordinary scratch row was too run-sensitive, and the deep GMP gate did not
confirm the candidate. This is useful evidence, but not permission to route.

## 2026-06-15: Recursive Toom-3 + Unroll4 Probe

Runs: `native/build-codex-fresh/native-test-runs/20260615-075145-c4b04caf`,
`runs/20260615-075234-c4b04caf`

The benchmark now includes `mul-toom3-u4-rec-vs-gmp` rows at the 16384 digit
frontier. The candidate uses depth-limited recursive Toom-3 with the MSVC x64
unroll4 leaf. A direct 12000 digit oracle test verifies exact output parity
against GMP before benchmark rows are trusted.

CTest benchmark:

- 16384 digits, leaf 64, depth 2: ratio `0.930`, stable `4/5`, worst pair
  ratio `1.266`, `promote-candidate`
- 16384 digits, leaf 96, depth 2: ratio `1.103`, stable `1/5`, worst pair
  ratio `1.317`, `observe-only`

CLI benchmark:

- 16384 digits, leaf 64, depth 2: ratio `1.082`, stable `0/5`, worst pair
  ratio `1.413`, `observe-only`
- 16384 digits, leaf 96, depth 2: ratio `1.178`, stable `1/5`, worst pair
  ratio `1.258`, `observe-only`

Decision: keep the recursive probe, but do not route it or add a deep gate yet.
The leaf 64 result was promising in one run and disappeared in the canonical
CLI benchmark. This points to benchmark noise or operand-shape sensitivity, not
a proven GMP replacement.

## 2026-06-15: Recursive Toom-3 + Unroll4 Deep Gate

Run: `runs/20260615-080354-c4b04caf`

The recursive leaf 64 row produced contradictory five-sample results, so the
benchmark now emits `mul-toom3-u4-rec-deep-vs-gmp` for the 16384 digit frontier
with nine paired samples.

CTest benchmark:

- 16384 digits, leaf 64, depth 2: ratio `1.092`, stable `1/5`,
  `observe-only`
- 16384 digits, leaf 96, depth 2: ratio `1.103`, stable `1/5`,
  `observe-only`
- 16384 digits, leaf 64, depth 2 deep gate: ratio `1.135`, stable `0/9`,
  worst pair ratio `1.724`, `observe-only`

CLI benchmark:

- 16384 digits, leaf 64, depth 2: ratio `1.016`, stable `2/5`,
  `observe-only`
- 16384 digits, leaf 96, depth 2: ratio `0.872`, stable `4/5`,
  `promote-candidate`
- 16384 digits, leaf 64, depth 2 deep gate: ratio `1.061`, stable `2/9`,
  worst pair ratio `1.451`, `observe-only`

Decision: reject recursive leaf 64 as a route candidate. The deep gate shows it
does not beat GMP consistently. Leaf 96 had a noisy five-sample win in the CLI
run, but it needs its own deep gate before it can become more than a scout row.

## 2026-06-15: Recursive Toom-3 Leaf 96 Deep Gate

Run: `runs/20260615-081355-c4b04caf`

The leaf 96 recursive row also looked noisy in prior runs, so the benchmark now
emits a second `mul-toom3-u4-rec-deep-vs-gmp` row at the 16384 digit frontier.
Both recursive leaf thresholds are now checked with nine paired samples before
they can influence any production route.

CTest benchmark:

- 16384 digits, leaf 64, depth 2: ratio `1.099`, stable `2/5`,
  `observe-only`
- 16384 digits, leaf 96, depth 2: ratio `1.103`, stable `1/5`,
  `observe-only`
- 16384 digits, leaf 64, depth 2 deep gate: ratio `1.080`, stable `0/9`,
  worst pair ratio `1.452`, `observe-only`
- 16384 digits, leaf 96, depth 2 deep gate: ratio `1.093`, stable `1/9`,
  worst pair ratio `1.183`, `observe-only`

CLI benchmark:

- 16384 digits, leaf 64, depth 2: ratio `0.856`, stable `4/5`,
  `promote-candidate`
- 16384 digits, leaf 96, depth 2: ratio `1.040`, stable `2/5`,
  `observe-only`
- 16384 digits, leaf 64, depth 2 deep gate: ratio `0.977`, stable `5/9`,
  worst pair ratio `1.567`, `observe-only`
- 16384 digits, leaf 96, depth 2 deep gate: ratio `1.103`, stable `2/9`,
  worst pair ratio `1.387`, `observe-only`

Decision: reject recursive leaf 96 as a route candidate. The deep gate shows it
does not beat GMP, and the broader recursive experiment still has high
worst-pair variance even when a five-sample scout row looks attractive.

## 2026-06-15: Specialized Square Basecase Probe

Runs: `native-test-runs/20260615-083151-c4b04caf` and
`runs/20260615-082901-c4b04caf`

GMP documents squaring as a separate basecase path because the off-diagonal
cross-products can be computed once and doubled instead of doing a full
rectangular multiply (`https://gmplib.org/manual/Basecase-Multiplication.html`).
The scratch engine now has an oracle-tested
`xray_bigint_square` primitive and emits `square` rows in the benchmark ladder,
but the production multiply route is unchanged.

CTest benchmark:

- 40 digits: ratio `1.038`, stable `0/5`, `oracle-only`
- 150 digits: ratio `0.676`, stable `5/5`, `allowed`
- 1000 digits: ratio `0.874`, stable `5/5`, `allowed`
- 4096 digits: ratio `1.500`, stable `0/5`, `oracle-only`
- 8192 digits: ratio `2.500`, stable `0/5`, `oracle-only`
- 16384 digits: ratio `2.673`, stable `0/5`, `oracle-only`

CLI benchmark:

- 40 digits: ratio `1.000`, stable `3/5`, `oracle-only`
- 150 digits: ratio `0.725`, stable `5/5`, `allowed`
- 1000 digits: ratio `0.849`, stable `4/5`, `allowed`
- 4096 digits: ratio `1.469`, stable `0/5`, `oracle-only`
- 8192 digits: ratio `2.095`, stable `0/5`, `oracle-only`
- 16384 digits: ratio `3.101`, stable `0/5`, `oracle-only`

Decision: keep square as a proved primitive and benchmark row, but do not route
large multiplication through it. It only wins in the bounded schoolbook-size
middle and loses badly once Karatsuba/Toom-style multiplication should dominate.

## 2026-06-15: Specialized Square Route Gate

Runs: `native-test-runs/20260615-084310-c4b04caf` and
`runs/20260615-084419-c4b04caf`

The first square probe compared specialized square to GMP. This gate compares
the same primitive to the current scratch self-multiply path using
`square-vs-mul`, so a production route must beat the code it would replace, not
just the GMP baseline.

CTest benchmark:

- 40 digits: ratio `1.231`, stable `0/5`, `observe-only`
- 150 digits: ratio `1.000`, stable `2/5`, `observe-only`
- 1000 digits: ratio `0.724`, stable `5/5`, `promote-candidate`
- 4096 digits: ratio `0.933`, stable `3/5`, `observe-only`
- 8192 digits: ratio `1.170`, stable `0/5`, `observe-only`
- 16384 digits: ratio `1.470`, stable `0/5`, `observe-only`

CLI benchmark:

- 40 digits: ratio `1.231`, stable `0/5`, `observe-only`
- 150 digits: ratio `1.000`, stable `1/5`, `observe-only`
- 1000 digits: ratio `0.707`, stable `5/5`, `promote-candidate`
- 4096 digits: ratio `0.903`, stable `3/5`, `observe-only`
- 8192 digits: ratio `0.948`, stable `3/5`, `observe-only`
- 16384 digits: ratio `1.589`, stable `0/5`, `observe-only`

Decision: reject the production square route for now. The 1000 digit row is
real, but adjacent and larger tiers do not pass stability, so the improvement is
not broad enough to apply safely.

## 2026-06-15: Karatsuba Square Probe

Runs: `native-test-runs/20260615-085808-c4b04caf` and
`runs/20260615-085910-c4b04caf`

The schoolbook square route gate showed that a specialized square primitive is
not enough at large sizes. This probe adds an unrouted recursive Karatsuba
square candidate and compares it against both current scratch self-multiply and
`mpz_mul`.

CTest benchmark:

- `square-karatsuba-vs-mul`, 1000 digits: ratio `0.686`, stable `5/5`,
  `promote-candidate`
- `square-karatsuba-vs-gmp`, 1000 digits: ratio `0.887`, stable `5/5`,
  `promote-candidate`
- `square-karatsuba-vs-mul`, 4096 digits: ratio `0.715`, stable `4/5`,
  `promote-candidate`
- `square-karatsuba-vs-gmp`, 4096 digits: ratio `0.966`, stable `3/5`,
  `observe-only`
- `square-karatsuba-vs-mul`, 8192 digits: ratio `1.040`, stable `2/5`,
  `observe-only`
- `square-karatsuba-vs-gmp`, 8192 digits: ratio `1.433`, stable `1/5`,
  `observe-only`
- `square-karatsuba-vs-mul`, 16384 digits: ratio `0.839`, stable `3/5`,
  `observe-only`
- `square-karatsuba-vs-gmp`, 16384 digits: ratio `1.546`, stable `0/5`,
  `observe-only`

CLI benchmark:

- `square-karatsuba-vs-mul`, 1000 digits: ratio `0.718`, stable `5/5`,
  `promote-candidate`
- `square-karatsuba-vs-gmp`, 1000 digits: ratio `0.895`, stable `4/5`,
  `promote-candidate`
- `square-karatsuba-vs-mul`, 4096 digits: ratio `0.690`, stable `5/5`,
  `promote-candidate`
- `square-karatsuba-vs-gmp`, 4096 digits: ratio `1.036`, stable `1/5`,
  `observe-only`
- `square-karatsuba-vs-mul`, 8192 digits: ratio `0.596`, stable `5/5`,
  `promote-candidate`
- `square-karatsuba-vs-gmp`, 8192 digits: ratio `1.279`, stable `2/5`,
  `observe-only`
- `square-karatsuba-vs-mul`, 16384 digits: ratio `0.737`, stable `5/5`,
  `promote-candidate`
- `square-karatsuba-vs-gmp`, 16384 digits: ratio `1.521`, stable `1/5`,
  `observe-only`

Decision: keep Karatsuba square as an exact, promising probe but do not route it
yet. It beats current scratch self-multiply in useful bands, but it only beats
GMP at 1000 digits and remains behind GMP at larger sizes on this laptop.

## 2026-06-15: Karatsuba Square Threshold Scout

Runs: `native-test-runs/20260615-090857-c4b04caf` and
`runs/20260615-091005-c4b04caf`

The threshold scout adds 32, 96, and 128 limb handoff rows for the large
Karatsuba-square candidate. The goal is to see whether the 64 limb handoff was
masking a GMP-competitive route.

CTest benchmark:

- Best 4096 digit `square-karatsuba-vs-gmp`: threshold `96`, ratio `1.054`,
  stable `1/5`, `observe-only`
- Best 8192 digit `square-karatsuba-vs-gmp`: threshold `64`, ratio `1.237`,
  stable `0/5`, `observe-only`
- Best 16384 digit `square-karatsuba-vs-gmp`: threshold `128`, ratio `1.302`,
  stable `0/5`, `observe-only`
- Best 4096 digit `square-karatsuba-vs-mul`: threshold `96`, ratio `0.737`,
  stable `5/5`, `promote-candidate`
- Best 8192 digit `square-karatsuba-vs-mul`: threshold `64`, ratio `0.658`,
  stable `4/5`, `promote-candidate`
- Best 16384 digit `square-karatsuba-vs-mul`: threshold `128`, ratio `0.689`,
  stable `5/5`, `promote-candidate`

CLI benchmark:

- Best 4096 digit `square-karatsuba-vs-gmp`: threshold `96`, ratio `0.994`,
  stable `2/5`, `observe-only`
- Best 8192 digit `square-karatsuba-vs-gmp`: threshold `96`, ratio `1.207`,
  stable `1/5`, `observe-only`
- Best 16384 digit `square-karatsuba-vs-gmp`: threshold `64`, ratio `1.370`,
  stable `0/5`, `observe-only`
- Best 4096 digit `square-karatsuba-vs-mul`: threshold `32`, ratio `0.633`,
  stable `4/5`, `promote-candidate`
- Best 8192 digit `square-karatsuba-vs-mul`: threshold `96`, ratio `0.717`,
  stable `5/5`, `promote-candidate`
- Best 16384 digit `square-karatsuba-vs-mul`: threshold `64`, ratio `0.709`,
  stable `5/5`, `promote-candidate`

Decision: do not route Karatsuba square yet. Threshold tuning confirms the
candidate is useful against our current self-multiply path, but no large
threshold is stable or fast enough against GMP.

## 2026-06-15: Decimal Format Benchmark Gate

Runs: `native-test-runs/20260615-092635-c4b04caf` and
`runs/20260615-092749-c4b04caf`

The parse path already uses 19 digit chunks, so a matching 19 digit decimal
formatter was tested as a possible serialization improvement. The local run was
exact but slower than both GMP and the restored 9 digit formatter on the larger
rows, so the production formatter remains on the existing 9 digit chunk path.
The benchmark ladder now keeps `format` rows beside `parse` rows so future
formatter changes have an explicit parity and speed gate.

Rejected 19 digit formatter CTest scout:

- 40 digits: ratio `2.183`, stable `0/5`, `oracle-only`
- 150 digits: ratio `2.230`, stable `0/5`, `oracle-only`
- 1000 digits: ratio `4.232`, stable `0/5`, `oracle-only`
- 4096 digits: ratio `6.833`, stable `0/5`, `oracle-only`
- 8192 digits: ratio `8.975`, stable `0/5`, `oracle-only`

Restored 9 digit formatter CTest benchmark:

- 40 digits: ratio `2.101`, stable `0/5`, `oracle-only`
- 150 digits: ratio `2.691`, stable `0/5`, `oracle-only`
- 1000 digits: ratio `3.091`, stable `0/5`, `oracle-only`
- 4096 digits: ratio `5.510`, stable `0/5`, `oracle-only`
- 8192 digits: ratio `6.069`, stable `0/5`, `oracle-only`

Restored 9 digit formatter CLI benchmark:

- 40 digits: ratio `2.087`, stable `0/5`, `oracle-only`
- 150 digits: ratio `2.585`, stable `0/5`, `oracle-only`
- 1000 digits: ratio `3.078`, stable `0/5`, `oracle-only`
- 4096 digits: ratio `4.475`, stable `0/5`, `oracle-only`
- 8192 digits: ratio `5.269`, stable `0/5`, `oracle-only`

Decision: do not adopt the 19 digit formatter. Keep decimal formatting measured
as an oracle-only row until a new algorithm beats `mpz_get_str` locally with
stable same-run ratios.

## 2026-06-15: Manual Decimal Chunk Writer

Runs: `native-test-runs/20260615-093548-c4b04caf` and
`runs/20260615-093650-c4b04caf`

The previous formatter called `snprintf` once per 9 digit output chunk after the
division pass. This change keeps the same chunking and division algorithm, but
writes the leading chunk and padded 9 digit chunks with small fixed-width loops.
It is a local overhead cleanup, not a new base-conversion algorithm.

CTest benchmark:

- 40 digits: ratio `0.934`, stable `3/5`, `oracle-only`
- 150 digits: ratio `1.113`, stable `0/5`, `oracle-only`
- 1000 digits: ratio `2.304`, stable `0/5`, `oracle-only`
- 4096 digits: ratio `4.927`, stable `0/5`, `oracle-only`
- 8192 digits: ratio `5.641`, stable `0/5`, `oracle-only`

CLI benchmark:

- 40 digits: ratio `0.834`, stable `3/5`, `oracle-only`
- 150 digits: ratio `1.056`, stable `2/5`, `oracle-only`
- 1000 digits: ratio `2.299`, stable `0/5`, `oracle-only`
- 4096 digits: ratio `4.410`, stable `0/5`, `oracle-only`
- 8192 digits: ratio `4.590`, stable `0/5`, `oracle-only`

Decision: keep the manual chunk writer because it improves the measured format
rows without changing arithmetic or output shape. Formatting remains
oracle-only; the next meaningful step is an algorithmic conversion change rather
than more output-copy tuning.

## 2026-06-15: Formatter In-Place Divmod Specialization

Runs: `native-test-runs/20260615-094557-c4b04caf` and
`runs/20260615-094706-c4b04caf`

The formatter's base-1e9 conversion used the generic `xray_bigint_divmod_u32`
helper once per output chunk, even though the divisor and output destination are
fixed. This change keeps the same conversion algorithm but uses a
formatter-private in-place loop with the precomputed reciprocal for `1e9`.

CTest benchmark:

- 40 digits: ratio `0.853`, stable `5/5`, `replacement-ready`
- 150 digits: ratio `1.032`, stable `1/5`, `oracle-only`
- 1000 digits: ratio `2.234`, stable `0/5`, `oracle-only`
- 4096 digits: ratio `4.890`, stable `0/5`, `oracle-only`
- 8192 digits: ratio `6.260`, stable `0/5`, `oracle-only`

CLI benchmark:

- 40 digits: ratio `0.851`, stable `5/5`, `replacement-ready`
- 150 digits: ratio `1.024`, stable `2/5`, `oracle-only`
- 1000 digits: ratio `2.215`, stable `0/5`, `oracle-only`
- 4096 digits: ratio `3.223`, stable `0/5`, `oracle-only`
- 8192 digits: ratio `4.328`, stable `0/5`, `oracle-only`

Decision: keep the specialization. It promotes the 40 digit format row under
both CTest and CLI, and CLI shows larger rows improving too. Large decimal
formatting remains far from GMP, so the next frontier is divide-and-conquer or
larger-base conversion rather than more wrapper trimming.

## 2026-06-15: Route Karatsuba Square

Runs: `native-test-runs/20260615-095524-c4b04caf` and
`runs/20260615-095624-c4b04caf`

`xray_bigint_square` was still calling the schoolbook square implementation
directly even though the Karatsuba square probe was exact and consistently
faster against the current scratch self-multiply baseline at larger sizes. This
change routes public square through the existing threshold dispatcher. Small
operands below the handoff still use schoolbook square.

CTest benchmark:

- 40 digits: ratio `1.190`, stable `0/5`, `oracle-only`
- 150 digits: ratio `0.744`, stable `5/5`, `replacement-ready`
- 1000 digits: ratio `0.892`, stable `5/5`, `replacement-ready`
- 4096 digits: ratio `1.062`, stable `0/5`, `oracle-only`
- 8192 digits: ratio `1.265`, stable `0/5`, `oracle-only`
- 16384 digits: ratio `1.564`, stable `0/5`, `oracle-only`

CLI benchmark:

- 40 digits: ratio `1.063`, stable `1/5`, `oracle-only`
- 150 digits: ratio `0.672`, stable `5/5`, `replacement-ready`
- 1000 digits: ratio `0.889`, stable `4/5`, `replacement-ready`
- 4096 digits: ratio `1.021`, stable `0/5`, `oracle-only`
- 8192 digits: ratio `1.303`, stable `0/5`, `oracle-only`
- 16384 digits: ratio `1.481`, stable `0/5`, `oracle-only`

Decision: keep the route. It substantially improves large square behavior
without claiming GMP replacement status. The 40 digit row remains too small and
noisy for promotion, while 150 and 1000 digit rows stay replacement-ready.

## 2026-06-15: Widen Unroll4 Multiply Window To 512 Limbs

Runs: `native-test-runs/20260615-100414-c4b04caf`,
`runs/20260615-100513-c4b04caf`, and `runs/20260615-100626-c4b04caf`

The production unroll4 multiply route was capped at `256` limbs, so the
8192-digit benchmark stayed on the scalar leaf even though the deep unroll4
probe showed a stable 8192-digit win. This change widens the balanced MSVC x64
window to `512` limbs. The 16384-digit row remains outside the route because its
probe still loses to GMP.

CTest benchmark:

- 4096 digit `mul`: ratio `0.882`, stable `4/5`, `replacement-ready`
- 8192 digit `mul`: ratio `0.957`, stable `4/5`, `replacement-ready`
- 16384 digit `mul`: ratio `1.202`, stable `2/5`, `oracle-only`

CLI benchmark:

- 4096 digit `mul`: ratio `0.906`, stable `3/5`, `oracle-only`
- 8192 digit `mul`: ratio `0.939`, stable `3/5`, `oracle-only`
- 16384 digit `mul`: ratio `1.313`, stable `1/5`, `oracle-only`

CLI repeat benchmark:

- 4096 digit `mul`: ratio `0.873`, stable `4/5`, `replacement-ready`
- 8192 digit `mul`: ratio `0.925`, stable `5/5`, `replacement-ready`
- 16384 digit `mul`: ratio `1.155`, stable `1/5`, `oracle-only`

Decision: keep the 512 limb cap. It targets the 8192-digit band that the deep
probe supported, avoids routing the still-losing 16384-digit band, and improves
the 8192 row in all local runs even when one CLI run did not cross the stability
gate.

## 2026-06-15: Large Decimal Horner Formatter

Runs: `native-test-runs/20260615-103430-c4b04caf`,
`runs/20260615-102828-c4b04caf`, and `runs/20260615-102935-c4b04caf`

The repeated base-1e9 division formatter still scales poorly on large decimal
output. This change adds a larger-limb conversion path that builds base-1e9
decimal chunks directly from binary limbs using `decimal = decimal * 2^64 +
limb`. Small values remain on the existing division path so the already-proven
short-format rows are protected.

CTest benchmark:

- 1000 digit `format`: ratio `2.235`, stable `0/5`, `oracle-only`
- 4096 digit `format`: ratio `2.780`, stable `0/5`, `oracle-only`
- 8192 digit `format`: ratio `3.617`, stable `0/5`, `oracle-only`

CLI benchmark:

- 1000 digit `format`: ratio `2.281`, stable `0/5`, `oracle-only`
- 4096 digit `format`: ratio `3.415`, stable `0/5`, `oracle-only`
- 8192 digit `format`: ratio `3.153`, stable `0/5`, `oracle-only`

CLI repeat benchmark:

- 1000 digit `format`: ratio `1.794`, stable `0/5`, `oracle-only`
- 4096 digit `format`: ratio `3.893`, stable `0/5`, `oracle-only`
- 8192 digit `format`: ratio `5.102`, stable `0/5`, `oracle-only`

Decision: keep the Horner path as a large-format improvement, but do not treat
it as a replacement route. Scratch time improves on the large rows, especially
4096 digits, while GMP still wins decisively at the largest output sizes. The
next formatter frontier is a divide-and-conquer conversion, not another linear
chunk loop.

## 2026-06-15: Copy Subtraction Tails After Borrow Clears

Runs: `native/build-codex-fresh/native-test-runs/20260615-112350-c4b04caf`
and `runs/20260615-112502-c4b04caf`

Large subtraction was close to the adoption gate but still unstable at the
8192-digit row. The subtraction loop now stops using the borrow-chain primitive
once the right operand is exhausted and no borrow remains, then copies the
unchanged high limbs from the left operand. The route is covered by a GMP oracle
test using a large left operand, a small right operand, and aliasing back into
the left operand.

CTest benchmark:

- summary: `176/176` passed, `40` replacement-ready, `12` oracle-only
- 4096 digit `sub`: ratio `0.63`, stable `5/5`, `replacement-ready`
- 8192 digit `sub`: ratio `0.71`, stable `4/5`, `replacement-ready`

CLI benchmark:

- summary: `176/176` passed, `40` replacement-ready, `12` oracle-only
- 4096 digit `sub`: ratio `0.67`, stable `5/5`, `replacement-ready`
- 8192 digit `sub`: ratio `0.62`, stable `5/5`, `replacement-ready`

Decision: keep the tail copy. It is a semantics-preserving subtraction fast path
and moves the 8192-digit subtraction row from evidence-only to locally
replacement-ready in both local benchmark paths.

## 2026-06-15: Pre-Reserve Decimal Formatter Chunks

Runs: `native/build-codex-fresh/native-test-runs/20260615-114431-c4b04caf`
and `runs/20260615-114528-c4b04caf`

The decimal formatter now reserves a conservative decimal-chunk capacity from
the binary limb count before conversion starts. This avoids repeated chunk-array
growth in both the small repeated-division formatter and the larger Horner limb
formatter. The reserve helper also rejects impossible allocation sizes before
`realloc` size multiplication can overflow.

CTest benchmark:

- summary: `176/176` passed, `41` replacement-ready, `11` oracle-only
- 40 digit `format`: ratio `0.85`, stable `4/5`, `replacement-ready`
- 150 digit `format`: ratio `0.84`, stable `5/5`, `replacement-ready`
- 1000 digit `format`: ratio `2.21`, stable `0/5`, `oracle-only`
- 4096 digit `format`: ratio `4.05`, stable `0/5`, `oracle-only`
- 8192 digit `format`: ratio `4.47`, stable `0/5`, `oracle-only`

CLI benchmark:

- summary: `176/176` passed, `40` replacement-ready, `12` oracle-only
- 40 digit `format`: ratio `0.82`, stable `5/5`, `replacement-ready`
- 150 digit `format`: ratio `0.84`, stable `4/5`, `replacement-ready`
- 1000 digit `format`: ratio `2.20`, stable `0/5`, `oracle-only`
- 4096 digit `format`: ratio `4.23`, stable `0/5`, `oracle-only`
- 8192 digit `format`: ratio `4.03`, stable `0/5`, `oracle-only`

Decision: keep the pre-reserve path as a small formatter safety and stability
improvement. It promotes the 150-digit format row in both local runs, but it
does not change the large-number conclusion: 1000+ digit formatting still needs
a divide-and-conquer conversion rather than more linear chunk-loop tuning.

## 2026-06-15: External MFastFermat Root-Gated Threshold Lesson

Source repo: `C:\Users\mike\Documents\MFastFermat`

MFastFermat's BigCPU Montgomery square path retested Karatsuba leaf/band
thresholds on real factor-form survivor rows, not generic random multiply
inputs. The useful result was shape-gated, not global:

- A global 96-limb Karatsuba band improved the 96k/128k frontier phase rows and
  product rows, but it regressed a smaller 3180-bit worker-matched product row
  versus the existing default.
- The promoted route kept smaller roots on the prior 128-limb fallback and used
  a 96-limb band only for roots of at least 1800 limbs.
- The rebuilt default then reported `active_band_threshold=128` at 8036 bits and
  `active_band_threshold=96` at 128036 bits.
- The 128036-bit product/GMP row moved from the old default miss
  (`0.855x` in `benchmark_product_default_vs_gmp_96k128k_20260615.txt`) to a
  same-shape default win (`1.047x` in
  `benchmark_promoted_default_vs_gmp_128k_20260615.txt`), with product/GMP
  sample fingerprints checked by the harness.

Decision: carry this lesson into PayamAnalysis threshold work. Do not promote a
single multiply/square threshold just because it wins a frontier row. Record the
operand shape and route by shape or size band when needed. Treat phase probes as
scouts only; the adoption signal is the full operation under the actual app
workload, paired against the GMP oracle with matching samples and stable-run
evidence. Also keep the sum-vs-difference Karatsuba middle term as an explicit
benchmark dimension: the MFastFermat difference-form plus endpoint-copy route
survived product testing better than the classic sum-form middle in that
workload.
