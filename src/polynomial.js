(function attachPolynomial(root, factory) {
  const api = factory();
  if (typeof module !== "undefined" && module.exports) {
    module.exports = api;
  }
  root.XRayPolynomial = api;
})(typeof globalThis !== "undefined" ? globalThis : this, function createPolynomialApi() {
  const cyclotomicCache = new Map();

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
    return factors;
  }

  function phi(n) {
    let result = Number(n);
    for (const [prime] of primeFactorization(n)) {
      result = Math.floor((result / prime) * (prime - 1));
    }
    return result;
  }

  function divisors(n) {
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
    return values.sort((a, b) => a - b);
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

  function evaluateCyclotomic(n, base) {
    return evaluatePolynomial(cyclotomicCoefficients(n), BigInt(base));
  }

  function evaluateCyclotomicMod(n, base, modulus) {
    return evaluatePolynomialMod(cyclotomicCoefficients(n), base, modulus);
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
    integerNthRoot,
    primeFactorization,
    phi,
    divisors,
    cyclotomicCoefficients,
    evaluatePolynomial,
    evaluatePolynomialMod,
    evaluateCyclotomic,
    evaluateCyclotomicMod,
    formatPolynomial,
    bigIntDigitLength,
    squareShape
  };
});
