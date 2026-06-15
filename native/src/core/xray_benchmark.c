#include "xray_workbench.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
#include <intrin.h>
#define XRAY_HAS_MSVC_CARRY_INTRINSICS 1
#else
#define XRAY_HAS_MSVC_CARRY_INTRINSICS 0
#endif

#define XRAY_BENCH_SAMPLES 5
#define XRAY_KERNEL_SAMPLES 5

static volatile unsigned long long kernel_probe_sink = 0;

static void append_result(XrayBenchmarkReport *report, const XrayBenchmarkResult *result) {
  XrayBenchmarkResult *next = (XrayBenchmarkResult *)realloc(report->results, sizeof(XrayBenchmarkResult) * (report->result_count + 1));
  if (!next) return;
  report->results = next;
  report->results[report->result_count++] = *result;
  if (result->passed) report->passed_count++;
  if (strcmp(result->category, "scratch-vs-gmp") == 0) {
    report->scratch_count++;
    if (strcmp(result->adoption, "allowed") == 0) report->replacement_ready_count++;
    else if (strcmp(result->adoption, "oracle-only") == 0) report->oracle_only_count++;
    else report->blocked_count++;
  }
}

const char *xray_scratch_adoption_for_result(const XrayBenchmarkResult *result) {
  if (!result || !result->parity_verified) return "blocked-output-mismatch";
  double limit = result->max_allowed_speed_ratio > 0.0 ? result->max_allowed_speed_ratio : 1.0;
  if (result->speed_ratio <= limit) return "allowed";
  return "oracle-only";
}

static unsigned long long median_samples(const unsigned long long *samples, size_t count) {
  unsigned long long sorted[XRAY_BENCH_SAMPLES] = {0};
  if (!samples || count == 0 || count > XRAY_BENCH_SAMPLES) return 0;
  for (size_t index = 0; index < count; ++index) sorted[index] = samples[index];
  for (size_t index = 1; index < count; ++index) {
    unsigned long long value = sorted[index];
    size_t pos = index;
    while (pos > 0 && sorted[pos - 1] > value) {
      sorted[pos] = sorted[pos - 1];
      pos--;
    }
    sorted[pos] = value;
  }
  return sorted[count / 2];
}

static double median_paired_ratio(const unsigned long long *numerator, const unsigned long long *denominator, size_t count) {
  double ratios[XRAY_BENCH_SAMPLES] = {0.0};
  if (!numerator || !denominator || count == 0 || count > XRAY_BENCH_SAMPLES) return 0.0;
  for (size_t index = 0; index < count; ++index) {
    ratios[index] = (double)(numerator[index] ? numerator[index] : 1ULL) /
      (double)(denominator[index] ? denominator[index] : 1ULL);
  }
  for (size_t index = 1; index < count; ++index) {
    double value = ratios[index];
    size_t pos = index;
    while (pos > 0 && ratios[pos - 1] > value) {
      ratios[pos] = ratios[pos - 1];
      pos--;
    }
    ratios[pos] = value;
  }
  return ratios[count / 2];
}

static void fill_kernel_u32(uint32_t *words, size_t count, uint32_t seed) {
  uint32_t state = seed ? seed : 1U;
  for (size_t index = 0; index < count; ++index) {
    state = state * 1664525U + 1013904223U;
    words[index] = state ^ (uint32_t)(index * 2654435761U);
  }
  if (count > 4) {
    words[1] = 0xffffffffU;
    words[2] = 0xffffffffU;
    words[count / 2] ^= 0x80000000U;
    words[count - 1] |= 0x40000000U;
  }
}

static void pack_kernel_u64(uint64_t *out, const uint32_t *words, size_t word_count) {
  size_t out_count = word_count / 2;
  for (size_t index = 0; index < out_count; ++index) {
    out[index] = (uint64_t)words[index * 2] | ((uint64_t)words[index * 2 + 1] << 32);
  }
}

static int compare_u32_to_u64_words(
  const uint32_t *words32,
  uint32_t carry32,
  const uint64_t *words64,
  uint64_t carry64,
  size_t word_count32) {
  if ((carry32 ? 1U : 0U) != (carry64 ? 1U : 0U)) return 0;
  for (size_t index = 0; index < word_count32 / 2; ++index) {
    uint32_t low = (uint32_t)words64[index];
    uint32_t high = (uint32_t)(words64[index] >> 32);
    if (words32[index * 2] != low || words32[index * 2 + 1] != high) return 0;
  }
  return 1;
}

static uint32_t kernel_add32_scalar(uint32_t *out, const uint32_t *left, const uint32_t *right, size_t count) {
  uint64_t carry = 0;
  for (size_t index = 0; index < count; ++index) {
    uint64_t sum = (uint64_t)left[index] + right[index] + carry;
    out[index] = (uint32_t)sum;
    carry = sum >> 32;
  }
  return (uint32_t)carry;
}

