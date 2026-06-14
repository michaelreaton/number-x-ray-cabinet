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
  const report = scanner.scanNumber(value, { nMin: 3, nMax: 128, baseWindow: 1, timeBudgetMs: 3000 });
  assert.equal(report.inputDigits, 1000);
  assert.equal(report.timedOut, false);
  assert.ok(report.elapsedMs < 3000, `elapsed ${report.elapsedMs}ms`);
  assert.ok(report.candidates.length >= 50);
});

test("deep config reaches the 8192 n ceiling and reports graceful partials", () => {
  const config = scanner.clampConfig({ mode: "deep", nMax: 20000, timeBudgetMs: 15000 });
  assert.equal(config.nMax, 8192);
  assert.equal(config.verificationLimit, 48);

  const report = scanner.scanNumber("111", { mode: "deep", nMin: 3, nMax: 8192, baseWindow: 0, timeBudgetMs: 120 });
  assert.ok(report.elapsedMs < 1000);
  assert.ok(report.discoveryStages.find((stage) => stage.name === "screen").scanned > 0);
  assert.ok(report.timedOut || report.discoveryStages.find((stage) => stage.name === "verify").status !== "complete");
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
