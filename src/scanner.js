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
  const REPORT_LIMIT = 128;
  const SOURCE_NOTE =
    "Credit: Payam. MY GFN2 (https://amathz.com/my_gfn.html) is the live source page for this app. Payam frames the target family as Phi(n)(2^p^m), with n squarefree and p a prime divisor of n; exact equality is reported separately from evidence and counterexamples.";
  const RSA_CHECKSUM_MODULUS = 991889n;
  const RSA_CHALLENGE_SOURCE = "https://www.ontko.com/pub/rayo/primes/rsa_fact.html";
  const RSA260_VALUE =
    "22112825529529666435281085255026230927612089502470015394413748319128822941402001986512729726569746599085900330031400051170742204560859276357953757185954298838958709229238491006703034124620545784566413664540684214361293017694020846391065875914794251435144458199";
  const RSA_CHALLENGES = [
    {
      key: "rsa260",
      label: "RSA-260",
      value: RSA260_VALUE,
      digits: 260,
      checksum: 327430,
      bits: 862,
      sourceUrl: RSA_CHALLENGE_SOURCE,
      sourceNote:
        "The RSA challenge list defines this as a product of two discarded, similarly sized random primes congruent to 2 modulo 3, suitable for public exponent 3."
    }
  ];
  const CADO_NFS_SOURCE = "https://cado-nfs.gitlabpages.inria.fr/";

  function normalizeInputText(raw) {
    return String(raw ?? "")
      .normalize("NFKC")
      .replace(/[\u200e\u200f\u202a-\u202e\u2066-\u2069]/g, "")
      .replace(/[\u2212\u2012\u2013\u2014\uFE63\uFF0D]/g, "-")
      .replace(/[\uFF0B]/g, "+")
      .replace(/[۰-۹]/g, (digit) => String(digit.charCodeAt(0) - 0x06f0))
      .replace(/[٠-٩]/g, (digit) => String(digit.charCodeAt(0) - 0x0660))
      .replace(/[０-９]/g, (digit) => String(digit.charCodeAt(0) - 0xff10));
  }

  function cleanIntegerCandidate(rawCandidate) {
    let candidate = String(rawCandidate || "").trim().replace(/^[`"'“”]+|[`"'“”]+$/g, "");
    if (!candidate) return null;
    if ((candidate.match(/[+-]/g) || []).length > 1 || /[+-].+\d.*[+-]/.test(candidate)) return null;

    let sign = "";
    if (/^[+-]/.test(candidate)) {
      sign = candidate[0] === "-" ? "-" : "";
      candidate = candidate.slice(1).trim();
    }

    if (/[.٫]/.test(candidate)) {
      const groups = candidate.split(/[.٫]/);
      const groupedThousands =
        groups.length > 1 &&
        /^[0-9]{1,3}$/.test(groups[0].replace(/[\s,_'’٬،]/g, "")) &&
        groups.slice(1).every((group) => /^[0-9]{3}$/.test(group.replace(/[\s,_'’٬،]/g, "")));
      if (!groupedThousands) {
        throw new Error("Decimals and scientific notation are not exact integer input. Paste the full decimal digits.");
      }
    }

    const digits = candidate.replace(/[.٫\s,_'’٬،]/g, "");
    if (!/^[0-9]+$/.test(digits)) return null;
    return `${sign}${digits}`;
  }

  function candidateRecords(text) {
    const records = [];
    const errors = [];
    const pattern = /[+-]?[0-9][0-9\s,_'’.,٬،٫]*/g;
    let match;
    while ((match = pattern.exec(text)) !== null) {
      try {
        const cleaned = cleanIntegerCandidate(match[0]);
        if (cleaned) {
          records.push({
            value: cleaned,
            digits: cleaned.replace(/^[+-]/, "").replace(/^0+(?=[0-9])/, "").length
          });
        }
      } catch (error) {
        errors.push(error.message);
      }
    }
    return { records, errors };
  }

  function chooseIntegerCandidate(records, errors = []) {
    const unique = [];
    const seen = new Set();
    for (const record of records) {
      if (!seen.has(record.value)) {
        unique.push(record);
        seen.add(record.value);
      }
    }
    if (!unique.length) {
      throw new Error(errors[0] || "Input must contain a positive integer.");
    }
    if (unique.length === 1) return unique[0].value;

    const ranked = unique.slice().sort((a, b) => b.digits - a.digits);
    if (ranked[0].digits > ranked[1].digits) return ranked[0].value;
    throw new Error("Input contains multiple possible integers. Use `N = ...` or paste only the target integer.");
  }

  function normalizeIntegerInput(raw) {
    const text = normalizeInputText(raw);
    if (/[0-9]\s*[eE]\s*[+-]?\s*[0-9]/.test(text)) {
      throw new Error("Scientific notation is not exact integer input. Paste the full decimal digits.");
    }

    const labeledPattern =
      /(?:^|[^A-Za-z0-9])(?:N|target|Target|input|Input|integer|Integer|value|Value|number|Number)\s*[:=]\s*([+-]?[0-9][0-9\s,_'’.,٬،٫]*)/g;
    let match;
    const labeled = [];
    const labeledErrors = [];
    while ((match = labeledPattern.exec(text)) !== null) {
      try {
        const cleaned = cleanIntegerCandidate(match[1]);
        if (cleaned) {
          labeled.push({
            value: cleaned,
            digits: cleaned.replace(/^[+-]/, "").replace(/^0+(?=[0-9])/, "").length
          });
        }
      } catch (error) {
        labeledErrors.push(error.message);
      }
    }
    if (labeled.length) return labeled[labeled.length - 1].value;

    if (text.includes("=")) {
      const rhs = text.slice(text.lastIndexOf("=") + 1);
      const rhsCandidates = candidateRecords(rhs);
      if (rhsCandidates.records.length) return chooseIntegerCandidate(rhsCandidates.records, rhsCandidates.errors);
      if (rhsCandidates.errors.length) throw new Error(rhsCandidates.errors[0]);
    }

    const all = candidateRecords(text);
    return chooseIntegerCandidate(all.records, all.errors);
  }

  function previewIntegerInput(raw) {
    const text = normalizeInputText(raw);
    try {
      const normalized = normalizeIntegerInput(text);
      return {
        parseable: true,
        digits: normalized.replace(/^[+-]/, "").replace(/^0+(?=[0-9])/, "").length,
        normalized
      };
    } catch (error) {
      return {
        parseable: false,
        digits: (text.match(/[0-9]/g) || []).length,
        error: error.message
      };
    }
  }

  function parseIntegerInput(raw) {
    const compact = normalizeIntegerInput(raw);
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
    const mode = ["explore", "counterexample", "verify", "deep", "rsa"].includes(config.mode) ? config.mode : "explore";
    const nCeiling = 32768;
    const defaultNMax = mode === "deep" || mode === "rsa" ? 8192 : 128;
    const nMin = clampInt(config.nMin ?? 3, 1, nCeiling);
    const nMax = clampInt(config.nMax ?? defaultNMax, nMin, nCeiling);
    const defaultBudget = mode === "deep" || mode === "rsa" ? 15000 : 3000;
    const defaultVerifyLimit = mode === "deep" ? 48 : mode === "rsa" ? 40 : mode === "verify" ? 64 : 24;
    return {
      nMin,
      nMax,
      baseWindow: clampInt(config.baseWindow ?? 2, 0, 12),
      timeBudgetMs: clampInt(config.timeBudgetMs ?? defaultBudget, 100, 60000),
      mode,
      k: BigInt(config.k ?? 1),
      verificationLimit: clampInt(config.verificationLimit ?? defaultVerifyLimit, 1, REPORT_LIMIT),
      rsaSmallPrimeLimit: clampInt(config.rsaSmallPrimeLimit ?? (mode === "rsa" ? 50000 : 10000), 100, 200000),
      rsaFermatIterations: clampInt(config.rsaFermatIterations ?? (mode === "rsa" ? 2000 : 250), 0, 25000),
      rsaRhoIterations: clampInt(config.rsaRhoIterations ?? (mode === "rsa" ? 700 : 0), 0, 25000),
      rsaSolverTimeBudgetMs: clampInt(config.rsaSolverTimeBudgetMs ?? (mode === "rsa" ? 3500 : 1200), 100, 8000),
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

  function knownChallengeFor(target) {
    const text = target.toString();
    return RSA_CHALLENGES.find((challenge) => challenge.value === text) || null;
  }

  function shouldRunRsaRecon(target, config) {
    return config.mode === "rsa" || Boolean(knownChallengeFor(target));
  }

  function checksumResidue(target) {
    return Number(target % RSA_CHECKSUM_MODULUS);
  }

  function formatFactorPair(factor, cofactor) {
    if (!factor || !cofactor) return null;
    return {
      factor: factor.toString(),
      cofactor: cofactor.toString(),
      factorDigits: factor.toString().length,
      cofactorDigits: cofactor.toString().length
    };
  }

  function factorPairFromSolver(target, solver) {
    if (!solver || !solver.factors?.length) return null;
    const expanded = [];
    for (const factor of solver.factors) {
      for (let index = 0; index < (factor.exponent || 1); index += 1) expanded.push(BigInt(factor.value));
    }
    if (expanded.length < 2) return null;
    const factor = expanded[0];
    const cofactor = target / factor;
    if (factor > 1n && cofactor > 1n && factor * cofactor === target) return formatFactorPair(factor, cofactor);
    return null;
  }

  function buildEscalationPlan(target, challenge, solver) {
    const label = challenge?.label || "input";
    return {
      required: solver.status !== "solved",
      reason:
        solver.status === "solved"
          ? "Browser-local factorization completed."
          : `${label} was not factored inside the local browser budget.`,
      recommendedTool: "CADO-NFS",
      sourceUrl: CADO_NFS_SOURCE,
      why: "CADO-NFS is a Number Field Sieve implementation that can run factoring phases in parallel over multiple computers.",
      commandPack: [
        "# Operator template for a GNFS-class run outside the browser",
        "git clone https://gitlab.inria.fr/cado-nfs/cado-nfs.git",
        "cd cado-nfs && make -j$(nproc)",
        `./cado-nfs.py -t $(nproc) ${target.toString()}`
      ],
      gcpNotes: [
        "Use preemptible/spot workers only if checkpointing is configured.",
        "Persist the working directory to durable storage before stopping instances.",
        "Track relation collection, matrix, square root, and factor verification as separate milestones."
      ]
    };
  }

  function serializeSmallSieve(scan) {
    return {
      limit: scan.limit,
      tested: scan.tested,
      factor: scan.factor ? scan.factor.toString() : null,
      status: scan.factor ? "factor-found" : "no-factor"
    };
  }

  function serializeFermat(scan) {
    return {
      found: scan.found,
      iterations: scan.iterations,
      offset: scan.offset ?? null,
      factor: scan.factor ? scan.factor.toString() : null,
      cofactor: scan.cofactor ? scan.cofactor.toString() : null,
      status: scan.found ? "factor-found" : "no-square-offset"
    };
  }

  function serializeRho(scan) {
    return {
      found: scan.found,
      attempts: scan.attempts || [],
      factor: scan.factor ? scan.factor.toString() : null,
      cofactor: scan.cofactor ? scan.cofactor.toString() : null,
      status: scan.found ? "factor-found" : "no-factor"
    };
  }

  function buildRsaRecon(target, config) {
    const startedAt = Date.now();
    const challenge = knownChallengeFor(target);
    const residue = checksumResidue(target);
    const small = math.smallFactorScan(target, config.rsaSmallPrimeLimit);
    const primality = math.isProbablePrime(target);
    const hasSmallFactor = Boolean(small.factor);
    const fermat = hasSmallFactor
      ? { found: false, iterations: 0, offset: null, status: "skipped" }
      : math.fermatFactorScout(target, config.rsaFermatIterations);
    const hasFermatFactor = Boolean(fermat.factor);
    const rho = hasSmallFactor || hasFermatFactor || config.rsaRhoIterations === 0
      ? { found: false, attempts: [], status: hasSmallFactor || hasFermatFactor ? "skipped" : "disabled" }
      : math.pollardRhoScout(target, { iterations: config.rsaRhoIterations });
    const solver = math.solveFactorization(target, {
      smallPrimeLimit: config.rsaSmallPrimeLimit,
      fermatIterations: config.rsaFermatIterations,
      rhoIterations: config.rsaRhoIterations,
      timeBudgetMs: config.rsaSolverTimeBudgetMs,
      maxPasses: 80
    });
    const factorPair =
      factorPairFromSolver(target, solver) ||
      formatFactorPair(small.factor, small.factor ? target / small.factor : null) ||
      formatFactorPair(fermat.factor, fermat.cofactor) ||
      formatFactorPair(rho.factor, rho.cofactor);
    const notes = [];

    if (challenge) notes.push(`${challenge.label} recognized from the RSA challenge list`);
    if (challenge && residue === challenge.checksum) notes.push("RSA checksum matches the published residue");
    if (challenge && target % 3n === 1n) notes.push("Consistent with p ≡ q ≡ 2 (mod 3), so N ≡ 1 (mod 3)");
    if (!small.factor) notes.push(`No divisor found among primes ≤ ${config.rsaSmallPrimeLimit}`);
    if (!factorPair && !fermat.found) notes.push(`No Fermat square found in ${config.rsaFermatIterations} offsets`);
    if (!factorPair && config.rsaRhoIterations > 0) notes.push(`Pollard Rho made bounded attempts without a factor`);
    if (!primality.probablyPrime) notes.push(`Miller-Rabin composite witness: ${primality.witness}`);
    if (solver.status === "solved") notes.push("All factors were found and product verification passed");
    else if (factorPair) notes.push("A nontrivial factor was found inside the browser budget");
    else notes.push("Escalate to GNFS-class tooling for a real solve attempt");

    const verdict =
      solver.status === "solved"
        ? "Solved"
        : factorPair
          ? "Partial factor found"
          : challenge
            ? `${challenge.label} unsolved locally; GNFS escalation required`
            : primality.probablyPrime
              ? "Probable prime under tested bases"
              : "Composite witness found; no browser-budget factor found";

    return {
      enabled: true,
      targetLabel: challenge?.label || "Uncatalogued RSA-style target",
      recognized: Boolean(challenge),
      sourceUrl: challenge?.sourceUrl || null,
      sourceNote: challenge?.sourceNote || null,
      digits: target.toString().length,
      bitLength: math.bitLength(target),
      checksum: {
        modulus: RSA_CHECKSUM_MODULUS.toString(),
        residue,
        expected: challenge?.checksum ?? null,
        pass: challenge ? residue === challenge.checksum : null
      },
      rsaShape: {
        odd: (target & 1n) === 1n,
        mod3: (target % 3n).toString(),
        exponent3Compatible: target % 3n === 1n,
        expectedPrimeDigits: challenge ? "about 130 digits each" : "unknown"
      },
      primality,
      smallPrimeSieve: serializeSmallSieve(small),
      fermat: serializeFermat(fermat),
      pollardRho: serializeRho(rho),
      factorPair,
      solver: {
        status: solver.status,
        factors: solver.factors,
        unresolved: solver.unresolved,
        productVerified: solver.productVerified,
        accountingVerified: solver.accountingVerified,
        fullyFactored: solver.fullyFactored,
        timedOut: solver.timedOut,
        passes: solver.passes,
        elapsedMs: solver.elapsedMs,
        steps: solver.steps.slice(0, 18),
        stepCount: solver.steps.length,
        escalation: buildEscalationPlan(target, challenge, solver)
      },
      verdict,
      notes,
      nextActions: solver.status === "solved"
        ? ["Use the verified factor list; product verification passed."]
        : factorPair
          ? ["Use the partial factor, then continue factoring the cofactor."]
        : [
            "Treat this as an unsolved local attempt.",
            "Export JSON and use GNFS-class tools for a real factorization attempt.",
            "Compare the cyclotomic matrix for nonrandom algebraic fingerprints."
          ],
      elapsedMs: Date.now() - startedAt
    };
  }

  function buildProfile(target, config) {
    return {
      digits: target.toString().length,
      bitLength: math.bitLength(target),
      log10Estimate: Number(math.log10Estimate(target).toFixed(4)),
      targetPreview: formatBigInt(target, 34),
      stageOrder: shouldRunRsaRecon(target, config) ? ["profile", "rsa", "solve", "screen", "hypothesize", "verify"] : ["profile", "screen", "hypothesize", "verify"],
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
    let rsaRecon = null;
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

    if (shouldRunRsaRecon(target, config)) {
      emit({ stage: "rsa", completed: 0, total: 1, message: "Running RSA challenge reconnaissance" });
      rsaRecon = buildRsaRecon(target, config);
      stages.push(
        makeStage("rsa", "complete", {
          recognized: rsaRecon.recognized,
          targetLabel: rsaRecon.targetLabel,
          checksumPass: rsaRecon.checksum.pass,
          factorFound: Boolean(rsaRecon.factorPair),
          elapsedMs: rsaRecon.elapsedMs
        })
      );
      stages.push(
        makeStage("solve", rsaRecon.solver.status === "solved" ? "complete" : "partial", {
          solverStatus: rsaRecon.solver.status,
          productVerified: rsaRecon.solver.productVerified,
          factors: rsaRecon.solver.factors.length,
          unresolved: rsaRecon.solver.unresolved.length,
          elapsedMs: rsaRecon.solver.elapsedMs
        })
      );
      emit({ stage: "rsa", completed: 1, total: 1, message: "RSA reconnaissance complete" });
    }

    const total = config.nMax - config.nMin + 1;
    const screenPrimes = SCREEN_PRIMES;
    const longRangeMode = config.mode === "deep" || config.mode === "rsa";
    const screenBudgetMs = Math.max(80, Math.floor(config.timeBudgetMs * (longRangeMode ? 0.72 : 0.58)));
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
      if (config.mode === "explore" && candidates.length >= REPORT_LIMIT && Date.now() - startedAt > screenBudgetMs && n < config.nMax) {
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
        verificationLimit: config.verificationLimit,
        rsaSmallPrimeLimit: config.rsaSmallPrimeLimit,
        rsaFermatIterations: config.rsaFermatIterations,
        rsaRhoIterations: config.rsaRhoIterations,
        rsaSolverTimeBudgetMs: config.rsaSolverTimeBudgetMs
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
      rsaRecon,
      sourceNotes: rsaRecon?.recognized
        ? [SOURCE_NOTE, `RSA source: ${rsaRecon.targetLabel} from ${rsaRecon.sourceUrl}; checksum ${rsaRecon.checksum.residue}.`]
        : [SOURCE_NOTE]
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
      case "fermat12":
        return (math.powBigInt(2n, 4096) + 1n).toString();
      case "carmichael":
        return "561";
      case "primepower":
        return math.powBigInt(3n, 7).toString();
      case "semiprime":
        return "10403";
      case "large":
        return "164265132454124777535030081362342972685864000000000000000000000000039";
      case "rsa260":
        return RSA260_VALUE;
      default:
        return "111";
    }
  }

  return {
    parseIntegerInput,
    normalizeIntegerInput,
    previewIntegerInput,
    formatBigInt,
    clampConfig,
    scanCandidate,
    scanNumber,
    buildCounterexamples,
    sampleValue,
    buildRsaRecon
  };
});
