const test = require("node:test");
const assert = require("node:assert/strict");
const math = require("../src/polynomial.js");

test("gcd handles BigInt inputs", () => {
  assert.equal(math.gcd(84n, 30n), 6n);
  assert.equal(math.gcd(-21n, 14n), 7n);
});

test("phi returns Euler totient values", () => {
  assert.equal(math.phi(1), 1);
  assert.equal(math.phi(3), 2);
  assert.equal(math.phi(8), 4);
  assert.equal(math.phi(21), 12);
});

test("integer nth root reports exactness", () => {
  assert.deepEqual(math.integerNthRoot(81n, 4), { root: 3n, exact: true });
  assert.deepEqual(math.integerNthRoot(80n, 4), { root: 2n, exact: false });
});

test("cyclotomic coefficients are exact for small known cases", () => {
  assert.deepEqual(math.cyclotomicCoefficients(3), [1n, 1n, 1n]);
  assert.deepEqual(math.cyclotomicCoefficients(5), [1n, 1n, 1n, 1n, 1n]);
  assert.deepEqual(math.cyclotomicCoefficients(8), [1n, 0n, 0n, 0n, 1n]);
});

test("known cyclotomic evaluations match the plan examples", () => {
  assert.equal(math.evaluateCyclotomic(3, 10n), 111n);
  assert.equal(math.evaluateCyclotomic(5, 2n), 31n);
  assert.equal(math.evaluateCyclotomic(8, 2n), 17n);
});
