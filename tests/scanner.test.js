const test = require("node:test");
const assert = require("node:assert/strict");
const math = require("../src/polynomial.js");
const scanner = require("../src/scanner.js");

test("scanner finds an exact cyclotomic hit", () => {
  const value = math.evaluateCyclotomic(5, 2n).toString();
  const report = scanner.scanNumber(value, { nMin: 3, nMax: 8, baseWindow: 1, timeBudgetMs: 1000 });
  const hit = report.candidates.find((candidate) => candidate.n === 5);
  assert.equal(hit.cyclotomicMatch, true);
  assert.equal(hit.verdict, "Exact");
});

test("scanner records perfect-root counterexample fragility", () => {
  const report = scanner.scanNumber("111", { nMin: 3, nMax: 3, baseWindow: 1, timeBudgetMs: 1000 });
  assert.equal(report.bestCandidate.n, 3);
  assert.equal(report.bestCandidate.cyclotomicMatch, true);
  assert.equal(report.bestCandidate.exactPower, false);
  assert.equal(report.fragileMatches.length, 1);
});

test("invalid input is rejected", () => {
  assert.throws(() => scanner.scanNumber("not-a-number"), /integer/);
  assert.throws(() => scanner.scanNumber("-17"), /positive/);
});

test("poorly formatted integer input is normalized when the target is clear", () => {
  assert.equal(scanner.parseIntegerInput("N = 1,234_567"), 1234567n);
  assert.equal(scanner.parseIntegerInput("wrapped:\n  1 234\n  567"), 1234567n);
  assert.equal(scanner.parseIntegerInput("Φ3(10) = 111"), 111n);
  assert.equal(scanner.parseIntegerInput("عدد = ۱۲۳٬۴۵۶"), 123456n);
  assert.equal(scanner.parseIntegerInput("１２３，４５６"), 123456n);
});

test("ambiguous or non-exact numeric input is rejected", () => {
  assert.throws(() => scanner.parseIntegerInput("111 and 222"), /multiple possible integers/);
  assert.throws(() => scanner.parseIntegerInput("1e6"), /Scientific notation/);
  assert.throws(() => scanner.parseIntegerInput("3.14"), /Decimals/);
});

test("input preview reports parsed digits or ambiguous digit counts", () => {
  assert.deepEqual(scanner.previewIntegerInput("N = 000,111"), {
    parseable: true,
    digits: 3,
    normalized: "000111"
  });
  const ambiguous = scanner.previewIntegerInput("111 and 222");
  assert.equal(ambiguous.parseable, false);
  assert.equal(ambiguous.digits, 6);
});

test("huge input produces a bounded report", () => {
  const report = scanner.scanNumber(
    "164265132454124777535030081362342972685864000000000000000000000000039",
    { nMin: 3, nMax: 42, baseWindow: 1, timeBudgetMs: 1200 }
  );
  assert.ok(report.inputDigits > 60);
  assert.ok(report.candidates.length > 0);
  assert.ok(report.bestCandidate);
});

test("timeout returns partial results instead of throwing", () => {
  const report = scanner.scanNumber("111", { nMin: 3, nMax: 4096, baseWindow: 0, timeBudgetMs: 100 });
  assert.ok(Array.isArray(report.candidates));
});

test("staged reports retain at least fifty ranked candidates when available", () => {
  const report = scanner.scanNumber("561", { nMin: 3, nMax: 90, baseWindow: 0, timeBudgetMs: 3000 });
  assert.ok(report.candidates.length >= 50);
  assert.deepEqual(report.discoveryStages.map((stage) => stage.name), ["profile", "screen", "hypothesize", "verify"]);
  assert.ok(report.candidates.every((candidate) => candidate.evidenceLabel));
});

test("default scan handles one-thousand digit input under the target budget", () => {
  const value = (10n ** 999n + 123456789n).toString();
  const report = scanner.scanNumber(value, { nMin: 3, nMax: 128, baseWindow: 1, timeBudgetMs: 5000 });
  assert.equal(report.inputDigits, 1000);
  assert.equal(report.timedOut, false);
  assert.ok(report.elapsedMs < 5000, `elapsed ${report.elapsedMs}ms`);
  assert.ok(report.candidates.length >= 50);
});

test("deep config reaches the expanded n ceiling and reports graceful partials", () => {
  const config = scanner.clampConfig({ mode: "deep", nMax: 50000, timeBudgetMs: 60000, verificationLimit: 200 });
  assert.equal(config.nMax, 32768);
  assert.equal(config.timeBudgetMs, 60000);
  assert.equal(config.verificationLimit, 128);

  const report = scanner.scanNumber("111", { mode: "deep", nMin: 3, nMax: 8192, baseWindow: 0, timeBudgetMs: 120 });
  assert.ok(report.elapsedMs < 1000);
  assert.ok(report.discoveryStages.find((stage) => stage.name === "screen").scanned > 0);
  assert.ok(report.timedOut || report.discoveryStages.find((stage) => stage.name === "verify").status !== "complete");
});