static uint32_t kernel_sub32_scalar(uint32_t *out, const uint32_t *left, const uint32_t *right, size_t count) {
  uint64_t borrow = 0;
  for (size_t index = 0; index < count; ++index) {
    uint64_t lhs = left[index];
    uint64_t rhs = (uint64_t)right[index] + borrow;
    out[index] = (uint32_t)(lhs - rhs);
    borrow = lhs < rhs;
  }
  return (uint32_t)borrow;
}

static uint64_t kernel_add64_scalar(uint64_t *out, const uint64_t *left, const uint64_t *right, size_t count) {
  uint64_t carry = 0;
  for (size_t index = 0; index < count; ++index) {
    uint64_t sum = left[index] + right[index];
    uint64_t carry_from_sum = sum < left[index];
    uint64_t with_carry = sum + carry;
    out[index] = with_carry;
    carry = carry_from_sum || (with_carry < sum);
  }
  return carry;
}

static uint64_t kernel_sub64_scalar(uint64_t *out, const uint64_t *left, const uint64_t *right, size_t count) {
  uint64_t borrow = 0;
  for (size_t index = 0; index < count; ++index) {
    uint64_t subtrahend = right[index] + borrow;
    uint64_t borrow_from_add = subtrahend < right[index];
    out[index] = left[index] - subtrahend;
    borrow = borrow_from_add || (left[index] < subtrahend);
  }
  return borrow;
}

#if XRAY_HAS_MSVC_CARRY_INTRINSICS
static uint32_t kernel_add32_intrinsic(uint32_t *out, const uint32_t *left, const uint32_t *right, size_t count) {
  unsigned char carry = 0;
  for (size_t index = 0; index < count; ++index) {
    unsigned int word = 0;
    carry = _addcarry_u32(carry, left[index], right[index], &word);
    out[index] = (uint32_t)word;
  }
  return (uint32_t)carry;
}

static uint32_t kernel_sub32_intrinsic(uint32_t *out, const uint32_t *left, const uint32_t *right, size_t count) {
  unsigned char borrow = 0;
  for (size_t index = 0; index < count; ++index) {
    unsigned int word = 0;
    borrow = _subborrow_u32(borrow, left[index], right[index], &word);
    out[index] = (uint32_t)word;
  }
  return (uint32_t)borrow;
}
#endif

static unsigned int kernel_iterations(size_t bits) {
  if (bits <= 2048) return 220000;
  if (bits <= 16384) return 26000;
  return 6000;
}

static void append_kernel_probe_result(
  XrayBenchmarkReport *report,
  const char *name,
  const char *operation,
  size_t bits,
  int parity,
  unsigned long long candidate_us,
  unsigned long long baseline_us,
  double paired_ratio,
  const char *candidate,
  const char *baseline,
  const char *feature_gate,
  const char *gmp_clue) {
  XrayBenchmarkResult result;
  memset(&result, 0, sizeof(result));
  snprintf(result.name, sizeof(result.name), "%s", name);
  snprintf(result.category, sizeof(result.category), "kernel-probe");
  snprintf(result.operation, sizeof(result.operation), "%s", operation);
  result.digits = (bits * 30103U + 99999U) / 100000U;
  result.scratch_us = candidate_us ? candidate_us : 1;
  result.gmp_us = baseline_us ? baseline_us : 1;
  result.speed_ratio = paired_ratio > 0.0 ? paired_ratio : (double)result.scratch_us / (double)result.gmp_us;
  result.max_allowed_speed_ratio = 0.98;
  result.parity_verified = parity;
  result.replacement_ready = parity && result.speed_ratio <= result.max_allowed_speed_ratio;
  snprintf(result.adoption, sizeof(result.adoption), "%s",
    !parity ? "blocked-output-mismatch" : (result.replacement_ready ? "promote-candidate" : "observe-only"));
  snprintf(result.status, sizeof(result.status), "%s",
    !parity ? "mismatch" : (result.replacement_ready ? "candidate-faster" : (result.speed_ratio < 1.0 ? "candidate-no-margin" : "baseline-faster")));
  result.passed = 1;
  result.elapsed_ms = (unsigned long)((result.scratch_us + result.gmp_us + 999ULL) / 1000ULL);
  snprintf(result.detail, sizeof(result.detail),
    "op=%s bits=%zu samples=%u candidate=%s baseline=%s candUs=%llu baseUs=%llu ratio=%.3f ratioMethod=paired-median max=%.2f featureGate=%s gmpClue=%s adoption=%s",
    operation,
    bits,
    XRAY_KERNEL_SAMPLES,
    candidate,
    baseline,
    result.scratch_us,
    result.gmp_us,
    result.speed_ratio,
    result.max_allowed_speed_ratio,
    feature_gate,
    gmp_clue,
    result.adoption);
  append_result(report, &result);
}

