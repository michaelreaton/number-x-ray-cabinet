#include "xray_workbench.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) do { \
  if (!(expr)) { \
    fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
    abort(); \
  } \
} while (0)

static void check_scratch_matches_mpz(const XrayScratchBigInt *actual, const mpz_t expected) {
  char *actual_text = xray_bigint_get_decimal(actual);
  char *expected_text = mpz_get_str(NULL, 10, expected);
  CHECK(actual_text != NULL);
  CHECK(expected_text != NULL);
  CHECK(strcmp(actual_text, expected_text) == 0);
  free(actual_text);
  free(expected_text);
}

static char *test_path_join(const char *dir, const char *name) {
  size_t dir_length = dir ? strlen(dir) : 0;
  size_t name_length = name ? strlen(name) : 0;
  char *path = (char *)calloc(dir_length + name_length + 2, 1);
  CHECK(path != NULL);
  snprintf(path, dir_length + name_length + 2, "%s/%s", dir ? dir : ".", name ? name : "");
  return path;
}

static char *read_text_file(const char *path) {
  FILE *file = NULL;
#ifdef _WIN32
  if (fopen_s(&file, path, "rb") != 0) file = NULL;
#else
  file = fopen(path, "rb");
#endif
  CHECK(file != NULL);
  CHECK(fseek(file, 0, SEEK_END) == 0);
  long length = ftell(file);
  CHECK(length >= 0);
  CHECK(fseek(file, 0, SEEK_SET) == 0);
  char *text = (char *)calloc((size_t)length + 1, 1);
  CHECK(text != NULL);
  if (length) CHECK(fread(text, 1, (size_t)length, file) == (size_t)length);
  fclose(file);
  return text;
}

static void test_parse_messy_input(void) {
  mpz_t value;
  mpz_init(value);
  char *normalized = NULL;
  char *error = NULL;
  CHECK(xray_parse_integer("N = 10,403\n", value, &normalized, &error));
  CHECK(strcmp(normalized, "10403") == 0);
  CHECK(mpz_cmp_ui(value, 10403) == 0);
  free(normalized);
  free(error);
  mpz_clear(value);
}

static void test_exact_expression_parser(void) {
  mpz_t value, expected;
  mpz_inits(value, expected, NULL);
  XrayExpressionResult expression;
  CHECK(xray_evaluate_expression("2^12 + 1", value, &expression));
  CHECK(strcmp(expression.normalized, "4097") == 0);
  CHECK(mpz_cmp_ui(value, 4097) == 0);
  xray_expression_result_clear(&expression);

  CHECK(xray_evaluate_expression("Phi(8192, 2)", value, &expression));
  mpz_set_ui(expected, 1);
  mpz_mul_2exp(expected, expected, 4096);
  mpz_add_ui(expected, expected, 1);
  CHECK(mpz_cmp(value, expected) == 0);
  CHECK(expression.digits > 1200);
  xray_expression_result_clear(&expression);

  CHECK(xray_evaluate_expression("Fermat(12)", value, &expression));
  CHECK(mpz_cmp(value, expected) == 0);
  xray_expression_result_clear(&expression);

  CHECK(!xray_evaluate_expression("10 / 3", value, &expression));
  CHECK(expression.error != NULL);
  xray_expression_result_clear(&expression);
  mpz_clears(value, expected, NULL);
}

static void test_cpu_feature_detection(void) {
  XrayCpuFeatures cpu;
  xray_cpu_features_detect(&cpu);
  CHECK(cpu.architecture[0] != '\0');
  CHECK(cpu.vendor[0] != '\0');
  CHECK(cpu.brand[0] != '\0');
  CHECK(cpu.logical_cpus >= 1);
  if (cpu.avx) CHECK(cpu.avx_os_enabled);
  if (cpu.avx2) CHECK(cpu.avx && cpu.avx_os_enabled);
  if (cpu.avx512f) CHECK(cpu.avx512_os_enabled);
  if (cpu.avx_os_enabled) CHECK((cpu.xcr0 & 0x6ULL) == 0x6ULL);
  if (cpu.avx512_os_enabled) CHECK((cpu.xcr0 & 0xe6ULL) == 0xe6ULL);
  char *summary = xray_cpu_features_summary(&cpu);
  CHECK(summary != NULL);
  CHECK(strstr(summary, "CPU:") != NULL);
  CHECK(strstr(summary, "flags=") != NULL);
  free(summary);
}

