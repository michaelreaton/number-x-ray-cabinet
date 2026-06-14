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

  report->elapsed_ms = xray_now_ms() - started;
  return 1;
}

void xray_benchmark_report_clear(XrayBenchmarkReport *report) {
  if (!report) return;
  free(report->results);
  memset(report, 0, sizeof(*report));
}
