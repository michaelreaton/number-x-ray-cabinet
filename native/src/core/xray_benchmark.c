#include "xray_workbench.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void append_result(XrayBenchmarkReport *report, const XrayBenchmarkResult *result) {
  XrayBenchmarkResult *next = (XrayBenchmarkResult *)realloc(report->results, sizeof(XrayBenchmarkResult) * (report->result_count + 1));
  if (!next) return;
  report->results = next;
  report->results[report->result_count++] = *result;
  if (result->passed) report->passed_count++;
}

static void run_factor_case(XrayBenchmarkReport *report, const char *name, const char *input, const char *expected_status, unsigned long budget_ms) {
  XrayBenchmarkResult result;
  memset(&result, 0, sizeof(result));
  snprintf(result.name, sizeof(result.name), "%s", name);
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
  unsigned long long gmp_us) {
  XrayBenchmarkResult result;
  memset(&result, 0, sizeof(result));
  snprintf(result.name, sizeof(result.name), "scratch %s %zu digits", operation, digits);
  snprintf(result.category, sizeof(result.category), "scratch-vs-gmp");
  snprintf(result.operation, sizeof(result.operation), "%s", operation);
  result.digits = digits;
  result.scratch_us = scratch_us ? scratch_us : 1;
  result.gmp_us = gmp_us ? gmp_us : 1;
  result.speed_ratio = (double)result.scratch_us / (double)result.gmp_us;
  result.parity_verified = parity;
  result.replacement_ready = parity && result.scratch_us <= result.gmp_us;
  result.elapsed_ms = (unsigned long)((result.scratch_us + result.gmp_us + 999ULL) / 1000ULL);
  result.passed = parity;
  snprintf(result.status, sizeof(result.status), "%s",
    !parity ? "failed" : (result.replacement_ready ? "replacement-ready" : "parity"));
  snprintf(result.detail, sizeof(result.detail),
    "operation=%s digits=%zu scratchUs=%llu gmpUs=%llu ratio=%.3f gate=%s",
    operation,
    digits,
    result.scratch_us,
    result.gmp_us,
    result.speed_ratio,
    result.replacement_ready ? "replace-allowed" : "keep-gmp");
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

  unsigned long long scratch_started = xray_now_us();
  for (unsigned int index = 0; ok && index < iterations; ++index) {
    ok = xray_bigint_set_decimal(&scratch, text);
  }
  unsigned long long scratch_us = xray_now_us() - scratch_started;

  unsigned long long gmp_started = xray_now_us();
  for (unsigned int index = 0; ok && index < iterations; ++index) {
    ok = mpz_set_str(gmp, text, 10) == 0;
  }
  unsigned long long gmp_us = xray_now_us() - gmp_started;

  char *scratch_text = xray_bigint_get_decimal(&scratch);
  char *gmp_text = mpz_get_str(NULL, 10, gmp);
  int parity = ok && scratch_text && gmp_text && strcmp(scratch_text, gmp_text) == 0;
  append_perf_result(report, "parse", digits, parity, scratch_us, gmp_us);

  free(scratch_text);
  free(gmp_text);
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

  unsigned long long scratch_started = xray_now_us();
  for (unsigned int index = 0; ok && index < iterations; ++index) {
    if (strcmp(operation, "add") == 0) ok = xray_bigint_add(&scratch_out, &a, &b);
    else if (strcmp(operation, "sub") == 0) ok = xray_bigint_sub(&scratch_out, &a, &b);
    else ok = xray_bigint_mul(&scratch_out, &a, &b);
  }
  unsigned long long scratch_us = xray_now_us() - scratch_started;

  unsigned long long gmp_started = xray_now_us();
  for (unsigned int index = 0; ok && index < iterations; ++index) {
    if (strcmp(operation, "add") == 0) mpz_add(gout, ga, gb);
    else if (strcmp(operation, "sub") == 0) mpz_sub(gout, ga, gb);
    else mpz_mul(gout, ga, gb);
  }
  unsigned long long gmp_us = xray_now_us() - gmp_started;

  char *scratch_text = xray_bigint_get_decimal(&scratch_out);
  char *gmp_text = mpz_get_str(NULL, 10, gout);
  int parity = ok && scratch_text && gmp_text && strcmp(scratch_text, gmp_text) == 0;
  append_perf_result(report, operation, digits, parity, scratch_us, gmp_us);

  free(scratch_text);
  free(gmp_text);
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

  unsigned long long scratch_started = xray_now_us();
  for (unsigned int index = 0; ok && index < iterations; ++index) {
    ok = xray_bigint_divmod_u32(&quotient, &scratch_remainder, &a, divisor);
  }
  unsigned long long scratch_us = xray_now_us() - scratch_started;

  unsigned long long gmp_started = xray_now_us();
  for (unsigned int index = 0; ok && index < iterations; ++index) {
    gmp_remainder = (unsigned long)mpz_tdiv_q_ui(gquot, ga, divisor);
  }
  unsigned long long gmp_us = xray_now_us() - gmp_started;

  char *scratch_text = xray_bigint_get_decimal(&quotient);
  char *gmp_text = mpz_get_str(NULL, 10, gquot);
  int parity = ok && scratch_text && gmp_text && strcmp(scratch_text, gmp_text) == 0 &&
    scratch_remainder == (uint32_t)gmp_remainder &&
    xray_bigint_mod_u32(&a, divisor) == (uint32_t)gmp_remainder;
  append_perf_result(report, "divmod-u32", digits, parity, scratch_us, gmp_us);

  free(scratch_text);
  free(gmp_text);
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

  unsigned long long scratch_started = xray_now_us();
  for (unsigned int index = 0; ok && index < iterations; ++index) {
    if (strcmp(operation, "mod-u32") == 0) scratch_result = xray_bigint_mod_u32(&a, modulus);
    else if (strcmp(operation, "gcd-u32") == 0) scratch_result = xray_bigint_gcd_u32(&a, gcd_operand);
    else scratch_result = xray_bigint_powmod_u32(&a, exponent, modulus);
  }
  unsigned long long scratch_us = xray_now_us() - scratch_started;

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
  unsigned long long gmp_us = xray_now_us() - gmp_started;

  int parity = ok && scratch_result == (uint32_t)gmp_result;
  append_perf_result(report, operation, digits, parity, scratch_us, gmp_us);

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
    if (sizes[index] <= 150) run_scratch_binary_case(report, "mul", sizes[index]);
  }
}