static void test_scratch_bigint_oracle(void) {
  XrayScratchBigInt a, b, sum, difference, product, quotient;
  xray_bigint_init(&a);
  xray_bigint_init(&b);
  xray_bigint_init(&sum);
  xray_bigint_init(&difference);
  xray_bigint_init(&product);
  xray_bigint_init(&quotient);
  CHECK(sizeof(*a.limbs) == sizeof(uint64_t));
  CHECK(xray_bigint_set_decimal(&a, "123456789012345678901234567890"));
  CHECK(xray_bigint_set_decimal(&b, "98765432109876543210"));
  CHECK(xray_bigint_set_decimal(&quotient, "000,123_456 789"));
  char *messy_text = xray_bigint_get_decimal(&quotient);
  CHECK(strcmp(messy_text, "123456789") == 0);
  free(messy_text);
  CHECK(xray_bigint_set_decimal(&quotient, "10,000_000 000,000_000 000"));
  messy_text = xray_bigint_get_decimal(&quotient);
  CHECK(strcmp(messy_text, "10000000000000000000") == 0);
  free(messy_text);
  CHECK(xray_bigint_set_decimal(&quotient, "000_000"));
  CHECK(xray_bigint_is_zero(&quotient));
  CHECK(!xray_bigint_set_decimal(&quotient, "12x34"));
  CHECK(!xray_bigint_set_decimal(&quotient, "   "));
  CHECK(xray_bigint_add(&sum, &a, &b));
  CHECK(xray_bigint_sub(&difference, &a, &b));
  CHECK(xray_bigint_mul(&product, &a, &b));
  uint32_t rem = 0;
  CHECK(xray_bigint_divmod_u32(&quotient, &rem, &a, 65537U));

  char *sum_text = xray_bigint_get_decimal(&sum);
  char *difference_text = xray_bigint_get_decimal(&difference);
  char *product_text = xray_bigint_get_decimal(&product);
  char *quotient_text = xray_bigint_get_decimal(&quotient);
  mpz_t ga, gb, gsum, gdifference, gproduct, gquotient, gmodulus, ggcd, gpow, gexponent;
  mpz_inits(ga, gb, gsum, gdifference, gproduct, gquotient, gmodulus, ggcd, gpow, gexponent, NULL);
  mpz_set_str(ga, "123456789012345678901234567890", 10);
  mpz_set_str(gb, "98765432109876543210", 10);
  mpz_add(gsum, ga, gb);
  mpz_sub(gdifference, ga, gb);
  mpz_mul(gproduct, ga, gb);
  unsigned long oracle_rem = mpz_tdiv_q_ui(gquotient, ga, 65537U);
  mpz_set_ui(gmodulus, 1000000007U);
  mpz_set_ui(gexponent, 12345U);
  mpz_powm(gpow, ga, gexponent, gmodulus);
  mpz_set_ui(gmodulus, 65537U);
  mpz_gcd(ggcd, ga, gmodulus);
  char *oracle_sum = mpz_get_str(NULL, 10, gsum);
  char *oracle_difference = mpz_get_str(NULL, 10, gdifference);
  char *oracle_product = mpz_get_str(NULL, 10, gproduct);
  char *oracle_quotient = mpz_get_str(NULL, 10, gquotient);
  CHECK(strcmp(sum_text, oracle_sum) == 0);
  CHECK(strcmp(difference_text, oracle_difference) == 0);
  CHECK(strcmp(product_text, oracle_product) == 0);
  CHECK(strcmp(quotient_text, oracle_quotient) == 0);
  CHECK(rem == (uint32_t)oracle_rem);
  CHECK(xray_bigint_mod_u32(&a, 65537U) == (uint32_t)oracle_rem);
  CHECK(xray_bigint_gcd_u32(&a, 65537U) == (uint32_t)mpz_get_ui(ggcd));
  CHECK(xray_bigint_powmod_u32(&a, 12345U, 1000000007U) == (uint32_t)mpz_get_ui(gpow));
  CHECK(!xray_bigint_is_zero(&a));
  free(sum_text);
  free(difference_text);
  free(product_text);
  free(quotient_text);
  free(oracle_sum);
  free(oracle_difference);
  free(oracle_product);
  free(oracle_quotient);

  CHECK(xray_bigint_set_decimal(&a, "999999999999999999999999999999"));
  CHECK(xray_bigint_set_decimal(&b, "1"));
  CHECK(xray_bigint_add(&sum, &a, &b));
  mpz_set_str(ga, "999999999999999999999999999999", 10);
  mpz_set_ui(gb, 1);
  mpz_add(gsum, ga, gb);
  char *carry_sum = xray_bigint_get_decimal(&sum);
  char *oracle_carry_sum = mpz_get_str(NULL, 10, gsum);
  CHECK(strcmp(carry_sum, oracle_carry_sum) == 0);
  free(carry_sum);
  free(oracle_carry_sum);

  CHECK(xray_bigint_sub(&difference, &sum, &b));
  mpz_sub(gdifference, gsum, gb);
  char *borrow_difference = xray_bigint_get_decimal(&difference);
  char *oracle_borrow_difference = mpz_get_str(NULL, 10, gdifference);
  CHECK(strcmp(borrow_difference, oracle_borrow_difference) == 0);
  free(borrow_difference);
  free(oracle_borrow_difference);

  CHECK(xray_bigint_add(&a, &a, &b));
  char *alias_sum = xray_bigint_get_decimal(&a);
  char *oracle_alias_sum = mpz_get_str(NULL, 10, gsum);
  CHECK(strcmp(alias_sum, oracle_alias_sum) == 0);
  free(alias_sum);
  free(oracle_alias_sum);

  CHECK(xray_bigint_sub(&a, &a, &b));
  char *alias_difference = xray_bigint_get_decimal(&a);
  char *oracle_alias_difference = mpz_get_str(NULL, 10, gdifference);
  CHECK(strcmp(alias_difference, oracle_alias_difference) == 0);
  free(alias_difference);
  free(oracle_alias_difference);

  CHECK(xray_bigint_set_decimal(&a, "123456789012345678901234567890"));
  CHECK(xray_bigint_set_decimal(&b, "98765432109876543210"));
  mpz_set_str(ga, "123456789012345678901234567890", 10);
  mpz_set_str(gb, "98765432109876543210", 10);
  CHECK(xray_bigint_mul(&a, &a, &b));
  mpz_mul(gproduct, ga, gb);
  check_scratch_matches_mpz(&a, gproduct);

  CHECK(xray_bigint_set_decimal(&a, "123456789012345678901234567890"));
  CHECK(xray_bigint_set_decimal(&b, "98765432109876543210"));
  CHECK(xray_bigint_mul(&b, &a, &b));
  check_scratch_matches_mpz(&b, gproduct);

  mpz_clears(ga, gb, gsum, gdifference, gproduct, gquotient, gmodulus, ggcd, gpow, gexponent, NULL);
  xray_bigint_clear(&a);
  xray_bigint_clear(&b);
  xray_bigint_clear(&sum);
  xray_bigint_clear(&difference);
  xray_bigint_clear(&product);
  xray_bigint_clear(&quotient);
}