static void run_kernel_probe64_case(XrayBenchmarkReport *report, const char *operation, size_t bits) {
  size_t limbs32 = bits / 32U;
  size_t limbs64 = bits / 64U;
  uint32_t *left32 = (uint32_t *)calloc(limbs32, sizeof(uint32_t));
  uint32_t *right32 = (uint32_t *)calloc(limbs32, sizeof(uint32_t));
  uint32_t *baseline32 = (uint32_t *)calloc(limbs32, sizeof(uint32_t));
  uint32_t *check32 = (uint32_t *)calloc(limbs32, sizeof(uint32_t));
  uint64_t *left64 = (uint64_t *)calloc(limbs64, sizeof(uint64_t));
  uint64_t *right64 = (uint64_t *)calloc(limbs64, sizeof(uint64_t));
  uint64_t *candidate64 = (uint64_t *)calloc(limbs64, sizeof(uint64_t));
  if (!left32 || !right32 || !baseline32 || !check32 || !left64 || !right64 || !candidate64) {
    free(left32); free(right32); free(baseline32); free(check32); free(left64); free(right64); free(candidate64);
    return;
  }

  fill_kernel_u32(left32, limbs32, 0x159a55e5U);
  fill_kernel_u32(right32, limbs32, 0x5eed1234U);
  if (strcmp(operation, "sub-carry") == 0) {
    for (size_t index = 0; index < limbs32; ++index) {
      left32[index] |= right32[index] & 0x7fffffffU;
    }
  }
  pack_kernel_u64(left64, left32, limbs32);
  pack_kernel_u64(right64, right32, limbs32);

  uint32_t baseline_carry = 0;
  uint64_t candidate_carry = 0;
  if (strcmp(operation, "add-carry") == 0) {
    baseline_carry = kernel_add32_scalar(baseline32, left32, right32, limbs32);
    candidate_carry = kernel_add64_scalar(candidate64, left64, right64, limbs64);
  } else {
    baseline_carry = kernel_sub32_scalar(baseline32, left32, right32, limbs32);
    candidate_carry = kernel_sub64_scalar(candidate64, left64, right64, limbs64);
  }
  int parity = compare_u32_to_u64_words(baseline32, baseline_carry, candidate64, candidate_carry, limbs32);

  unsigned int iterations = kernel_iterations(bits);
  unsigned long long candidate_samples[XRAY_KERNEL_SAMPLES] = {0};
  unsigned long long baseline_samples[XRAY_KERNEL_SAMPLES] = {0};
  for (unsigned int sample = 0; sample < XRAY_KERNEL_SAMPLES; ++sample) {
    unsigned long long baseline_started = xray_now_us();
    for (unsigned int index = 0; index < iterations; ++index) {
      if (strcmp(operation, "add-carry") == 0) baseline_carry = kernel_add32_scalar(check32, left32, right32, limbs32);
      else baseline_carry = kernel_sub32_scalar(check32, left32, right32, limbs32);
      kernel_probe_sink ^= (unsigned long long)check32[(index + sample) % limbs32] ^ baseline_carry;
    }
    baseline_samples[sample] = xray_now_us() - baseline_started;

    unsigned long long candidate_started = xray_now_us();
    for (unsigned int index = 0; index < iterations; ++index) {
      if (strcmp(operation, "add-carry") == 0) candidate_carry = kernel_add64_scalar(candidate64, left64, right64, limbs64);
      else candidate_carry = kernel_sub64_scalar(candidate64, left64, right64, limbs64);
      kernel_probe_sink ^= candidate64[(index + sample) % limbs64] ^ candidate_carry;
    }
    candidate_samples[sample] = xray_now_us() - candidate_started;
  }

  char name[64];
  snprintf(name, sizeof(name), "kernel %s 64-bit limbs %zu-bit", operation, bits);
  append_kernel_probe_result(
    report,
    name,
    operation,
    bits,
    parity,
    median_samples(candidate_samples, XRAY_KERNEL_SAMPLES),
    median_samples(baseline_samples, XRAY_KERNEL_SAMPLES),
    median_paired_ratio(candidate_samples, baseline_samples, XRAY_KERNEL_SAMPLES),
    "scalar64-limb",
    "scalar32-limb",
    "portable-c",
    "gmp64limbs");

  free(left32); free(right32); free(baseline32); free(check32); free(left64); free(right64); free(candidate64);
}

