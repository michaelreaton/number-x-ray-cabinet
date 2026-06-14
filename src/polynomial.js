(function attachPolynomial(root, factory) {
  const api = factory();
  if (typeof module !== "undefined" && module.exports) {
    module.exports = api;
  }
  root.XRayPolynomial = api;
})(typeof globalThis !== "undefined" ? globalThis : this, function createPolynomialApi() {
  const cyclotomicCache = new Map();
  const cyclotomicModCache = new Map();
  const factorCache = new Map();
  const phiCache = new Map();
  const divisorCache = new Map();
  const mobiusCache = new Map();

  function absBigInt(value) {
    return value < 0n ? -value : value;
  }

  function gcd(a, b) {
    a = absBigInt(BigInt(a));
    b = absBigInt(BigInt(b));
    while (b !== 0n) {
      const next = a % b;
      a = b;
      b = next;
    }
    return a;
  }

  function powBigInt(base, exponent) {
    base = BigInt(base);
    let exp = BigInt(exponent);
    let result = 1n;
    while (exp > 0n) {
      if (exp & 1n) result *= base;
      exp >>= 1n;
      if (exp > 0n) base *= base;
    }
    return result;
  }

  function powCompare(base, exponent, limit) {
    base = BigInt(base);
    limit = BigInt(limit);
    let result = 1n;
    for (let i = 0; i < exponent; i += 1) {
      result *= base;
      if (result > limit) return 1;
    }
    if (result === limit) return 0;
    return -1;
  }

  function modNormalize(value, modulus) {
    const mod = BigInt(modulus);
    let next = BigInt(value) % mod;
    if (next < 0n) next += mod;
    return next;
  }

  function modPow(base, exponent, modulus) {
    const mod = BigInt(modulus);
    let nextBase = modNormalize(base, mod);
    let exp = BigInt(exponent);
    let result = 1n;
    while (exp > 0n) {
      if (exp & 1n) result = (result * nextBase) % mod;
      exp >>= 1n;
      if (exp > 0n) nextBase = (nextBase * nextBase) % mod;
    }
    return result;
  }

  function primeSieve(limit) {
    limit = Number(limit);
    if (!Number.isInteger(limit) || limit < 2) return [];
    const flags = new Uint8Array(limit + 1);
    const primes = [];
    for (let value = 2; value <= limit; value += 1) {
      if (flags[value]) continue;
      primes.push(value);
      if (value * value <= limit) {
        for (let multiple = value * value; multiple <= limit; multiple += value) flags[multiple] = 1;
      }
    }
    return primes;
  }

  function smallFactorScan(value, limit = 10000) {
    const n = absBigInt(BigInt(value));
    const primes = primeSieve(limit);
    for (let index = 0; index < primes.length; index += 1) {
      const prime = primes[index];
      const factor = BigInt(prime);
      if (n === factor) {
        return { limit, tested: primes.length, factor: null, factorText: null, exactSelf: true };
      }
      if (n % factor === 0n) {
        return { limit, tested: index + 1, factor, factorText: factor.toString(), exactSelf: false };
      }
    }
    return { limit, tested: primes.length, factor: null, factorText: null, exactSelf: false };
  }

  function isProbablePrime(value, bases = [2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37]) {
    const n = BigInt(value);
    if (n < 2n) return { probablyPrime: false, witness: "lt-2", rounds: 0 };
    for (const small of [2n, 3n, 5n, 7n, 11n, 13n, 17n, 19n, 23n, 29n, 31n, 37n]) {
      if (n === small) return { probablyPrime: true, witness: null, rounds: 0 };
      if (n % small === 0n) return { probablyPrime: false, witness: small.toString(), rounds: 0 };
    }

    let d = n - 1n;
    let s = 0;
    while ((d & 1n) === 0n) {
      d >>= 1n;
      s += 1;
    }

    let rounds = 0;
    for (const rawBase of bases) {
      const a = BigInt(rawBase) % n;
      if (a < 2n) continue;
      rounds += 1;
      let x = modPow(a, d, n);
      if (x === 1n || x === n - 1n) continue;
      let maybePrime = false;
      for (let r = 1; r < s; r += 1) {
        x = (x * x) % n;
        if (x === n - 1n) {
          maybePrime = true;
          break;
        }
      }
      if (!maybePrime) {
        return { probablyPrime: false, witness: a.toString(), rounds };
      }
    }
    return { probablyPrime: true, witness: null, rounds };
  }

  function fermatFactorScout(value, iterations = 1000) {
    const n = BigInt(value);
    const maxIterations = Math.max(0, Number(iterations) || 0);
    if (n < 2n) return { found: false, reason: "n < 2", iterations: 0 };
    if ((n & 1n) === 0n) {
      return { found: true, iterations: 0, offset: 0, factor: 2n, cofactor: n / 2n, method: "even" };
    }

    const root = integerNthRoot(n, 2);
    let a = root.exact ? root.root : root.root + 1n;
    for (let offset = 0; offset <= maxIterations; offset += 1) {
      const b2 = a * a - n;
      const b = integerNthRoot(b2, 2);
      if (b.exact) {
        const factor = a - b.root;
        const cofactor = a + b.root;
        if (factor > 1n && cofactor > 1n && factor * cofactor === n) {
          return { found: true, iterations: offset, offset, factor, cofactor, method: "fermat" };
        }
      }
      a += 1n;
    }
    return {
      found: false,
      iterations: maxIterations,
      offset: maxIterations,
      start: root.exact ? root.root.toString() : (root.root + 1n).toString(),
      method: "fermat"
    };
  }

  function pollardRhoScout(value, options = {}) {
    const n = BigInt(value);
    const iterations = Math.max(0, Number(options.iterations ?? 1000) || 0);
    const seeds = (options.seeds || [2n, 3n, 5n, 7n]).map((seed) => BigInt(seed));
    const constants = (options.constants || [1n, 3n, 5n, 11n]).map((constant) => BigInt(constant));
    if (n < 2n) return { found: false, reason: "n < 2", attempts: [] };
    if ((n & 1n) === 0n) {
      return { found: true, factor: 2n, cofactor: n / 2n, attempts: [{ seed: "2", c: "0", iterations: 0, status: "factor" }] };
    }

    const attempts = [];
    for (let index = 0; index < Math.min(seeds.length, constants.length); index += 1) {
      let x = modNormalize(seeds[index], n);
      let y = x;
      const c = modNormalize(constants[index], n);
      let d = 1n;
      let step = 0;
      const f = (next) => (next * next + c) % n;
      while (d === 1n && step < iterations) {
        x = f(x);
        y = f(f(y));
        d = gcd(absBigInt(x - y), n);
        step += 1;
      }
      const attempt = { seed: seeds[index].toString(), c: c.toString(), iterations: step, status: "miss" };
      if (d > 1n && d < n) {
        attempt.status = "factor";
        attempt.factor = d.toString();
        attempts.push(attempt);
        return { found: true, factor: d, cofactor: n / d, attempts };
      }
      if (d === n) attempt.status = "cycle";
      attempts.push(attempt);
    }
    return { found: false, attempts };
  }

  function perfectPowerScout(value, maxExponent = 64) {
    const n = absBigInt(BigInt(value));
    if (n < 4n) return { found: false };
    const ceiling = Math.max(2, Math.min(Number(maxExponent) || 64, bitLength(n)));
    for (let exponent = 2; exponent <= ceiling; exponent += 1) {
      const root = integerNthRoot(n, exponent);
      if (root.exact && root.root > 1n) {
        return { found: true, base: root.root, exponent };
      }
    }
    return { found: false };
  }

  function compactFactorRecords(factors) {
    const counts = new Map();
    for (const factor of factors) {
      const key = BigInt(factor.value).toString();
      const current = counts.get(key) || { ...factor, value: key, exponent: 0 };
      current.exponent += factor.exponent || 1;
      current.probablePrime = current.probablePrime && factor.probablePrime !== false;
      current.methods = Array.from(new Set([...(current.methods || []), factor.method].filter(Boolean)));
      counts.set(key, current);
    }
    return Array.from(counts.values()).sort((a, b) => {
      const left = BigInt(a.value);
      const right = BigInt(b.value);
      return left < right ? -1 : left > right ? 1 : 0;
    });
  }

  function productFromRecords(records) {
    let product = 1n;
    for (const record of records) {
      product *= powBigInt(BigInt(record.value), record.exponent || 1);
    }
    return product;
  }

  function solveFactorization(value, options = {}) {
    const startedAt = Date.now();
    const n = absBigInt(BigInt(value));
    const config = {
      smallPrimeLimit: Math.max(2, Number(options.smallPrimeLimit ?? 10000) || 10000),
      fermatIterations: Math.max(0, Number(options.fermatIterations ?? 1000) || 0),
      rhoIterations: Math.max(0, Number(options.rhoIterations ?? 1000) || 0),
      timeBudgetMs: Math.max(25, Number(options.timeBudgetMs ?? 2500) || 2500),
      maxPasses: Math.max(1, Number(options.maxPasses ?? 48) || 48),
      maxPerfectPowerExponent: Math.max(2, Number(options.maxPerfectPowerExponent ?? 64) || 64)
    };
    const queue = [n];
    const factors = [];
    const unresolved = [];
    const steps = [];
    let passes = 0;
    let timedOut = false;

    const timeRemaining = () => Date.now() - startedAt <= config.timeBudgetMs;
    const pushStep = (method, target, detail = {}) => {
      steps.push({
        method,
        target: target.toString(),
        targetDigits: target.toString().length,
        ...detail
      });
    };

    while (queue.length && passes < config.maxPasses) {
      if (!timeRemaining()) {
        timedOut = true;
        break;
      }
      const current = queue.pop();
      if (current === 1n) continue;
      passes += 1;

      const primality = isProbablePrime(current);
      if (primality.probablyPrime) {
        factors.push({ value: current.toString(), exponent: 1, probablePrime: true, method: "probable-prime" });
        pushStep("probable-prime", current, { status: "accepted", rounds: primality.rounds });
        continue;
      }

      const small = smallFactorScan(current, config.smallPrimeLimit);
      if (small.factor) {
        const cofactor = current / small.factor;
        pushStep("small-prime-sieve", current, {
          status: "factor-found",
          factor: small.factor.toString(),
          tested: small.tested,
          limit: small.limit
        });
        queue.push(cofactor, small.factor);
        continue;
      }
      pushStep("small-prime-sieve", current, { status: "miss", tested: small.tested, limit: small.limit });

      const perfect = perfectPowerScout(current, config.maxPerfectPowerExponent);
      if (perfect.found) {
        pushStep("perfect-power", current, {
          status: "factor-found",
          base: perfect.base.toString(),
          exponent: perfect.exponent
        });
        for (let index = 0; index < perfect.exponent; index += 1) queue.push(perfect.base);
        continue;
      }

      const fermat = mathSafeFermat(current, config.fermatIterations);
      if (fermat.factor) {
        pushStep("fermat", current, {
          status: "factor-found",
          factor: fermat.factor.toString(),
          iterations: fermat.iterations
        });
        queue.push(fermat.cofactor, fermat.factor);
        continue;
      }
      pushStep("fermat", current, { status: fermat.status || "miss", iterations: fermat.iterations || 0 });

      const rho = config.rhoIterations > 0 ? pollardRhoScout(current, { iterations: config.rhoIterations }) : { found: false, attempts: [] };
      if (rho.factor) {
        pushStep("pollard-rho", current, {
          status: "factor-found",
          factor: rho.factor.toString(),
          attempts: rho.attempts.length
        });
        queue.push(rho.cofactor, rho.factor);
        continue;
      }
      pushStep("pollard-rho", current, { status: config.rhoIterations > 0 ? "miss" : "disabled", attempts: rho.attempts.length });
      unresolved.push({
        value: current.toString(),
        digits: current.toString().length,
        probablePrime: false,
        compositeWitness: primality.witness || null
      });
    }

    while (queue.length) {
      const current = queue.pop();
      if (current > 1n) unresolved.push({ value: current.toString(), digits: current.toString().length, probablePrime: false, deferred: true });
    }

    const compactFactors = compactFactorRecords(factors);
    const factorProduct = productFromRecords(compactFactors);
    let unresolvedProduct = 1n;
    for (const item of unresolved) unresolvedProduct *= BigInt(item.value);
    const accountingVerified = factorProduct * unresolvedProduct === n;
    const productVerified = unresolved.length === 0 && factorProduct === n;
    const fullyFactored = productVerified;
    const status =
      fullyFactored && compactFactors.length
        ? "solved"
        : compactFactors.length
          ? "partial"
          : timedOut
            ? "timeout"
            : unresolved.length
              ? "unsolved"
              : "unit";

    return {
      status,
      input: n.toString(),
      digits: n.toString().length,
      bitLength: bitLength(n),
      factors: compactFactors,
      unresolved,
      productVerified,
      accountingVerified,
      fullyFactored,
      passes,
      timedOut,
      steps,
      config,
      elapsedMs: Date.now() - startedAt
    };
  }

  function mathSafeFermat(value, iterations) {
    if (!iterations) return { found: false, iterations: 0, status: "disabled" };
    return fermatFactorScout(value, iterations);
  }

  function modInverse(value, modulus) {
    const mod = BigInt(modulus);
    let a = modNormalize(value, mod);
    let b = mod;
    let x0 = 1n;
    let x1 = 0n;
    while (b !== 0n) {
      const quotient = a / b;
      [a, b] = [b, a - quotient * b];
      [x0, x1] = [x1, x0 - quotient * x1];
    }
    if (a !== 1n) return null;
    return modNormalize(x0, mod);
  }

  function integerNthRoot(value, exponent) {
    value = BigInt(value);
    exponent = Number(exponent);
    if (!Number.isInteger(exponent) || exponent < 1) {
      throw new RangeError("exponent must be a positive integer");
    }
    if (value < 0n) throw new RangeError("value must be non-negative");
    if (value < 2n || exponent === 1) {
      return { root: value, exact: true };
    }

    const decimalDigits = value.toString().length;
    const highDigits = Math.max(1, Math.ceil(decimalDigits / exponent) + 1);
    let low = 1n;
    let high = 10n ** BigInt(highDigits);

    while (low <= high) {
      const mid = (low + high) >> 1n;
      const comparison = powCompare(mid, exponent, value);
      if (comparison === 0) return { root: mid, exact: true };
      if (comparison < 0) {
        low = mid + 1n;
      } else {
        high = mid - 1n;
      }
    }

    return { root: high, exact: false };
  }

  function primeFactorization(n) {
    n = Number(n);
    if (!Number.isInteger(n) || n < 1) throw new RangeError("n must be a positive integer");
    if (factorCache.has(n)) return factorCache.get(n).map((factor) => factor.slice());
    const factors = [];
    let remaining = n;
    for (let p = 2; p * p <= remaining; p += p === 2 ? 1 : 2) {
      if (remaining % p === 0) {
        let exp = 0;
        while (remaining % p === 0) {
          remaining /= p;
          exp += 1;
        }
        factors.push([p, exp]);
      }
    }
    if (remaining > 1) factors.push([remaining, 1]);
    factorCache.set(n, factors.map((factor) => factor.slice()));
    return factors;
  }

  function phi(n) {
    n = Number(n);
    if (phiCache.has(n)) return phiCache.get(n);
    let result = Number(n);
    for (const [prime] of primeFactorization(n)) {
      result = Math.floor((result / prime) * (prime - 1));
    }
    phiCache.set(n, result);
    return result;
  }

  function divisors(n) {
    n = Number(n);
    if (divisorCache.has(n)) return divisorCache.get(n).slice();
    const factors = primeFactorization(n);
    let values = [1];
    for (const [prime, exponent] of factors) {
      const current = values.slice();
      let power = 1;
      for (let exp = 1; exp <= exponent; exp += 1) {
        power *= prime;
        for (const value of current) values.push(value * power);
      }
    }
    values = values.sort((a, b) => a - b);
    divisorCache.set(n, values.slice());
    return values;
  }

  function mobius(n) {
    n = Number(n);
    if (mobiusCache.has(n)) return mobiusCache.get(n);
    let sign = 1;
    for (const [, exponent] of primeFactorization(n)) {
      if (exponent > 1) {
        mobiusCache.set(n, 0);
        return 0;
      }
      sign *= -1;
    }
    mobiusCache.set(n, sign);
    return sign;
  }

  function trim(poly) {
    let last = poly.length - 1;
    while (last > 0 && poly[last] === 0n) last -= 1;
    return poly.slice(0, last + 1);
  }

  function multiplyPoly(a, b) {
    const result = Array(a.length + b.length - 1).fill(0n);
    for (let i = 0; i < a.length; i += 1) {
      for (let j = 0; j < b.length; j += 1) {
        result[i + j] += a[i] * b[j];
      }
    }
    return trim(result);
  }

  function dividePolyExact(numerator, denominator) {
    numerator = trim(numerator.slice());
    denominator = trim(denominator.slice());
    const denDegree = denominator.length - 1;
    const denLead = denominator[denDegree];
    if (denLead !== 1n && denLead !== -1n) {
      throw new Error("cyclotomic denominator must be monic");
    }
    const quotient = Array(Math.max(1, numerator.length - denominator.length + 1)).fill(0n);
    while (numerator.length >= denominator.length) {
      const degree = numerator.length - denominator.length;
      const coefficient = numerator[numerator.length - 1] / denLead;
      quotient[degree] = coefficient;
      for (let i = 0; i < denominator.length; i += 1) {
        numerator[degree + i] -= coefficient * denominator[i];
      }
      numerator = trim(numerator);
    }
    if (numerator.some((value) => value !== 0n)) {
      throw new Error("polynomial division was not exact");
    }
    return trim(quotient);
  }

  function trimMod(poly) {
    let last = poly.length - 1;
    while (last > 0 && poly[last] === 0) last -= 1;
    return poly.slice(0, last + 1);
  }

  function multiplyPolyMod(a, b, modulus) {
    const result = Array(a.length + b.length - 1).fill(0);
    for (let i = 0; i < a.length; i += 1) {
      for (let j = 0; j < b.length; j += 1) {
        result[i + j] = (result[i + j] + a[i] * b[j]) % modulus;
      }
    }
    return trimMod(result);
  }

  function dividePolyExactMod(numerator, denominator, modulus) {
    numerator = trimMod(numerator.slice());
    denominator = trimMod(denominator.slice());
    const denDegree = denominator.length - 1;
    const inverseLead = Number(modInverse(BigInt(denominator[denDegree]), BigInt(modulus)));
    if (!Number.isFinite(inverseLead)) throw new Error("modular polynomial denominator is not invertible");
    const quotient = Array(Math.max(1, numerator.length - denominator.length + 1)).fill(0);
    while (numerator.length >= denominator.length) {
      const degree = numerator.length - denominator.length;
      const coefficient = (numerator[numerator.length - 1] * inverseLead) % modulus;
      quotient[degree] = coefficient;
      for (let i = 0; i < denominator.length; i += 1) {
        numerator[degree + i] = (numerator[degree + i] - coefficient * denominator[i]) % modulus;
        if (numerator[degree + i] < 0) numerator[degree + i] += modulus;
      }
      numerator = trimMod(numerator);
    }
    if (numerator.some((value) => value % modulus !== 0)) {
      throw new Error("modular polynomial division was not exact");
    }
    return trimMod(quotient);
  }

  function cyclotomicCoefficients(n) {
    n = Number(n);
    if (cyclotomicCache.has(n)) return cyclotomicCache.get(n).slice();
    let result;
    if (n === 1) {
      result = [-1n, 1n];
    } else {
      const numerator = Array(n + 1).fill(0n);
      numerator[0] = -1n;
      numerator[n] = 1n;
      let denominator = [1n];
      for (const d of divisors(n)) {
        if (d < n) denominator = multiplyPoly(denominator, cyclotomicCoefficients(d));
      }
      result = dividePolyExact(numerator, denominator);
    }
    cyclotomicCache.set(n, result.slice());
    return result;
  }

  function cyclotomicCoefficientsMod(n, modulus) {
    n = Number(n);
    modulus = Number(modulus);
    const key = `${n}:${modulus}`;
    if (cyclotomicModCache.has(key)) return cyclotomicModCache.get(key).slice();
    let result;
    if (n === 1) {
      result = [modulus - 1, 1];
    } else {
      const numerator = Array(n + 1).fill(0);
      numerator[0] = modulus - 1;
      numerator[n] = 1;
      let denominator = [1];
      for (const d of divisors(n)) {
        if (d < n) denominator = multiplyPolyMod(denominator, cyclotomicCoefficientsMod(d, modulus), modulus);
      }
      result = dividePolyExactMod(numerator, denominator, modulus);
    }
    cyclotomicModCache.set(key, result.slice());
    return result;
  }

  function evaluatePolynomial(coefficients, x) {
    x = BigInt(x);
    let value = 0n;
    for (let i = coefficients.length - 1; i >= 0; i -= 1) {
      value = value * x + coefficients[i];
    }
    return value;
  }

  function evaluatePolynomialMod(coefficients, x, modulus) {
    const mod = BigInt(modulus);
    let value = 0n;
    let bx = BigInt(x) % mod;
    if (bx < 0n) bx += mod;
    for (let i = coefficients.length - 1; i >= 0; i -= 1) {
      value = (value * bx + coefficients[i]) % mod;
      if (value < 0n) value += mod;
    }
    return value;
  }

  function evaluatePolynomialModNumber(coefficients, x, modulus) {
    let value = 0;
    let nextX = Number(modNormalize(x, BigInt(modulus)));
    for (let i = coefficients.length - 1; i >= 0; i -= 1) {
      value = (value * nextX + coefficients[i]) % modulus;
      if (value < 0) value += modulus;
    }
    return BigInt(value);
  }

  function evaluateCyclotomic(n, base) {
    return evaluatePolynomial(cyclotomicCoefficients(n), BigInt(base));
  }

  function evaluateCyclotomicModProduct(n, base, modulus) {
    const mod = BigInt(modulus);
    let result = 1n;
    for (const d of divisors(n)) {
      const mu = mobius(Number(n) / d);
      if (mu === 0) continue;
      const term = modNormalize(modPow(base, d, mod) - 1n, mod);
      if (mu > 0) {
        result = (result * term) % mod;
      } else {
        const inverse = modInverse(term, mod);
        if (inverse === null) return null;
        result = (result * inverse) % mod;
      }
    }
    return result;
  }

  function evaluateCyclotomicMod(n, base, modulus) {
    const productValue = evaluateCyclotomicModProduct(n, base, modulus);
    if (productValue !== null) return productValue;
    return evaluatePolynomialModNumber(cyclotomicCoefficientsMod(n, modulus), base, Number(modulus));
  }

  function formatPolynomial(coefficients, variable = "x", maxTerms = 9) {
    const terms = [];
    for (let degree = coefficients.length - 1; degree >= 0; degree -= 1) {
      const coefficient = coefficients[degree];
      if (coefficient === 0n) continue;
      const sign = coefficient < 0n ? "-" : "+";
      const magnitude = absBigInt(coefficient);
      let body = "";
      if (degree === 0) {
        body = magnitude.toString();
      } else {
        const coeffText = magnitude === 1n ? "" : `${magnitude.toString()}·`;
        body = degree === 1 ? `${coeffText}${variable}` : `${coeffText}${variable}^${degree}`;
      }
      terms.push({ sign, body });
    }
    if (terms.length === 0) return "0";
    const visible = terms.slice(0, maxTerms);
    let text = visible
      .map((term, index) => (index === 0 && term.sign === "+" ? term.body : `${term.sign} ${term.body}`))
      .join(" ");
    if (terms.length > maxTerms) text += " …";
    return text;
  }

  function bigIntDigitLength(value) {
    return absBigInt(BigInt(value)).toString().length;
  }

  function bitLength(value) {
    const text = absBigInt(BigInt(value)).toString(2);
    return text === "0" ? 0 : text.length;
  }

  function log10Estimate(value) {
    const text = absBigInt(BigInt(value)).toString();
    const head = Number(text.slice(0, Math.min(16, text.length)));
    return Math.log10(head) + text.length - Math.min(16, text.length);
  }

  function squareShape(base) {
    const rootInfo = integerNthRoot(absBigInt(BigInt(base)), 2);
    if (rootInfo.exact) {
      return { b: 1n, y: rootInfo.root, exact: true, note: "base is a perfect square" };
    }
    return { b: BigInt(base), y: 1n, exact: false, note: "not a visible b·y² decomposition" };
  }

  return {
    absBigInt,
    gcd,
    powBigInt,
    powCompare,
    primeSieve,
    smallFactorScan,
    isProbablePrime,
    fermatFactorScout,
    pollardRhoScout,
    perfectPowerScout,
    solveFactorization,
    modPow,
    modInverse,
    integerNthRoot,
    primeFactorization,
    phi,
    divisors,
    mobius,
    cyclotomicCoefficients,
    cyclotomicCoefficientsMod,
    evaluatePolynomial,
    evaluatePolynomialMod,
    evaluateCyclotomic,
    evaluateCyclotomicModProduct,
    evaluateCyclotomicMod,
    formatPolynomial,
    bigIntDigitLength,
    bitLength,
    log10Estimate,
    squareShape
  };
});