static void test_scratch_bigint_oracle_sweep(void) {
  const char *values[] = {
    "0",
    "1",
    "4294967295",
    "4294967296",
    "18446744073709551615",
    "18446744073709551616",
    "999999999",
    "1000000000",
    "1000000000000000000000000000000",
    "1234567890123456789012345678901234567890",
    "80852963074185296307418529630741852963074185296307418529630741852963074185296307418529630741852963074185296307418529630741852963074185296307418529630741852963074185296307",
    NULL
  };
  const uint32_t divisors[] = {1U, 2U, 3U, 5U, 65535U, 65537U, 1000000007U, 2147483649U, 4294967295U};

  XrayScratchBigInt a, b, out, quotient;
  xray_bigint_init(&a);
  xray_bigint_init(&b);
  xray_bigint_init(&out);
  xray_bigint_init(&quotient);
  mpz_t ga, gb, gout, gquotient, gdivisor, ggcd, gpow;
  mpz_inits(ga, gb, gout, gquotient, gdivisor, ggcd, gpow, NULL);

  for (size_t i = 0; values[i]; ++i) {
    CHECK(xray_bigint_set_decimal(&a, values[i]));
    CHECK(mpz_set_str(ga, values[i], 10) == 0);
    check_scratch_matches_mpz(&a, ga);

    for (size_t d = 0; d < sizeof(divisors) / sizeof(divisors[0]); ++d) {
      uint32_t rem = 0;
      CHECK(xray_bigint_divmod_u32(&quotient, &rem, &a, divisors[d]));
      unsigned long oracle_rem = mpz_tdiv_q_ui(gquotient, ga, divisors[d]);
      check_scratch_matches_mpz(&quotient, gquotient);
      CHECK(rem == (uint32_t)oracle_rem);
      CHECK(xray_bigint_mod_u32(&a, divisors[d]) == (uint32_t)mpz_tdiv_ui(ga, divisors[d]));

      mpz_set_ui(gdivisor, divisors[d]);
      mpz_gcd(ggcd, ga, gdivisor);
      CHECK(xray_bigint_gcd_u32(&a, divisors[d]) == (uint32_t)mpz_get_ui(ggcd));
    }

    mpz_set_ui(gdivisor, 1000000007U);
    mpz_powm_ui(gpow, ga, 65537U, gdivisor);
    CHECK(xray_bigint_powmod_u32(&a, 65537U, 1000000007U) == (uint32_t)mpz_get_ui(gpow));

    for (size_t j = 0; values[j]; ++j) {
      CHECK(xray_bigint_set_decimal(&b, values[j]));
      CHECK(mpz_set_str(gb, values[j], 10) == 0);

      CHECK(xray_bigint_add(&out, &a, &b));
      mpz_add(gout, ga, gb);
      check_scratch_matches_mpz(&out, gout);

      CHECK(xray_bigint_mul(&out, &a, &b));
      mpz_mul(gout, ga, gb);
      check_scratch_matches_mpz(&out, gout);

      if (mpz_cmp(ga, gb) >= 0) {
        CHECK(xray_bigint_sub(&out, &a, &b));
        mpz_sub(gout, ga, gb);
        check_scratch_matches_mpz(&out, gout);
      } else {
        CHECK(!xray_bigint_sub(&out, &a, &b));
      }
    }
  }

  mpz_clears(ga, gb, gout, gquotient, gdivisor, ggcd, gpow, NULL);
  xray_bigint_clear(&a);
  xray_bigint_clear(&b);
  xray_bigint_clear(&out);
  xray_bigint_clear(&quotient);
}