#if XRAY_HAS_MSVC_CARRY_INTRINSICS
static void run_kernel_probe_intrinsic_case(XrayBenchmarkReport *report, const char *operation, size_t bits) {
  size_t limbs = bits / 32U;
  uint32_t *left = (uint32_t *)calloc(limbs, sizeof(uint32_t));
  uint32_t *right = (uint32_t *)calloc(limbs, sizeof(uint32_t));
  uint32_t *baseline = (uint32_t *)calloc(limbs, sizeof(uint32_t));
  uint32_t *candidate = (uint32_t *)calloc(limbs, sizeof(uint32_t));
  if (!left || !right || !baseline || !candidate) {
    free(left); free(right); free(baseline); free(candidate);
    return;
  }

  fill_kernel_u32(left, limbs, 0x243f6a88U);
  fill_kernel_u32(right, limbs, 0x85a308d3U);
  if (strcmp(operation, "sub-carry") == 0) {
    for (size_t index = 0; index < limbs; ++index) {
      left[index] |= right[index] & 0x7fffffffU;
    }
  }

  uint32_t baseline_carry = 0;
  uint32_t candidate_carry = 0;
  if (strcmp(operation, "add-carry") == 0) {
    baseline_carry = kernel_add32_scalar(baseline, left, right, limbs);
    candidate_carry = kernel_add32_intrinsic(candidate, left, right, limbs);
  } else {
    baseline_carry = kernel_sub32_scalar(baseline, left, right, limbs);
    candidate_carry = kernel_sub32_intrinsic(candidate, left, right, limbs);
  }
  int parity = baseline_carry == candidate_carry && memcmp(baseline, candidate, sizeof(uint32_t) * limbs) == 0;

  unsigned int iterations = kernel_iterations(bits);
  unsigned long long candidate_samples[XRAY_KERNEL_SAMPLES] = {0};
  unsigned long long baseline_samples[XRAY_KERNEL_SAMPLES] = {0};
  for (unsigned int sample = 0; sample < XRAY_KERNEL_SAMPLES; ++sample) {
    unsigned long long baseline_started = xray_now_us();
    for (unsigned int index = 0; index < iterations; ++index) {
      if (strcmp(operation, "add-carry") == 0) baseline_carry = kernel_add32_scalar(baseline, left, right, limbs);
      else baseline_carry = kernel_sub32_scalar(baseline, left, right, limbs);
      kernel_probe_sink ^= (unsigned long long)baseline[(index + sample) % limbs] ^ baseline_carry;
    }
    baseline_samples[sample] = xray_now_us() - baseline_started;

    unsigned long long candidate_started = xray_now_us();
    for (unsigned int index = 0; index < iterations; ++index) {
      if (strcmp(operation, "add-carry") == 0) candidate_carry = kernel_add32_intrinsic(candidate, left, right, limbs);
      else candidate_carry = kernel_sub32_intrinsic(candidate, left, right, limbs);
      kernel_probe_sink ^= (unsigned long long)candidate[(index + sample) % limbs] ^ candidate_carry;
    }
    candidate_samples[sample] = xray_now_us() - candidate_started;
  }

  char name[64];
  snprintf(name, sizeof(name), "kernel %s MSVC carry %zu-bit", operation, bits);
  append_kernel_probe_result(
    report,
    name,
    operation,
    bits,
    parity,
    median_samples(candidate_samples, XRAY_KERNEL_SAMPLES),
    median_samples(baseline_samples, XRAY_KERNEL_SAMPLES),
    median_paired_ratio(candidate_samples, baseline_samples, XRAY_KERNEL_SAMPLES),
    strcmp(operation, "add-carry") == 0 ? "_addcarry_u32" : "_subborrow_u32",
    "scalar32-limb",
    "msvc-x86-intrinsic",
    "gmpCpuKernelsLocalWin");

  free(left); free(right); free(baseline); free(candidate);
}
#endif

static void run_factor_case(XrayBenchmarkReport *report, const char *name, const char *input, const char *expected_status, unsigned long budget_ms) {
  XrayBenchmarkResult result;
  memset(&result, 0, sizeof(result));
  snprintf(result.name, sizeof(result.name), "%s", name);
  snprintf(result.category, sizeof(result.category), "factor-benchmark");
  snprintf(result.operation, sizeof(result.operation), "factor-solve");
  unsigned long started = xray_now_ms();
  XrayFactorConfig config = xray_factor_default_config();
  config.time_budget_ms = budget_ms;
  XrayFactorReport factor;
  int ok = xray_factor_solve(input, &config, &factor);
  result.elapsed_ms = xray_now_ms() - started;
  snprintf(result.status, sizeof(result.status), "%s", ok ? factor.status : "invalid");
  result.passed = ok && strcmp(factor.status, expected_status) == 0;
  snprintf(result.detail, sizeof(result.detail), "status=%s productVerified=%s factors=%zu unresolved=%zu",
    factor.status,
    factor.product_verified ? "true" : "false",
    factor.factor_count,
    factor.unresolved_count);
  xray_factor_report_clear(&factor);
  append_result(report, &result);
}

