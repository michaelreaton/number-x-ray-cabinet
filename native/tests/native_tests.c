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
  XrayScratchBigInt a, b, sum, product;
  xray_bigint_init(&a);
  xray_bigint_init(&b);
  xray_bigint_init(&sum);
  xray_bigint_init(&product);
  CHECK(xray_bigint_set_decimal(&a, "123456789012345678901234567890"));
  CHECK(xray_bigint_set_decimal(&b, "98765432109876543210"));
  CHECK(xray_bigint_add(&sum, &a, &b));
  CHECK(xray_bigint_mul(&product, &a, &b));

  char *sum_text = xray_bigint_get_decimal(&sum);
  char *product_text = xray_bigint_get_decimal(&product);
  mpz_t ga, gb, gsum, gproduct;
  mpz_inits(ga, gb, gsum, gproduct, NULL);
  mpz_set_str(ga, "123456789012345678901234567890", 10);
  mpz_set_str(gb, "98765432109876543210", 10);
  mpz_add(gsum, ga, gb);
  mpz_mul(gproduct, ga, gb);
  char *oracle_sum = mpz_get_str(NULL, 10, gsum);
  char *oracle_product = mpz_get_str(NULL, 10, gproduct);
  CHECK(strcmp(sum_text, oracle_sum) == 0);
  CHECK(strcmp(product_text, oracle_product) == 0);
  free(sum_text);
  free(product_text);
  free(oracle_sum);
  free(oracle_product);
  mpz_clears(ga, gb, gsum, gproduct, NULL);
  xray_bigint_clear(&a);
  xray_bigint_clear(&b);
  xray_bigint_clear(&sum);
  xray_bigint_clear(&product);
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
  CHECK(report.result_count >= 9);
  CHECK(report.passed_count == report.result_count);
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