static void test_ambiguous_input_rejected(void) {
  mpz_t value;
  mpz_init(value);
  char *error = NULL;
  CHECK(!xray_parse_integer("1.23e9", value, NULL, &error));
  CHECK(error != NULL);
  free(error);
  mpz_clear(value);
}

static void test_factor_solver_exact(void) {
  XrayFactorConfig config = xray_factor_default_config();
  config.time_budget_ms = 1000;
  XrayFactorReport report;
  CHECK(xray_factor_solve("10403", &config, &report));
  CHECK(strcmp(report.status, "solved") == 0);
  CHECK(report.product_verified);
  CHECK(report.accounting_verified);
  CHECK(report.factor_count == 2);
  xray_factor_report_clear(&report);
}

static void test_factor_solver_unresolved_budget(void) {
  XrayFactorConfig config = xray_factor_default_config();
  config.trial_limit = 50;
  config.fermat_iterations = 0;
  config.rho_iterations = 0;
  config.pm1_bound = 0;
  config.brent_iterations = 0;
  config.time_budget_ms = 1000;
  XrayFactorReport report;
  CHECK(xray_factor_solve("10403", &config, &report));
  CHECK(strcmp(report.status, "unsolved") == 0);
  CHECK(!report.product_verified);
  CHECK(report.accounting_verified);
  CHECK(report.unresolved_count == 1);
  xray_factor_report_clear(&report);
}