static void run_cyclo_case(XrayBenchmarkReport *report, const char *name, unsigned int n, const char *base_text, const char *expected_text) {
  XrayBenchmarkResult result;
  memset(&result, 0, sizeof(result));
  snprintf(result.name, sizeof(result.name), "%s", name);
  snprintf(result.category, sizeof(result.category), "cyclotomic-benchmark");
  snprintf(result.operation, sizeof(result.operation), "cyclotomic-eval");
  unsigned long started = xray_now_ms();
  mpz_t base, actual, expected;
  mpz_inits(base, actual, expected, NULL);
  mpz_set_str(base, base_text, 10);
  mpz_set_str(expected, expected_text, 10);
  int ok = xray_cyclotomic_eval_ui(actual, n, base);
  result.elapsed_ms = xray_now_ms() - started;
  result.passed = ok && mpz_cmp(actual, expected) == 0;
  snprintf(result.status, sizeof(result.status), "%s", result.passed ? "solved" : "failed");
  char *actual_text = mpz_get_str(NULL, 10, actual);
  snprintf(result.detail, sizeof(result.detail), "Phi_%u(%s)=%s expected=%s", n, base_text, actual_text ? actual_text : "n/a", expected_text);
  free(actual_text);
  mpz_clears(base, actual, expected, NULL);
  append_result(report, &result);
}

static char *benchmark_decimal(size_t digits, unsigned int seed, int high_lead) {
  char *text = (char *)calloc(digits + 1, 1);
  if (!text) return NULL;
  text[0] = (char)('0' + (high_lead ? 7 : 1) + (seed % 2));
  for (size_t index = 1; index < digits; ++index) {
    text[index] = (char)('0' + ((index * 7 + seed * 3) % 10));
  }
  return text;
}

static unsigned int perf_iterations(const char *operation, size_t digits) {
  if (strcmp(operation, "parse") == 0) {
    if (digits <= 40) return 4000;
    if (digits <= 150) return 1200;
    return 160;
  }
  if (strcmp(operation, "mul") == 0) {
    if (digits <= 40) return 2000;
    if (digits <= 150) return 180;
    return 12;
  }
  if (strcmp(operation, "powmod-u32") == 0) {
    if (digits <= 40) return 12000;
    if (digits <= 150) return 8000;
    return 2200;
  }
  if (strcmp(operation, "mod-u32") == 0 || strcmp(operation, "gcd-u32") == 0) {
    if (digits <= 40) return 30000;
    if (digits <= 150) return 10000;
    return 2200;
  }
  if (digits <= 40) return 20000;
  if (digits <= 150) return 8000;
  return 1600;
}

static void append_perf_result(
  XrayBenchmarkReport *report,
  const char *operation,
  size_t digits,
  int parity,
  unsigned long long scratch_us,
  unsigned long long gmp_us,
  double paired_ratio) {
  XrayBenchmarkResult result;
  memset(&result, 0, sizeof(result));
  snprintf(result.name, sizeof(result.name), "scratch %s %zu digits", operation, digits);
  snprintf(result.category, sizeof(result.category), "scratch-vs-gmp");
  snprintf(result.operation, sizeof(result.operation), "%s", operation);
  result.digits = digits;
  result.scratch_us = scratch_us ? scratch_us : 1;
  result.gmp_us = gmp_us ? gmp_us : 1;
  result.speed_ratio = paired_ratio > 0.0 ? paired_ratio : (double)result.scratch_us / (double)result.gmp_us;
  result.max_allowed_speed_ratio = 1.0;
  result.parity_verified = parity;
  const char *adoption = xray_scratch_adoption_for_result(&result);
  result.replacement_ready = strcmp(adoption, "allowed") == 0;
  snprintf(result.adoption, sizeof(result.adoption), "%s", adoption);
  result.elapsed_ms = (unsigned long)((result.scratch_us + result.gmp_us + 999ULL) / 1000ULL);
  result.passed = parity;
  snprintf(result.status, sizeof(result.status), "%s",
    !parity ? "failed" : (result.replacement_ready ? "replacement-ready" : "parity"));
  snprintf(result.detail, sizeof(result.detail),
    "operation=%s digits=%zu samples=%u scratchUs=%llu gmpUs=%llu ratio=%.3f ratioMethod=paired-median maxAllowedRatio=%.1f adoption=%s",
    operation,
    digits,
    XRAY_BENCH_SAMPLES,
    result.scratch_us,
    result.gmp_us,
    result.speed_ratio,
    result.max_allowed_speed_ratio,
    result.adoption);
  append_result(report, &result);
}

