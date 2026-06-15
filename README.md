# Number X-Ray Cabinet

A self-contained browser lab inspired by Payam's live [MY GFN2](https://amathz.com/my_gfn.html) page.

The app explores Payam's idea of looking for hidden cyclotomic structure inside large integers. In MY GFN2, Payam starts with familiar Fermat-style numbers, shows how generalized Mersenne chains contain "principal" cyclotomic parts, and then expands the pattern through Shanks, alternating Shanks, and mixed-prime families. His compact destination is `Phi(n)(2^p^m)`, where `n` is squarefree and `p` is a prime dividing `n`.

Number X-Ray runs that story backward. Given an arbitrary integer `N`, it asks whether `N` could be an exact value, a near miss, or a misleading shortcut candidate for one of those cyclotomic families. The scanner therefore reports candidate `n`, `phi(n)`, estimated bases, `Phi_n(b)` matches, residue checks, and counterexamples where root-shortcut logic is fragile.

Deep Scan uses a staged pipeline: profile the input, screen many `n` values with fast modular probes, rank hypotheses, then spend the remaining budget on exact `Phi_n(b) = N` verification. Exact matches are only labeled exact after verification; screened hints remain evidence.

The sample set includes Fermat F12, `2^4096 + 1 = Phi_8192(2)`, as a large structured cyclotomic target. It is a discovery and stress example, not a promise that the local browser or native workbench can factor F12.

Factor Solver adds a bounded proof track for composites, including RSA-260 as a 260-digit regression fixture:

- recognizes the published RSA-260 decimal value when that fixture is loaded
- verifies the RSA-list checksum modulo `991889` for that fixture
- runs browser-safe small-prime, Miller-Rabin, Fermat-offset, and Pollard Rho probes
- verifies exact local factors when they are found
- reports "unsolved locally; GNFS escalation required" instead of implying that a large challenge number was factored
- keeps the cyclotomic matrix visible so Payam-style structure evidence and RSA-style factor reconnaissance can be compared side by side

## Native Proof Workbench

The `native/` folder contains the first C + GTK proof workbench milestone. It is separate from the static web app and uses GMP-compatible arbitrary precision arithmetic so integer size is limited by memory rather than JavaScript number types.

Measurable native objectives:

- solve and product-verify benchmark composites such as `10403 = 101 × 103`, `8051 = 83 × 97`, prime powers, and Carmichael numbers
- evaluate known cyclotomic values such as `Phi3(10)=111`, `Phi5(2)=31`, `Phi8(2)=17`, and Fermat F12 as `Phi8192(2)`
- parse messy integer pastes into exact decimal inputs
- emit JSON reports with factors, unresolved cofactors, proof status, timings, limits, and source notes
- keep large challenge fixtures explicitly unresolved unless exact factors are found and product verification passes

See [`native/README.md`](native/README.md) for build and import instructions. GTK4 is optional at configure time: the math core, CLI, and tests still build when GTK4 development headers are unavailable.

Open `index.html` directly in a browser, or host the folder as a static site.

GitHub repository: <https://github.com/michaelreaton/number-x-ray-cabinet>

After the launch PR is merged, the public site will be:

```text
https://michaelreaton.github.io/number-x-ray-cabinet/
```

## Persian Version

Use the in-app `FA` switch or open the app with:

```text
index.html?lang=fa
```

On GitHub Pages, Payam can use either:

```text
https://michaelreaton.github.io/number-x-ray-cabinet/?lang=fa
https://michaelreaton.github.io/number-x-ray-cabinet/fa/
```

This version is written right-to-left in Persian for Payam while keeping formulas, candidate tables, and exported JSON math fields stable.

## Credit

Concept and source page: Payam, [MY GFN2](https://amathz.com/my_gfn.html).

Payam's page builds a forward construction: start with Fermat-style and generalized Mersenne chains, extract principal cyclotomic factors, then describe the broader family as `Phi(n)(2^p^m)`. This implementation is the reverse instrument: paste a large integer, scan for fingerprints of that family, and keep exact matches separate from evidence, partial results, and counterexamples.

RSA-260 metadata and checksum come from the public RSA Challenge List mirror at <https://www.ontko.com/pub/rayo/primes/rsa_fact.html>.

## Deploy

Recommended: GitHub Pages via the included workflow in `.github/workflows/pages.yml`.

Fallback: GCP static hosting. See [`docs/deploy.md`](docs/deploy.md).
