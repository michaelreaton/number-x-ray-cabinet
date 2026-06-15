#ifndef XRAY_WORKBENCH_H
#define XRAY_WORKBENCH_H

#include <stddef.h>
#include <stdint.h>
#include <gmp.h>

#ifdef __cplusplus
extern "C" {
#endif

#define XRAY_VERSION "0.1.0-native-proof"

typedef struct XrayScratchBigInt {
  uint64_t *limbs;
  size_t count;
  size_t capacity;
} XrayScratchBigInt;

void xray_bigint_init(XrayScratchBigInt *value);
void xray_bigint_clear(XrayScratchBigInt *value);
int xray_bigint_set_decimal(XrayScratchBigInt *value, const char *decimal);
char *xray_bigint_get_decimal(const XrayScratchBigInt *value);
int xray_bigint_is_zero(const XrayScratchBigInt *value);
int xray_bigint_copy(XrayScratchBigInt *out, const XrayScratchBigInt *value);
int xray_bigint_add(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right);
int xray_bigint_sub(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right);
int xray_bigint_mul(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right);
int xray_bigint_square(XrayScratchBigInt *out, const XrayScratchBigInt *value);
int xray_bigint_square_karatsuba_probe(XrayScratchBigInt *out, const XrayScratchBigInt *value, size_t threshold);
int xray_bigint_mul_with_threshold(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right, size_t threshold);
int xray_bigint_mul_toom3_probe(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right, size_t leaf_threshold);
int xray_bigint_mul_toom3_unroll4_probe(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right, size_t leaf_threshold);
int xray_bigint_mul_toom3_unroll4_recursive_probe(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right, size_t leaf_threshold, size_t depth_limit);
int xray_bigint_mul_unroll4_probe(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right, size_t leaf_threshold);
int xray_bigint_compare(const XrayScratchBigInt *left, const XrayScratchBigInt *right);
uint32_t xray_bigint_mod_u32(const XrayScratchBigInt *value, uint32_t modulus);
int xray_bigint_divmod_u32(XrayScratchBigInt *quotient, uint32_t *remainder, const XrayScratchBigInt *value, uint32_t divisor);
uint32_t xray_bigint_gcd_u32(const XrayScratchBigInt *value, uint32_t other);
uint32_t xray_bigint_powmod_u32(const XrayScratchBigInt *base, uint32_t exponent, uint32_t modulus);

typedef struct XrayFactorConfig {
  unsigned long trial_limit;
  unsigned long fermat_iterations;
  unsigned long rho_iterations;
  unsigned long pm1_bound;
  unsigned long brent_iterations;
  unsigned long max_passes;
  unsigned long time_budget_ms;
  const volatile int *cancel_flag;
} XrayFactorConfig;

typedef struct XrayExpressionResult {
  char *raw;
  char *normalized;
  char *error;
  size_t digits;
  size_t bit_length;
  int ok;
} XrayExpressionResult;

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
  char category[32];
  char operation[32];
  char status[24];
  size_t digits;
  unsigned long long scratch_us;
  unsigned long long gmp_us;
  double speed_ratio;
  double max_allowed_speed_ratio;
  double worst_pair_ratio;
  size_t stable_sample_count;
  size_t sample_count;
  char adoption[32];
  int parity_verified;
  int replacement_ready;
  unsigned long elapsed_ms;
  int passed;
  char detail[384];
} XrayBenchmarkResult;

typedef struct XrayCpuFeatures {
  char architecture[24];
  char vendor[32];
  char brand[96];
  unsigned int logical_cpus;
  int cpuid_supported;
  int sse2;
  int sse3;
  int ssse3;
  int sse41;
  int sse42;
  int pclmulqdq;
  int popcnt;
  int aes;
  int fma;
  int xsave;
  int osxsave;
  unsigned long long xcr0;
  int avx_os_enabled;
  int avx512_os_enabled;
  int avx;
  int avx2;
  int avx512f;
  int avx512dq;
  int avx512ifma;
  int avx512bw;
  int avx512vl;
  int vaes;
  int vpclmulqdq;
  int bmi1;
  int bmi2;
  int adx;
} XrayCpuFeatures;

