# Number X-Ray Cabinet

A self-contained browser lab inspired by Payam's `Payam_Idea.pdf`.

The app explores Payam's idea of using an "X-ray" process to look for hidden cyclotomic structure inside large integers. It computes candidate `n`, `phi(n)`, estimated bases, `Phi_n(b)` matches, residue checks, and counterexamples where the shortcut logic is fragile.

Deep Scan uses a staged pipeline: profile the input, screen many `n` values with fast modular probes, rank hypotheses, then spend the remaining budget on exact `Phi_n(b) = N` verification. Exact matches are only labeled exact after verification; screened hints remain evidence.

RSA Scout adds a bounded reconnaissance track for RSA-260, the 260-digit RSA challenge number:

- recognizes the published RSA-260 decimal value
- verifies the RSA-list checksum modulo `991889`
- runs browser-safe small-prime, Miller-Rabin, Fermat-offset, and Pollard Rho probes
- reports "no browser-budget factor found" instead of implying that RSA-260 was factored
- keeps the cyclotomic matrix visible so Payam-style structure evidence and RSA-style factor reconnaissance can be compared side by side

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

Concept and source paper: Payam, [`Payam_Idea.pdf`](assets/Payam_Idea.pdf).

The PDF export cuts off the promised Python script after `import sympy as sp`, so this implementation reconstructs the scanner idea and labels reconstructed or skeptical behavior in the UI.

RSA-260 metadata and checksum come from the public RSA Challenge List mirror at <https://www.ontko.com/pub/rayo/primes/rsa_fact.html>.

## Deploy

Recommended: GitHub Pages via the included workflow in `.github/workflows/pages.yml`.

Fallback: GCP static hosting. See [`docs/deploy.md`](docs/deploy.md).