static void run_scratch_parse_case(XrayBenchmarkReport *report, size_t digits) {
  char *text = benchmark_decimal(digits, 3, 1);
  if (!text) return;
  unsigned int iterations = perf_iterations("parse", digits);
  XrayScratchBigInt scratch;
  xray_bigint_init(&scratch);
  mpz_t gmp;
  mpz_init(gmp);
  int ok = 1;
  unsigned long long scratch_samples[XRAY_BENCH_SAMPLES] = {0};
  unsigned long long gmp_samples[XRAY_BENCH_SAMPLES] = {0};
  int parity = 1;

  for (unsigned int sample = 0; sample < XRAY_BENCH_SAMPLES; ++sample) {
    unsigned long long scratch_started = xray_now_us();
    for (unsigned int index = 0; ok && index < iterations; ++index) {
      ok = xray_bigint_set_decimal(&scratch, text);
    }
    scratch_samples[sample] = xray_now_us() - scratch_started;

    unsigned long long gmp_started = xray_now_us();
    for (unsigned int index = 0; ok && index < iterations; ++index) {
      ok = mpz_set_str(gmp, text, 10) == 0;
    }
    gmp_samples[sample] = xray_now_us() - gmp_started;

    char *scratch_text = xray_bigint_get_decimal(&scratch);
    char *gmp_text = mpz_get_str(NULL, 10, gmp);
    parity = parity && ok && scratch_text && gmp_text && strcmp(scratch_text, gmp_text) == 0;
    free(scratch_text);
    free(gmp_text);
  }
  unsigned long long scratch_us = median_samples(scratch_samples, XRAY_BENCH_SAMPLES);
  unsigned long long gmp_us = median_samples(gmp_samples, XRAY_BENCH_SAMPLES);
  double paired_ratio = median_paired_ratio(scratch_samples, gmp_samples, XRAY_BENCH_SAMPLES);
  append_perf_result(report, "parse", digits, parity, scratch_us, gmp_us, paired_ratio);

  mpz_clear(gmp);
  xray_bigint_clear(&scratch);
  free(text);
}

static void run_scratch_binary_case(XrayBenchmarkReport *report, const char *operation, size_t digits) {
  char *left_text = benchmark_decimal(digits, 5, 1);
  char *right_text = benchmark_decimal(digits, 11, 0);
  if (!left_text || !right_text) {
    free(left_text);
    free(right_text);
    return;
  }
  unsigned int iterations = perf_iterations(operation, digits);
  XrayScratchBigInt a, b, scratch_out;
  xray_bigint_init(&a);
  xray_bigint_init(&b);
  xray_bigint_init(&scratch_out);
  mpz_t ga, gb, gout;
  mpz_inits(ga, gb, gout, NULL);
  int ok = xray_bigint_set_decimal(&a, left_text) &&
    xray_bigint_set_decimal(&b, right_text) &&
    mpz_set_str(ga, left_text, 10) == 0 &&
    mpz_set_str(gb, right_text, 10) == 0;

  unsigned long long scratch_samples[XRAY_BENCH_SAMPLES] = {0};
  unsigned long long gmp_samples[XRAY_BENCH_SAMPLES] = {0};
  int parity = 1;
  for (unsigned int sample = 0; sample < XRAY_BENCH_SAMPLES; ++sample) {
    unsigned long long scratch_started = xray_now_us();
    for (unsigned int index = 0; ok && index < iterations; ++index) {
      if (strcmp(operation, "add") == 0) ok = xray_bigint_add(&scratch_out, &a, &b);
      else if (strcmp(operation, "sub") == 0) ok = xray_bigint_sub(&scratch_out, &a, &b);
      else ok = xray_bigint_mul(&scratch_out, &a, &b);
    }
    scratch_samples[sample] = xray_now_us() - scratch_started;

    unsigned long long gmp_started = xray_now_us();
    for (unsigned int index = 0; ok && index < iterations; ++index) {
      if (strcmp(operation, "add") == 0) mpz_add(gout, ga, gb);
      else if (strcmp(operation, "sub") == 0) mpz_sub(gout, ga, gb);
      else mpz_mul(gout, ga, gb);
    }
    gmp_samples[sample] = xray_now_us() - gmp_started;

    char *scratch_text = xray_bigint_get_decimal(&scratch_out);
    char *gmp_text = mpz_get_str(NULL, 10, gout);
    parity = parity && ok && scratch_text && gmp_text && strcmp(scratch_text, gmp_text) == 0;
    free(scratch_text);
    free(gmp_text);
  }
  unsigned long long scratch_us = median_samples(scratch_samples, XRAY_BENCH_SAMPLES);
  unsigned long long gmp_us = median_samples(gmp_samples, XRAY_BENCH_SAMPLES);
  double paired_ratio = median_paired_ratio(scratch_samples, gmp_samples, XRAY_BENCH_SAMPLES);
  append_perf_result(report, operation, digits, parity, scratch_us, gmp_us, paired_ratio);

  mpz_clears(ga, gb, gout, NULL);
  xray_bigint_clear(&a);
  xray_bigint_clear(&b);
  xray_bigint_clear(&scratch_out);
  free(left_text);
  free(right_text);
}