typedef struct XrayBenchmarkReport {
  XrayCpuFeatures cpu;
  XrayBenchmarkResult *results;
  size_t result_count;
  size_t passed_count;
  size_t scratch_count;
  size_t replacement_ready_count;
  size_t oracle_only_count;
  size_t blocked_count;
  unsigned long elapsed_ms;
} XrayBenchmarkReport;

typedef struct XrayGnfsStage {
  char name[48];
  char status[32];
  char artifact[192];
  char detail[240];
  unsigned long elapsed_ms;
} XrayGnfsStage;

typedef struct XrayGnfsReport {
  char *input;
  char *run_dir;
  char status[32];
  XrayGnfsStage *stages;
  size_t stage_count;
  unsigned long elapsed_ms;
} XrayGnfsReport;

typedef struct XrayRunConfig {
  XrayFactorConfig factor;
  XrayCyclotomicConfig cyclotomic;
  int enable_factor;
  int enable_cyclotomic;
  int enable_benchmark;
  int enable_gnfs_stage_proof;
  unsigned int threads;
  unsigned long memory_mb;
  char scan_depth[32];
  char proof_strategy[64];
  char primality_mode[64];
  char workspace_root[260];
  const volatile int *cancel_flag;
} XrayRunConfig;

typedef struct XrayWorkbenchReport {
  XrayCpuFeatures cpu;
  XrayExpressionResult expression;
  XrayFactorReport factor;
  XrayCyclotomicReport cyclotomic;
  XrayBenchmarkReport benchmark;
  XrayGnfsReport gnfs;
  char *run_dir;
  char *json;
  char *events_jsonl;
  char *source_notes;
} XrayWorkbenchReport;

XrayFactorConfig xray_factor_default_config(void);
XrayCyclotomicConfig xray_cyclotomic_default_config(void);
XrayRunConfig xray_run_default_config(void);

int xray_parse_integer(const char *raw, mpz_t out, char **normalized, char **error_message);
int xray_evaluate_expression(const char *raw, mpz_t out, XrayExpressionResult *result);
void xray_expression_result_clear(XrayExpressionResult *result);
char *xray_preview_decimal(const mpz_t value, size_t max_chars);
unsigned long xray_now_ms(void);
unsigned long long xray_now_us(void);
void xray_cpu_features_detect(XrayCpuFeatures *features);
char *xray_cpu_features_summary(const XrayCpuFeatures *features);

int xray_is_probable_prime(const mpz_t value, int rounds);
int xray_integer_nth_root(mpz_t root, const mpz_t value, unsigned long n);
int xray_small_factor(mpz_t factor, const mpz_t value, unsigned long limit);
int xray_fermat_factor(mpz_t factor, mpz_t cofactor, const mpz_t value, unsigned long iterations);
int xray_pollard_rho_factor(mpz_t factor, mpz_t cofactor, const mpz_t value, unsigned long iterations);
int xray_brent_rho_factor(mpz_t factor, mpz_t cofactor, const mpz_t value, unsigned long iterations);
int xray_pollard_pm1_factor(mpz_t factor, mpz_t cofactor, const mpz_t value, unsigned long bound);
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
char *xray_benchmark_report_tsv(const XrayBenchmarkReport *report);
const char *xray_scratch_adoption_for_result(const XrayBenchmarkResult *result);

int xray_gnfs_stage_proof(const char *normalized_input, const char *run_dir, XrayGnfsReport *report);
void xray_gnfs_report_clear(XrayGnfsReport *report);

int xray_workbench_run(const char *raw_input, const XrayRunConfig *config, XrayWorkbenchReport *report);
void xray_workbench_report_clear(XrayWorkbenchReport *report);
char *xray_workbench_full_report_json(const XrayWorkbenchReport *report);
char *xray_workbench_report_json(const XrayFactorReport *factor, const XrayCyclotomicReport *cyclotomic, const XrayBenchmarkReport *benchmark);

#ifdef __cplusplus
}
#endif

#endif