static void test_rho_and_prime_power(void) {
  XrayFactorConfig config = xray_factor_default_config();
  config.time_budget_ms = 1000;
  XrayFactorReport rho;
  CHECK(xray_factor_solve("8051", &config, &rho));
  CHECK(strcmp(rho.status, "solved") == 0);
  CHECK(rho.product_verified);
  xray_factor_report_clear(&rho);

  XrayFactorReport power;
  CHECK(xray_factor_solve("2187", &config, &power));
  CHECK(strcmp(power.status, "solved") == 0);
  CHECK(power.product_verified);
  xray_factor_report_clear(&power);
}

static void test_stronger_factor_methods(void) {
  mpz_t value, factor, cofactor;
  mpz_inits(value, factor, cofactor, NULL);
  mpz_set_ui(value, 8051);
  CHECK(xray_brent_rho_factor(factor, cofactor, value, 5000));
  CHECK(mpz_cmp_ui(factor, 1) > 0);
  CHECK(mpz_cmp_ui(cofactor, 1) > 0);
  mpz_mul(factor, factor, cofactor);
  CHECK(mpz_cmp(factor, value) == 0);

  mpz_set_ui(value, 8051);
  CHECK(xray_pollard_pm1_factor(factor, cofactor, value, 32));
  mpz_mul(factor, factor, cofactor);
  CHECK(mpz_cmp(factor, value) == 0);
  mpz_clears(value, factor, cofactor, NULL);
}

static void test_cyclotomic_known_values(void) {
  mpz_t base, value, expected;
  mpz_inits(base, value, expected, NULL);
  mpz_set_ui(base, 10);
  mpz_set_ui(expected, 111);
  CHECK(xray_cyclotomic_eval_ui(value, 3, base));
  CHECK(mpz_cmp(value, expected) == 0);

  mpz_set_ui(base, 2);
  mpz_set_ui(expected, 31);
  CHECK(xray_cyclotomic_eval_ui(value, 5, base));
  CHECK(mpz_cmp(value, expected) == 0);

  mpz_set_ui(expected, 17);
  CHECK(xray_cyclotomic_eval_ui(value, 8, base));
  CHECK(mpz_cmp(value, expected) == 0);
  mpz_clears(base, value, expected, NULL);
}

static void test_workspace_and_gnfs_artifacts(void) {
  XrayRunConfig config = xray_run_default_config();
  snprintf(config.workspace_root, sizeof(config.workspace_root), "native-test-runs");
  config.enable_benchmark = 0;
  XrayWorkbenchReport report;
  CHECK(xray_workbench_run("2^12 + 1", &config, &report));
  CHECK(report.expression.ok);
  CHECK(strcmp(report.expression.normalized, "4097") == 0);
  CHECK(report.run_dir != NULL);
  CHECK(report.json != NULL);
  CHECK(report.cpu.logical_cpus >= 1);
  CHECK(report.gnfs.stage_count == 7);
  CHECK(strstr(report.json, "\"expression\"") != NULL);
  CHECK(strstr(report.json, "\"cpu\"") != NULL);
  CHECK(strstr(report.json, "\"avx\"") != NULL);
  CHECK(strstr(report.json, "\"gnfsReport\"") != NULL);
  CHECK(strstr(report.events_jsonl, "\"stage\":\"benchmark\",\"status\":\"skipped\"") != NULL);
  CHECK(strstr(report.events_jsonl, "\"stage\":\"cpu\",\"status\":\"profiled\"") != NULL);
  xray_workbench_report_clear(&report);
}

static void test_cyclotomic_scan_exact(void) {
  XrayCyclotomicConfig config = xray_cyclotomic_default_config();
  config.n_min = 3;
  config.n_max = 12;
  config.base_window = 1;
  XrayCyclotomicReport report;
  CHECK(xray_cyclotomic_scan("111", &config, &report));
  CHECK(report.candidate_count > 0);
  CHECK(report.exact_matches > 0);
  CHECK(report.candidates[0].exact_match);
  xray_cyclotomic_report_clear(&report);
}

