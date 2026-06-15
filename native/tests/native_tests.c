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

static void test_scratch_bigint_oracle(void) {
  XrayScratchBigInt a, b, sum, difference, product, quotient;
  xray_bigint_init(&a);
  xray_bigint_init(&b);
  xray_bigint_init(&sum);
  xray_bigint_init(&difference);
  xray_bigint_init(&product);
  xray_bigint_init(&quotient);
  CHECK(xray_bigint_set_decimal(&a, "123456789012345678901234567890"));
  CHECK(xray_bigint_set_decimal(&b, "98765432109876543210"));
  CHECK(xray_bigint_set_decimal(&quotient, "000,123_456 789"));
  char *messy_text = xray_bigint_get_decimal(&quotient);
  CHECK(strcmp(messy_text, "123456789") == 0);
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
  mpz_clears(ga, gb, gsum, gdifference, gproduct, gquotient, gmodulus, ggcd, gpow, gexponent, NULL);
  xray_bigint_clear(&a);
  xray_bigint_clear(&b);
  xray_bigint_clear(&sum);
  xray_bigint_clear(&difference);
  xray_bigint_clear(&product);
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
  CHECK(report.gnfs.stage_count == 7);
  CHECK(strstr(report.json, "\"expression\"") != NULL);
  CHECK(strstr(report.json, "\"gnfsReport\"") != NULL);
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
  XrayBenchmarkReport report;
  CHECK(xray_benchmark_run(&report));
  CHECK(report.result_count >= 32);
  CHECK(report.passed_count == report.result_count);
  size_t scratch_rows = 0;
  size_t replacement_ready_rows = 0;
  size_t oracle_only_rows = 0;
  size_t blocked_rows = 0;
  for (size_t index = 0; index < report.result_count; ++index) {
    if (strcmp(report.results[index].category, "scratch-vs-gmp") == 0) {
      scratch_rows++;
      CHECK(report.results[index].parity_verified);
      CHECK(report.results[index].scratch_us > 0);
      CHECK(report.results[index].gmp_us > 0);
      CHECK(report.results[index].speed_ratio > 0.0);
      CHECK(report.results[index].max_allowed_speed_ratio == 1.0);
      const char *adoption = xray_scratch_adoption_for_result(&report.results[index]);
      CHECK(strcmp(report.results[index].adoption, adoption) == 0);
      CHECK(report.results[index].replacement_ready == (strcmp(adoption, "allowed") == 0));
      if (strcmp(adoption, "allowed") == 0) replacement_ready_rows++;
      else if (strcmp(adoption, "oracle-only") == 0) oracle_only_rows++;
      else blocked_rows++;
    }
  }
  XrayBenchmarkResult mismatch;
  memset(&mismatch, 0, sizeof(mismatch));
  CHECK(strcmp(xray_scratch_adoption_for_result(&mismatch), "blocked-output-mismatch") == 0);
  CHECK(scratch_rows >= 23);
  CHECK(report.scratch_count == scratch_rows);
  CHECK(report.replacement_ready_count == replacement_ready_rows);
  CHECK(report.oracle_only_count == oracle_only_rows);
  CHECK(report.blocked_count == blocked_rows);
  CHECK(report.scratch_count == report.replacement_ready_count + report.oracle_only_count + report.blocked_count);
  char *json = xray_benchmark_report_json(&report);
  CHECK(json != NULL);
  CHECK(strstr(json, "\"replacementReady\"") != NULL);
  CHECK(strstr(json, "\"scratchRows\"") != NULL);
  CHECK(strstr(json, "\"replacementReadyRows\"") != NULL);
  CHECK(strstr(json, "\"oracleOnlyRows\"") != NULL);
  CHECK(strstr(json, "\"blockedRows\"") != NULL);
  CHECK(strstr(json, "\"adoption\"") != NULL);
  CHECK(strstr(json, "\"maxAllowedSpeedRatio\"") != NULL);
  CHECK(strstr(json, "\"scratchUs\"") != NULL);
  free(json);
  xray_benchmark_report_clear(&report);
}

int main(void) {
  test_parse_messy_input();
  test_exact_expression_parser();
  test_scratch_bigint_oracle();
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
