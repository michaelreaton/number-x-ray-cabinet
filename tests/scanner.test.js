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
