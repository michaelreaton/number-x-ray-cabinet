#ifndef XRAY_WORKBENCH_H
#define XRAY_WORKBENCH_H

#include <stddef.h>
#include <stdint.h>
#include <gmp.h>

#ifdef __cplusplus
extern "C" {
#endif

#define XRAY_VERSION "0.1.0-native-proof"
#define XRAY_ABI_VERSION 1u

#ifndef XRAY_API
#if defined(_WIN32) && defined(XRAY_SHARED)
#if defined(XRAY_BUILDING_LIBRARY)
#define XRAY_API __declspec(dllexport)
#else
#define XRAY_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) && defined(XRAY_SHARED)
#define XRAY_API __attribute__((visibility("default")))
#else
#define XRAY_API
#endif
#endif

typedef struct XrayScratchBigInt {
  uint64_t *limbs;
  size_t count;
  size_t capacity;
} XrayScratchBigInt;

typedef struct XrayBigIntRouteConfig {
  unsigned int word_bits;
  size_t karatsuba_threshold_limbs;
  size_t decimal_horner_min_limbs;
  size_t mul_unroll4_route_min_limbs;
  size_t mul_unroll4_route_max_limbs;
  int mul_unroll4_route_enabled;
  int msvc_uint128_helpers;
} XrayBigIntRouteConfig;

typedef struct XrayBigIntU32ModContext {
  uint32_t modulus;
  uint64_t reciprocal;
  int use_fermat_65537;
} XrayBigIntU32ModContext;

/**
 * Return the runtime Number X-Ray version string for the loaded library.
 *
 * The returned pointer is borrowed static storage and must not be freed.
 */
XRAY_API const char *xray_version(void);

/**
 * Return the runtime C ABI version for the loaded library.
 *
 * The ABI version changes when the exported C layout or calling contract makes
 * a backward-incompatible change. The current ABI is 1.
 */
XRAY_API unsigned int xray_abi_version(void);

/**
 * Return the configured GMP-compatible bignum backend name.
 *
 * Typical values are "GMP" on Linux/macOS and "MPIR" on Windows vcpkg builds.
 * The returned pointer is borrowed static storage and must not be freed.
 */
XRAY_API const char *xray_bignum_backend_name(void);

/**
 * Return the configured GMP-compatible backend runtime version string.
 *
 * MPIR builds return the MPIR compile-time version when exposed by the
 * compatibility header. GMP builds return GMP_VERSION. The returned pointer is
 * borrowed static storage and must not be freed.
 */
XRAY_API const char *xray_bignum_backend_version(void);

/**
 * Return the configured GMP-compatible backend library basename.
 *
 * Examples include "mpir.lib" on Windows vcpkg MPIR and "libgmp.so" on many
 * Unix-like GMP builds. The returned pointer is borrowed static storage and
 * must not be freed.
 */
XRAY_API const char *xray_bignum_backend_library(void);

/**
 * Return compile-time scratch bigint production routing thresholds.
 *
 * Values are limb counts for the currently loaded library. Benchmark reports
 * and foreign-language bindings can use this to explain which scratch bigint
 * routes were eligible during a run, instead of assuming MPIR/GMP-compatible
 * baselines and Number X-Ray scratch routes are interchangeable.
 */
XRAY_API XrayBigIntRouteConfig xray_bigint_route_config(void);

/**
 * Release memory returned by Number X-Ray allocation-returning API calls.
 *
 * Pass pointers returned by functions such as xray_bigint_get_decimal(),
 * xray_preview_decimal(), xray_*_report_json(), and
 * xray_cpu_features_summary(). Passing NULL is allowed. Use this instead of
 * plain free() across shared-library or foreign-language boundaries.
 */
XRAY_API void xray_free(void *ptr);

/**
 * Initialize an empty scratch bigint.
 *
 * Call this before passing a value to any other xray_bigint_* function. The
 * initialized value represents zero and must eventually be released with
 * xray_bigint_clear().
 */
XRAY_API void xray_bigint_init(XrayScratchBigInt *value);

/**
 * Release memory owned by a scratch bigint and reset it to zero state.
 *
 * It is safe to call on a value initialized by xray_bigint_init(), including
 * after failed operations.
 */
XRAY_API void xray_bigint_clear(XrayScratchBigInt *value);

/**
 * Parse a non-negative decimal integer into a scratch bigint.
 *
 * The parser accepts common paste separators such as spaces, commas, and
 * underscores. It returns 1 on success and 0 for invalid text or allocation
 * failure. On success, any previous contents of value are replaced.
 */
XRAY_API int xray_bigint_set_decimal(XrayScratchBigInt *value, const char *decimal);

/**
 * Parse a non-negative decimal integer using a diagnostic chunk size.
 *
 * chunk_digits must be between 1 and 19. This probe exists for benchmark
 * tournaments and external tooling that wants to compare decimal ingestion
 * strategies without changing the production parser route.
 */
XRAY_API int xray_bigint_set_decimal_chunk_probe(XrayScratchBigInt *value, const char *decimal, unsigned int chunk_digits);

/**
 * Format a scratch bigint as a newly allocated decimal string.
 *
 * The caller owns the returned string and must release it with xray_free().
 * Returns NULL on allocation failure.
 */
XRAY_API char *xray_bigint_get_decimal(const XrayScratchBigInt *value);

/**
 * Return non-zero when value is exactly zero.
 */
XRAY_API int xray_bigint_is_zero(const XrayScratchBigInt *value);

/**
 * Copy value into out.
 *
 * out may alias value. Returns 1 on success and 0 on allocation failure.
 */
XRAY_API int xray_bigint_copy(XrayScratchBigInt *out, const XrayScratchBigInt *value);

/**
 * Compute out = left + right.
 *
 * out may alias either input. Returns 1 on success and 0 on allocation failure.
 */
XRAY_API int xray_bigint_add(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right);

/**
 * Compute out = left - right for non-negative results.
 *
 * out may alias either input. Returns 1 on success, or 0 if left < right or an
 * allocation fails.
 */
XRAY_API int xray_bigint_sub(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right);

/**
 * Compute out = left * right using the production scratch multiply route.
 *
 * out may alias either input. Returns 1 on success and 0 on allocation failure.
 */
XRAY_API int xray_bigint_mul(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right);

/**
 * Compute out = value * value using the production scratch square route.
 *
 * out may alias value. Returns 1 on success and 0 on allocation failure.
 */
XRAY_API int xray_bigint_square(XrayScratchBigInt *out, const XrayScratchBigInt *value);

/**
 * Add two non-negative decimal integers and return a decimal string.
 *
 * This convenience API is intended for FFI and plugin-style consumers that do
 * not want to manage XrayScratchBigInt storage directly. Inputs accept the same
 * separators as xray_bigint_set_decimal(). The caller owns the returned string
 * and must release it with xray_free(). Returns NULL for invalid input or
 * allocation failure.
 */
XRAY_API char *xray_bigint_add_decimal(const char *left_decimal, const char *right_decimal);

/**
 * Subtract two non-negative decimal integers and return a decimal string.
 *
 * Inputs accept the same separators as xray_bigint_set_decimal(). The caller
 * owns the returned string and must release it with xray_free(). Returns NULL
 * for invalid input, allocation failure, or a negative result.
 */
XRAY_API char *xray_bigint_sub_decimal(const char *left_decimal, const char *right_decimal);

/**
 * Multiply two non-negative decimal integers and return a decimal string.
 *
 * Inputs accept the same separators as xray_bigint_set_decimal(). The caller
 * owns the returned string and must release it with xray_free(). Returns NULL
 * for invalid input or allocation failure.
 */
XRAY_API char *xray_bigint_mul_decimal(const char *left_decimal, const char *right_decimal);

/**
 * Square a non-negative decimal integer and return a decimal string.
 *
 * The input accepts the same separators as xray_bigint_set_decimal(). The
 * caller owns the returned string and must release it with xray_free(). Returns
 * NULL for invalid input or allocation failure.
 */
XRAY_API char *xray_bigint_square_decimal(const char *decimal);

/**
 * Compare two non-negative decimal integers.
 *
 * Inputs accept the same separators as xray_bigint_set_decimal(). On success,
 * *comparison receives -1, 0, or 1 for left < right, left == right, or
 * left > right. Returns 1 on success and 0 for invalid input or a NULL
 * comparison pointer.
 */
XRAY_API int xray_bigint_compare_decimal(const char *left_decimal, const char *right_decimal, int *comparison);

/**
 * Precompute divisor state for repeated modulo operations by a 32-bit modulus.
 *
 * The context is plain caller-owned storage and can be reused across calls as
 * long as the modulus is unchanged. Returns 1 on success and 0 for a NULL
 * context or zero modulus.
 */
XRAY_API int xray_bigint_u32_mod_context_init(XrayBigIntU32ModContext *context, uint32_t modulus);

/**
 * Compute value mod context->modulus using precomputed divisor state.
 *
 * Returns 0 when value or context is NULL, when context->modulus is zero, or
 * when the mathematical remainder is zero.
 */
XRAY_API uint32_t xray_bigint_mod_u32_precomputed(const XrayScratchBigInt *value, const XrayBigIntU32ModContext *context);

/**
 * Compute gcd(value, context->modulus) using precomputed modulo state.
 *
 * Returns 0 when value or context is NULL or when context->modulus is zero.
 */
XRAY_API uint32_t xray_bigint_gcd_u32_precomputed(const XrayScratchBigInt *value, const XrayBigIntU32ModContext *context);

/**
 * Compute base^exponent mod context->modulus using precomputed reduction state.
 *
 * The precompute only applies to reducing the bigint base; the exponentiation
 * itself is over 32-bit residues. Returns 0 for invalid context or zero modulus.
 */
XRAY_API uint32_t xray_bigint_powmod_u32_precomputed(const XrayScratchBigInt *base, uint32_t exponent, const XrayBigIntU32ModContext *context);

/**
 * Compute a square with an explicit Karatsuba threshold for benchmarking.
 *
 * This is a diagnostic probe, not the stable production route. Returns 1 on
 * success and 0 on allocation failure.
 */
XRAY_API int xray_bigint_square_karatsuba_probe(XrayScratchBigInt *out, const XrayScratchBigInt *value, size_t threshold);

/**
 * Format value using an explicit Horner handoff threshold for benchmarking.
 *
 * The caller owns the returned string and must release it with xray_free().
 * This is a diagnostic probe and may not match the production route's threshold.
 */
XRAY_API char *xray_bigint_get_decimal_horner_threshold_probe(const XrayScratchBigInt *value, size_t horner_min_limbs);

/**
 * Format value while forcing or disabling the direct divider probe route.
 *
 * The caller owns the returned string and must release it with xray_free().
 * This is intended for benchmark comparisons.
 */
XRAY_API char *xray_bigint_get_decimal_divider_probe(const XrayScratchBigInt *value, int use_direct_divider);

/**
 * Format value through the folded 2^64-to-1e9 decimal chunk probe route.
 *
 * The caller owns the returned string and must release it with xray_free().
 * This diagnostic route compares an alternate constant-divisor decomposition
 * against the production formatter.
 */
XRAY_API char *xray_bigint_get_decimal_folded_probe(const XrayScratchBigInt *value);

/**
 * Format value through the pair-table decimal writer probe route.
 *
 * The caller owns the returned string and must release it with xray_free().
 * This diagnostic route uses the production chunk generator but emits decimal
 * text through two-digit lookup pairs for benchmark comparison.
 */
XRAY_API char *xray_bigint_get_decimal_pair_writer_probe(const XrayScratchBigInt *value);

/**
 * Format value through the folded chunk probe plus pair-table writer route.
 *
 * The caller owns the returned string and must release it with xray_free().
 * This diagnostic route combines the folded 2^64-to-1e9 chunk converter with
 * the two-digit lookup writer so benchmarks can separate arithmetic and text
 * emission costs.
 */
XRAY_API char *xray_bigint_get_decimal_folded_pair_writer_probe(const XrayScratchBigInt *value);

/**
 * Format value through the pair-table writer with a mixed 4+4 digit split.
 *
 * The caller owns the returned string and must release it with xray_free().
 * This diagnostic route uses production chunks and changes only text emission
 * for benchmark comparisons.
 */
XRAY_API char *xray_bigint_get_decimal_mixed_pair_writer_probe(const XrayScratchBigInt *value);

/**
 * Format value through the folded chunk route using plain 64-bit division.
 *
 * The caller owns the returned string and must release it with xray_free().
 * This diagnostic route tests whether constant reciprocal reduction or native
 * division is faster for folded decimal carry propagation on the local CPU.
 */
XRAY_API char *xray_bigint_get_decimal_folded_hwdiv_probe(const XrayScratchBigInt *value);

/**
 * Format value through folded-hwdiv chunks plus the mixed pair writer.
 *
 * The caller owns the returned string and must release it with xray_free().
 * This diagnostic route combines the folded-hwdiv chunk converter with the
 * mixed two-pair text emitter so benchmarks can isolate compound effects.
 */
XRAY_API char *xray_bigint_get_decimal_folded_hwdiv_mixed_pair_probe(const XrayScratchBigInt *value);

/**
 * Format value by repeatedly dividing a copy by 10^19.
 *
 * The caller owns the returned string and must release it with xray_free().
 * This diagnostic route mirrors the small/basecase shape used by MPIR/GMP's
 * mpn_get_str path: divide by the largest decimal power that fits one limb,
 * collect remainders, then emit padded chunks.
 */
XRAY_API char *xray_bigint_get_decimal_divide_1e19_probe(const XrayScratchBigInt *value);

/**
 * Format value through a divide-and-conquer decimal conversion probe.
 *
 * leaf_chunks controls when recursion falls back to repeated division by
 * 10^19. Pass 0 for the default leaf. The caller owns the returned string and
 * must release it with xray_free(). This diagnostic route uses per-call
 * precomputed powers of 10^19 and full scratch-bignum division; it exists to
 * test the larger MPIR/GMP mpn_get_str strategy without changing production
 * formatting.
 */
XRAY_API char *xray_bigint_get_decimal_dc_probe(const XrayScratchBigInt *value, size_t leaf_chunks);

/**
 * Format value through a divide-and-conquer decimal conversion probe using a
 * power-of-two table for 10^19 powers.
 *
 * leaf_chunks controls when recursion falls back to repeated division by
 * 10^19. Pass 0 for the default leaf. The caller owns the returned string and
 * must release it with xray_free(). This diagnostic route tests the MPIR/GMP
 * style B^1, B^2, B^4, ... precompute ladder against the linear power builder.
 */
XRAY_API char *xray_bigint_get_decimal_dc_ladder_probe(const XrayScratchBigInt *value, size_t leaf_chunks);

/**
 * Format value through a divide-and-conquer decimal conversion probe using a
 * small built-in power-of-two table for 10^19 powers.
 *
 * leaf_chunks controls when recursion falls back to repeated division by
 * 10^19. Pass 0 for the default leaf. The caller owns the returned string and
 * must release it with xray_free(). This diagnostic route tests static
 * precompute against the per-call ladder without changing production
 * formatting.
 */
XRAY_API char *xray_bigint_get_decimal_dc_static_ladder_probe(const XrayScratchBigInt *value, size_t leaf_chunks);

/**
 * Format value through a ladder-backed divide-and-conquer decimal conversion
 * probe that writes into one final output buffer.
 *
 * leaf_chunks controls when recursion falls back to repeated division by
 * 10^19. Pass 0 for the default leaf. The caller owns the returned string and
 * must release it with xray_free(). This diagnostic route tests whether
 * recursive allocation, concatenation, and padding copies are a material part
 * of the remaining large-format gap.
 */
XRAY_API char *xray_bigint_get_decimal_dc_direct_probe(const XrayScratchBigInt *value, size_t leaf_chunks);

/**
 * Format value through the direct-output divide-and-conquer probe using a small
 * built-in power-of-two table for 10^19 powers.
 *
 * leaf_chunks controls when recursion falls back to repeated division by
 * 10^19. Pass 0 for the default leaf. The caller owns the returned string and
 * must release it with xray_free(). This diagnostic route measures static
 * power precompute plus direct output buffering as one candidate.
 */
XRAY_API char *xray_bigint_get_decimal_dc_static_direct_probe(const XrayScratchBigInt *value, size_t leaf_chunks);

/**
 * Format value through the 19-digit decimal chunk probe route.
 *
 * The caller owns the returned string and must release it with xray_free().
 * This diagnostic route is intended for benchmark comparisons against the
 * production formatter before any wider decimal chunk policy is promoted.
 */
XRAY_API char *xray_bigint_get_decimal_wide_probe(const XrayScratchBigInt *value);

/**
 * Multiply with an explicit Karatsuba leaf threshold for benchmarking.
 *
 * out may alias either input. Returns 1 on success and 0 on allocation failure.
 */
XRAY_API int xray_bigint_mul_with_threshold(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right, size_t threshold);

/**
 * Multiply through the classic sum-middle Karatsuba probe route.
 *
 * The production Karatsuba route uses a difference-form middle product. This
 * diagnostic probe keeps the same threshold policy but computes
 * (a0 + a1) * (b0 + b1) - z0 - z2 so benchmarks can compare the middle-term
 * shape directly. out may alias either input. Returns 1 on success and 0 on
 * allocation failure.
 */
XRAY_API int xray_bigint_mul_karatsuba_sum_probe(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right, size_t threshold);

/**
 * Multiply through the bounded Toom-3 probe route.
 *
 * This is a benchmark/research probe. Returns 1 on success and 0 on allocation
 * failure or unsupported operand shape.
 */
XRAY_API int xray_bigint_mul_toom3_probe(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right, size_t leaf_threshold);

/**
 * Multiply through the Toom-3 probe with the unroll4 leaf schedule.
 *
 * This is a benchmark/research probe. Returns 1 on success and 0 on allocation
 * failure or unsupported operand shape.
 */
XRAY_API int xray_bigint_mul_toom3_unroll4_probe(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right, size_t leaf_threshold);

/**
 * Multiply through the recursive Toom-3/unroll4 probe route.
 *
 * depth_limit bounds recursion for benchmark safety. Returns 1 on success and
 * 0 on allocation failure or unsupported operand shape.
 */
XRAY_API int xray_bigint_mul_toom3_unroll4_recursive_probe(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right, size_t leaf_threshold, size_t depth_limit);

/**
 * Multiply with the unroll4 basecase probe route.
 *
 * This is a benchmark/research probe. Returns 1 on success and 0 on allocation
 * failure.
 */
XRAY_API int xray_bigint_mul_unroll4_probe(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right, size_t leaf_threshold);

/**
 * Compare two scratch bigints.
 *
 * Returns -1 when left < right, 0 when equal, and 1 when left > right.
 */
XRAY_API int xray_bigint_compare(const XrayScratchBigInt *left, const XrayScratchBigInt *right);

/**
 * Return value mod modulus for a 32-bit modulus.
 *
 * modulus must be non-zero.
 */
XRAY_API uint32_t xray_bigint_mod_u32(const XrayScratchBigInt *value, uint32_t modulus);

/**
 * Divide value by divisor and return both quotient and remainder.
 *
 * divisor must be non-zero. quotient may alias value. Returns 1 on success and
 * 0 on allocation failure or invalid divisor.
 */
XRAY_API int xray_bigint_divmod_u32(XrayScratchBigInt *quotient, uint32_t *remainder, const XrayScratchBigInt *value, uint32_t divisor);

/**
 * Return gcd(value, other) for a 32-bit other operand.
 */
XRAY_API uint32_t xray_bigint_gcd_u32(const XrayScratchBigInt *value, uint32_t other);

/**
 * Return base^exponent mod modulus for a scratch bigint base.
 *
 * modulus must be non-zero.
 */
XRAY_API uint32_t xray_bigint_powmod_u32(const XrayScratchBigInt *base, uint32_t exponent, uint32_t modulus);

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
  char detail[512];
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

/**
 * Return the default local factoring configuration.
 *
 * The returned structure is a value copy that callers may edit before passing
 * it to xray_factor_solve() or embedding it in XrayRunConfig.
 */
XRAY_API XrayFactorConfig xray_factor_default_config(void);

/**
 * Return the default cyclotomic scanner configuration.
 *
 * The returned structure is a value copy that callers may edit before passing
 * it to xray_cyclotomic_scan() or embedding it in XrayRunConfig.
 */
XRAY_API XrayCyclotomicConfig xray_cyclotomic_default_config(void);

/**
 * Return the default combined workbench run configuration.
 *
 * The returned structure enables the normal proof-workbench stages and may be
 * edited by callers before xray_workbench_run().
 */
XRAY_API XrayRunConfig xray_run_default_config(void);

/**
 * Parse messy decimal integer text into an initialized GMP integer.
 *
 * out must already be initialized with mpz_init(). On success, *normalized is a
 * newly allocated canonical decimal string that the caller must release with
 * xray_free(). On failure, *error_message is a newly allocated explanation that
 * the caller must release with xray_free(). Either output pointer may be NULL
 * if the caller does not need it.
 */
XRAY_API int xray_parse_integer(const char *raw, mpz_t out, char **normalized, char **error_message);

/**
 * Evaluate an exact integer expression into an initialized GMP integer.
 *
 * Supports decimal integers, exact arithmetic, powers, factorials, Fermat(),
 * Phi()/Cyclotomic(), and related aliases. out must already be initialized.
 * result receives allocated diagnostic fields and must be cleared with
 * xray_expression_result_clear().
 */
XRAY_API int xray_evaluate_expression(const char *raw, mpz_t out, XrayExpressionResult *result);

/**
 * Free all heap fields inside an expression result and reset it.
 */
XRAY_API void xray_expression_result_clear(XrayExpressionResult *result);

/**
 * Return a newly allocated shortened decimal preview of value.
 *
 * max_chars bounds the preview length. The caller owns the returned string and
 * must release it with xray_free().
 */
XRAY_API char *xray_preview_decimal(const mpz_t value, size_t max_chars);

/**
 * Return a monotonic-ish wall clock timestamp in milliseconds.
 *
 * Intended for elapsed-time reporting, not cryptographic timing.
 */
XRAY_API unsigned long xray_now_ms(void);

/**
 * Return a high-resolution timestamp in microseconds for local benchmarks.
 */
XRAY_API unsigned long long xray_now_us(void);

/**
 * Detect CPU architecture, brand, and SIMD/carry-related feature flags.
 *
 * features must point to writable storage. Unsupported or unknown flags are
 * reported as zero.
 */
XRAY_API void xray_cpu_features_detect(XrayCpuFeatures *features);

/**
 * Format CPU feature information as a newly allocated one-line summary.
 *
 * The caller owns the returned string and must release it with xray_free().
 */
XRAY_API char *xray_cpu_features_summary(const XrayCpuFeatures *features);

/**
 * Run GMP-backed Miller-Rabin style probable-prime testing.
 *
 * Returns non-zero for probable primes and zero for composites. rounds controls
 * the amount of probabilistic checking delegated to GMP.
 */
XRAY_API int xray_is_probable_prime(const mpz_t value, int rounds);

/**
 * Compute the integer nth root of value.
 *
 * root must already be initialized. Returns non-zero when value is an exact
 * nth power and zero otherwise; root receives the floor root in both cases.
 */
XRAY_API int xray_integer_nth_root(mpz_t root, const mpz_t value, unsigned long n);

/**
 * Search for a small factor by trial division up to limit.
 *
 * factor must already be initialized. Returns non-zero when a factor is found.
 */
XRAY_API int xray_small_factor(mpz_t factor, const mpz_t value, unsigned long limit);

/**
 * Attempt Fermat factorization for a bounded number of iterations.
 *
 * factor and cofactor must already be initialized. Returns non-zero only when
 * a non-trivial factorization is found.
 */
XRAY_API int xray_fermat_factor(mpz_t factor, mpz_t cofactor, const mpz_t value, unsigned long iterations);

/**
 * Attempt classic Pollard Rho factorization for a bounded number of iterations.
 *
 * factor and cofactor must already be initialized. Returns non-zero only when
 * a non-trivial factorization is found.
 */
XRAY_API int xray_pollard_rho_factor(mpz_t factor, mpz_t cofactor, const mpz_t value, unsigned long iterations);

/**
 * Attempt Brent's Pollard Rho variant for a bounded number of iterations.
 *
 * factor and cofactor must already be initialized. Returns non-zero only when
 * a non-trivial factorization is found.
 */
XRAY_API int xray_brent_rho_factor(mpz_t factor, mpz_t cofactor, const mpz_t value, unsigned long iterations);

/**
 * Attempt Pollard p-1 factorization up to a smoothness bound.
 *
 * factor and cofactor must already be initialized. Returns non-zero only when
 * a non-trivial factorization is found.
 */
XRAY_API int xray_pollard_pm1_factor(mpz_t factor, mpz_t cofactor, const mpz_t value, unsigned long bound);

/**
 * Detect whether value is an exact perfect power.
 *
 * base must already be initialized. On success, base and exponent describe
 * value = base^exponent. max_exponent bounds the search.
 */
XRAY_API int xray_perfect_power_factor(mpz_t base, unsigned long *exponent, const mpz_t value, unsigned long max_exponent);

/**
 * Run the bounded local factor solver on raw input text.
 *
 * report receives allocated fields and must be cleared with
 * xray_factor_report_clear(). Returns non-zero when the run was produced; check
 * report->status and verification flags before treating factors as solved.
 */
XRAY_API int xray_factor_solve(const char *raw_input, const XrayFactorConfig *config, XrayFactorReport *report);

/**
 * Free all heap fields inside a factor report and reset it.
 */
XRAY_API void xray_factor_report_clear(XrayFactorReport *report);

/**
 * Serialize a factor report as newly allocated JSON.
 *
 * The caller owns the returned string and must release it with xray_free().
 */
XRAY_API char *xray_factor_report_json(const XrayFactorReport *report);

/**
 * Run the factor solver with the default config and return JSON.
 *
 * This one-shot helper is intended for FFI, plugin, and scripting consumers
 * that want a self-contained report without managing XrayFactorReport storage.
 * Invalid inputs are represented as JSON reports when possible. The caller owns
 * the returned string and must release it with xray_free(). Returns NULL only
 * when report allocation or serialization fails.
 */
XRAY_API char *xray_factor_solve_json(const char *raw_input);

/**
 * Return Euler's totient phi(n) for an unsigned integer n.
 */
XRAY_API unsigned int xray_phi_ui(unsigned int n);

/**
 * Evaluate the nth cyclotomic polynomial at a GMP base.
 *
 * out and base must already be initialized. Returns non-zero on success.
 */
XRAY_API int xray_cyclotomic_eval_ui(mpz_t out, unsigned int n, const mpz_t base);

/**
 * Scan raw input text for bounded cyclotomic evidence.
 *
 * report receives allocated fields and must be cleared with
 * xray_cyclotomic_report_clear(). Exact matches, evidence, and timeouts are
 * reported separately in the report fields.
 */
XRAY_API int xray_cyclotomic_scan(const char *raw_input, const XrayCyclotomicConfig *config, XrayCyclotomicReport *report);

/**
 * Free all heap fields inside a cyclotomic report and reset it.
 */
XRAY_API void xray_cyclotomic_report_clear(XrayCyclotomicReport *report);

/**
 * Serialize a cyclotomic report as newly allocated JSON.
 *
 * The caller owns the returned string and must release it with xray_free().
 */
XRAY_API char *xray_cyclotomic_report_json(const XrayCyclotomicReport *report);

/**
 * Run the cyclotomic scanner with the default config and return JSON.
 *
 * This one-shot helper is intended for FFI, plugin, and scripting consumers
 * that want a self-contained report without managing XrayCyclotomicReport
 * storage. Invalid inputs are represented as JSON reports when possible. The
 * caller owns the returned string and must release it with xray_free(). Returns
 * NULL only when report allocation or serialization fails.
 */
XRAY_API char *xray_cyclotomic_scan_json(const char *raw_input);

/**
 * Run the scratch-vs-GMP benchmark and kernel-probe ladder.
 *
 * report receives allocated result rows and must be cleared with
 * xray_benchmark_report_clear(). Benchmark rows are evidence for local routing
 * decisions; they do not prove mathematical results.
 */
XRAY_API int xray_benchmark_run(XrayBenchmarkReport *report);

/**
 * Free all heap fields inside a benchmark report and reset it.
 */
XRAY_API void xray_benchmark_report_clear(XrayBenchmarkReport *report);

/**
 * Serialize a benchmark report as newly allocated JSON.
 *
 * The caller owns the returned string and must release it with xray_free().
 */
XRAY_API char *xray_benchmark_report_json(const XrayBenchmarkReport *report);

/**
 * Serialize benchmark rows as newly allocated TSV text.
 *
 * The caller owns the returned string and must release it with xray_free().
 */
XRAY_API char *xray_benchmark_report_tsv(const XrayBenchmarkReport *report);

/**
 * Format the benchmark frontier summary as newly allocated text.
 *
 * The caller owns the returned string and must release it with xray_free().
 */
XRAY_API char *xray_benchmark_frontier_text(const XrayBenchmarkReport *report);

/**
 * Return the adoption label for one benchmark result row.
 *
 * The returned pointer is borrowed static storage and must not be freed.
 */
XRAY_API const char *xray_scratch_adoption_for_result(const XrayBenchmarkResult *result);

/**
 * Emit a toy GNFS stage-proof artifact pipeline for an already normalized input.
 *
 * run_dir may be NULL to skip artifact files. report receives allocated stage
 * fields and must be cleared with xray_gnfs_report_clear(). This function does
 * not claim factorization unless later product verification proves it.
 */
XRAY_API int xray_gnfs_stage_proof(const char *normalized_input, const char *run_dir, XrayGnfsReport *report);

/**
 * Free all heap fields inside a GNFS report and reset it.
 */
XRAY_API void xray_gnfs_report_clear(XrayGnfsReport *report);

/**
 * Run the combined workbench pipeline on raw input text.
 *
 * report receives expression, factor, cyclotomic, benchmark, GNFS, event-log,
 * and JSON fields according to config. Clear it with xray_workbench_report_clear().
 */
XRAY_API int xray_workbench_run(const char *raw_input, const XrayRunConfig *config, XrayWorkbenchReport *report);

/**
 * Free all heap fields inside a workbench report and reset it.
 */
XRAY_API void xray_workbench_report_clear(XrayWorkbenchReport *report);

/**
 * Serialize a full workbench report as newly allocated JSON.
 *
 * The caller owns the returned string and must release it with xray_free().
 */
XRAY_API char *xray_workbench_full_report_json(const XrayWorkbenchReport *report);

/**
 * Run the standard proof workbench pipeline and return JSON.
 *
 * This one-shot helper is intended for FFI, plugin, and scripting consumers
 * that want expression, factor, cyclotomic, and GNFS-stage reports without
 * managing XrayWorkbenchReport storage. It uses the default workbench config
 * with the benchmark ladder disabled to avoid surprise long-running calls, and
 * still writes the normal workspace artifacts. The caller owns the returned
 * string and must release it with xray_free(). Returns NULL only when report
 * allocation or serialization fails.
 */
XRAY_API char *xray_workbench_run_json(const char *raw_input);

/**
 * Serialize selected factor/cyclotomic/benchmark reports as workbench JSON.
 *
 * Any argument may be NULL. The caller owns the returned string and must
 * release it with xray_free().
 */
XRAY_API char *xray_workbench_report_json(const XrayFactorReport *factor, const XrayCyclotomicReport *cyclotomic, const XrayBenchmarkReport *benchmark);

#ifdef __cplusplus
}
#endif

#endif
