(function attachScanner(root, factory) {
  const api = factory(root.XRayPolynomial || (typeof require !== "undefined" ? require("./polynomial.js") : null));
  if (typeof module !== "undefined" && module.exports) {
    module.exports = api;
  }
  root.XRayScanner = api;
})(typeof globalThis !== "undefined" ? globalThis : this, function createScannerApi(math) {
  if (!math) throw new Error("XRayPolynomial is required before XRayScanner");

  const RESIDUE_PRIMES = [3, 5, 7, 11, 13, 17, 19, 23, 29];
  const SCREEN_PRIMES = [1000003, 1000033, 1000037, 1000039, 1000081];
  const REPORT_LIMIT = 64;
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
    if (value === null || value === undefined) return "n/a";
    const text = BigInt(value).toString();
    if (text.length <= max) return text;
    const head = Math.ceil((max - 1) / 2);
    const tail = Math.floor((max - 1) / 2);
    return `${text.slice(0, head)}…${text.slice(-tail)}`;
  }

  function clampConfig(config = {}) {
    const mode = ["explore", "counterexample", "verify", "deep"].includes(config.mode) ? config.mode : "explore";
    const nCeiling = 8192;
    const defaultNMax = mode === "deep" ? 8192 : 128;
    const nMin = clampInt(config.nMin ?? 3, 1, nCeiling);
    const nMax = clampInt(config.nMax ?? defaultNMax, nMin, nCeiling);
    const defaultBudget = mode === "deep" ? 15000 : 3000;
    const defaultVerifyLimit = mode === "deep" ? 48 : mode === "verify" ? 64 : 24;
    return {
      nMin,
      nMax,
      baseWindow: clampInt(config.baseWindow ?? 2, 0, 12),
      timeBudgetMs: clampInt(config.timeBudgetMs ?? defaultBudget, 100, 15000),
      mode,
      k: BigInt(config.k ?? 1),
      verificationLimit: clampInt(config.verificationLimit ?? defaultVerifyLimit, 1, REPORT_LIMIT),
      reportLimit: REPORT_LIMIT
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

  function residueChecks(n, base, target, primes = RESIDUE_PRIMES, options = {}) {
    const checks = [];
    for (const prime of primes) {
      const expected = target % BigInt(prime);
      const actual =
        options.fastOnly && math.evaluateCyclotomicModProduct
          ? math.evaluateCyclotomicModProduct(n, base, prime)
          : math.evaluateCyclotomicMod(n, base, prime);
      if (actual === null) {
        checks.push({
          prime,
          pass: false,
          skipped: true,
          actual: "indeterminate",
          expected: expected.toString()
        });
        continue;
      }
      checks.push({
        prime,
        pass: actual === expected,
        actual: actual.toString(),
        expected: expected.toString()
      });
    }
    return checks;
  }

  function residueRatio(checks) {
    const known = checks.filter((check) => !check.skipped);
    if (!known.length) return 0;
    return known.filter((check) => check.pass).length / known.length;
  }

  function closenessScore(diff, target) {
    if (diff === 0n) return 1;
    const targetDigits = Math.max(1, math.bigIntDigitLength(target));
    const diffDigits = math.bigIntDigitLength(diff);
    return Math.max(0, 1 - diffDigits / (targetDigits + 1));
  }

  function classifyEvidence(score, exactMatch, exactPower, gcdValue, timedOut) {
    if (timedOut) return "Timed out";
    if (gcdValue > 1n) return exactMatch ? "Exact but invalid k" : "Invalid";
    if (exactMatch) return "Exact";
    if (score >= 0.75) return "Strong evidence";
    if (score >= 0.4) return "Weak evidence";
    return "No match";
  }

  function evidenceLabel(verdict, exactMatch, exactPower) {
    if (exactMatch && !exactPower) return "counterexample";
    if (verdict === "Exact") return "exact";
    if (verdict === "Strong evidence") return "strong evidence";
    if (verdict === "Weak evidence") return "weak evidence";
    return "no match";
  }

  function buildProfile(target, config) {
    return {
      digits: target.toString().length,
      bitLength: math.bitLength(target),
      log10Estimate: Number(math.log10Estimate(target).toFixed(4)),
      targetPreview: formatBigInt(target, 34),
      stageOrder: ["profile", "screen", "hypothesize", "verify"],
      nRange: [config.nMin, config.nMax],
      residuePrimes: SCREEN_PRIMES,
      exactVerificationLimit: config.verificationLimit
    };
  }

  function powerSignal(target, base, phiValue) {
    const power = math.powBigInt(base, phiValue);
    const diff = math.absBigInt(power - target);
    return {
      difference: diff,
      score: closenessScore(diff, target)
    };
  }

  function notesForScreen(best, rootInfo, gcdValue) {
    const notes = ["Screened before exact Φn(b) verification"];
    if (rootInfo.exact) notes.push("Perfect-root shortcut passes");
    if (gcdValue > 1n) notes.push("gcd(k,n) > 1");
    if (best.residueRatio >= 0.75) notes.push("Residues mostly agree");
    if (best.rootProximity >= 0.75) notes.push("Root proximity is high");
    if (best.score < 0.4) notes.push("Low evidence score");
    return notes;
  }

  function scanCandidate(target, n, config, options = {}) {
    const phiValue = math.phi(n);
    const rootInfo = math.integerNthRoot(target, phiValue);
    const candidates = baseCandidates(rootInfo.root, config.baseWindow);
    const gcdValue = math.gcd(config.k, BigInt(n));
    const primes = options.primes || RESIDUE_PRIMES;
    let best = null;

    for (const base of candidates) {
      const residues = residueChecks(n, base, target, primes, { fastOnly: true });
      const residueRatioValue = residueRatio(residues);
      const root = powerSignal(target, base, phiValue);
      const score = Math.max(
        0,
        Math.min(
          0.99,
          0.42 * residueRatioValue +
            0.35 * root.score +
            0.08 * (rootInfo.exact ? 1 : 0) +
            0.08 * (base >= 2n ? 1 : 0) +
            0.07 * (gcdValue === 1n ? 1 : 0)
        )
      );
      const record = {
        base,
        powerDifference: root.difference,
        rootProximity: root.score,
        residues,
        residueRatio: residueRatioValue,
        score
      };
      if (
        !best ||
        record.score > best.score ||
        (record.residueRatio > best.residueRatio && record.rootProximity >= best.rootProximity)
      ) {
        best = record;
      }
    }

    const verdict = classifyEvidence(best.score, false, rootInfo.exact, gcdValue, false);
    const square = math.squareShape(best.base);
    const nextAction =
      best.score >= 0.4
        ? "Verify exact Φn(b) for the strongest tested bases."
        : "Expand n range or change the base window only if this region matters.";

    return {
      n,
      phi: phiValue,
      stage: "screen",
      verificationStatus: "screened",
      baseEstimate: rootInfo.root.toString(),
      testedBases: candidates.map((value) => value.toString()),
      bestBase: best.base.toString(),
      exactPower: rootInfo.exact,
      powerRoot: rootInfo.root.toString(),
      cyclotomicMatch: false,
      cyclotomicValue: null,
      cyclotomicDifference: null,
      difference: best.powerDifference.toString(),
      powerDifference: best.powerDifference.toString(),
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
      evidenceLabel: evidenceLabel(verdict, false, rootInfo.exact),
      polynomialPreview: "Φn(x) exact polynomial deferred until verification",
      discoverySignals: {
        rootProximity: Number(best.rootProximity.toFixed(4)),
        residueConfidence: Number(best.residueRatio.toFixed(4)),
        exactPower: rootInfo.exact,
        gcdOk: gcdValue === 1n,
        exactVerified: false
      },
      nextAction,
      notes: notesForScreen(best, rootInfo, gcdValue)
    };
  }

  function verifyCandidate(target, candidate, config) {
    const gcdValue = BigInt(candidate.gcdKN || "1");
    let best = null;
    for (const baseText of candidate.testedBases || [candidate.bestBase]) {
      const base = BigInt(baseText);
      const value = math.evaluateCyclotomic(candidate.n, base);
      const diff = math.absBigInt(value - target);
      const residues = residueChecks(candidate.n, base, target, RESIDUE_PRIMES);
      const residueRatioValue = residueRatio(residues);
      const exactMatch = diff === 0n;
      const rootScore = closenessScore(diff, target);
      const score =
        exactMatch
          ? 1
          : Math.max(
              0,
              Math.min(
                0.99,
                0.5 * rootScore +
                  0.3 * residueRatioValue +
                  0.08 * (candidate.exactPower ? 1 : 0) +
                  0.07 * (gcdValue === 1n ? 1 : 0) +
                  0.05
              )
            );
      const record = { base, value, diff, residues, residueRatio: residueRatioValue, exactMatch, rootScore, score };
      if (!best || record.exactMatch || record.score > best.score || record.diff < best.diff) {
        best = record;
        if (record.exactMatch) break;
      }
    }

    const verdict = classifyEvidence(best.score, best.exactMatch, candidate.exactPower, gcdValue, false);
    const polynomial = math.cyclotomicCoefficients(candidate.n);
    const square = math.squareShape(best.base);
    const notes = [];
    if (best.exactMatch) notes.push("Exact Φn(b) equality");
    if (best.exactMatch && !candidate.exactPower) notes.push("Fails perfect-root shortcut");
    if (!best.exactMatch) notes.push("Exact Φn(b) verification missed");
    if (gcdValue > 1n) notes.push("gcd(k,n) > 1");
    if (best.residueRatio >= 0.75) notes.push("Residues mostly agree");
    if (best.score < 0.4) notes.push("Low evidence score");

    return {
      ...candidate,
      stage: "verify",
      verificationStatus: best.exactMatch ? "verified-exact" : "verified-miss",
      bestBase: best.base.toString(),
      cyclotomicMatch: best.exactMatch,
      cyclotomicValue: best.value.toString(),
      cyclotomicDifference: best.diff.toString(),
      difference: best.diff.toString(),
      residueChecks: best.residues,
      residueRatio: best.residueRatio,
      squareShape: {
        b: square.b.toString(),
        y: square.y.toString(),
        exact: square.exact,
        note: square.note
      },
      score: Number(best.score.toFixed(4)),
      verdict,
      evidenceLabel: evidenceLabel(verdict, best.exactMatch, candidate.exactPower),
      polynomialPreview: math.formatPolynomial(polynomial, "x", 8),
      discoverySignals: {
        ...candidate.discoverySignals,
        rootProximity: Number(best.rootScore.toFixed(4)),
        residueConfidence: Number(best.residueRatio.toFixed(4)),
        exactVerified: true,
        exactMatch: best.exactMatch
      },
      nextAction: best.exactMatch
        ? "Treat as exact cyclotomic equality; inspect whether the root shortcut was fragile."
        : "Keep as evidence only; exact Φn(b) equality was not found for tested bases.",
      notes
    };
  }

  function sortCandidates(a, b) {
    const exactDelta = Number(Boolean(b.cyclotomicMatch)) - Number(Boolean(a.cyclotomicMatch));
    if (exactDelta) return exactDelta;
    const verifiedDelta =
      Number((b.verificationStatus || "").startsWith("verified")) -
      Number((a.verificationStatus || "").startsWith("verified"));
    if (verifiedDelta) return verifiedDelta;
    return b.score - a.score || a.n - b.n;
  }

  function makeStage(name, status, data = {}) {
    return { name, status, ...data };
  }

  function scanNumber(rawInput, incomingConfig = {}, options = {}) {
    const startedAt = Date.now();
    const config = clampConfig(incomingConfig);
    const target = typeof rawInput === "bigint" ? rawInput : parseIntegerInput(rawInput);
    const profile = buildProfile(target, config);
    const stages = [makeStage("profile", "complete", profile)];
    const candidates = [];
    const verifiedByN = new Map();
    let timedOut = false;
    let cancelled = false;

    const emit = (progress) => {
      if (typeof options.onProgress === "function") {
        options.onProgress({ ...progress, elapsedMs: Date.now() - startedAt });
      }
    };

    emit({ stage: "profile", completed: 1, total: 1, message: "Profiled input magnitude" });

    const total = config.nMax - config.nMin + 1;
    const screenPrimes = SCREEN_PRIMES;
    const screenBudgetMs = Math.max(80, Math.floor(config.timeBudgetMs * (config.mode === "deep" ? 0.72 : 0.58)));
    for (let n = config.nMin; n <= config.nMax; n += 1) {
      if (options.cancelled && options.cancelled()) {
        cancelled = true;
        break;
      }
      if (Date.now() - startedAt > config.timeBudgetMs) {
        timedOut = true;
        break;
      }
      try {
        candidates.push(scanCandidate(target, n, config, { primes: screenPrimes }));
      } catch (error) {
        candidates.push({
          n,
          phi: null,
          stage: "screen",
          verificationStatus: "error",
          baseEstimate: "n/a",
          testedBases: [],
          bestBase: null,
          residueChecks: [],
          residueRatio: 0,
          score: 0,
          verdict: "Error",
          evidenceLabel: "no match",
          notes: [error.message]
        });
      }
      if ((n - config.nMin + 1) % 64 === 0 || n === config.nMax) {
        emit({ stage: "screen", completed: n - config.nMin + 1, total, message: "Screening residues and roots" });
      }
      if (candidates.length >= REPORT_LIMIT && Date.now() - startedAt > screenBudgetMs && n < config.nMax) {
        timedOut = true;
        break;
      }
    }

    const screenedCount = candidates.length;
    stages.push(
      makeStage(timedOut || cancelled ? "screen" : "screen", timedOut ? "partial" : cancelled ? "cancelled" : "complete", {
        scanned: screenedCount,
        total,
        residuePrimes: screenPrimes
      })
    );

    const hypotheses = candidates.slice().sort(sortCandidates).slice(0, Math.max(REPORT_LIMIT, config.verificationLimit));
    stages.push(makeStage("hypothesize", "complete", { retained: hypotheses.length, reportLimit: REPORT_LIMIT }));
    emit({ stage: "hypothesize", completed: hypotheses.length, total: hypotheses.length, message: "Ranking hypotheses" });

    const verifyTotal = Math.min(config.verificationLimit, hypotheses.length);
    for (let index = 0; index < verifyTotal; index += 1) {
      if (options.cancelled && options.cancelled()) {
        cancelled = true;
        break;
      }
      if (Date.now() - startedAt > config.timeBudgetMs) {
        timedOut = true;
        break;
      }
      const candidate = hypotheses[index];
      if (!candidate.bestBase || candidate.verdict === "Error") continue;
      try {
        const verified = verifyCandidate(target, candidate, config);
        verifiedByN.set(verified.n, verified);
      } catch (error) {
        verifiedByN.set(candidate.n, {
          ...candidate,
          verificationStatus: "error",
          verdict: "Error",
          evidenceLabel: "no match",
          notes: [...(candidate.notes || []), error.message]
        });
      }
      emit({ stage: "verify", completed: index + 1, total: verifyTotal, message: "Verifying exact Φn(b)" });
    }

    stages.push(
      makeStage("verify", timedOut ? "partial" : cancelled ? "cancelled" : "complete", {
        verified: verifiedByN.size,
        requested: verifyTotal
      })
    );

    const merged = candidates.map((candidate) => verifiedByN.get(candidate.n) || candidate);
    const ranked = merged.slice().sort(sortCandidates).slice(0, REPORT_LIMIT);
    const bestCandidate = ranked[0] || null;
    const counterexamples = buildCounterexamples();
    const exactMatches = ranked.filter((candidate) => candidate.cyclotomicMatch);
    const fragileMatches = exactMatches.filter((candidate) => !candidate.exactPower);

    return {
      input: target.toString(),
      inputDigits: target.toString().length,
      profile,
      config: {
        nMin: config.nMin,
        nMax: config.nMax,
        baseWindow: config.baseWindow,
        timeBudgetMs: config.timeBudgetMs,
        mode: config.mode,
        k: config.k.toString(),
        verificationLimit: config.verificationLimit
      },
      elapsedMs: Date.now() - startedAt,
      timedOut,
      cancelled,
      discoveryStages: stages,
      candidates: ranked,
      bestCandidate,
      bestVerdict: bestCandidate ? bestCandidate.evidenceLabel : "no match",
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
      case "phi3large":
        return math.evaluateCyclotomic(3, 10n ** 500n).toString();
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