static void test_large_nonhit_does_not_false_solve(void) {
  const char *rsa260 = "22112825529529666435281085255026230927612089502470015394413748319128822941402001986512729726569746599085900330031400051170742204560859276357953757185954298838958709229238491006703034124620545784566413664540684214361293017694020846391065875914794251435144458199";
  XrayFactorConfig config = xray_factor_default_config();
  config.time_budget_ms = 100;
  config.trial_limit = 1000;
  config.fermat_iterations = 10;
  config.rho_iterations = 10;
  XrayFactorReport report;
  CHECK(xray_factor_solve(rsa260, &config, &report));
  CHECK(strcmp(report.status, "solved") != 0);
  CHECK(!report.product_verified);
  CHECK(report.accounting_verified);
  xray_factor_report_clear(&report);
}

static void test_benchmarks(void) {
  XrayRunConfig config = xray_run_default_config();
  snprintf(config.workspace_root, sizeof(config.workspace_root), "native-test-runs");
  config.enable_factor = 0;
  config.enable_cyclotomic = 0;
  config.enable_gnfs_stage_proof = 0;
  config.enable_benchmark = 1;
  XrayWorkbenchReport workbench;
  CHECK(xray_workbench_run("10403", &config, &workbench));
  XrayBenchmarkReport *report = &workbench.benchmark;
  CHECK(workbench.cpu.logical_cpus >= 1);
  CHECK(report->cpu.logical_cpus >= 1);
  CHECK(report->result_count >= 32);
  CHECK(report->passed_count == report->result_count);
  size_t scratch_rows = 0;
  size_t kernel_rows = 0;
  size_t replacement_ready_rows = 0;
  size_t oracle_only_rows = 0;
  size_t blocked_rows = 0;
  for (size_t index = 0; index < report->result_count; ++index) {
    if (strcmp(report->results[index].category, "scratch-vs-gmp") == 0) {
      scratch_rows++;
      CHECK(report->results[index].parity_verified);
      CHECK(report->results[index].scratch_us > 0);
      CHECK(report->results[index].gmp_us > 0);
      CHECK(report->results[index].speed_ratio > 0.0);
      CHECK(report->results[index].max_allowed_speed_ratio == 1.0);
      CHECK(strstr(report->results[index].detail, "ratioMethod=paired-median") != NULL);
      const char *adoption = xray_scratch_adoption_for_result(&report->results[index]);
      CHECK(strcmp(report->results[index].adoption, adoption) == 0);
      CHECK(report->results[index].replacement_ready == (strcmp(adoption, "allowed") == 0));
      if (strcmp(adoption, "allowed") == 0) replacement_ready_rows++;
      else if (strcmp(adoption, "oracle-only") == 0) oracle_only_rows++;
      else blocked_rows++;
    } else if (strcmp(report->results[index].category, "kernel-probe") == 0) {
      kernel_rows++;
      CHECK(report->results[index].passed);
      CHECK(report->results[index].scratch_us > 0);
      CHECK(report->results[index].gmp_us > 0);
      CHECK(report->results[index].speed_ratio > 0.0);
      CHECK(report->results[index].max_allowed_speed_ratio == 0.98);
      CHECK(strstr(report->results[index].detail, "ratioMethod=paired-median") != NULL);
      CHECK(strstr(report->results[index].detail, "featureGate=") != NULL);
      CHECK(strstr(report->results[index].detail, "gmpClue=") != NULL);
      CHECK(strstr(report->results[index].adoption, "promote-candidate") != NULL ||
        strstr(report->results[index].adoption, "observe-only") != NULL ||
        strstr(report->results[index].adoption, "blocked-output-mismatch") != NULL);
    }
  }
  XrayBenchmarkResult mismatch;
  memset(&mismatch, 0, sizeof(mismatch));
  CHECK(strcmp(xray_scratch_adoption_for_result(&mismatch), "blocked-output-mismatch") == 0);
  CHECK(scratch_rows >= 24);
  CHECK(kernel_rows >= 4);
  CHECK(report->scratch_count == scratch_rows);
  CHECK(report->replacement_ready_count == replacement_ready_rows);
  CHECK(report->oracle_only_count == oracle_only_rows);
  CHECK(report->blocked_count == blocked_rows);
  CHECK(report->scratch_count == report->replacement_ready_count + report->oracle_only_count + report->blocked_count);
  char *json = xray_benchmark_report_json(report);
  CHECK(json != NULL);
  CHECK(strstr(json, "\"replacementReady\"") != NULL);
  CHECK(strstr(json, "\"cpu\"") != NULL);
  CHECK(strstr(json, "kernel-probe") != NULL);
  CHECK(strstr(json, "\"avx\"") != NULL);
  CHECK(strstr(json, "\"avx2\"") != NULL);
  CHECK(strstr(json, "\"scratchRows\"") != NULL);
  CHECK(strstr(json, "\"replacementReadyRows\"") != NULL);
  CHECK(strstr(json, "\"oracleOnlyRows\"") != NULL);
  CHECK(strstr(json, "\"blockedRows\"") != NULL);
  CHECK(strstr(json, "\"adoption\"") != NULL);
  CHECK(strstr(json, "\"maxAllowedSpeedRatio\"") != NULL);
  CHECK(strstr(json, "\"scratchUs\"") != NULL);
  free(json);
  char *tsv = xray_benchmark_report_tsv(report);
  CHECK(tsv != NULL);
  CHECK(strstr(tsv, "category\tname\toperation") != NULL);
  CHECK(strstr(tsv, "factor-benchmark") != NULL);
  CHECK(strstr(tsv, "cyclotomic-benchmark") != NULL);
  CHECK(strstr(tsv, "scratch-vs-gmp") != NULL);
  CHECK(strstr(tsv, "kernel-probe") != NULL);
  CHECK(strstr(tsv, "gmpClue=") != NULL);
  CHECK(strstr(tsv, "replacement-ready") != NULL || strstr(tsv, "parity") != NULL);
  free(tsv);

  CHECK(workbench.run_dir != NULL);
  CHECK(workbench.events_jsonl != NULL);
  CHECK(strstr(workbench.events_jsonl, "\"stage\":\"benchmark\"") != NULL);
  CHECK(strstr(workbench.events_jsonl, "\"stage\":\"cpu\"") != NULL);
  char *benchmark_json_path = test_path_join(workbench.run_dir, "benchmark.json");
  char *benchmark_tsv_path = test_path_join(workbench.run_dir, "benchmark.tsv");
  char *cpu_path = test_path_join(workbench.run_dir, "cpu_features.txt");
  char *benchmark_json = read_text_file(benchmark_json_path);
  char *benchmark_tsv = read_text_file(benchmark_tsv_path);
  char *cpu_text = read_text_file(cpu_path);
  CHECK(strstr(benchmark_json, "\"benchmarkReport\"") != NULL);
  CHECK(strstr(benchmark_json, "\"cpu\"") != NULL);
  CHECK(strstr(benchmark_json, "\"scratchRows\"") != NULL);
  CHECK(strstr(benchmark_tsv, "scratch-vs-gmp") != NULL);
  CHECK(strstr(benchmark_tsv, "kernel-probe") != NULL);
  CHECK(strstr(benchmark_tsv, "speedRatio") != NULL);
  CHECK(strstr(benchmark_tsv, "ratioMethod=paired-median") != NULL);
  CHECK(strstr(cpu_text, "CPU:") != NULL);
  CHECK(strstr(cpu_text, "flags=") != NULL);
  free(benchmark_json_path);
  free(benchmark_tsv_path);
  free(cpu_path);
  free(benchmark_json);
  free(benchmark_tsv);
  free(cpu_text);
  xray_workbench_report_clear(&workbench);
}

int main(void) {
  test_parse_messy_input();
  test_exact_expression_parser();
  test_cpu_feature_detection();
  test_scratch_bigint_oracle();
  test_scratch_bigint_oracle_sweep();
  test_ambiguous_input_rejected();
  test_factor_solver_exact();
  test_factor_solver_unresolved_budget();
  test_rho_and_prime_power();
  test_stronger_factor_methods();
  test_cyclotomic_known_values();
  test_cyclotomic_scan_exact();
  test_workspace_and_gnfs_artifacts();
  test_large_nonhit_does_not_false_solve();
  test_benchmarks();
  puts("native xray tests passed");
  return 0;
}
