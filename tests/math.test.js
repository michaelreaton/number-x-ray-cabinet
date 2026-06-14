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

test("modular cyclotomic evaluation matches exact evaluation on small cases", () => {
  for (const n of [1, 2, 3, 4, 5, 6, 8, 9, 10, 12, 15, 21, 30]) {
    for (const base of [2n, 3n, 5n, 10n, 37n]) {
      const exact = math.evaluateCyclotomic(n, base);
      for (const prime of [3, 5, 7, 11, 13, 1000003]) {
        const expected = ((exact % BigInt(prime)) + BigInt(prime)) % BigInt(prime);
        assert.equal(math.evaluateCyclotomicMod(n, base, prime), expected, `n=${n} base=${base} mod=${prime}`);
      }
    }
  }
});

test("product-form modular screen agrees when the division path is invertible", () => {
  for (const n of [3, 5, 7, 8, 11, 16, 21, 32, 45]) {
    for (const base of [2n, 10n, 123456789n]) {
      for (const prime of [1000003, 1000033, 1000037]) {
        const product = math.evaluateCyclotomicModProduct(n, base, prime);
        if (product === null) continue;
        const exact = math.evaluateCyclotomic(n, base);
        const expected = ((exact % BigInt(prime)) + BigInt(prime)) % BigInt(prime);
        assert.equal(product, expected, `n=${n} base=${base} mod=${prime}`);
      }
    }
  }
});

test("small factor sieve and Miller-Rabin expose composite inputs", () => {
  assert.deepEqual(math.primeSieve(12), [2, 3, 5, 7, 11]);
  const small = math.smallFactorScan(10403n, 200);
  assert.equal(small.factor, 101n);
  assert.equal(math.isProbablePrime(10403n).probablyPrime, false);
  assert.equal(math.isProbablePrime(6700417n).probablyPrime, true);
});

test("bounded Fermat and Pollard Rho scouts can recover toy semiprime factors", () => {
  const fermat = math.fermatFactorScout(10403n, 20);
  assert.equal(fermat.found, true);
  assert.equal(fermat.factor * fermat.cofactor, 10403n);

  const rho = math.pollardRhoScout(8051n, { iterations: 200, seeds: [2n, 3n], constants: [1n, 3n] });
  assert.equal(rho.found, true);
  assert.equal(rho.factor * rho.cofactor, 8051n);
});