int xray_benchmark_run(XrayBenchmarkReport *report) {
  if (!report) return 0;
  memset(report, 0, sizeof(*report));
  unsigned long started = xray_now_ms();

  run_factor_case(report, "toy semiprime 10403", "10403", "solved", 1000);
  run_factor_case(report, "rho semiprime 8051", "8051", "solved", 1000);
  run_factor_case(report, "prime power 3^7", "2187", "solved", 1000);
  run_factor_case(report, "carmichael 561", "561", "solved", 1000);
  run_factor_case(report, "41 digit Fermat semiprime", "10000000000000000111000000000000000041769", "solved", 1000);
  run_factor_case(report, "RSA-260 unresolved guard", "22112825529529666435281085255026230927612089502470015394413748319128822941402001986512729726569746599085900330031400051170742204560859276357953757185954298838958709229238491006703034124620545784566413664540684214361293017694020846391065875914794251435144458199", "unsolved", 100);
  run_cyclo_case(report, "Phi_3(10)", 3, "10", "111");
  run_cyclo_case(report, "Phi_5(2)", 5, "2", "31");
  run_cyclo_case(report, "Phi_8(2)", 8, "2", "17");
  run_scratch_bigint_gates(report);

  report->elapsed_ms = xray_now_ms() - started;
  return 1;
}

void xray_benchmark_report_clear(XrayBenchmarkReport *report) {
  if (!report) return;
  free(report->results);
  memset(report, 0, sizeof(*report));
}