test("RSA-260 sample is recognized and receives a bounded recon dossier", () => {
  const value = scanner.sampleValue("rsa260");
  const report = scanner.scanNumber(value, {
    mode: "rsa",
    nMin: 3,
    nMax: 32,
    baseWindow: 0,
    timeBudgetMs: 2000,
    verificationLimit: 4,
    rsaSmallPrimeLimit: 5000,
    rsaFermatIterations: 50,
    rsaRhoIterations: 50
  });

  assert.equal(value.length, 260);
  assert.equal(report.rsaRecon.targetLabel, "RSA-260");
  assert.equal(report.rsaRecon.checksum.pass, true);
  assert.equal(report.rsaRecon.checksum.residue, 327430);
  assert.equal(report.rsaRecon.primality.probablyPrime, false);
  assert.equal(report.rsaRecon.factorPair, null);
  assert.equal(report.rsaRecon.solver.status, "unsolved");
  assert.equal(report.rsaRecon.solver.productVerified, false);
  assert.equal(report.rsaRecon.solver.escalation.required, true);
  assert.equal(report.rsaRecon.solver.escalation.recommendedTool, "CADO-NFS");
  assert.ok(report.discoveryStages.some((stage) => stage.name === "rsa" && stage.status === "complete"));
  assert.ok(report.discoveryStages.some((stage) => stage.name === "solve" && stage.status === "partial"));
});

test("known RSA-260 input triggers recon even outside RSA mode", () => {
  const report = scanner.scanNumber(scanner.sampleValue("rsa260"), {
    nMin: 3,
    nMax: 8,
    baseWindow: 0,
    timeBudgetMs: 1200,
    verificationLimit: 2,
    rsaSmallPrimeLimit: 1000,
    rsaFermatIterations: 5,
    rsaRhoIterations: 0
  });
  assert.equal(report.config.mode, "explore");
  assert.equal(report.rsaRecon.recognized, true);
});

test("RSA solver mode verifies solvable semiprime factors", () => {
  const report = scanner.scanNumber(scanner.sampleValue("semiprime"), {
    mode: "rsa",
    nMin: 3,
    nMax: 16,
    baseWindow: 0,
    timeBudgetMs: 2000,
    verificationLimit: 2,
    rsaSmallPrimeLimit: 50,
    rsaFermatIterations: 20,
    rsaRhoIterations: 200,
    rsaSolverTimeBudgetMs: 1000
  });
  assert.equal(report.rsaRecon.solver.status, "solved");
  assert.equal(report.rsaRecon.solver.productVerified, true);
  assert.deepEqual(report.rsaRecon.solver.factors.map((factor) => factor.value), ["101", "103"]);
  assert.equal(report.rsaRecon.solver.escalation.required, false);
  assert.ok(report.discoveryStages.some((stage) => stage.name === "solve" && stage.status === "complete"));
});

test("planted 1k digit cyclotomic fixture is exactly recovered", () => {
  const value = math.evaluateCyclotomic(3, 10n ** 500n).toString();
  const report = scanner.scanNumber(value, { mode: "deep", nMin: 3, nMax: 256, baseWindow: 1, timeBudgetMs: 5000 });
  const hit = report.candidates.find((candidate) => candidate.n === 3);
  assert.equal(value.length, 1001);
  assert.ok(hit);
  assert.equal(hit.cyclotomicMatch, true);
  assert.equal(hit.verificationStatus, "verified-exact");
  assert.equal(hit.evidenceLabel, "counterexample");
  assert.equal(report.bestCandidate.n, 3);
});

test("Fermat F12 sample is exposed as Phi_8192(2)", () => {
  const value = scanner.sampleValue("fermat12");
  const report = scanner.scanNumber(value, {
    mode: "deep",
    nMin: 8192,
    nMax: 8192,
    baseWindow: 0,
    timeBudgetMs: 15000,
    verificationLimit: 1
  });

  assert.equal(value.length, 1234);
  assert.equal(report.bestCandidate.n, 8192);
  assert.equal(report.bestCandidate.bestBase, "2");
  assert.equal(report.bestCandidate.cyclotomicMatch, true);
  assert.equal(report.bestCandidate.verdict, "Exact");
});

test("progress and cancellation callbacks are honored", () => {
  const progress = [];
  const report = scanner.scanNumber("111", { nMin: 3, nMax: 48, baseWindow: 0, timeBudgetMs: 1000 }, {
    onProgress(update) {
      progress.push(update.stage);
    }
  });
  assert.equal(report.cancelled, false);
  assert.ok(progress.includes("profile"));
  assert.ok(progress.includes("screen"));
  assert.ok(progress.includes("verify"));

  let calls = 0;
  const cancelled = scanner.scanNumber("111", { nMin: 3, nMax: 128, baseWindow: 0, timeBudgetMs: 1000 }, {
    cancelled() {
      calls += 1;
      return calls > 1;
    }
  });
  assert.equal(cancelled.cancelled, true);
});
