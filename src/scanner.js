(function attachScanner(root, factory) {
  const api = factory(root.XRayPolynomial || (typeof require !== "undefined" ? require("./polynomial.js") : null));
  if (typeof module !== "undefined" && module.exports) {
    module.exports = api;
  }
  root.XRayScanner = api;
})(typeof globalThis !== "undefined" ? globalThis : this, function createScannerApi(math) {
  if (!math) throw new Error("XRayPolynomial is required before XRayScanner");

  const RESIDUE_PRIMES = [3, 5, 7, 11, 13, 17, 19, 23, 29];
  const SOURCE_NOTE =
    "Credit: Payam. Payam_Idea.pdf is the source paper for this app. Its visible script is cut off after `import sympy as sp`, so this app reconstructs the scanner idea and exposes fragile assumptions.";

  function parseIntegerInput(raw) {
    const compact = String(raw || "").replace(/[\s,_]/g, "");
    if (!/^[+-]?\d+$/.test(compact)) {
      throw new Error("Input must be an integer. Spaces, commas, and underscores are allowed as separators.");
    }
    const value = BigInt(compact);
    if (value <= 0n) throw new Error("Input must be a positive integer.");
    return value;
  }

  function formatBigInt(value, max = 24) {
    const text = BigInt(value).toString();
    if (text.length <= max) return text;
    const head = Math.ceil((max - 1) / 2);
    const tail = Math.floor((max - 1) / 2);
    return `${text.slice(0, head)}…${text.slice(-tail)}`;
  }

  function clampConfig(config = {}) {
    const nMin = clampInt(config.nMin ?? 3, 1, 4096);
    const nMax = clampInt(config.nMax ?? 128, nMin, 4096);
    return {
      nMin,
      nMax,
      baseWindow: clampInt(config.baseWindow ?? 2, 0, 12),
      timeBudgetMs: clampInt(config.timeBudgetMs ?? 1800, 100, 15000),
      mode: ["explore", "counterexample", "verify"].includes(config.mode) ? config.mode : "explore",
      k: BigInt(config.k ?? 1)
    };
  }

  function clampInt(value, min, max) {
    const number = Number(value);
    if (!Number.isFinite(number)) return min;
    return Math.max(min, Math.min(max, Math.floor(number)));
  }

  function baseCandidates(root, windowSize) {
    const values = new Set();
    const center = root < 2n ? 2n : root;
    for (let offset = -windowSize; offset <= windowSize; offset += 1) {
      const next = center + BigInt(offset);
      if (next >= 2n) values.add(next.toString());
    }
    if (root >= 1n) values.add(root.toString());
    return Array.from(values, (text) => BigInt(text)).sort((a, b) => (a < b ? -1 : a > b ? 1 : 0));
  }

  function residueChecks(n, base, target) {
    const checks = [];
    for (const prime of RESIDUE_PRIMES) {
      const expected = target % BigInt(prime);
      const actual = math.evaluateCyclotomicMod(n, base, prime);
      checks.push({
        prime,
        pass: actual === expected,
        actual: actual.toString(),
        expected: expected.toString()
      });
    }
    return checks;
  }

  function closenessScore(diff, target) {
    if (diff === 0n) return 1;
    const targetDigits = Math.max(1, math.bigIntDigitLength(target));
    const diffDigits = math.bigIntDigitLength(diff);
    return Math.max(0, 1 - diffDigits / (targetDigits + 1));
  }

  function classifyCandidate(score, exactMatch, gcdValue, timedOut) {
    if (timedOut) return "Timed out";
    if (gcdValue > 1n) return exactMatch ? "Exact but invalid k" : "Invalid";
    if (exactMatch) return "Exact";
    if (score >= 0.75) return "Strong";
    if (score >= 0.4) return "Weak";
    return "Filtered";
  }

  function scanCandidate(target, n, config) {
    const phiValue = math.phi(n);
    const rootInfo = math.integerNthRoot(target, phiValue);
    const candidates = baseCandidates(rootInfo.root, config.baseWindow);
    const gcdValue = math.gcd(config.k, BigInt(n));
    let best = null;

    for (const base of candidates) {
      const value = math.evaluateCyclotomic(n, base);
      const diff = math.absBigInt(value - target);
      const residues = residueChecks(n, base, target);
      const residueRatio = residues.filter((check) => check.pass).length / residues.length;
      const exactMatch = diff === 0n;
      const score =
        exactMatch
          ? 1
          : Math.max(
              0,
              Math.min(
                0.99,
                0.42 * closenessScore(diff, target) +
                  0.3 * residueRatio +
                  0.13 * (rootInfo.exact ? 1 : 0) +
                  0.1 * (gcdValue === 1n ? 1 : 0) +
                  0.05 * (base >= 2n ? 1 : 0)
              )
            );
      const record = {
        base,
        value,
        diff,
        residues,
        residueRatio,
        exactMatch,
        score
      };
      if (!best || record.score > best.score || (record.exactMatch && !best.exactMatch)) {
        best = record;
      }
    }

    const polynomial = math.cyclotomicCoefficients(n);
    const exactPower = rootInfo.exact;
    const verdict = classifyCandidate(best.score, best.exactMatch, gcdValue, false);
    const square = math.squareShape(best.base);
    const notes = [];
    if (best.exactMatch) notes.push("Exact Φn(b) equality");
    if (!exactPower && best.exactMatch) notes.push("Fails perfect-root shortcut");
    if (gcdValue > 1n) notes.push("gcd(k,n) > 1");
    if (best.residueRatio >= 0.75) notes.push("Residues mostly agree");
    if (best.score < 0.4) notes.push("Low evidence score");

    return {
      n,
      phi: phiValue,
      baseEstimate: rootInfo.root.toString(),
      testedBases: candidates.map((value) => value.toString()),
      bestBase: best.base.toString(),
      exactPower,
      powerRoot: rootInfo.root.toString(),
      cyclotomicMatch: best.exactMatch,
      cyclotomicValue: best.value.toString(),
      difference: best.diff.toString(),
      residueChecks: best.residues,
      residueRatio: best.residueRatio,
      gcdKN: gcdValue.toString(),
      squareShape: {
        b: square.b.toString(),
        y: square.y.toString(),
        exact: square.exact,
        note: square.note
      },
      score: Number(best.score.toFixed(4)),
      verdict,
      polynomialPreview: math.formatPolynomial(polynomial, "x", 8),
      notes
    };
  }

  function scanNumber(rawInput, incomingConfig = {}, options = {}) {
    const startedAt = Date.now();
    const config = clampConfig(incomingConfig);
    const target = typeof rawInput === "bigint" ? rawInput : parseIntegerInput(rawInput);
    const candidates = [];
    let timedOut = false;

    for (let n = config.nMin; n <= config.nMax; n += 1) {
      if (options.cancelled && options.cancelled()) break;
      if (Date.now() - startedAt > config.timeBudgetMs) {
        timedOut = true;
        break;
      }
      try {
        candidates.push(scanCandidate(target, n, config));
      } catch (error) {
        candidates.push({
          n,
          phi: null,
          baseEstimate: "n/a",
          testedBases: [],
          score: 0,
          verdict: "Error",
          notes: [error.message]
        });
      }
    }

    const ranked = candidates
      .slice()
      .sort((a, b) => b.score - a.score || a.n - b.n)
      .slice(0, 48);
    const bestCandidate = ranked[0] || null;
    const counterexamples = buildCounterexamples();
    const exactMatches = ranked.filter((candidate) => candidate.cyclotomicMatch);
    const fragileMatches = exactMatches.filter((candidate) => !candidate.exactPower);

    return {
      input: target.toString(),
      inputDigits: target.toString().length,
      config: {
        nMin: config.nMin,
        nMax: config.nMax,
        baseWindow: config.baseWindow,
        timeBudgetMs: config.timeBudgetMs,
        mode: config.mode,
        k: config.k.toString()
      },
      elapsedMs: Date.now() - startedAt,
      timedOut,
      candidates: ranked,
      bestCandidate,
      exactMatches,
      fragileMatches,
      counterexamples,
      sourceNotes: [SOURCE_NOTE]
    };
  }

  function buildCounterexamples() {
    const seeds = [
      { n: 3, b: 10n, label: "Φ3(10) = 111" },
      { n: 5, b: 2n, label: "Φ5(2) = 31" },
      { n: 8, b: 2n, label: "Φ8(2) = 17" }
    ];
    return seeds.map((seed) => {
      const value = math.evaluateCyclotomic(seed.n, seed.b);
      const phiValue = math.phi(seed.n);
      const root = math.integerNthRoot(value, phiValue);
      return {
        ...seed,
        b: seed.b.toString(),
        value: value.toString(),
        phi: phiValue,
        perfectRoot: root.exact,
        root: root.root.toString(),
        explanation: root.exact
          ? "This one also passes the perfect-root test."
          : "Exact cyclotomic value, but not a perfect φ(n)-th power."
      };
    });
  }

  function sampleValue(key) {
    switch (key) {
      case "phi21":
        return math.evaluateCyclotomic(21, 3n).toString();
      case "phi3":
        return math.evaluateCyclotomic(3, 10n).toString();
      case "phi5":
        return math.evaluateCyclotomic(5, 2n).toString();
      case "carmichael":
        return "561";
      case "primepower":
        return math.powBigInt(3n, 7).toString();
      case "large":
        return "164265132454124777535030081362342972685864000000000000000000000000039";
      default:
        return "111";
    }
  }

  return {
    parseIntegerInput,
    formatBigInt,
    clampConfig,
    scanCandidate,
    scanNumber,
    buildCounterexamples,
    sampleValue
  };
});
