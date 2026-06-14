#ifndef XRAY_WORKBENCH_H
#define XRAY_WORKBENCH_H

#include <stddef.h>
#include <gmp.h>

#ifdef __cplusplus
extern "C" {
#endif

#define XRAY_VERSION "0.1.0-native-proof"

typedef struct XrayFactorConfig {
  unsigned long trial_limit;
  unsigned long fermat_iterations;
  unsigned long rho_iterations;
  unsigned long max_passes;
  unsigned long time_budget_ms;
  const volatile int *cancel_flag;
} XrayFactorConfig;

typedef struct XrayFactorRecord {
  char *value;
  unsigned int exponent;
  int probable_prime;
  char methods[128];
} XrayFactorRecord;

typedef struct XrayUnresolvedRecord {
  char *value;
  size_t digits;
  int probable_prime;
  int deferred;
  char note[128];
} XrayUnresolvedRecord;

typedef struct XrayFactorStep {
  char method[32];
  char status[32];
  char target_preview[80];
  char detail[160];
  unsigned long elapsed_ms;
} XrayFactorStep;

typedef struct XrayFactorReport {
  char *input;
  char status[24];
  size_t digits;
  size_t bit_length;
  int product_verified;
  int accounting_verified;
  int timed_out;
  int cancelled;
  unsigned long elapsed_ms;
  XrayFactorConfig config;
  XrayFactorRecord *factors;
  size_t factor_count;
  XrayUnresolvedRecord *unresolved;
  size_t unresolved_count;
  XrayFactorStep *steps;
  size_t step_count;
} XrayFactorReport;

typedef struct XrayCyclotomicConfig {
  unsigned int n_min;
  unsigned int n_max;
  unsigned int base_window;
  unsigned int report_limit;
  unsigned long time_budget_ms;
} XrayCyclotomicConfig;

typedef struct XrayCyclotomicCandidate {
  unsigned int n;
  unsigned int phi;
  char *base;
  double score;
  int exact_match;
  char verdict[32];
  char difference_preview[80];
} XrayCyclotomicCandidate;

typedef struct XrayCyclotomicReport {
  char *input;
  XrayCyclotomicConfig config;
  XrayCyclotomicCandidate *candidates;
  size_t candidate_count;
  size_t scanned;
  size_t exact_matches;
  int timed_out;
  unsigned long elapsed_ms;
} XrayCyclotomicReport;

typedef struct XrayBenchmarkResult {
  char name[64];
  char status[24];
  unsigned long elapsed_ms;
  int passed;
  char detail[192];
} XrayBenchmarkResult;

typedef struct XrayBenchmarkReport {
  XrayBenchmarkResult *results;
  size_t result_count;
  size_t passed_count;
  unsigned long elapsed_ms;
} XrayBenchmarkReport;

typedef struct XrayWorkbenchReport {
  XrayFactorReport factor;
  XrayCyclotomicReport cyclotomic;
  XrayBenchmarkReport benchmark;
  char *source_notes;
} XrayWorkbenchReport;

XrayFactorConfig xray_factor_default_config(void);
XrayCyclotomicConfig xray_cyclotomic_default_config(void);

int xray_parse_integer(const char *raw, mpz_t out, char **normalized, char **error_message);
char *xray_preview_decimal(const mpz_t value, size_t max_chars);
unsigned long xray_now_ms(void);

int xray_is_probable_prime(const mpz_t value, int rounds);
int xray_integer_nth_root(mpz_t root, const mpz_t value, unsigned long n);
int xray_small_factor(mpz_t factor, const mpz_t value, unsigned long limit);
int xray_fermat_factor(mpz_t factor, mpz_t cofactor, const mpz_t value, unsigned long iterations);
int xray_pollard_rho_factor(mpz_t factor, mpz_t cofactor, const mpz_t value, unsigned long iterations);
int xray_perfect_power_factor(mpz_t base, unsigned long *exponent, const mpz_t value, unsigned long max_exponent);

int xray_factor_solve(const char *raw_input, const XrayFactorConfig *config, XrayFactorReport *report);
void xray_factor_report_clear(XrayFactorReport *report);
char *xray_factor_report_json(const XrayFactorReport *report);

unsigned int xray_phi_ui(unsigned int n);
int xray_cyclotomic_eval_ui(mpz_t out, unsigned int n, const mpz_t base);
int xray_cyclotomic_scan(const char *raw_input, const XrayCyclotomicConfig *config, XrayCyclotomicReport *report);
void xray_cyclotomic_report_clear(XrayCyclotomicReport *report);
char *xray_cyclotomic_report_json(const XrayCyclotomicReport *report);

int xray_benchmark_run(XrayBenchmarkReport *report);
void xray_benchmark_report_clear(XrayBenchmarkReport *report);
char *xray_benchmark_report_json(const XrayBenchmarkReport *report);

char *xray_workbench_report_json(const XrayFactorReport *factor, const XrayCyclotomicReport *cyclotomic, const XrayBenchmarkReport *benchmark);

#ifdef __cplusplus
}
#endif

#endif