static void run_scratch_divmod_case(XrayBenchmarkReport *report, size_t digits) {
  char *text = benchmark_decimal(digits, 17, 1);
  if (!text) return;
  const uint32_t divisor = 65537U;
  unsigned int iterations = perf_iterations("divmod-u32", digits);
  XrayScratchBigInt a, quotient;
  xray_bigint_init(&a);
  xray_bigint_init(&quotient);
  mpz_t ga, gquot;
  mpz_inits(ga, gquot, NULL);
  int ok = xray_bigint_set_decimal(&a, text) && mpz_set_str(ga, text, 10) == 0;
  uint32_t scratch_remainder = 0;
  unsigned long gmp_remainder = 0;

  unsigned long long scratch_samples[XRAY_BENCH_SAMPLES] = {0};
  unsigned long long gmp_samples[XRAY_BENCH_SAMPLES] = {0};
  int parity = 1;
  for (unsigned int sample = 0; sample < XRAY_BENCH_SAMPLES; ++sample) {
    unsigned long long scratch_started = xray_now_us();
    for (unsigned int index = 0; ok && index < iterations; ++index) {
      ok = xray_bigint_divmod_u32(&quotient, &scratch_remainder, &a, divisor);
    }
    scratch_samples[sample] = xray_now_us() - scratch_started;

    unsigned long long gmp_started = xray_now_us();
    for (unsigned int index = 0; ok && index < iterations; ++index) {
      gmp_remainder = (unsigned long)mpz_tdiv_q_ui(gquot, ga, divisor);
    }
    gmp_samples[sample] = xray_now_us() - gmp_started;

    char *scratch_text = xray_bigint_get_decimal(&quotient);
    char *gmp_text = mpz_get_str(NULL, 10, gquot);
    parity = parity && ok && scratch_text && gmp_text && strcmp(scratch_text, gmp_text) == 0 &&
      scratch_remainder == (uint32_t)gmp_remainder &&
      xray_bigint_mod_u32(&a, divisor) == (uint32_t)gmp_remainder;
    free(scratch_text);
    free(gmp_text);
  }
  unsigned long long scratch_us = median_samples(scratch_samples, XRAY_BENCH_SAMPLES);
  unsigned long long gmp_us = median_samples(gmp_samples, XRAY_BENCH_SAMPLES);
  double paired_ratio = median_paired_ratio(scratch_samples, gmp_samples, XRAY_BENCH_SAMPLES);
  append_perf_result(report, "divmod-u32", digits, parity, scratch_us, gmp_us, paired_ratio);

  mpz_clears(ga, gquot, NULL);
  xray_bigint_clear(&a);
  xray_bigint_clear(&quotient);
  free(text);
}

