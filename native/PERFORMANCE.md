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