static void run_scratch_modular_case(XrayBenchmarkReport *report, const char *operation, size_t digits) {
  char *text = benchmark_decimal(digits, 23, 1);
  if (!text) return;
  const uint32_t modulus = 1000000007U;
  const uint32_t gcd_operand = 65537U;
  const uint32_t exponent = 65537U;
  unsigned int iterations = perf_iterations(operation, digits);
  XrayScratchBigInt a;
  xray_bigint_init(&a);
  mpz_t ga, gmodulus, gout;
  mpz_inits(ga, gmodulus, gout, NULL);
  int ok = xray_bigint_set_decimal(&a, text) && mpz_set_str(ga, text, 10) == 0;
  uint32_t scratch_result = 0;
  unsigned long gmp_result = 0;

  unsigned long long scratch_samples[XRAY_BENCH_SAMPLES] = {0};
  unsigned long long gmp_samples[XRAY_BENCH_SAMPLES] = {0};
  int parity = 1;
  for (unsigned int sample = 0; sample < XRAY_BENCH_SAMPLES; ++sample) {
    unsigned long long scratch_started = xray_now_us();
    for (unsigned int index = 0; ok && index < iterations; ++index) {
      if (strcmp(operation, "mod-u32") == 0) scratch_result = xray_bigint_mod_u32(&a, modulus);
      else if (strcmp(operation, "gcd-u32") == 0) scratch_result = xray_bigint_gcd_u32(&a, gcd_operand);
      else scratch_result = xray_bigint_powmod_u32(&a, exponent, modulus);
    }
    scratch_samples[sample] = xray_now_us() - scratch_started;

    unsigned long long gmp_started = xray_now_us();
    for (unsigned int index = 0; ok && index < iterations; ++index) {
      if (strcmp(operation, "mod-u32") == 0) {
        gmp_result = (unsigned long)mpz_tdiv_ui(ga, modulus);
      } else if (strcmp(operation, "gcd-u32") == 0) {
        mpz_set_ui(gmodulus, gcd_operand);
        mpz_gcd(gout, ga, gmodulus);
        gmp_result = (unsigned long)mpz_get_ui(gout);
      } else {
        mpz_set_ui(gmodulus, modulus);
        mpz_powm_ui(gout, ga, exponent, gmodulus);
        gmp_result = (unsigned long)mpz_get_ui(gout);
      }
    }
    gmp_samples[sample] = xray_now_us() - gmp_started;
    parity = parity && ok && scratch_result == (uint32_t)gmp_result;
  }
  unsigned long long scratch_us = median_samples(scratch_samples, XRAY_BENCH_SAMPLES);
  unsigned long long gmp_us = median_samples(gmp_samples, XRAY_BENCH_SAMPLES);
  double paired_ratio = median_paired_ratio(scratch_samples, gmp_samples, XRAY_BENCH_SAMPLES);
  append_perf_result(report, operation, digits, parity, scratch_us, gmp_us, paired_ratio);

  mpz_clears(ga, gmodulus, gout, NULL);
  xray_bigint_clear(&a);
  free(text);
}

static void run_scratch_bigint_gates(XrayBenchmarkReport *report) {
  const size_t sizes[] = {40, 150, 1000};
  for (size_t index = 0; index < sizeof(sizes) / sizeof(sizes[0]); ++index) {
    run_scratch_parse_case(report, sizes[index]);
    run_scratch_binary_case(report, "add", sizes[index]);
    run_scratch_binary_case(report, "sub", sizes[index]);
    run_scratch_modular_case(report, "mod-u32", sizes[index]);
    run_scratch_modular_case(report, "gcd-u32", sizes[index]);
    run_scratch_modular_case(report, "powmod-u32", sizes[index]);
    run_scratch_divmod_case(report, sizes[index]);
    run_scratch_binary_case(report, "mul", sizes[index]);
  }
}

static void run_kernel_probes(XrayBenchmarkReport *report) {
  const size_t bit_sizes[] = {2048, 16384};
  for (size_t index = 0; index < sizeof(bit_sizes) / sizeof(bit_sizes[0]); ++index) {
    run_kernel_probe64_case(report, "add-carry", bit_sizes[index]);
    run_kernel_probe64_case(report, "sub-carry", bit_sizes[index]);
#if XRAY_HAS_MSVC_CARRY_INTRINSICS
    run_kernel_probe_intrinsic_case(report, "add-carry", bit_sizes[index]);
    run_kernel_probe_intrinsic_case(report, "sub-carry", bit_sizes[index]);
#endif
  }
}

int xray_benchmark_run(XrayBenchmarkReport *report) {
  if (!report) return 0;
  memset(report, 0, sizeof(*report));
  unsigned long started = xray_now_ms();
  xray_cpu_features_detect(&report->cpu);

  run_factor_case(report, "toy semiprime 10403", "10403", "solved", 1000);
  run_factor_case(report, "rho semiprime 8051", "8051", "solved", 1000);
  run_factor_case(report, "prime power 3^7", "2187", "solved", 1000);
  run_factor_case(report, "carmichael 561", "561", "solved", 1000);
  run_factor_case(report, "41 digit Fermat semiprime", "10000000000000000111000000000000000041769", "solved", 1000);
  run_factor_case(report, "RSA-260 unresolved guard", "22112825529529666435281085255026230927612089502470015394413748319128822941402001986512729726569746599085900330031400051170742204560859276357953757185954298838958709229238491006703034124620545784566413664540684214361293017694020846391065875914794251435144458199", "unsolved", 100);
  run_cyclo_case(report, "Phi_3(10)", 3, "10", "111");
  run_cyclo_case(report, "Phi_5(2)", 5, "2", "31");
  run_cyclo_case(report, "Phi_8(2)", 8, "2", "17");
  run_kernel_probes(report);
  run_scratch_bigint_gates(report);

  report->elapsed_ms = xray_now_ms() - started;
  return 1;
}

void xray_benchmark_report_clear(XrayBenchmarkReport *report) {
  if (!report) return;
  free(report->results);
  memset(report, 0, sizeof(*report));
}
