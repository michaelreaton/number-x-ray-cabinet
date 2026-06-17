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

static char *make_pattern_decimal(size_t digits, const char *pattern) {
  size_t pattern_length = pattern ? strlen(pattern) : 0;
  CHECK(digits > 0);
  CHECK(pattern_length > 0);
  char *text = (char *)calloc(digits + 1, 1);
  CHECK(text != NULL);
  for (size_t index = 0; index < digits; ++index) {
    text[index] = pattern[index % pattern_length];
  }
  if (text[0] == '0') text[0] = '1';
  return text;
}

static char *make_messy_decimal(const char *digits) {
  size_t digit_length = digits ? strlen(digits) : 0;
  CHECK(digit_length > 0);
  size_t capacity = digit_length + digit_length / 17U + digit_length / 29U + digit_length / 113U + 4U;
  char *text = (char *)calloc(capacity + 1U, 1);
  CHECK(text != NULL);
  size_t out = 0;
  text[out++] = ' ';
  for (size_t index = 0; index < digit_length; ++index) {
    text[out++] = digits[index];
    size_t position = index + 1U;
    if (position % 17U == 0) text[out++] = ',';
    if (position % 29U == 0) text[out++] = '_';
    if (position % 113U == 0) text[out++] = ' ';
  }
  CHECK(out <= capacity);
  return text;
}

static void set_karatsuba_halves(XrayScratchBigInt *value, uint64_t low_top, uint64_t high_top, uint64_t salt) {
  const size_t half = 40;
  const size_t count = half * 2;
  value->limbs = (uint64_t *)calloc(count, sizeof(uint64_t));
  CHECK(value->limbs != NULL);
  value->capacity = count;
  value->count = count;
  for (size_t index = 0; index < half; ++index) {
    value->limbs[index] = salt + index + 1;
    value->limbs[half + index] = salt + half + index + 1;
  }
  value->limbs[half - 1] = low_top;
  value->limbs[count - 1] = high_top;
}

static void set_toom3_parts(XrayScratchBigInt *value, uint64_t part0_top, uint64_t part1_top, uint64_t part2_top, uint64_t salt) {
  const size_t part = 30;
  const size_t count = part * 3;
  value->limbs = (uint64_t *)calloc(count, sizeof(uint64_t));
  CHECK(value->limbs != NULL);
  value->capacity = count;
  value->count = count;
  for (size_t index = 0; index < count; ++index) {
    value->limbs[index] = salt + index + 1;
  }
  value->limbs[part - 1] = part0_top;
  value->limbs[part * 2 - 1] = part1_top;
  value->limbs[part * 3 - 1] = part2_top;
}

static void set_sparse_limbs(
  XrayScratchBigInt *value,
  size_t count,
  const size_t *indices,
  const uint64_t *words,
  size_t used) {
  value->limbs = (uint64_t *)calloc(count, sizeof(uint64_t));
  CHECK(value->limbs != NULL);
  value->capacity = count;
  value->count = count;
  for (size_t index = 0; index < used; ++index) {
    CHECK(indices[index] < count);
    CHECK(words[index] != 0);
    value->limbs[indices[index]] = words[index];
  }
  while (value->count && value->limbs[value->count - 1U] == 0) value->count--;
}

static void mpz_set_from_scratch_limbs(mpz_t out, const XrayScratchBigInt *value) {
  mpz_import(out, value->count, -1, sizeof(uint64_t), 0, 0, value->limbs);
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

static void test_public_allocator_contract(void) {
  xray_free(NULL);

  XrayScratchBigInt value;
  xray_bigint_init(&value);
  CHECK(xray_bigint_set_decimal(&value, "1,234,567"));
  char *decimal = xray_bigint_get_decimal(&value);
  CHECK(decimal != NULL);
  CHECK(strcmp(decimal, "1234567") == 0);
  xray_free(decimal);
  xray_bigint_clear(&value);

  mpz_t parsed;
  mpz_init(parsed);
  char *normalized = NULL;
  char *error = NULL;
  CHECK(xray_parse_integer("10_403", parsed, &normalized, &error));
  CHECK(strcmp(normalized, "10403") == 0);
  xray_free(normalized);
  xray_free(error);
  mpz_clear(parsed);
}

static void test_runtime_version_contract(void) {
  CHECK(strcmp(xray_version(), XRAY_VERSION) == 0);
  CHECK(xray_abi_version() == XRAY_ABI_VERSION);
  CHECK(xray_abi_version() >= 1u);
  CHECK(xray_bignum_backend_name() != NULL);
  CHECK(xray_bignum_backend_name()[0] != '\0');
  CHECK(xray_bignum_backend_version() != NULL);
  CHECK(xray_bignum_backend_version()[0] != '\0');
  CHECK(xray_bignum_backend_library() != NULL);
  CHECK(xray_bignum_backend_library()[0] != '\0');
  XrayBigIntRouteConfig route = xray_bigint_route_config();
  CHECK(route.word_bits == 64u);
  CHECK(route.karatsuba_threshold_limbs > 0);
  CHECK(route.decimal_horner_min_limbs > 0);
  CHECK(route.mul_unroll4_route_min_limbs <= route.mul_unroll4_route_max_limbs);
  if (route.mul_unroll4_route_enabled) CHECK(route.msvc_uint128_helpers);
  char *route_json = xray_bigint_route_config_json();
  CHECK(route_json != NULL);
  CHECK(strstr(route_json, "\"wordBits\":64") != NULL);
  CHECK(strstr(route_json, "\"karatsubaThresholdLimbs\":") != NULL);
  CHECK(strstr(route_json, "\"decimalHornerMinLimbs\":") != NULL);
  CHECK(strstr(route_json, "\"decimalWideChunkDigits\":19") != NULL);
  CHECK(strstr(route_json, "\"decimalDcMinWideChunks\":") != NULL);
  CHECK(strstr(route_json, "\"parseChunkDigits\":19") != NULL);
  CHECK(strstr(route_json, "\"parseLargeMinDigits\":2048") != NULL);
  CHECK(strstr(route_json, "\"parseLargeChunkDigits\":15") != NULL);
  CHECK(strstr(route_json, "\"sparseSquareMinLimbs\":") != NULL);
  CHECK(strstr(route_json, "\"sparseMulMinProducts\":") != NULL);
  CHECK(strstr(route_json, "\"productionRoutes\"") != NULL);
  CHECK(strstr(route_json, "\"decimal-dc-ladder\"") != NULL);
  CHECK(strstr(route_json, "D&C ladder at >=4096 digits") != NULL);
  CHECK(strstr(route_json, "\"decimal-dc-preinv-qhat\"") != NULL);
  CHECK(strstr(route_json, "\"decimal-parse-large\"") != NULL);
  CHECK(strstr(route_json, "\"diagnosticProbeFamilies\"") != NULL);
  CHECK(strstr(route_json, "\"decimal-parse-chunk\"") != NULL);
  CHECK(strstr(route_json, "GMP separates decimal conversion") != NULL);
  if (route.mul_unroll4_route_enabled) {
    CHECK(strstr(route_json, "\"mulUnroll4RouteEnabled\":true") != NULL);
  } else {
    CHECK(strstr(route_json, "\"mulUnroll4RouteEnabled\":false") != NULL);
  }
  xray_free(route_json);
  XrayBuildInfo build;
  xray_build_info_detect(&build);
  CHECK(build.compiler[0] != '\0');
  CHECK(build.compiler_version[0] != '\0');
  CHECK(build.build_config[0] != '\0');
  CHECK(build.cmake_generator[0] != '\0');
  char *build_summary = xray_build_info_summary(&build);
  CHECK(build_summary != NULL);
  CHECK(strstr(build_summary, "Build: compiler=") != NULL);
  CHECK(strstr(build_summary, "ipo=") != NULL);
  xray_free(build_summary);
}

static void test_benchmark_tsv_comparison(void) {
  const char *header =
    "category\tname\toperation\tdigits\tstatus\tpassed\tparityVerified\treplacementReady\tadoption\tscratchUs\tgmpUs\tspeedRatio\tmaxAllowedSpeedRatio\tworstPairRatio\tstableSampleCount\tsampleCount\telapsedMs\tdetail\tbuildConfig\tipo\tcompiler\tcompilerVersion\n";
  const char *left_rows =
    "scratch-vs-gmp\tscratch parse 1000 digits\tparse\t1000\treplacement-ready\ttrue\ttrue\ttrue\tallowed\t10\t20\t0.500000\t1.000000\t0.700000\t5\t5\t1\tdetail\tRelease\tfalse\tMSVC\t1929\n"
    "kernel-probe\tkernel mul threshold 64 limbs 4096 digits\tmul-threshold\t4096\tcandidate-faster\ttrue\ttrue\ttrue\tpromote-candidate\t30\t40\t0.750000\t0.980000\t0.900000\t5\t5\t1\tdetail\tRelease\tfalse\tMSVC\t1929\n"
    "policy-gate\tpolicy gate format preinv 1000 digits\tformat-policy-safety\t1000\tpolicy-ready\ttrue\ttrue\ttrue\tpromotion-ready\t50\t60\t0.800000\t0.980000\t0.950000\t2\t2\t1\tdetail\tRelease\tfalse\tMSVC\t1929\n";
  const char *right_rows =
    "scratch-vs-gmp\tscratch parse 1000 digits\tparse\t1000\treplacement-ready\ttrue\ttrue\ttrue\tallowed\t12\t20\t0.600000\t1.000000\t0.800000\t5\t5\t1\tdetail\tRelease\ttrue\tMSVC\t1929\n"
    "kernel-probe\tkernel mul threshold 64 limbs 4096 digits\tmul-threshold\t4096\tcandidate-no-margin\ttrue\ttrue\tfalse\tobserve-only\t30\t40\t0.760000\t0.980000\t1.400000\t4\t5\t1\tdetail\tRelease\ttrue\tMSVC\t1929\n"
    "policy-gate\tpolicy gate format preinv 1000 digits\tformat-policy-safety\t1000\tworst-pair-regression\ttrue\ttrue\tfalse\tobserve-only\t50\t60\t0.790000\t0.980000\t1.200000\t2\t2\t1\tdetail\tRelease\ttrue\tMSVC\t1929\n";
  size_t left_len = strlen(header) + strlen(left_rows) + 1U;
  size_t right_len = strlen(header) + strlen(right_rows) + 1U;
  char *left = (char *)calloc(left_len, 1);
  char *right = (char *)calloc(right_len, 1);
  CHECK(left != NULL);
  CHECK(right != NULL);
  snprintf(left, left_len, "%s%s", header, left_rows);
  snprintf(right, right_len, "%s%s", header, right_rows);

  char *review = xray_benchmark_compare_tsv_text(left, right);
  CHECK(review != NULL);
  CHECK(strstr(review, "BENCHMARK CROSS-BUILD REVIEW") != NULL);
  CHECK(strstr(review, "Left:  buildConfig=Release ipo=false compiler=MSVC 1929") != NULL);
  CHECK(strstr(review, "Right: buildConfig=Release ipo=true compiler=MSVC 1929") != NULL);
  CHECK(strstr(review, "matched=3") != NULL);
  CHECK(strstr(review, "bothReady=1") != NULL);
  CHECK(strstr(review, "oneBuildOnly=2") != NULL);
  CHECK(strstr(review, "worstPairRejected=2") != NULL);
  CHECK(strstr(review, "Both-build ready rows") != NULL);
  CHECK(strstr(review, "Ready in one build only") != NULL);
  CHECK(strstr(review, "Median wins rejected by worst-pair safety") != NULL);
  CHECK(strstr(review, "mul-threshold") != NULL);
  CHECK(strstr(review, "format-policy-safety") != NULL);
  xray_free(review);

  char *left_mul = xray_benchmark_filter_tsv_text(left, "mul-threshold");
  char *right_mul = xray_benchmark_filter_tsv_text(right, "mul-threshold");
  CHECK(left_mul != NULL);
  CHECK(right_mul != NULL);
  CHECK(strstr(left_mul, "kernel mul threshold") != NULL);
  CHECK(strstr(left_mul, "scratch parse") == NULL);
  CHECK(strstr(right_mul, "format preinv") == NULL);
  char *mul_review = xray_benchmark_compare_tsv_text(left_mul, right_mul);
  CHECK(mul_review != NULL);
  CHECK(strstr(mul_review, "matched=1") != NULL);
  CHECK(strstr(mul_review, "bothReady=0") != NULL);
  CHECK(strstr(mul_review, "oneBuildOnly=1") != NULL);
  CHECK(strstr(mul_review, "worstPairRejected=1") != NULL);
  CHECK(strstr(mul_review, "mul-threshold") != NULL);
  CHECK(strstr(mul_review, "format-policy-safety") == NULL);
  xray_free(mul_review);
  xray_free(left_mul);
  xray_free(right_mul);
  free(left);
  free(right);
}

static void test_benchmark_progress_digest(void) {
  const char *header =
    "category\tname\toperation\tdigits\tstatus\tpassed\tparityVerified\treplacementReady\tadoption\tscratchUs\tgmpUs\tspeedRatio\tmaxAllowedSpeedRatio\tworstPairRatio\tstableSampleCount\tsampleCount\telapsedMs\tdetail\tbuildConfig\tipo\tcompiler\tcompilerVersion\n";
  const char *rows =
    "scratch-vs-gmp\tscratch parse 1000 digits\tparse\t1000\treplacement-ready\ttrue\ttrue\ttrue\tallowed\t10\t20\t0.500000\t1.000000\t0.700000\t5\t5\t1\tdetail\tRelease\tfalse\tMSVC\t1929\n"
    "policy-gate\tpolicy gate format preinv 1000 digits\tformat-policy-safety\t1000\tpolicy-ready\ttrue\ttrue\ttrue\tpromotion-ready\t50\t60\t0.800000\t0.980000\t0.950000\t5\t5\t1\tpolicy=preinv candidate=decimal-divide-1e19-preinv activeCandidate=decimal-divide-1e19-preinv baseline=mpz_get_str featureGate=decimal-format-policy-divide-1e19-preinv gmpClue=mpn_get_str hashGate=matched SetupSeconds=0.123456 setupUs=42 setupSamples=5 setupPolicy=reported-not-scored WarmupSecondsMedian=0.000042\tRelease\tfalse\tMSVC\t1929\n"
    "policy-gate\tpolicy gate format product-gated 960 digits\tformat-policy-deep-safety\t960\tpolicy-ready\ttrue\ttrue\ttrue\tpromotion-ready\t45\t60\t0.750000\t0.980000\t0.880000\t5\t5\t1\tpolicy=deep-preinv gate=960 candidate=decimal-divide-1e19-preinv activeCandidate=decimal-divide-1e19-preinv baseline=mpz_get_str featureGate=decimal-format-policy-divide-1e19-preinv gmpClue=product-codegen hashGate=matched forcedCandidate=yes thresholdSafety=forced-neighbor deepConfirmation=required noAutoRoute=1\tRelease\tfalse\tMSVC\t1929\n"
    "policy-probe\tpolicy mul current-default 1000 digits\tmul-policy\t1000\tpolicy-ready\ttrue\ttrue\ttrue\tpromotion-ready\t21\t42\t0.500000\t1.000000\t0.800000\t5\t5\t1\tpolicy=current-default candidate=current-scratch-mul baseline=mpz_mul\tRelease\tfalse\tMSVC\t1929\n"
    "scratch-vs-gmp\tscratch format 896 digits\tformat\t896\tparity\ttrue\ttrue\tfalse\toracle-only\t190\t100\t1.900000\t1.000000\t1.950000\t0\t5\t1\tdetail\tRelease\tfalse\tMSVC\t1929\n"
    "frontier-scout\tfrontier scout mul 65536 digits\tmul-frontier\t65536\tnoisy-control\ttrue\ttrue\tfalse\tobserve-only\t70\t100\t0.700000\t1.000000\t1.500000\t1\t3\t1\tduplicateControl=default controlSafety=noisy-control\tRelease\tfalse\tMSVC\t1929\n"
    "policy-gate\tpolicy gate format window 896 digits\tformat-policy-deep-safety\t896\tworst-pair-regression\ttrue\ttrue\tfalse\tobserve-only\t70\t100\t0.700000\t0.980000\t1.300000\t7\t9\t1\tpolicy=window threshold=16\tRelease\tfalse\tMSVC\t1929\n"
    "kernel-probe\tkernel divmod warmup review 32768 digits\tdivmod-precomputed\t32768\treview-warmup\tfalse\tfalse\tfalse\tobserve-only\t180\t100\t1.800000\t0.980000\t1.900000\t0\t3\t240\tWarmupPolicy=review-warmup setupUs=450000 setupPolicy=review-warmup cacheRole=divisor-context\tRelease\tfalse\tMSVC\t1929\n"
    "kernel-probe\tkernel divmod timeout 16384 digits\tdivmod-precomputed\t16384\ttimeout lower-bound\tfalse\tfalse\tfalse\tobserve-only\t300\t100\t3.000000\t0.980000\t3.000000\t0\t0\t300\tCompletedRuns=0 Status=timeout lower-bound\tRelease\tfalse\tMSVC\t1929\n"
    "kernel-probe\tkernel product failed 32768 digits\tproduct-prefix\t32768\trun failed\tfalse\tfalse\tfalse\tobserve-only\t0\t0\t0.000000\t0.980000\t0.000000\t0\t0\t300\tRuns=1 CompletedRuns=0 Status=run failed exitCode=1\tRelease\tfalse\tMSVC\t1929\n";
  size_t tsv_len = strlen(header) + strlen(rows) + 1U;
  char *tsv = (char *)calloc(tsv_len, 1);
  CHECK(tsv != NULL);
  snprintf(tsv, tsv_len, "%s%s", header, rows);

  char *digest = xray_benchmark_progress_tsv_text(tsv);
  CHECK(digest != NULL);
  CHECK(strstr(digest, "BENCHMARK PROGRESS DIGEST") != NULL);
  CHECK(strstr(digest, "Artifact: buildConfig=Release ipo=false compiler=MSVC 1929") != NULL);
  CHECK(strstr(digest, "routeCandidates=5") != NULL);
  CHECK(strstr(digest, "routeCompleted=2") != NULL);
  CHECK(strstr(digest, "routeOpen=3") != NULL);
  CHECK(strstr(digest, "productGatedOpen=1") != NULL);
  CHECK(strstr(digest, "baselineExcluded=1") != NULL);
  CHECK(strstr(digest, "controlsExcluded=1") != NULL);
  CHECK(strstr(digest, "noisyControls=1") != NULL);
  CHECK(strstr(digest, "safetyRejected=1") != NULL);
  CHECK(strstr(digest, "warmupReviewRows=1") != NULL);
  CHECK(strstr(digest, "setupContextRows=1") != NULL);
  CHECK(strstr(digest, "lowerBoundRows=1") != NULL);
  CHECK(strstr(digest, "runFailedRows=1") != NULL);
  CHECK(strstr(digest, "Product/backend route candidate rows observed") != NULL);
  CHECK(strstr(digest, "Open/noisy route rows observed") != NULL);
  CHECK(strstr(digest, "Product-gated route rows observed") != NULL);
  CHECK(strstr(digest, "Setup/warmup context rows observed") != NULL);
  CHECK(strstr(digest, "Warmup-review rows observed") != NULL);
  CHECK(strstr(digest, "Safety-rejected rows observed") != NULL);
  CHECK(strstr(digest, "Lower-bound/incomplete rows observed") != NULL);
  CHECK(strstr(digest, "Run-failed rows observed") != NULL);
  CHECK(strstr(digest, "Baseline/current rows observed") != NULL);
  CHECK(strstr(digest, "Control/noise rows observed") != NULL);
  CHECK(strstr(digest, "parse") != NULL);
  CHECK(strstr(digest, "format-policy-safety policy=preinv") != NULL);
  CHECK(strstr(digest, "setup-context") != NULL);
  CHECK(strstr(digest, "format-policy-deep-safety policy=deep-preinv") != NULL);
  CHECK(strstr(digest, "mul-policy policy=current-default") != NULL);
  CHECK(strstr(digest, "mul-frontier") != NULL);
  CHECK(strstr(digest, "divmod-precomputed") != NULL);
  CHECK(strstr(digest, "review-warmup") != NULL);
  CHECK(strstr(digest, "Lower-bound/incomplete rows") != NULL);
  CHECK(strstr(digest, "baseline/current, duplicate-control, noisy-control, product-gated, warmup-review, lower-bound/incomplete, and run-failed rows") != NULL);
  CHECK(strstr(digest, "Setup/warmup context rows are reported for review but are not scored") != NULL);
  xray_free(digest);

  char *classification = xray_benchmark_progress_classification_tsv(tsv);
  CHECK(classification != NULL);
  CHECK(strstr(classification, "primaryLane\trouteCandidate\trouteCompleted\trouteOpen\tproductGated\thasSetupContext\tsetupSeconds\twarmupReview\tlowerBound\trunFailed\tattemptedRuns\tcompletedRuns") != NULL);
  CHECK(strstr(classification, "compilerVersion\tdigitBand\tworkloadShape\tpolicy\tcandidate\tactiveCandidate\tbaseline\tfeatureGate\tgmpClue\tcontrolSafety\tthresholdSafety\thashGate") != NULL);
  CHECK(strstr(classification, "format-policy-safety policy=preinv baseline=mpz_get_str featureGate=decimal-format-policy-divide-1e19-preinv candidate=decimal-divide-1e19-preinv\tcompleted\ttrue\ttrue\tfalse\tfalse\ttrue\t0.123456\tfalse\tfalse") != NULL);
  CHECK(strstr(classification, "0.123456\tfalse\tfalse\tfalse\t0\t0\tfalse\tfalse\tfalse\tfalse\ttrue\tpolicy-ready\tpromotion-ready\t0.800000") != NULL);
  CHECK(strstr(classification, "MSVC\t1929\tmedium\tdecimal-format\tpreinv\tdecimal-divide-1e19-preinv\tdecimal-divide-1e19-preinv\tmpz_get_str\tdecimal-format-policy-divide-1e19-preinv\tmpn_get_str\t\t\tmatched") != NULL);
  CHECK(strstr(classification, "format-policy-deep-safety policy=deep-preinv baseline=mpz_get_str featureGate=decimal-format-policy-divide-1e19-preinv candidate=decimal-divide-1e19-preinv\tproduct-gated\ttrue\tfalse\ttrue\ttrue\tfalse\t0.000000\tfalse\tfalse") != NULL);
  CHECK(strstr(classification, "MSVC\t1929\tmedium\tdecimal-format\tdeep-preinv\tdecimal-divide-1e19-preinv\tdecimal-divide-1e19-preinv\tmpz_get_str\tdecimal-format-policy-divide-1e19-preinv\tproduct-codegen\t\tforced-neighbor\tmatched") != NULL);
  CHECK(strstr(classification, "mul-policy policy=current-default baseline=mpz_mul candidate=current-scratch-mul\tbaseline\tfalse\tfalse\tfalse") != NULL);
  CHECK(strstr(classification, "mul-frontier\tcontrol\tfalse\tfalse\tfalse\tfalse\tfalse\t0.000000\tfalse\tfalse\tfalse\t0\t0\tfalse\tfalse\ttrue\ttrue") != NULL);
  CHECK(strstr(classification, "MSVC\t1929\tfrontier\tfrontier-scout") != NULL);
  CHECK(strstr(classification, "divmod-precomputed\twarmup-review\tfalse\tfalse\tfalse\tfalse\ttrue\t0.450000\ttrue\tfalse") != NULL);
  CHECK(strstr(classification, "divmod-precomputed\tlower-bound\tfalse\tfalse\tfalse\tfalse\tfalse\t0.000000\tfalse\ttrue\tfalse\t0\t0") != NULL);
  CHECK(strstr(classification, "product-prefix\trun-failed\tfalse\tfalse\tfalse\tfalse\tfalse\t0.000000\tfalse\tfalse\ttrue\t1\t0") != NULL);
  CHECK(strstr(classification, "0.000000\tfalse\tfalse\ttrue\t1\t0\tfalse\tfalse\tfalse\tfalse\tfalse\trun failed\tobserve-only\t0.000000") != NULL);
  xray_free(classification);

  char *focused = xray_benchmark_filter_tsv_digits(tsv, 768, 1000);
  CHECK(focused != NULL);
  CHECK(strstr(focused, "category\tname\toperation") != NULL);
  CHECK(strstr(focused, "scratch parse 1000 digits") != NULL);
  CHECK(strstr(focused, "policy gate format product-gated 960 digits") != NULL);
  CHECK(strstr(focused, "scratch format 896 digits") != NULL);
  CHECK(strstr(focused, "kernel divmod warmup review 32768 digits") == NULL);
  CHECK(strstr(focused, "kernel divmod timeout 16384 digits") == NULL);
  CHECK(strstr(focused, "frontier scout mul 65536 digits") == NULL);
  CHECK(strstr(focused, "kernel product failed 32768 digits") == NULL);

  char *focused_digest = xray_benchmark_progress_tsv_text(focused);
  CHECK(focused_digest != NULL);
  CHECK(strstr(focused_digest, "Rows: total=6") != NULL);
  CHECK(strstr(focused_digest, "routeCandidates=5") != NULL);
  CHECK(strstr(focused_digest, "productGatedOpen=1") != NULL);
  CHECK(strstr(focused_digest, "baselineExcluded=1") != NULL);
  CHECK(strstr(focused_digest, "controlsExcluded=0") != NULL);
  CHECK(strstr(focused_digest, "lowerBoundRows=0") != NULL);
  CHECK(strstr(focused_digest, "runFailedRows=0") != NULL);
  CHECK(strstr(focused_digest, "divmod-precomputed") == NULL);
  xray_free(focused_digest);

  char *focused_classification = xray_benchmark_progress_classification_tsv(focused);
  CHECK(focused_classification != NULL);
  CHECK(strstr(focused_classification, "format-policy-deep-safety policy=deep-preinv baseline=mpz_get_str featureGate=decimal-format-policy-divide-1e19-preinv candidate=decimal-divide-1e19-preinv\tproduct-gated") != NULL);
  CHECK(strstr(focused_classification, "scratch format 896 digits") != NULL);
  CHECK(strstr(focused_classification, "65536") == NULL);
  CHECK(strstr(focused_classification, "32768") == NULL);
  xray_free(focused_classification);
  xray_free(focused);

  char *mul_focused = xray_benchmark_filter_tsv_text(tsv, "mul");
  CHECK(mul_focused != NULL);
  CHECK(strstr(mul_focused, "policy mul current-default") != NULL);
  CHECK(strstr(mul_focused, "frontier scout mul") != NULL);
  CHECK(strstr(mul_focused, "scratch parse") == NULL);
  CHECK(strstr(mul_focused, "policy gate format") == NULL);
  char *mul_digest = xray_benchmark_progress_tsv_text(mul_focused);
  CHECK(mul_digest != NULL);
  CHECK(strstr(mul_digest, "Rows: total=2") != NULL);
  CHECK(strstr(mul_digest, "routeCandidates=0") != NULL);
  CHECK(strstr(mul_digest, "baselineExcluded=1") != NULL);
  CHECK(strstr(mul_digest, "controlsExcluded=1") != NULL);
  CHECK(strstr(mul_digest, "noisyControls=1") != NULL);
  CHECK(strstr(mul_digest, "parse") == NULL);
  CHECK(strstr(mul_digest, "format-policy") == NULL);
  xray_free(mul_digest);
  char *empty_filter = xray_benchmark_filter_tsv_text(tsv, "");
  CHECK(empty_filter != NULL);
  CHECK(strcmp(empty_filter, tsv) == 0);
  xray_free(empty_filter);
  xray_free(mul_focused);
  free(tsv);
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
  XrayScratchBigInt chunk_probe, chunk_baseline;
  xray_bigint_init(&chunk_probe);
  xray_bigint_init(&chunk_baseline);
  const char *chunk_probe_input = "987,654_321 012345678909876543210123456789098765432101234567890";
  CHECK(xray_bigint_set_decimal(&chunk_baseline, chunk_probe_input));
  const unsigned int parse_chunk_sizes[] = {1U, 8U, 9U, 10U, 15U, 18U, 19U};
  for (size_t chunk_index = 0; chunk_index < sizeof(parse_chunk_sizes) / sizeof(parse_chunk_sizes[0]); ++chunk_index) {
    CHECK(xray_bigint_set_decimal_chunk_probe(&chunk_probe, chunk_probe_input, parse_chunk_sizes[chunk_index]));
    CHECK(xray_bigint_compare(&chunk_probe, &chunk_baseline) == 0);
  }
  CHECK(!xray_bigint_set_decimal_chunk_probe(&chunk_probe, chunk_probe_input, 0U));
  CHECK(!xray_bigint_set_decimal_chunk_probe(&chunk_probe, chunk_probe_input, 20U));
  char *large_parse_input = make_pattern_decimal(2048U, "98765432101234567890");
  char *large_parse_messy = make_messy_decimal(large_parse_input);
  CHECK(xray_bigint_set_decimal(&chunk_baseline, large_parse_messy));
  CHECK(xray_bigint_set_decimal_chunk_probe(&chunk_probe, large_parse_messy, 15U));
  CHECK(xray_bigint_compare(&chunk_probe, &chunk_baseline) == 0);
  free(large_parse_input);
  free(large_parse_messy);
  xray_bigint_clear(&chunk_probe);
  xray_bigint_clear(&chunk_baseline);
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
  XrayBigIntU32ModContext mod_context;
  CHECK(!xray_bigint_u32_mod_context_init(NULL, 1000000007U));
  CHECK(!xray_bigint_u32_mod_context_init(&mod_context, 0U));
  CHECK(xray_bigint_u32_mod_context_init(&mod_context, 1000000007U));
  CHECK(mod_context.modulus == 1000000007U);
  CHECK(!mod_context.use_fermat_65537);
  CHECK(xray_bigint_mod_u32_precomputed(&a, &mod_context) == xray_bigint_mod_u32(&a, 1000000007U));
  CHECK(xray_bigint_gcd_u32_precomputed(&a, &mod_context) == xray_bigint_gcd_u32(&a, 1000000007U));
  CHECK(xray_bigint_powmod_u32_precomputed(&a, 12345U, &mod_context) == xray_bigint_powmod_u32(&a, 12345U, 1000000007U));
  CHECK(xray_bigint_u32_mod_context_init(&mod_context, 65537U));
  CHECK(mod_context.use_fermat_65537);
  CHECK(xray_bigint_mod_u32_precomputed(&a, &mod_context) == xray_bigint_mod_u32(&a, 65537U));
  CHECK(xray_bigint_gcd_u32_precomputed(&a, &mod_context) == xray_bigint_gcd_u32(&a, 65537U));
  CHECK(!xray_bigint_is_zero(&a));
  free(sum_text);
  free(difference_text);
  free(product_text);
  free(quotient_text);
  free(oracle_sum);
  free(oracle_difference);
  free(oracle_product);
  free(oracle_quotient);

  XrayBigIntRouteConfig route = xray_bigint_route_config();
  const size_t format_roundtrip_sizes[] = {40U, 150U, 767U, 768U, 896U, 897U, 1000U, 4096U, 8192U};
  for (size_t index = 0; index < sizeof(format_roundtrip_sizes) / sizeof(format_roundtrip_sizes[0]); ++index) {
    char *roundtrip_input = make_pattern_decimal(format_roundtrip_sizes[index], "97531864208642135790");
    CHECK(roundtrip_input != NULL);
    CHECK(xray_bigint_set_decimal(&a, roundtrip_input));
    CHECK(mpz_set_str(ga, roundtrip_input, 10) == 0);
    char *roundtrip_text = xray_bigint_get_decimal(&a);
    char *roundtrip_legacy = xray_bigint_get_decimal_horner_threshold_probe(&a, route.decimal_horner_min_limbs);
    char *roundtrip_folded = xray_bigint_get_decimal_folded_probe(&a);
    char *roundtrip_pair = xray_bigint_get_decimal_pair_writer_probe(&a);
    char *roundtrip_folded_pair = xray_bigint_get_decimal_folded_pair_writer_probe(&a);
    char *roundtrip_mixed_pair = xray_bigint_get_decimal_mixed_pair_writer_probe(&a);
    char *roundtrip_folded_hwdiv = xray_bigint_get_decimal_folded_hwdiv_probe(&a);
    char *roundtrip_folded_hwdiv_mixed = xray_bigint_get_decimal_folded_hwdiv_mixed_pair_probe(&a);
    char *roundtrip_divide_1e19 = xray_bigint_get_decimal_divide_1e19_probe(&a);
    char *roundtrip_divide_1e19_pairs = xray_bigint_get_decimal_divide_1e19_pair_writer_probe(&a);
    char *roundtrip_divide_1e19_preinv = xray_bigint_get_decimal_divide_1e19_preinv_probe(&a);
    char *roundtrip_divide_1e19_preinv_pairs = xray_bigint_get_decimal_divide_1e19_preinv_pair_writer_probe(&a);
    char *roundtrip_dc8 = xray_bigint_get_decimal_dc_probe(&a, 8U);
    char *roundtrip_dc32 = xray_bigint_get_decimal_dc_probe(&a, 32U);
    char *roundtrip_dc_ladder8 = xray_bigint_get_decimal_dc_ladder_probe(&a, 8U);
    char *roundtrip_dc_ladder32 = xray_bigint_get_decimal_dc_ladder_probe(&a, 32U);
    char *roundtrip_dc_static_ladder8 = xray_bigint_get_decimal_dc_static_ladder_probe(&a, 8U);
    char *roundtrip_dc_static_ladder32 = xray_bigint_get_decimal_dc_static_ladder_probe(&a, 32U);
    char *roundtrip_dc_direct8 = xray_bigint_get_decimal_dc_direct_probe(&a, 8U);
    char *roundtrip_dc_direct16 = xray_bigint_get_decimal_dc_direct_probe(&a, 16U);
    char *roundtrip_dc_direct32 = xray_bigint_get_decimal_dc_direct_probe(&a, 32U);
    char *roundtrip_dc_static_direct8 = xray_bigint_get_decimal_dc_static_direct_probe(&a, 8U);
    char *roundtrip_dc_static_direct32 = xray_bigint_get_decimal_dc_static_direct_probe(&a, 32U);
    char *roundtrip_dc_workspace8 = xray_bigint_get_decimal_dc_workspace_probe(&a, 8U);
    char *roundtrip_dc_workspace16 = xray_bigint_get_decimal_dc_workspace_probe(&a, 16U);
    char *roundtrip_dc_preinv_qhat8 = xray_bigint_get_decimal_dc_preinv_qhat_probe(&a, 8U);
    char *roundtrip_dc_preinv_qhat16 = xray_bigint_get_decimal_dc_preinv_qhat_probe(&a, 16U);
    char *roundtrip_wide = xray_bigint_get_decimal_wide_probe(&a);
    char *roundtrip_oracle = mpz_get_str(NULL, 10, ga);
    CHECK(roundtrip_text != NULL);
    CHECK(roundtrip_legacy != NULL);
    CHECK(roundtrip_folded != NULL);
    CHECK(roundtrip_pair != NULL);
    CHECK(roundtrip_folded_pair != NULL);
    CHECK(roundtrip_mixed_pair != NULL);
    CHECK(roundtrip_folded_hwdiv != NULL);
    CHECK(roundtrip_folded_hwdiv_mixed != NULL);
    CHECK(roundtrip_divide_1e19 != NULL);
    CHECK(roundtrip_divide_1e19_pairs != NULL);
    CHECK(roundtrip_divide_1e19_preinv != NULL);
    CHECK(roundtrip_divide_1e19_preinv_pairs != NULL);
    CHECK(roundtrip_dc8 != NULL);
    CHECK(roundtrip_dc32 != NULL);
    CHECK(roundtrip_dc_ladder8 != NULL);
    CHECK(roundtrip_dc_ladder32 != NULL);
    CHECK(roundtrip_dc_static_ladder8 != NULL);
    CHECK(roundtrip_dc_static_ladder32 != NULL);
    CHECK(roundtrip_dc_direct8 != NULL);
    CHECK(roundtrip_dc_direct16 != NULL);
    CHECK(roundtrip_dc_direct32 != NULL);
    CHECK(roundtrip_dc_static_direct8 != NULL);
    CHECK(roundtrip_dc_static_direct32 != NULL);
    CHECK(roundtrip_dc_workspace8 != NULL);
    CHECK(roundtrip_dc_workspace16 != NULL);
    CHECK(roundtrip_dc_preinv_qhat8 != NULL);
    CHECK(roundtrip_dc_preinv_qhat16 != NULL);
    CHECK(roundtrip_wide != NULL);
    CHECK(roundtrip_oracle != NULL);
    CHECK(strcmp(roundtrip_text, roundtrip_oracle) == 0);
    CHECK(strcmp(roundtrip_legacy, roundtrip_oracle) == 0);
    CHECK(strcmp(roundtrip_folded, roundtrip_oracle) == 0);
    CHECK(strcmp(roundtrip_pair, roundtrip_oracle) == 0);
    CHECK(strcmp(roundtrip_folded_pair, roundtrip_oracle) == 0);
    CHECK(strcmp(roundtrip_mixed_pair, roundtrip_oracle) == 0);
    CHECK(strcmp(roundtrip_folded_hwdiv, roundtrip_oracle) == 0);
    CHECK(strcmp(roundtrip_folded_hwdiv_mixed, roundtrip_oracle) == 0);
    CHECK(strcmp(roundtrip_divide_1e19, roundtrip_oracle) == 0);
    CHECK(strcmp(roundtrip_divide_1e19_pairs, roundtrip_oracle) == 0);
    CHECK(strcmp(roundtrip_divide_1e19_preinv, roundtrip_oracle) == 0);
    CHECK(strcmp(roundtrip_divide_1e19_preinv_pairs, roundtrip_oracle) == 0);
    CHECK(strcmp(roundtrip_dc8, roundtrip_oracle) == 0);
    CHECK(strcmp(roundtrip_dc32, roundtrip_oracle) == 0);
    CHECK(strcmp(roundtrip_dc_ladder8, roundtrip_oracle) == 0);
    CHECK(strcmp(roundtrip_dc_ladder32, roundtrip_oracle) == 0);
    CHECK(strcmp(roundtrip_dc_static_ladder8, roundtrip_oracle) == 0);
    CHECK(strcmp(roundtrip_dc_static_ladder32, roundtrip_oracle) == 0);
    CHECK(strcmp(roundtrip_dc_direct8, roundtrip_oracle) == 0);
    CHECK(strcmp(roundtrip_dc_direct16, roundtrip_oracle) == 0);
    CHECK(strcmp(roundtrip_dc_direct32, roundtrip_oracle) == 0);
    CHECK(strcmp(roundtrip_dc_static_direct8, roundtrip_oracle) == 0);
    CHECK(strcmp(roundtrip_dc_static_direct32, roundtrip_oracle) == 0);
    CHECK(strcmp(roundtrip_dc_workspace8, roundtrip_oracle) == 0);
    CHECK(strcmp(roundtrip_dc_workspace16, roundtrip_oracle) == 0);
    CHECK(strcmp(roundtrip_dc_preinv_qhat8, roundtrip_oracle) == 0);
    CHECK(strcmp(roundtrip_dc_preinv_qhat16, roundtrip_oracle) == 0);
    CHECK(strcmp(roundtrip_wide, roundtrip_oracle) == 0);
    free(roundtrip_input);
    free(roundtrip_text);
    free(roundtrip_legacy);
    free(roundtrip_folded);
    free(roundtrip_pair);
    free(roundtrip_folded_pair);
    free(roundtrip_mixed_pair);
    free(roundtrip_folded_hwdiv);
    free(roundtrip_folded_hwdiv_mixed);
    free(roundtrip_divide_1e19);
    free(roundtrip_divide_1e19_pairs);
    free(roundtrip_divide_1e19_preinv);
    free(roundtrip_divide_1e19_preinv_pairs);
    free(roundtrip_dc8);
    free(roundtrip_dc32);
    free(roundtrip_dc_ladder8);
    free(roundtrip_dc_ladder32);
    free(roundtrip_dc_static_ladder8);
    free(roundtrip_dc_static_ladder32);
    free(roundtrip_dc_direct8);
    free(roundtrip_dc_direct16);
    free(roundtrip_dc_direct32);
    free(roundtrip_dc_static_direct8);
    free(roundtrip_dc_static_direct32);
    free(roundtrip_dc_workspace8);
    free(roundtrip_dc_workspace16);
    free(roundtrip_dc_preinv_qhat8);
    free(roundtrip_dc_preinv_qhat16);
    free(roundtrip_wide);
    free(roundtrip_oracle);
  }

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

  mpz_ui_pow_ui(ga, 2U, 4096U);
  mpz_add_ui(ga, ga, 987654321U);
  mpz_set_ui(gb, 123456789U);
  mpz_sub(gdifference, ga, gb);
  char *large_tail_left = mpz_get_str(NULL, 10, ga);
  char *large_tail_right = mpz_get_str(NULL, 10, gb);
  CHECK(xray_bigint_set_decimal(&a, large_tail_left));
  CHECK(xray_bigint_set_decimal(&b, large_tail_right));
  CHECK(a.count > b.count + 16U);
  CHECK(xray_bigint_sub(&difference, &a, &b));
  check_scratch_matches_mpz(&difference, gdifference);
  CHECK(xray_bigint_sub(&a, &a, &b));
  check_scratch_matches_mpz(&a, gdifference);
  free(large_tail_left);
  free(large_tail_right);

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

  CHECK(xray_bigint_set_decimal(&a, "18446744073709551615"));
  CHECK(xray_bigint_set_decimal(&b, "1"));
  CHECK(xray_bigint_mod_u32(&a, 65537U) == 0);
  CHECK(xray_bigint_gcd_u32(&a, 65537U) == 65537U);
  CHECK(xray_bigint_add(&sum, &a, &b));
  mpz_set_str(ga, "18446744073709551615", 10);
  mpz_set_ui(gb, 1);
  mpz_add(gsum, ga, gb);
  check_scratch_matches_mpz(&sum, gsum);
  CHECK(xray_bigint_mod_u32(&sum, 65537U) == 1);
  CHECK(xray_bigint_gcd_u32(&sum, 65537U) == 1);

  CHECK(xray_bigint_sub(&difference, &sum, &b));
  mpz_sub(gdifference, gsum, gb);
  check_scratch_matches_mpz(&difference, gdifference);

  mpz_clears(ga, gb, gsum, gdifference, gproduct, gquotient, gmodulus, ggcd, gpow, gexponent, NULL);
  xray_bigint_clear(&a);
  xray_bigint_clear(&b);
  xray_bigint_clear(&sum);
  xray_bigint_clear(&difference);
  xray_bigint_clear(&product);
  xray_bigint_clear(&quotient);
}

static void check_decimal_string_matches_mpz(const char *actual, const mpz_t expected) {
  char *expected_text = mpz_get_str(NULL, 10, expected);
  CHECK(actual != NULL);
  CHECK(expected_text != NULL);
  CHECK(strcmp(actual, expected_text) == 0);
  free(expected_text);
}

static void test_decimal_ffi_helpers(void) {
  mpz_t left, right, expected;
  mpz_inits(left, right, expected, NULL);
  CHECK(mpz_set_str(left, "123456789012345678901234567890", 10) == 0);
  CHECK(mpz_set_str(right, "98765432109876543210", 10) == 0);

  char *sum = xray_bigint_add_decimal(
    "123,456,789,012,345,678,901,234,567,890",
    "98_765 432109876543210");
  mpz_add(expected, left, right);
  check_decimal_string_matches_mpz(sum, expected);
  xray_free(sum);

  char *difference = xray_bigint_sub_decimal(
    "123,456,789,012,345,678,901,234,567,890",
    "98_765 432109876543210");
  mpz_sub(expected, left, right);
  check_decimal_string_matches_mpz(difference, expected);
  xray_free(difference);

  char *product = xray_bigint_mul_decimal(
    "123,456,789,012,345,678,901,234,567,890",
    "98_765 432109876543210");
  mpz_mul(expected, left, right);
  check_decimal_string_matches_mpz(product, expected);
  xray_free(product);

  char *square = xray_bigint_square_decimal("4 294 967 296");
  mpz_set_str(left, "4294967296", 10);
  mpz_mul(expected, left, left);
  check_decimal_string_matches_mpz(square, expected);
  xray_free(square);

  int comparison = 99;
  CHECK(xray_bigint_compare_decimal("001_000", "1000", &comparison));
  CHECK(comparison == 0);
  CHECK(xray_bigint_compare_decimal("999", "1000", &comparison));
  CHECK(comparison == -1);
  CHECK(xray_bigint_compare_decimal("1001", "1000", &comparison));
  CHECK(comparison == 1);

  CHECK(xray_bigint_add_decimal("12x", "1") == NULL);
  CHECK(xray_bigint_sub_decimal("1", "2") == NULL);
  CHECK(!xray_bigint_compare_decimal("12x", "1", &comparison));
  CHECK(!xray_bigint_compare_decimal("1", "1", NULL));

  char *large_left = make_pattern_decimal(1000U, "98765432101234567890");
  char *large_right = make_pattern_decimal(1000U, "12345678900987654321");
  CHECK(large_left != NULL);
  CHECK(large_right != NULL);
  CHECK(mpz_set_str(left, large_left, 10) == 0);
  CHECK(mpz_set_str(right, large_right, 10) == 0);

  char *large_sum = xray_bigint_add_decimal(large_left, large_right);
  mpz_add(expected, left, right);
  check_decimal_string_matches_mpz(large_sum, expected);
  xray_free(large_sum);

  char *large_product = xray_bigint_mul_decimal(large_left, large_right);
  mpz_mul(expected, left, right);
  check_decimal_string_matches_mpz(large_product, expected);
  xray_free(large_product);

  free(large_left);
  free(large_right);
  mpz_clears(left, right, expected, NULL);
}

static void test_scratch_bigint_oracle_sweep(void) {
  const char *values[] = {
    "0",
    "1",
    "4294967295",
    "4294967296",
    "18446744073709551615",
    "18446744073709551616",
    "18446744073709551617",
    "999999999",
    "1000000000",
    "1000000000000000000000000000000",
    "1234567890123456789012345678901234567890",
    "80852963074185296307418529630741852963074185296307418529630741852963074185296307418529630741852963074185296307418529630741852963074185296307418529630741852963074185296307",
    NULL
  };
  const uint32_t divisors[] = {1U, 2U, 3U, 5U, 65535U, 65537U, 1000000007U, 2147483649U, 4294967295U};

  XrayScratchBigInt a, b, out, quotient, remainder, alias;
  XrayBigIntDivisorContext divisor_context;
  XrayBigIntDivisionWorkspace division_workspace;
  xray_bigint_init(&a);
  xray_bigint_init(&b);
  xray_bigint_init(&out);
  xray_bigint_init(&quotient);
  xray_bigint_init(&remainder);
  xray_bigint_init(&alias);
  xray_bigint_divisor_context_init(&divisor_context);
  xray_bigint_division_workspace_init(&division_workspace);
  mpz_t ga, gb, gout, gquotient, gremainder, gdivisor, ggcd, gpow;
  mpz_inits(ga, gb, gout, gquotient, gremainder, gdivisor, ggcd, gpow, NULL);

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

      if (mpz_sgn(gb) != 0) {
        CHECK(xray_bigint_divmod(&quotient, &remainder, &a, &b));
        mpz_tdiv_qr(gquotient, gremainder, ga, gb);
        check_scratch_matches_mpz(&quotient, gquotient);
        check_scratch_matches_mpz(&remainder, gremainder);

        CHECK(xray_bigint_divisor_context_set(&divisor_context, &b));
        CHECK(divisor_context.valid);
        CHECK(xray_bigint_divmod_precomputed(&quotient, &remainder, &a, &divisor_context));
        check_scratch_matches_mpz(&quotient, gquotient);
        check_scratch_matches_mpz(&remainder, gremainder);

        CHECK(xray_bigint_divmod_precomputed_workspace(&quotient, &remainder, &a, &divisor_context, &division_workspace));
        check_scratch_matches_mpz(&quotient, gquotient);
        check_scratch_matches_mpz(&remainder, gremainder);

        CHECK(xray_bigint_divmod_preinv_qhat_probe(&quotient, &remainder, &a, &divisor_context, &division_workspace));
        check_scratch_matches_mpz(&quotient, gquotient);
        check_scratch_matches_mpz(&remainder, gremainder);
      } else {
        CHECK(!xray_bigint_divmod(&quotient, &remainder, &a, &b));
        CHECK(!xray_bigint_divisor_context_set(&divisor_context, &b));
        CHECK(!xray_bigint_divmod_precomputed(&quotient, &remainder, &a, &divisor_context));
        CHECK(!xray_bigint_divmod_precomputed_workspace(&quotient, &remainder, &a, &divisor_context, &division_workspace));
        CHECK(!xray_bigint_divmod_preinv_qhat_probe(&quotient, &remainder, &a, &divisor_context, &division_workspace));
      }
    }
  }

  CHECK(xray_bigint_set_decimal(&a, "12345678901234567890123456789012345678901234567890"));
  CHECK(xray_bigint_set_decimal(&b, "1000000000000000000000000000000"));
  CHECK(mpz_set_str(ga, "12345678901234567890123456789012345678901234567890", 10) == 0);
  CHECK(mpz_set_str(gb, "1000000000000000000000000000000", 10) == 0);
  mpz_tdiv_qr(gquotient, gremainder, ga, gb);

  CHECK(xray_bigint_copy(&alias, &a));
  CHECK(xray_bigint_divmod(&alias, &remainder, &alias, &b));
  check_scratch_matches_mpz(&alias, gquotient);
  check_scratch_matches_mpz(&remainder, gremainder);

  CHECK(xray_bigint_copy(&alias, &b));
  CHECK(xray_bigint_divmod(&quotient, &alias, &a, &alias));
  check_scratch_matches_mpz(&quotient, gquotient);
  check_scratch_matches_mpz(&alias, gremainder);
  CHECK(!xray_bigint_divmod(&quotient, &quotient, &a, &b));

  CHECK(xray_bigint_divisor_context_set(&divisor_context, &b));
  CHECK(xray_bigint_copy(&alias, &a));
  CHECK(xray_bigint_divmod_precomputed(&alias, &remainder, &alias, &divisor_context));
  check_scratch_matches_mpz(&alias, gquotient);
  check_scratch_matches_mpz(&remainder, gremainder);

  CHECK(xray_bigint_copy(&alias, &a));
  CHECK(xray_bigint_divmod_precomputed(&quotient, &alias, &alias, &divisor_context));
  check_scratch_matches_mpz(&quotient, gquotient);
  check_scratch_matches_mpz(&alias, gremainder);
  CHECK(!xray_bigint_divmod_precomputed(&quotient, &quotient, &a, &divisor_context));

  CHECK(xray_bigint_copy(&alias, &a));
  CHECK(xray_bigint_divmod_precomputed_workspace(&alias, &remainder, &alias, &divisor_context, &division_workspace));
  check_scratch_matches_mpz(&alias, gquotient);
  check_scratch_matches_mpz(&remainder, gremainder);

  CHECK(xray_bigint_copy(&alias, &a));
  CHECK(xray_bigint_divmod_precomputed_workspace(&quotient, &alias, &alias, &divisor_context, &division_workspace));
  check_scratch_matches_mpz(&quotient, gquotient);
  check_scratch_matches_mpz(&alias, gremainder);
  CHECK(!xray_bigint_divmod_precomputed_workspace(&quotient, &quotient, &a, &divisor_context, &division_workspace));
  CHECK(!xray_bigint_divmod_precomputed_workspace(
    &division_workspace.normalized_numerator,
    &remainder,
    &a,
    &divisor_context,
    &division_workspace));

  CHECK(xray_bigint_copy(&alias, &a));
  CHECK(xray_bigint_divmod_preinv_qhat_probe(&alias, &remainder, &alias, &divisor_context, &division_workspace));
  check_scratch_matches_mpz(&alias, gquotient);
  check_scratch_matches_mpz(&remainder, gremainder);

  CHECK(xray_bigint_copy(&alias, &a));
  CHECK(xray_bigint_divmod_preinv_qhat_probe(&quotient, &alias, &alias, &divisor_context, &division_workspace));
  check_scratch_matches_mpz(&quotient, gquotient);
  check_scratch_matches_mpz(&alias, gremainder);
  CHECK(!xray_bigint_divmod_preinv_qhat_probe(&quotient, &quotient, &a, &divisor_context, &division_workspace));
  CHECK(!xray_bigint_divmod_preinv_qhat_probe(
    &division_workspace.normalized_numerator,
    &remainder,
    &a,
    &divisor_context,
    &division_workspace));

  CHECK(xray_bigint_set_decimal(&b, "0"));
  CHECK(!xray_bigint_divisor_context_set(&divisor_context, &b));
  CHECK(!xray_bigint_divmod_precomputed(&quotient, &remainder, &a, &divisor_context));
  CHECK(!xray_bigint_divmod_precomputed_workspace(&quotient, &remainder, &a, &divisor_context, &division_workspace));
  CHECK(!xray_bigint_divmod_preinv_qhat_probe(&quotient, &remainder, &a, &divisor_context, &division_workspace));

  mpz_clears(ga, gb, gout, gquotient, gremainder, gdivisor, ggcd, gpow, NULL);
  xray_bigint_division_workspace_clear(&division_workspace);
  xray_bigint_divisor_context_clear(&divisor_context);
  xray_bigint_clear(&a);
  xray_bigint_clear(&b);
  xray_bigint_clear(&out);
  xray_bigint_clear(&quotient);
  xray_bigint_clear(&remainder);
  xray_bigint_clear(&alias);
}

static void test_scratch_bigint_large_mul_oracle(void) {
  char *left_text = make_pattern_decimal(1500, "98765432101234567890");
  char *right_text = make_pattern_decimal(1500, "31415926535897932384");
  XrayScratchBigInt a, b, product;
  xray_bigint_init(&a);
  xray_bigint_init(&b);
  xray_bigint_init(&product);
  mpz_t ga, gb, gproduct;
  mpz_inits(ga, gb, gproduct, NULL);

  CHECK(xray_bigint_set_decimal(&a, left_text));
  CHECK(xray_bigint_set_decimal(&b, right_text));
  CHECK(a.count >= 64);
  CHECK(b.count >= 64);
  CHECK(mpz_set_str(ga, left_text, 10) == 0);
  CHECK(mpz_set_str(gb, right_text, 10) == 0);
  mpz_mul(gproduct, ga, gb);

  CHECK(xray_bigint_mul(&product, &a, &b));
  check_scratch_matches_mpz(&product, gproduct);

  CHECK(xray_bigint_mul(&a, &a, &b));
  check_scratch_matches_mpz(&a, gproduct);

  xray_bigint_clear(&a);
  xray_bigint_clear(&b);
  xray_bigint_clear(&product);
  mpz_clears(ga, gb, gproduct, NULL);
  free(left_text);
  free(right_text);
}

static void test_scratch_bigint_square_oracle(void) {
  const char *small_cases[] = {"0", "1", "18446744073709551616", "340282366920938463463374607431768211455"};
  for (size_t index = 0; index < sizeof(small_cases) / sizeof(small_cases[0]); ++index) {
    XrayScratchBigInt value, square, product;
    xray_bigint_init(&value);
    xray_bigint_init(&square);
    xray_bigint_init(&product);
    mpz_t gvalue, gsquare;
    mpz_inits(gvalue, gsquare, NULL);

    CHECK(xray_bigint_set_decimal(&value, small_cases[index]));
    CHECK(mpz_set_str(gvalue, small_cases[index], 10) == 0);
    mpz_mul(gsquare, gvalue, gvalue);
    CHECK(xray_bigint_square(&square, &value));
    CHECK(xray_bigint_mul(&product, &value, &value));
    CHECK(xray_bigint_compare(&square, &product) == 0);
    check_scratch_matches_mpz(&square, gsquare);
    CHECK(xray_bigint_square_fused_leaf_probe(&square, &value, 4));
    check_scratch_matches_mpz(&square, gsquare);

    CHECK(xray_bigint_set_decimal(&value, small_cases[index]));
    CHECK(xray_bigint_mul(&value, &value, &value));
    check_scratch_matches_mpz(&value, gsquare);

    CHECK(xray_bigint_set_decimal(&value, small_cases[index]));
    CHECK(xray_bigint_square(&value, &value));
    check_scratch_matches_mpz(&value, gsquare);

    mpz_clears(gvalue, gsquare, NULL);
    xray_bigint_clear(&value);
    xray_bigint_clear(&square);
    xray_bigint_clear(&product);
  }

  char *tiny_route_text = make_pattern_decimal(150, "98765432101234567890");
  XrayScratchBigInt tiny, tiny_square, tiny_product;
  xray_bigint_init(&tiny);
  xray_bigint_init(&tiny_square);
  xray_bigint_init(&tiny_product);
  mpz_t gtiny, gtiny_square;
  mpz_inits(gtiny, gtiny_square, NULL);
  CHECK(xray_bigint_set_decimal(&tiny, tiny_route_text));
  CHECK(mpz_set_str(gtiny, tiny_route_text, 10) == 0);
  mpz_mul(gtiny_square, gtiny, gtiny);
  CHECK(xray_bigint_square(&tiny_square, &tiny));
  CHECK(xray_bigint_mul(&tiny_product, &tiny, &tiny));
  CHECK(xray_bigint_compare(&tiny_square, &tiny_product) == 0);
  check_scratch_matches_mpz(&tiny_square, gtiny_square);
  CHECK(xray_bigint_set_decimal(&tiny, tiny_route_text));
  CHECK(xray_bigint_mul(&tiny, &tiny, &tiny));
  check_scratch_matches_mpz(&tiny, gtiny_square);
  CHECK(xray_bigint_set_decimal(&tiny, tiny_route_text));
  CHECK(xray_bigint_square(&tiny, &tiny));
  check_scratch_matches_mpz(&tiny, gtiny_square);
  CHECK(xray_bigint_set_decimal(&tiny, tiny_route_text));
  CHECK(xray_bigint_square_fused_leaf_probe(&tiny, &tiny, 64));
  check_scratch_matches_mpz(&tiny, gtiny_square);
  mpz_clears(gtiny, gtiny_square, NULL);
  xray_bigint_clear(&tiny);
  xray_bigint_clear(&tiny_square);
  xray_bigint_clear(&tiny_product);
  free(tiny_route_text);

  XrayScratchBigInt mersenne, mersenne_square;
  xray_bigint_init(&mersenne);
  xray_bigint_init(&mersenne_square);
  mpz_t gmersenne, gmersenne_square;
  mpz_inits(gmersenne, gmersenne_square, NULL);
  mpz_ui_pow_ui(gmersenne, 2U, 512U);
  mpz_sub_ui(gmersenne, gmersenne, 1U);
  char *mersenne_text = mpz_get_str(NULL, 10, gmersenne);
  mpz_mul(gmersenne_square, gmersenne, gmersenne);
  CHECK(xray_bigint_set_decimal(&mersenne, mersenne_text));
  CHECK(xray_bigint_square(&mersenne_square, &mersenne));
  check_scratch_matches_mpz(&mersenne_square, gmersenne_square);
  CHECK(xray_bigint_square_karatsuba_probe(&mersenne_square, &mersenne, 4));
  check_scratch_matches_mpz(&mersenne_square, gmersenne_square);
  CHECK(xray_bigint_square_fused_leaf_probe(&mersenne_square, &mersenne, 4));
  check_scratch_matches_mpz(&mersenne_square, gmersenne_square);
  free(mersenne_text);
  mpz_clears(gmersenne, gmersenne_square, NULL);
  xray_bigint_clear(&mersenne);
  xray_bigint_clear(&mersenne_square);

  char *large_text = make_pattern_decimal(2200, "13579135791357924680");
  XrayScratchBigInt large, square;
  xray_bigint_init(&large);
  xray_bigint_init(&square);
  mpz_t glarge, gsquare;
  mpz_inits(glarge, gsquare, NULL);

  CHECK(xray_bigint_set_decimal(&large, large_text));
  CHECK(mpz_set_str(glarge, large_text, 10) == 0);
  mpz_mul(gsquare, glarge, glarge);
  CHECK(xray_bigint_square(&square, &large));
  check_scratch_matches_mpz(&square, gsquare);
  CHECK(xray_bigint_square_karatsuba_probe(&square, &large, 16));
  check_scratch_matches_mpz(&square, gsquare);
  CHECK(xray_bigint_square_fused_leaf_probe(&square, &large, 16));
  check_scratch_matches_mpz(&square, gsquare);

  CHECK(xray_bigint_square(&large, &large));
  check_scratch_matches_mpz(&large, gsquare);
  CHECK(xray_bigint_set_decimal(&large, large_text));
  CHECK(xray_bigint_square_karatsuba_probe(&large, &large, 16));
  check_scratch_matches_mpz(&large, gsquare);
  CHECK(xray_bigint_set_decimal(&large, large_text));
  CHECK(xray_bigint_square_fused_leaf_probe(&large, &large, 16));
  check_scratch_matches_mpz(&large, gsquare);

  mpz_clears(glarge, gsquare, NULL);
  xray_bigint_clear(&large);
  xray_bigint_clear(&square);
  free(large_text);
}

static void test_scratch_bigint_sparse_zero_limb_oracle(void) {
  mpz_t fermat_like, shifted_like, three_term_like, second_three_term_like, expected;
  mpz_inits(fermat_like, shifted_like, three_term_like, second_three_term_like, expected, NULL);
  mpz_ui_pow_ui(fermat_like, 2U, 4096U);
  mpz_add_ui(fermat_like, fermat_like, 1U);
  mpz_ui_pow_ui(shifted_like, 2U, 2048U);
  mpz_add_ui(shifted_like, shifted_like, 1U);
  mpz_ui_pow_ui(three_term_like, 2U, 4096U);
  mpz_ui_pow_ui(expected, 2U, 2048U);
  mpz_add(three_term_like, three_term_like, expected);
  mpz_add_ui(three_term_like, three_term_like, 1U);
  mpz_ui_pow_ui(second_three_term_like, 2U, 3072U);
  mpz_ui_pow_ui(expected, 2U, 128U);
  mpz_add(second_three_term_like, second_three_term_like, expected);
  mpz_add_ui(second_three_term_like, second_three_term_like, 1U);

  char *fermat_text = mpz_get_str(NULL, 10, fermat_like);
  char *shifted_text = mpz_get_str(NULL, 10, shifted_like);
  char *three_term_text = mpz_get_str(NULL, 10, three_term_like);
  char *second_three_term_text = mpz_get_str(NULL, 10, second_three_term_like);
  CHECK(fermat_text != NULL);
  CHECK(shifted_text != NULL);
  CHECK(three_term_text != NULL);
  CHECK(second_three_term_text != NULL);

  XrayScratchBigInt a, b, out;
  xray_bigint_init(&a);
  xray_bigint_init(&b);
  xray_bigint_init(&out);

  CHECK(xray_bigint_set_decimal(&a, fermat_text));
  CHECK(xray_bigint_set_decimal(&b, shifted_text));
  CHECK(a.count > 32);
  CHECK(b.count > 16);

  mpz_mul(expected, fermat_like, fermat_like);
  CHECK(xray_bigint_square(&out, &a));
  check_scratch_matches_mpz(&out, expected);
  CHECK(xray_bigint_square_fused_leaf_probe(&out, &a, 64));
  check_scratch_matches_mpz(&out, expected);
  CHECK(xray_bigint_set_decimal(&a, fermat_text));
  CHECK(xray_bigint_square(&a, &a));
  check_scratch_matches_mpz(&a, expected);

  CHECK(xray_bigint_set_decimal(&a, fermat_text));
  mpz_mul(expected, fermat_like, shifted_like);
  CHECK(xray_bigint_mul(&out, &a, &b));
  check_scratch_matches_mpz(&out, expected);
  CHECK(xray_bigint_mul(&b, &a, &b));
  check_scratch_matches_mpz(&b, expected);

  CHECK(xray_bigint_set_decimal(&a, three_term_text));
  CHECK(xray_bigint_set_decimal(&b, second_three_term_text));
  mpz_mul(expected, three_term_like, second_three_term_like);
  CHECK(xray_bigint_mul(&out, &a, &b));
  check_scratch_matches_mpz(&out, expected);
  CHECK(xray_bigint_mul(&a, &a, &b));
  check_scratch_matches_mpz(&a, expected);

  xray_bigint_clear(&a);
  xray_bigint_clear(&b);
  xray_bigint_init(&a);
  xray_bigint_init(&b);
  const size_t left_indices[] = {0U, 5U, 11U, 19U, 31U, 43U, 57U, 64U};
  const uint64_t left_words[] = {
    UINT64_MAX, UINT64_C(17), UINT64_C(29), UINT64_C(41),
    UINT64_C(53), UINT64_C(67), UINT64_C(79), UINT64_C(3)
  };
  const size_t right_indices[] = {0U, 7U, 13U, 23U, 37U, 47U, 59U, 64U};
  const uint64_t right_words[] = {
    UINT64_MAX, UINT64_C(23), UINT64_C(31), UINT64_C(43),
    UINT64_C(59), UINT64_C(71), UINT64_C(83), UINT64_C(5)
  };
  set_sparse_limbs(&a, 65U, left_indices, left_words, sizeof(left_indices) / sizeof(left_indices[0]));
  set_sparse_limbs(&b, 65U, right_indices, right_words, sizeof(right_indices) / sizeof(right_indices[0]));
  mpz_t ga, gb;
  mpz_inits(ga, gb, NULL);
  mpz_set_from_scratch_limbs(ga, &a);
  mpz_set_from_scratch_limbs(gb, &b);
  mpz_mul(expected, ga, gb);
  CHECK(xray_bigint_mul(&out, &a, &b));
  check_scratch_matches_mpz(&out, expected);
  CHECK(xray_bigint_mul(&b, &a, &b));
  check_scratch_matches_mpz(&b, expected);
  mpz_clears(ga, gb, NULL);

  xray_bigint_clear(&a);
  xray_bigint_clear(&b);
  xray_bigint_clear(&out);
  free(fermat_text);
  free(shifted_text);
  free(three_term_text);
  free(second_three_term_text);
  mpz_clears(fermat_like, shifted_like, three_term_like, second_three_term_like, expected, NULL);
}

static void test_scratch_bigint_karatsuba_middle_signs(void) {
  const uint64_t cases[][4] = {
    {3, 9, 5, 11},
    {3, 9, 11, 5},
    {9, 3, 5, 11},
    {9, 3, 11, 5}
  };

  for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
    XrayScratchBigInt a, b, product;
    xray_bigint_init(&a);
    xray_bigint_init(&b);
    xray_bigint_init(&product);
    set_karatsuba_halves(&a, cases[index][0], cases[index][1], 100 + (uint64_t)index * 10);
    set_karatsuba_halves(&b, cases[index][2], cases[index][3], 300 + (uint64_t)index * 10);

    mpz_t ga, gb, gproduct;
    mpz_inits(ga, gb, gproduct, NULL);
    mpz_set_from_scratch_limbs(ga, &a);
    mpz_set_from_scratch_limbs(gb, &b);
    mpz_mul(gproduct, ga, gb);

    CHECK(xray_bigint_mul(&product, &a, &b));
    check_scratch_matches_mpz(&product, gproduct);

    mpz_clears(ga, gb, gproduct, NULL);
    xray_bigint_clear(&a);
    xray_bigint_clear(&b);
    xray_bigint_clear(&product);
  }
}

static void test_scratch_bigint_mul_thresholds(void) {
  const size_t thresholds[] = {0, 16, 32, 48, 64, 96, 128};
  char *left_text = make_pattern_decimal(1800, "80852963074185296307");
  char *right_text = make_pattern_decimal(1800, "27182818284590452353");
  XrayScratchBigInt a, b, product, alias;
  xray_bigint_init(&a);
  xray_bigint_init(&b);
  xray_bigint_init(&product);
  xray_bigint_init(&alias);
  mpz_t ga, gb, gproduct;
  mpz_inits(ga, gb, gproduct, NULL);

  CHECK(xray_bigint_set_decimal(&a, left_text));
  CHECK(xray_bigint_set_decimal(&b, right_text));
  CHECK(mpz_set_str(ga, left_text, 10) == 0);
  CHECK(mpz_set_str(gb, right_text, 10) == 0);
  mpz_mul(gproduct, ga, gb);

  for (size_t index = 0; index < sizeof(thresholds) / sizeof(thresholds[0]); ++index) {
    CHECK(xray_bigint_mul_with_threshold(&product, &a, &b, thresholds[index]));
    check_scratch_matches_mpz(&product, gproduct);
  }

  CHECK(xray_bigint_copy(&alias, &a));
  CHECK(xray_bigint_mul_with_threshold(&alias, &alias, &b, 48));
  check_scratch_matches_mpz(&alias, gproduct);

  xray_bigint_clear(&a);
  xray_bigint_clear(&b);
  xray_bigint_clear(&product);
  xray_bigint_clear(&alias);
  mpz_clears(ga, gb, gproduct, NULL);
  free(left_text);
  free(right_text);
}

static void test_scratch_bigint_karatsuba_sum_probe_oracle(void) {
  const size_t thresholds[] = {32, 64, 96, 128};
  char *left_text = make_pattern_decimal(2400, "86420975318642097531");
  char *right_text = make_pattern_decimal(2400, "13579246801357924680");
  XrayScratchBigInt a, b, product, alias, current;
  xray_bigint_init(&a);
  xray_bigint_init(&b);
  xray_bigint_init(&product);
  xray_bigint_init(&alias);
  xray_bigint_init(&current);
  mpz_t ga, gb, gproduct;
  mpz_inits(ga, gb, gproduct, NULL);

  CHECK(xray_bigint_set_decimal(&a, left_text));
  CHECK(xray_bigint_set_decimal(&b, right_text));
  CHECK(mpz_set_str(ga, left_text, 10) == 0);
  CHECK(mpz_set_str(gb, right_text, 10) == 0);
  mpz_mul(gproduct, ga, gb);

  for (size_t index = 0; index < sizeof(thresholds) / sizeof(thresholds[0]); ++index) {
    CHECK(xray_bigint_mul_karatsuba_sum_probe(&product, &a, &b, thresholds[index]));
    check_scratch_matches_mpz(&product, gproduct);
    CHECK(xray_bigint_mul_with_threshold(&current, &a, &b, thresholds[index]));
    CHECK(xray_bigint_compare(&product, &current) == 0);

    CHECK(xray_bigint_copy(&alias, &a));
    CHECK(xray_bigint_mul_karatsuba_sum_probe(&alias, &alias, &b, thresholds[index]));
    check_scratch_matches_mpz(&alias, gproduct);
  }

  xray_bigint_clear(&a);
  xray_bigint_clear(&b);
  xray_bigint_clear(&product);
  xray_bigint_clear(&alias);
  xray_bigint_clear(&current);
  mpz_clears(ga, gb, gproduct, NULL);
  free(left_text);
  free(right_text);
}

static void test_scratch_bigint_toom3_probe_oracle(void) {
  char *left_text = make_pattern_decimal(2400, "97531864208642135790");
  char *right_text = make_pattern_decimal(2400, "24681357913579246801");
  XrayScratchBigInt a, b, product, alias;
  xray_bigint_init(&a);
  xray_bigint_init(&b);
  xray_bigint_init(&product);
  xray_bigint_init(&alias);
  mpz_t ga, gb, gproduct;
  mpz_inits(ga, gb, gproduct, NULL);

  CHECK(xray_bigint_set_decimal(&a, left_text));
  CHECK(xray_bigint_set_decimal(&b, right_text));
  CHECK(mpz_set_str(ga, left_text, 10) == 0);
  CHECK(mpz_set_str(gb, right_text, 10) == 0);
  mpz_mul(gproduct, ga, gb);

  CHECK(xray_bigint_mul_toom3_probe(&product, &a, &b, 32));
  check_scratch_matches_mpz(&product, gproduct);

  CHECK(xray_bigint_copy(&alias, &a));
  CHECK(xray_bigint_mul_toom3_probe(&alias, &alias, &b, 32));
  check_scratch_matches_mpz(&alias, gproduct);

#if defined(_MSC_VER) && defined(_M_X64)
  CHECK(xray_bigint_mul_toom3_unroll4_probe(&product, &a, &b, 32));
  check_scratch_matches_mpz(&product, gproduct);

  CHECK(xray_bigint_copy(&alias, &a));
  CHECK(xray_bigint_mul_toom3_unroll4_probe(&alias, &alias, &b, 32));
  check_scratch_matches_mpz(&alias, gproduct);
#else
  CHECK(!xray_bigint_mul_toom3_unroll4_probe(&product, &a, &b, 32));
#endif

  xray_bigint_clear(&a);
  xray_bigint_clear(&b);
  xray_bigint_clear(&product);
  xray_bigint_clear(&alias);
  mpz_clears(ga, gb, gproduct, NULL);
  free(left_text);
  free(right_text);
}

static void test_scratch_bigint_toom3_recursive_probe_oracle(void) {
#if defined(_MSC_VER) && defined(_M_X64)
  char *left_text = make_pattern_decimal(12000, "98673142086421357905");
  char *right_text = make_pattern_decimal(12000, "31415926535897932384");
  XrayScratchBigInt a, b, product, alias;
  xray_bigint_init(&a);
  xray_bigint_init(&b);
  xray_bigint_init(&product);
  xray_bigint_init(&alias);
  mpz_t ga, gb, gproduct;
  mpz_inits(ga, gb, gproduct, NULL);

  CHECK(xray_bigint_set_decimal(&a, left_text));
  CHECK(xray_bigint_set_decimal(&b, right_text));
  CHECK(a.count >= 576);
  CHECK(b.count >= 576);
  CHECK(mpz_set_str(ga, left_text, 10) == 0);
  CHECK(mpz_set_str(gb, right_text, 10) == 0);
  mpz_mul(gproduct, ga, gb);

  CHECK(xray_bigint_mul_toom3_unroll4_recursive_probe(&product, &a, &b, 64, 2));
  check_scratch_matches_mpz(&product, gproduct);

  CHECK(xray_bigint_copy(&alias, &a));
  CHECK(xray_bigint_mul_toom3_unroll4_recursive_probe(&alias, &alias, &b, 64, 2));
  check_scratch_matches_mpz(&alias, gproduct);

  xray_bigint_clear(&a);
  xray_bigint_clear(&b);
  xray_bigint_clear(&product);
  xray_bigint_clear(&alias);
  mpz_clears(ga, gb, gproduct, NULL);
  free(left_text);
  free(right_text);
#else
  XrayScratchBigInt value;
  xray_bigint_init(&value);
  CHECK(!xray_bigint_mul_toom3_unroll4_recursive_probe(&value, &value, &value, 64, 2));
  xray_bigint_clear(&value);
#endif
}

static void test_scratch_bigint_unroll4_probe_oracle(void) {
  char *left_text = make_pattern_decimal(1800, "80852963074185296307");
  char *right_text = make_pattern_decimal(1800, "27182818284590452353");
  XrayScratchBigInt a, b, product, alias;
  xray_bigint_init(&a);
  xray_bigint_init(&b);
  xray_bigint_init(&product);
  xray_bigint_init(&alias);
  mpz_t ga, gb, gproduct;
  mpz_inits(ga, gb, gproduct, NULL);

  CHECK(xray_bigint_set_decimal(&a, left_text));
  CHECK(xray_bigint_set_decimal(&b, right_text));
  CHECK(mpz_set_str(ga, left_text, 10) == 0);
  CHECK(mpz_set_str(gb, right_text, 10) == 0);
  mpz_mul(gproduct, ga, gb);

#if defined(_MSC_VER) && defined(_M_X64)
  CHECK(xray_bigint_mul_unroll4_probe(&product, &a, &b, 64));
  check_scratch_matches_mpz(&product, gproduct);

  CHECK(xray_bigint_copy(&alias, &a));
  CHECK(xray_bigint_mul_unroll4_probe(&alias, &alias, &b, 64));
  check_scratch_matches_mpz(&alias, gproduct);
#else
  CHECK(!xray_bigint_mul_unroll4_probe(&product, &a, &b, 64));
#endif

  xray_bigint_clear(&a);
  xray_bigint_clear(&b);
  xray_bigint_clear(&product);
  xray_bigint_clear(&alias);
  mpz_clears(ga, gb, gproduct, NULL);
  free(left_text);
  free(right_text);
}

static void test_scratch_bigint_toom3_minus_one_signs(void) {
  XrayScratchBigInt a, b, product;
  xray_bigint_init(&a);
  xray_bigint_init(&b);
  xray_bigint_init(&product);
  set_toom3_parts(&a, 2, 19, 3, 1000);
  set_toom3_parts(&b, 5, 23, 7, 2000);

  mpz_t ga, gb, gproduct;
  mpz_inits(ga, gb, gproduct, NULL);
  mpz_set_from_scratch_limbs(ga, &a);
  mpz_set_from_scratch_limbs(gb, &b);
  mpz_mul(gproduct, ga, gb);

  CHECK(xray_bigint_mul_toom3_probe(&product, &a, &b, 16));
  check_scratch_matches_mpz(&product, gproduct);

  mpz_clears(ga, gb, gproduct, NULL);
  xray_bigint_clear(&a);
  xray_bigint_clear(&b);
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

  mpz_set_ui(value, 49999);
  mpz_mul_ui(value, value, 50021);
  CHECK(xray_small_factor(factor, value, 50000));
  CHECK(mpz_cmp_ui(factor, 49999) == 0);
  mpz_divexact(cofactor, value, factor);
  mpz_mul(factor, factor, cofactor);
  CHECK(mpz_cmp(factor, value) == 0);

  mpz_set_ui(value, 100003);
  mpz_mul_ui(value, value, 1000003);
  CHECK(xray_pollard_pm1_factor(factor, cofactor, value, 100003));
  CHECK(mpz_cmp_ui(factor, 1) > 0);
  CHECK(mpz_cmp(factor, value) < 0);
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

typedef struct RunEventCapture {
  size_t count;
  size_t benchmark_row_count;
  int saw_expression_complete;
  int saw_factor_event;
  int saw_benchmark_running;
  int saw_benchmark_complete;
  int saw_benchmark_disabled;
  int saw_benchmark_row_scratch;
  int saw_benchmark_row_kernel;
  int saw_benchmark_row_factor;
  int saw_benchmark_row_cyclotomic;
  int saw_assemble_complete;
  char sequence[1024];
} RunEventCapture;

static void capture_run_event(const char *stage, const char *status, const char *detail, void *user_data) {
  RunEventCapture *capture = (RunEventCapture *)user_data;
  if (!capture) return;
  capture->count++;
  if (strcmp(stage, "expression") == 0 && strcmp(status, "complete") == 0) capture->saw_expression_complete = 1;
  if (strcmp(stage, "factor") == 0) capture->saw_factor_event = 1;
  if (strcmp(stage, "benchmark") == 0 && strcmp(status, "running") == 0) capture->saw_benchmark_running = 1;
  if (strcmp(stage, "benchmark") == 0 && strcmp(status, "complete") == 0) capture->saw_benchmark_complete = 1;
  if (strcmp(stage, "benchmark") == 0 && strcmp(status, "disabled") == 0) capture->saw_benchmark_disabled = 1;
  if (strcmp(stage, "benchmark-row") == 0) {
    capture->benchmark_row_count++;
    if (detail && strstr(detail, "category=scratch-vs-gmp")) capture->saw_benchmark_row_scratch = 1;
    if (detail && strstr(detail, "category=kernel-probe")) capture->saw_benchmark_row_kernel = 1;
    if (detail && strstr(detail, "category=factor-benchmark")) capture->saw_benchmark_row_factor = 1;
    if (detail && strstr(detail, "category=cyclotomic-benchmark")) capture->saw_benchmark_row_cyclotomic = 1;
  }
  if (strcmp(stage, "assemble") == 0 && strcmp(status, "complete") == 0) capture->saw_assemble_complete = 1;
  size_t used = strlen(capture->sequence);
  if (used + strlen(stage) + strlen(status) + 4U < sizeof(capture->sequence)) {
    snprintf(capture->sequence + used, sizeof(capture->sequence) - used, "%s:%s;", stage, status);
  }
}

static void test_workspace_and_gnfs_artifacts(void) {
  XrayRunConfig config = xray_run_default_config();
  snprintf(config.workspace_root, sizeof(config.workspace_root), "native-test-runs");
  config.enable_benchmark = 0;
  RunEventCapture events;
  memset(&events, 0, sizeof(events));
  config.event_callback = capture_run_event;
  config.event_user_data = &events;
  XrayWorkbenchReport report;
  CHECK(xray_workbench_run("2^12 + 1", &config, &report));
  CHECK(report.expression.ok);
  CHECK(strcmp(report.expression.normalized, "4097") == 0);
  CHECK(report.run_dir != NULL);
  CHECK(report.json != NULL);
  CHECK(report.input_path != NULL);
  CHECK(report.normalized_path != NULL);
  CHECK(report.config_path != NULL);
  CHECK(report.cpu_features_path != NULL);
  CHECK(report.report_json_path != NULL);
  CHECK(report.events_jsonl_path != NULL);
  CHECK(report.benchmark_json_path == NULL);
  CHECK(report.benchmark_tsv_path == NULL);
  CHECK(report.benchmark_frontier_path == NULL);
  CHECK(report.benchmark_progress_path == NULL);
  CHECK(report.benchmark_progress_tsv_path == NULL);
  CHECK(report.cpu.logical_cpus >= 1);
  CHECK(report.gnfs.stage_count == 7);
  CHECK(strstr(report.json, "\"expression\"") != NULL);
  CHECK(strstr(report.json, "\"cpu\"") != NULL);
  CHECK(strstr(report.json, "\"artifactPaths\"") != NULL);
  CHECK(strstr(report.json, "\"benchmarkFrontier\":null") != NULL);
  CHECK(strstr(report.json, "\"benchmarkProgress\":null") != NULL);
  CHECK(strstr(report.json, "\"benchmarkProgressTsv\":null") != NULL);
  CHECK(strstr(report.json, "\"avx\"") != NULL);
  CHECK(strstr(report.json, "\"gnfsReport\"") != NULL);
  CHECK(strstr(report.events_jsonl, "\"stage\":\"benchmark\",\"status\":\"skipped\"") != NULL);
  CHECK(strstr(report.events_jsonl, "\"stage\":\"cpu\",\"status\":\"profiled\"") != NULL);
  char *normalized_text = read_text_file(report.normalized_path);
  char *config_text = read_text_file(report.config_path);
  char *report_json_text = read_text_file(report.report_json_path);
  char *events_text = read_text_file(report.events_jsonl_path);
  CHECK(strcmp(normalized_text, "4097") == 0);
  CHECK(strstr(config_text, "gnfsStageProof=1") != NULL);
  CHECK(strstr(report_json_text, "\"artifactPaths\"") != NULL);
  CHECK(strstr(events_text, "\"stage\":\"benchmark\",\"status\":\"skipped\"") != NULL);
  free(normalized_text);
  free(config_text);
  free(report_json_text);
  free(events_text);
  CHECK(events.count >= 10);
  CHECK(events.saw_expression_complete);
  CHECK(events.saw_factor_event);
  CHECK(events.saw_benchmark_disabled);
  CHECK(events.saw_assemble_complete);
  CHECK(strstr(events.sequence, "expression:running;cpu:profiled;expression:complete;") != NULL);
  xray_workbench_report_clear(&report);
}

static void test_report_json_ffi_helpers(void) {
  char *factor_json = xray_factor_solve_json("10_403");
  CHECK(factor_json != NULL);
  CHECK(strstr(factor_json, "\"factorReport\"") != NULL);
  CHECK(strstr(factor_json, "\"input\":\"10403\"") != NULL);
  CHECK(strstr(factor_json, "\"status\":\"solved\"") != NULL);
  CHECK(strstr(factor_json, "\"productVerified\":true") != NULL);
  xray_free(factor_json);

  char *invalid_factor_json = xray_factor_solve_json("12x");
  CHECK(invalid_factor_json != NULL);
  CHECK(strstr(invalid_factor_json, "\"factorReport\"") != NULL);
  CHECK(strstr(invalid_factor_json, "\"status\":\"invalid\"") != NULL);
  xray_free(invalid_factor_json);

  char *cyclotomic_json = xray_cyclotomic_scan_json("111");
  CHECK(cyclotomic_json != NULL);
  CHECK(strstr(cyclotomic_json, "\"cyclotomicReport\"") != NULL);
  CHECK(strstr(cyclotomic_json, "\"input\":\"111\"") != NULL);
  CHECK(strstr(cyclotomic_json, "\"exactMatches\":") != NULL);
  CHECK(strstr(cyclotomic_json, "\"verdict\":\"exact\"") != NULL);
  xray_free(cyclotomic_json);

  char *invalid_cyclotomic_json = xray_cyclotomic_scan_json("12x");
  CHECK(invalid_cyclotomic_json != NULL);
  CHECK(strstr(invalid_cyclotomic_json, "\"cyclotomicReport\"") != NULL);
  CHECK(strstr(invalid_cyclotomic_json, "Unexpected trailing input") != NULL);
  CHECK(strstr(invalid_cyclotomic_json, "\"scanned\":0") != NULL);
  xray_free(invalid_cyclotomic_json);

  char *workbench_json = xray_workbench_run_json("2^12 + 1");
  CHECK(workbench_json != NULL);
  CHECK(strstr(workbench_json, "\"app\":\"Number X-Ray Workbench\"") != NULL);
  CHECK(strstr(workbench_json, "\"normalized\":\"4097\"") != NULL);
  CHECK(strstr(workbench_json, "\"factorReport\"") != NULL);
  CHECK(strstr(workbench_json, "\"cyclotomicReport\"") != NULL);
  CHECK(strstr(workbench_json, "\"benchmarkReport\":null") != NULL);
  CHECK(strstr(workbench_json, "\"gnfsReport\"") != NULL);
  xray_free(workbench_json);
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
  RunEventCapture events;
  memset(&events, 0, sizeof(events));
  config.event_callback = capture_run_event;
  config.event_user_data = &events;
  XrayWorkbenchReport workbench;
  CHECK(xray_workbench_run("10403", &config, &workbench));
  XrayBenchmarkReport *report = &workbench.benchmark;
  CHECK(workbench.cpu.logical_cpus >= 1);
  CHECK(report->cpu.logical_cpus >= 1);
  CHECK(report->result_count >= 40);
  CHECK(report->passed_count == report->result_count);
  CHECK(events.saw_benchmark_running);
  CHECK(events.saw_benchmark_complete);
  CHECK(events.benchmark_row_count == report->result_count);
  CHECK(events.saw_benchmark_row_factor);
  CHECK(events.saw_benchmark_row_cyclotomic);
  CHECK(events.saw_benchmark_row_scratch);
  CHECK(events.saw_benchmark_row_kernel);
  size_t scratch_rows = 0;
  size_t kernel_rows = 0;
  size_t replacement_ready_rows = 0;
  size_t oracle_only_rows = 0;
  size_t blocked_rows = 0;
  size_t lane_promotion_ready_rows = 0;
  size_t lane_oracle_only_rows = 0;
  size_t lane_safety_rejected_rows = 0;
  int saw_8192_scratch = 0;
  int saw_16384_scratch_mul = 0;
  int saw_frontier_scout = 0;
  int saw_frontier_mul32768 = 0;
  int saw_frontier_mul65536 = 0;
  int saw_frontier_square32768 = 0;
  int saw_frontier_square65536 = 0;
  int saw_scratch_square = 0;
  int saw_scratch_format = 0;
  int saw_scratch_format768 = 0;
  int saw_scratch_format896 = 0;
  int saw_8192_kernel_probe = 0;
  int saw_16384_kernel_probe = 0;
  int saw_square_vs_mul_probe = 0;
  int saw_format_threshold_probe = 0;
  int saw_format_threshold8_probe = 0;
  int saw_format_threshold16_probe = 0;
  int saw_format_threshold24_probe = 0;
  int saw_format_threshold32_probe = 0;
  int saw_format_threshold40_probe = 0;
  int saw_format_threshold44_probe = 0;
  int saw_format_threshold48_probe = 0;
  int saw_format_threshold52_probe = 0;
  int saw_format_threshold56_probe = 0;
  int saw_format_threshold64_probe = 0;
  int saw_format_threshold80_probe = 0;
  int saw_format_threshold96_probe = 0;
  int saw_format_threshold128_probe = 0;
  int saw_format_threshold160_probe = 0;
  int saw_format_threshold192_probe = 0;
  int saw_format_threshold256limb_probe = 0;
  int saw_format_threshold256_probe = 0;
  int saw_format_threshold512_probe = 0;
  int saw_format_threshold1000_probe = 0;
  int saw_format_threshold2048_probe = 0;
  int saw_format_threshold4096_probe = 0;
  int saw_format_threshold8192_probe = 0;
  int saw_format_threshold16384_probe = 0;
  int saw_format_divider_probe = 0;
  int saw_format_divider1000_probe = 0;
  int saw_format_divider4096_probe = 0;
  int saw_format_divider8192_probe = 0;
  int saw_format_folded_probe = 0;
  int saw_format_folded1000_probe = 0;
  int saw_format_folded4096_probe = 0;
  int saw_format_folded8192_probe = 0;
  int saw_format_pair_writer_probe = 0;
  int saw_format_pair_writer_current_probe = 0;
  int saw_format_pair_writer_folded_probe = 0;
  int saw_format_pair_writer40_probe = 0;
  int saw_format_pair_writer150_probe = 0;
  int saw_format_pair_writer256_probe = 0;
  int saw_format_pair_writer512_probe = 0;
  int saw_format_pair_writer1000_probe = 0;
  int saw_format_pair_writer2048_probe = 0;
  int saw_format_pair_writer4096_probe = 0;
  int saw_format_pair_writer8192_probe = 0;
  int saw_format_pair_writer16384_probe = 0;
  int saw_format_wide_probe = 0;
  int saw_format_wide1000_probe = 0;
  int saw_format_wide4096_probe = 0;
  int saw_format_wide8192_probe = 0;
  int saw_format_mixed_writer_probe = 0;
  int saw_format_folded_hwdiv_probe = 0;
  int saw_format_hwdiv_mixed_probe = 0;
  int saw_format_divide_1e19_probe = 0;
  int saw_format_divide_1e19_pairs_probe = 0;
  int saw_format_divide_1e19_preinv_probe = 0;
  int saw_format_divide_1e19_preinv_pairs_probe = 0;
  int saw_format_dc_probe = 0;
  int saw_format_dc_leaf8_probe = 0;
  int saw_format_dc_leaf16_probe = 0;
  int saw_format_dc_leaf32_probe = 0;
  int saw_format_dc_leaf64_probe = 0;
  int saw_format_dc_ladder_probe = 0;
  int saw_format_dc_ladder_leaf8_probe = 0;
  int saw_format_dc_ladder_leaf16_probe = 0;
  int saw_format_dc_ladder_leaf32_probe = 0;
  int saw_format_dc_ladder_leaf64_probe = 0;
  int saw_format_dc_direct_probe = 0;
  int saw_format_dc_direct_leaf8_probe = 0;
  int saw_format_dc_direct_leaf16_probe = 0;
  int saw_format_dc_direct_leaf32_probe = 0;
  int saw_format_dc_direct_leaf64_probe = 0;
  int saw_format_dc_static_ladder_probe = 0;
  int saw_format_dc_static_ladder_leaf8_probe = 0;
  int saw_format_dc_static_ladder_leaf16_probe = 0;
  int saw_format_dc_static_ladder_leaf32_probe = 0;
  int saw_format_dc_static_ladder_leaf64_probe = 0;
  int saw_format_dc_static_direct_probe = 0;
  int saw_format_dc_static_direct_leaf8_probe = 0;
  int saw_format_dc_static_direct_leaf16_probe = 0;
  int saw_format_dc_static_direct_leaf32_probe = 0;
  int saw_format_dc_static_direct_leaf64_probe = 0;
  int saw_format_dc_workspace_probe = 0;
  int saw_format_dc_workspace_leaf8_probe = 0;
  int saw_format_dc_workspace_leaf16_probe = 0;
  int saw_format_dc_preinv_qhat_probe = 0;
  int saw_format_dc_preinv_qhat_leaf8_probe = 0;
  int saw_format_dc_preinv_qhat_leaf16_probe = 0;
  int saw_format_dc_route_probe = 0;
  int saw_format_dc_route1000_probe = 0;
  int saw_format_dc_route4096_probe = 0;
  int saw_format_dc_route8192_probe = 0;
  int saw_format_dc_route16384_probe = 0;
  int saw_format_dc_route_safety_gate = 0;
  int saw_format_route_tournament_probe = 0;
  int saw_format_route_tournament768_probe = 0;
  int saw_format_route_tournament896_probe = 0;
  int saw_format_route_tournament1000_probe = 0;
  int saw_format_route_tournament4096_probe = 0;
  int saw_format_route_tournament8192_probe = 0;
  int saw_format_route_tournament_detail = 0;
  int saw_format_route_tournament_detail_current = 0;
  int saw_format_route_tournament_detail_preinv = 0;
  size_t format_route_tournament_detail_rows = 0;
  int saw_policy_probe = 0;
  int saw_policy_gate = 0;
  int saw_format_policy_current = 0;
  int saw_format_policy_direct4096 = 0;
  int saw_format_policy_direct8192 = 0;
  int saw_format_policy_static4096 = 0;
  int saw_format_policy_static8192 = 0;
  int saw_format_policy_workspace4096 = 0;
  int saw_format_policy_preinv4096 = 0;
  int saw_format_policy_preinv8192 = 0;
  int saw_format_policy_preinv16384 = 0;
  int saw_format_policy_preinv10e19_window = 0;
  int saw_format_policy_preinv10e19_pairs_window = 0;
  int saw_format_policy_preinv10e19_window768_896 = 0;
  int saw_format_policy_preinv10e19_pairs_window768_896 = 0;
  int saw_format_policy_preinv10e19_window768_960 = 0;
  int saw_format_policy_preinv10e19_pairs_window768_960 = 0;
  int saw_format_policy_preinv10e19_window896_1000 = 0;
  int saw_format_policy_preinv10e19_pairs_window896_1000 = 0;
  int saw_format_policy_gate_direct4096 = 0;
  int saw_format_policy_gate_direct8192 = 0;
  int saw_format_policy_gate_static4096 = 0;
  int saw_format_policy_gate_static8192 = 0;
  int saw_format_policy_gate_workspace4096 = 0;
  int saw_format_policy_gate_preinv4096 = 0;
  int saw_format_policy_gate_preinv8192 = 0;
  int saw_format_policy_gate_preinv16384 = 0;
  int saw_format_policy_gate_preinv10e19_window = 0;
  int saw_format_policy_gate_preinv10e19_pairs_window = 0;
  int saw_format_policy_gate_preinv10e19_window768_896 = 0;
  int saw_format_policy_gate_preinv10e19_pairs_window768_896 = 0;
  int saw_format_policy_gate_preinv10e19_window768_960 = 0;
  int saw_format_policy_gate_preinv10e19_pairs_window768_960 = 0;
  int saw_format_policy_gate_preinv10e19_window896_1000 = 0;
  int saw_format_policy_gate_preinv10e19_pairs_window896_1000 = 0;
  int saw_format_policy_deep_gate_preinv10e19_window768_1000 = 0;
  int saw_format_policy_deep_gate_preinv10e19_window768_896 = 0;
  int saw_format_policy_deep_gate_preinv10e19_window768_960 = 0;
  int saw_format_policy_deep_gate_preinv10e19_window896_1000 = 0;
  int saw_format_policy_deep_gate_preinv10e19_pairs_window768_896 = 0;
  int saw_format_policy_deep_gate_preinv10e19_pairs_window768_960 = 0;
  int saw_format_policy_deep_gate_preinv10e19_pairs_window896_1000 = 0;
  int saw_format_policy_route_audit = 0;
  int saw_format_policy_route_audit_preinv10e19 = 0;
  int saw_format_policy_route_audit_preinv10e19_pairs = 0;
  int saw_divmod_preinv_qhat_safety_gate = 0;
  int saw_mul_policy_safety_threshold96_gate = 0;
  int saw_mul_policy_safety_toom_leaf48_gate = 0;
  int saw_mul_policy_safety_toom_rec_gate = 0;
  int saw_format_policy768_probe = 0;
  int saw_format_policy896_probe = 0;
  int saw_format_policy960_probe = 0;
  int saw_format_policy1000_probe = 0;
  int saw_format_policy4096_probe = 0;
  int saw_format_policy8192_probe = 0;
  int saw_format_policy16384_probe = 0;
  int saw_square_policy_probe = 0;
  int saw_square_policy_current = 0;
  int saw_square_policy_thr96 = 0;
  int saw_square_policy1000_probe = 0;
  int saw_square_policy4096_probe = 0;
  int saw_square_policy8192_probe = 0;
  int saw_square_policy16384_probe = 0;
  int saw_mul_policy_probe = 0;
  int saw_mul_policy_current = 0;
  int saw_mul_policy_threshold96 = 0;
  int saw_mul_policy_toom_leaf48 = 0;
  int saw_mul_policy_toom_rec = 0;
  int saw_mul_policy1000_probe = 0;
  int saw_mul_policy4096_probe = 0;
  int saw_mul_policy8192_probe = 0;
  int saw_mul_policy16384_probe = 0;
  int saw_mul_threshold_tournament_probe = 0;
  int saw_mul_threshold_tournament512_probe = 0;
  int saw_mul_threshold_tournament1000_probe = 0;
  int saw_mul_threshold_tournament2048_probe = 0;
  int saw_mul_threshold_tournament4096_probe = 0;
  int saw_mul_threshold_tournament8192_probe = 0;
  int saw_mul_threshold_tournament16384_probe = 0;
  int saw_karatsuba_middle_probe = 0;
  int saw_karatsuba_middle64_probe = 0;
  int saw_karatsuba_middle96_probe = 0;
  int saw_karatsuba_middle128_probe = 0;
  int saw_karatsuba_middle1000_probe = 0;
  int saw_karatsuba_middle4096_probe = 0;
  int saw_karatsuba_middle8192_probe = 0;
  int saw_karatsuba_middle16384_probe = 0;
  int saw_format_strategy1000_probe = 0;
  int saw_format_strategy4096_probe = 0;
  int saw_format_strategy8192_probe = 0;
  int saw_format_strategy16384_probe = 0;
  int saw_square_karatsuba_vs_mul_probe = 0;
  int saw_square_karatsuba_vs_gmp_probe = 0;
  int saw_square_leaf_order_probe = 0;
  int saw_square_leaf_order1000_probe = 0;
  int saw_square_leaf_order4096_probe = 0;
  int saw_square_leaf_order8192_probe = 0;
  int saw_square_leaf_order16384_probe = 0;
  int saw_toom3_probe = 0;
  int saw_toom3_vs_scratch_probe = 0;
  int saw_toom3_unroll4_vs_scratch_probe = 0;
  int saw_toom3_unroll4_vs_gmp_probe = 0;
  int saw_toom3_unroll4_deep_vs_gmp_probe = 0;
  int saw_toom3_unroll4_deep_leaf64_probe = 0;
  int saw_toom3_unroll4_deep_leaf96_probe = 0;
  int saw_toom3_unroll4_recursive_vs_gmp_probe = 0;
  int saw_toom3_unroll4_recursive_deep_vs_gmp_probe = 0;
  int saw_toom3_unroll4_recursive_deep_leaf64_probe = 0;
  int saw_toom3_unroll4_recursive_deep_leaf96_probe = 0;
  int saw_muladd_bmi2_adx_probe = 0;
  int saw_muladd_unroll_probe = 0;
  int saw_muladd_unroll8_probe = 0;
  int saw_qhat_preinv_probe = 0;
  int saw_qhat_u32_limb_probe = 0;
  int saw_u32_precompute_probe = 0;
  int saw_mod_u32_precompute_probe = 0;
  int saw_gcd_u32_precompute_probe = 0;
  int saw_powmod_u32_precompute_probe = 0;
  int saw_u32_precompute40_probe = 0;
  int saw_u32_precompute150_probe = 0;
  int saw_u32_precompute512_probe = 0;
  int saw_u32_precompute1000_probe = 0;
  int saw_u32_precompute2048_probe = 0;
  int saw_u32_precompute4096_probe = 0;
  int saw_u32_precompute8192_probe = 0;
  int saw_u32_precompute16384_probe = 0;
  int saw_parse_chunk_probe = 0;
  int saw_parse_chunk8_probe = 0;
  int saw_parse_chunk9_probe = 0;
  int saw_parse_chunk10_probe = 0;
  int saw_parse_chunk15_probe = 0;
  int saw_parse_chunk18_probe = 0;
  int saw_parse_chunk19_probe = 0;
  int saw_parse_chunk40_probe = 0;
  int saw_parse_chunk150_probe = 0;
  int saw_parse_chunk512_probe = 0;
  int saw_parse_chunk1000_probe = 0;
  int saw_parse_chunk2048_probe = 0;
  int saw_parse_chunk4096_probe = 0;
  int saw_parse_chunk8192_probe = 0;
  int saw_parse_chunk16384_probe = 0;
  int saw_divmod_dc_power_probe = 0;
  int saw_divmod_dc_power4096_probe = 0;
  int saw_divmod_dc_power8192_probe = 0;
  int saw_divmod_dc_power16384_probe = 0;
  int saw_divmod_precomputed_probe = 0;
  int saw_divmod_precomputed4096_probe = 0;
  int saw_divmod_precomputed8192_probe = 0;
  int saw_divmod_precomputed16384_probe = 0;
  int saw_divmod_workspace_probe = 0;
  int saw_divmod_workspace4096_probe = 0;
  int saw_divmod_workspace8192_probe = 0;
  int saw_divmod_workspace16384_probe = 0;
  int saw_divmod_preinv_qhat_probe = 0;
  int saw_divmod_preinv_qhat4096_probe = 0;
  int saw_divmod_preinv_qhat8192_probe = 0;
  int saw_divmod_preinv_qhat16384_probe = 0;
  int saw_mul_unroll4_vs_scratch_probe = 0;
  int saw_mul_unroll4_vs_gmp_probe = 0;
  int saw_mul_unroll4_deep_vs_gmp_probe = 0;
  for (size_t index = 0; index < report->result_count; ++index) {
    char lane_brief[192];
    if (xray_benchmark_result_is_promotion_ready(&report->results[index])) {
      lane_promotion_ready_rows++;
      xray_benchmark_result_brief(&report->results[index], index + 1U, lane_brief, sizeof(lane_brief));
      CHECK(strstr(lane_brief, "d=") != NULL);
    }
    if (xray_benchmark_result_is_oracle_only(&report->results[index])) {
      lane_oracle_only_rows++;
      xray_benchmark_result_brief(&report->results[index], index + 1U, lane_brief, sizeof(lane_brief));
      CHECK(strstr(lane_brief, "r=") != NULL);
    }
    if (xray_benchmark_result_is_safety_rejected(&report->results[index])) {
      lane_safety_rejected_rows++;
      xray_benchmark_result_brief(&report->results[index], index + 1U, lane_brief, sizeof(lane_brief));
      CHECK(strstr(lane_brief, "s=") != NULL);
    }
    if (strcmp(report->results[index].category, "scratch-vs-gmp") == 0) {
      scratch_rows++;
      CHECK(report->results[index].parity_verified);
      CHECK(report->results[index].scratch_us > 0);
      CHECK(report->results[index].gmp_us > 0);
      CHECK(report->results[index].speed_ratio > 0.0);
      CHECK(report->results[index].max_allowed_speed_ratio == 1.0);
      CHECK(report->results[index].sample_count == 5);
      CHECK(report->results[index].stable_sample_count <= report->results[index].sample_count);
      if (report->results[index].digits == 8192) saw_8192_scratch = 1;
      if (report->results[index].digits == 16384 && strcmp(report->results[index].operation, "mul") == 0) saw_16384_scratch_mul = 1;
      if (strcmp(report->results[index].operation, "square") == 0) saw_scratch_square = 1;
      if (strcmp(report->results[index].operation, "format") == 0) {
        saw_scratch_format = 1;
        if (report->results[index].digits == 768) saw_scratch_format768 = 1;
        if (report->results[index].digits == 896) saw_scratch_format896 = 1;
      }
      CHECK(strstr(report->results[index].detail, "ratioMethod=paired-median") != NULL);
      CHECK(strstr(report->results[index].detail, "stablePairs=") != NULL);
      CHECK(strstr(report->results[index].detail, "worstPairRatio=") != NULL);
      CHECK(strstr(report->results[index].detail, strcmp(report->results[index].operation, "mul") == 0 ? "operandFamilies=2" : "operandFamilies=1") != NULL);
      const char *adoption = xray_scratch_adoption_for_result(&report->results[index]);
      CHECK(strcmp(report->results[index].adoption, adoption) == 0);
      CHECK(report->results[index].replacement_ready == (strcmp(adoption, "allowed") == 0));
      if (strcmp(adoption, "allowed") == 0) {
        CHECK(report->results[index].stable_sample_count >= 4);
        CHECK(report->results[index].worst_pair_ratio <= 1.0);
        replacement_ready_rows++;
      }
      else if (strcmp(adoption, "oracle-only") == 0) oracle_only_rows++;
      else blocked_rows++;
    } else if (strcmp(report->results[index].category, "frontier-scout") == 0) {
      saw_frontier_scout = 1;
      CHECK(report->results[index].passed);
      CHECK(report->results[index].parity_verified);
      CHECK(!report->results[index].replacement_ready);
      CHECK(strcmp(report->results[index].adoption, "observe-only") == 0);
      CHECK(strcmp(report->results[index].status, "noisy-control") == 0 ||
          strcmp(report->results[index].status, "stable-control") == 0);
      CHECK(report->results[index].scratch_us > 0);
      CHECK(report->results[index].gmp_us > 0);
      CHECK(report->results[index].speed_ratio > 0.0);
      CHECK(report->results[index].max_allowed_speed_ratio == 1.0);
      CHECK(report->results[index].sample_count == 3);
      CHECK(report->results[index].stable_sample_count <= report->results[index].sample_count);
      CHECK(strstr(report->results[index].detail, "op=frontier-scout") != NULL);
      CHECK(strstr(report->results[index].detail, "estimatedBits=") != NULL);
      CHECK(strstr(report->results[index].detail, "warmupPasses=1") != NULL);
      CHECK(strstr(report->results[index].detail, "ratioMethod=paired-median") != NULL);
      CHECK(strstr(report->results[index].detail, "duplicateControl=default") != NULL);
      CHECK(strstr(report->results[index].detail, "controlPlacement=tail") != NULL);
      CHECK(strstr(report->results[index].detail, "controlSafety=") != NULL);
      CHECK(strstr(report->results[index].detail, "controlRatio=") != NULL);
      CHECK(strstr(report->results[index].detail, "controlWorst=") != NULL);
      CHECK(strstr(report->results[index].detail, "controlStable=") != NULL);
      CHECK(strstr(report->results[index].detail, "baseline=mpz_mul") != NULL);
      CHECK(strstr(report->results[index].detail, "oracle=mpz_mul") != NULL);
      CHECK(strstr(report->results[index].detail, "featureGate=very-large-frontier-scout") != NULL);
      CHECK(strstr(report->results[index].detail, "gmpClue=mfast8m-difdit24-complete") != NULL);
      CHECK(strstr(report->results[index].detail, "mfastKnob=difdit24k") != NULL);
      CHECK(strstr(report->results[index].detail, "mfastPG=1.612") != NULL);
      CHECK(strstr(report->results[index].detail, "mfastCW=1.078") != NULL);
      CHECK(strstr(report->results[index].detail, "mfastPocket=difdit98304") != NULL);
      CHECK(strstr(report->results[index].detail, "mfastPocketFloor=98304") != NULL);
      CHECK(strstr(report->results[index].detail, "mfastPocket6144=1.019") != NULL);
      CHECK(strstr(report->results[index].detail, "mfastPocket8192PG=1.320") != NULL);
      CHECK(strstr(report->results[index].detail, "mfastPocketGate=noisy-control") != NULL);
      CHECK(strstr(report->results[index].detail, "mfast1mGate=noisy") != NULL);
      CHECK(strstr(report->results[index].detail, "mfast1mBest=difdit16000PG1.480") != NULL);
      CHECK(strstr(report->results[index].detail, "mfast16mBest=difdit24k") != NULL);
      CHECK(strstr(report->results[index].detail, "mfast16mD98304=0.404") != NULL);
      CHECK(strstr(report->results[index].detail, "noAutoRoute=1") != NULL);
      if (strcmp(report->results[index].operation, "mul-frontier") == 0 &&
          report->results[index].digits == 32768) saw_frontier_mul32768 = 1;
      else if (strcmp(report->results[index].operation, "mul-frontier") == 0 &&
          report->results[index].digits == 65536) saw_frontier_mul65536 = 1;
      else if (strcmp(report->results[index].operation, "square-frontier") == 0 &&
          report->results[index].digits == 32768) saw_frontier_square32768 = 1;
      else if (strcmp(report->results[index].operation, "square-frontier") == 0 &&
          report->results[index].digits == 65536) saw_frontier_square65536 = 1;
      else CHECK(0);
    } else if (strcmp(report->results[index].category, "kernel-probe") == 0) {
      kernel_rows++;
      if (report->results[index].digits == 8192) saw_8192_kernel_probe = 1;
      if (report->results[index].digits == 16384) saw_16384_kernel_probe = 1;
      CHECK(report->results[index].passed);
      CHECK(report->results[index].scratch_us > 0);
      CHECK(report->results[index].gmp_us > 0);
      CHECK(report->results[index].speed_ratio > 0.0);
      CHECK(report->results[index].max_allowed_speed_ratio == 0.98);
      if (strcmp(report->results[index].operation, "mul-unroll4-deep-vs-gmp") == 0 ||
          strcmp(report->results[index].operation, "mul-toom3-unroll4-deep-vs-gmp") == 0 ||
          strcmp(report->results[index].operation, "mul-toom3-u4-rec-deep-vs-gmp") == 0) {
        CHECK(report->results[index].sample_count == 9);
      } else {
        CHECK(report->results[index].sample_count == 5);
      }
      CHECK(report->results[index].stable_sample_count <= report->results[index].sample_count);
      CHECK(strstr(report->results[index].detail, "ratioMethod=paired-median") != NULL);
      CHECK(strstr(report->results[index].detail, "stablePairs=") != NULL);
      CHECK(strstr(report->results[index].detail, "worstPairRatio=") != NULL);
      CHECK(strstr(report->results[index].detail, "featureGate=") != NULL);
      CHECK(strstr(report->results[index].detail, "gmpClue=") != NULL);
      CHECK(strstr(report->results[index].detail, "adoption=") != NULL);
      if (strcmp(report->results[index].operation, "mul-threshold") == 0) {
        CHECK(strstr(report->results[index].detail, "operandFamilies=2") != NULL);
      }
      if (strcmp(report->results[index].operation, "mul-threshold-tournament") == 0) {
        saw_mul_threshold_tournament_probe = 1;
        if (report->results[index].digits == 512) saw_mul_threshold_tournament512_probe = 1;
        else if (report->results[index].digits == 1000) saw_mul_threshold_tournament1000_probe = 1;
        else if (report->results[index].digits == 2048) saw_mul_threshold_tournament2048_probe = 1;
        else if (report->results[index].digits == 4096) saw_mul_threshold_tournament4096_probe = 1;
        else if (report->results[index].digits == 8192) saw_mul_threshold_tournament8192_probe = 1;
        else if (report->results[index].digits == 16384) saw_mul_threshold_tournament16384_probe = 1;
        else CHECK(0);
        CHECK(report->results[index].parity_verified);
        CHECK(!report->results[index].replacement_ready);
        CHECK(strcmp(report->results[index].adoption, "observe-only") == 0);
        CHECK(strstr(report->results[index].detail, "bestThreshold=") != NULL);
        CHECK(strstr(report->results[index].detail, "currentThreshold=64") != NULL);
        CHECK(strstr(report->results[index].detail, "thresholdsTested=7") != NULL);
        CHECK(strstr(report->results[index].detail, "candidateThresholds=32,48,64,80,96,128,160") != NULL);
        CHECK(strstr(report->results[index].detail, "candidate=best-threshold-mul-production-leaf") != NULL);
        CHECK(strstr(report->results[index].detail, "baseline=current-scratch-mul") != NULL);
        CHECK(strstr(report->results[index].detail, "featureGate=root-size-threshold-tournament") != NULL);
        CHECK(strstr(report->results[index].detail, "gmpClue=mpn_mul-threshold-table") != NULL);
        CHECK(strstr(report->results[index].detail, "noAutoRoute=1") != NULL);
        CHECK(strstr(report->results[index].detail, "operandFamilies=2") != NULL);
      }
      if (strcmp(report->results[index].operation, "qhat-u32-limb") == 0) {
        saw_qhat_u32_limb_probe = 1;
        CHECK(report->results[index].digits == 0);
        CHECK(report->results[index].parity_verified);
        CHECK(!report->results[index].replacement_ready);
        CHECK(strcmp(report->results[index].adoption, "observe-only") == 0);
        CHECK(strstr(report->results[index].detail, "cases=4096") != NULL);
        CHECK(strstr(report->results[index].detail, "passesPerSample=96") != NULL);
        CHECK(strstr(report->results[index].detail, "candidate=u32-limb-knuth-qhat") != NULL);
        CHECK(strstr(report->results[index].detail, "baseline=direct-udiv128-qhat") != NULL);
        CHECK(strstr(report->results[index].detail, "parityTarget=qhat+rhat") != NULL);
        CHECK(strstr(report->results[index].detail, "featureGate=division-qhat-estimator") != NULL);
        CHECK(strstr(report->results[index].detail, "gmpClue=mpn_sbpi1_div_qr-qhat") != NULL);
        CHECK(strstr(report->results[index].detail, "precomputeScope=per-divisor") != NULL);
        CHECK(strstr(report->results[index].detail, "noAutoRoute=1") != NULL);
      }
      if (strcmp(report->results[index].operation, "qhat-preinv") == 0) {
        saw_qhat_preinv_probe = 1;
        CHECK(report->results[index].digits == 0);
        CHECK(report->results[index].parity_verified);
        CHECK(!report->results[index].replacement_ready);
        CHECK(strcmp(report->results[index].adoption, "observe-only") == 0);
        CHECK(strstr(report->results[index].detail, "cases=4096") != NULL);
        CHECK(strstr(report->results[index].detail, "passesPerSample=96") != NULL);
        CHECK(strstr(report->results[index].detail, "candidate=preinverted-limb-qhat") != NULL);
        CHECK(strstr(report->results[index].detail, "baseline=direct-udiv128-qhat") != NULL);
        CHECK(strstr(report->results[index].detail, "parityTarget=qhat+rhat") != NULL);
        CHECK(strstr(report->results[index].detail, "featureGate=division-qhat-estimator") != NULL);
        CHECK(strstr(report->results[index].detail, "gmpClue=mpn_sbpi1_div_qr-qhat") != NULL);
        CHECK(strstr(report->results[index].detail, "precomputeScope=per-divisor") != NULL);
        CHECK(strstr(report->results[index].detail, "noAutoRoute=1") != NULL);
      }
      if (strcmp(report->results[index].operation, "mul-karatsuba-middle") == 0) {
        saw_karatsuba_middle_probe = 1;
        CHECK(strstr(report->results[index].detail, "mode=sum-vs-difference") != NULL);
        CHECK(strstr(report->results[index].detail, "candidate=karatsuba-sum-middle") != NULL);
        CHECK(strstr(report->results[index].detail, "baseline=karatsuba-difference-middle") != NULL);
        CHECK(strstr(report->results[index].detail, "featureGate=karatsuba-middle-form") != NULL);
        CHECK(strstr(report->results[index].detail, "gmpClue=mpn_mul_n-middle-term") != NULL);
        CHECK(strstr(report->results[index].detail, "operandFamilies=2") != NULL);
        if (strstr(report->results[index].detail, "threshold=64 ") != NULL) saw_karatsuba_middle64_probe = 1;
        else if (strstr(report->results[index].detail, "threshold=96 ") != NULL) saw_karatsuba_middle96_probe = 1;
        else if (strstr(report->results[index].detail, "threshold=128 ") != NULL) saw_karatsuba_middle128_probe = 1;
        else CHECK(0);
        if (report->results[index].digits == 1000) saw_karatsuba_middle1000_probe = 1;
        else if (report->results[index].digits == 4096) saw_karatsuba_middle4096_probe = 1;
        else if (report->results[index].digits == 8192) saw_karatsuba_middle8192_probe = 1;
        else if (report->results[index].digits == 16384) saw_karatsuba_middle16384_probe = 1;
        else CHECK(0);
      }
      if (strcmp(report->results[index].operation, "mod-u32-precompute") == 0 ||
          strcmp(report->results[index].operation, "gcd-u32-precompute") == 0 ||
          strcmp(report->results[index].operation, "powmod-u32-precompute") == 0) {
        saw_u32_precompute_probe = 1;
        if (strcmp(report->results[index].operation, "mod-u32-precompute") == 0) saw_mod_u32_precompute_probe = 1;
        else if (strcmp(report->results[index].operation, "gcd-u32-precompute") == 0) saw_gcd_u32_precompute_probe = 1;
        else saw_powmod_u32_precompute_probe = 1;
        if (report->results[index].digits == 40) saw_u32_precompute40_probe = 1;
        else if (report->results[index].digits == 150) saw_u32_precompute150_probe = 1;
        else if (report->results[index].digits == 512) saw_u32_precompute512_probe = 1;
        else if (report->results[index].digits == 1000) saw_u32_precompute1000_probe = 1;
        else if (report->results[index].digits == 2048) saw_u32_precompute2048_probe = 1;
        else if (report->results[index].digits == 4096) saw_u32_precompute4096_probe = 1;
        else if (report->results[index].digits == 8192) saw_u32_precompute8192_probe = 1;
        else if (report->results[index].digits == 16384) saw_u32_precompute16384_probe = 1;
        else CHECK(0);
        CHECK(strstr(report->results[index].detail, "candidate=u32-mod-context") != NULL);
        CHECK(strstr(report->results[index].detail, "baseline=one-shot-u32") != NULL);
        CHECK(strstr(report->results[index].detail, "featureGate=u32-mod-context") != NULL);
        CHECK(strstr(report->results[index].detail, "modulus=1000000007") != NULL);
      }
      if (strcmp(report->results[index].operation, "parse-chunk") == 0) {
        saw_parse_chunk_probe = 1;
        if (strstr(report->results[index].detail, "chunkDigits=8") != NULL) saw_parse_chunk8_probe = 1;
        else if (strstr(report->results[index].detail, "chunkDigits=9") != NULL) saw_parse_chunk9_probe = 1;
        else if (strstr(report->results[index].detail, "chunkDigits=10") != NULL) saw_parse_chunk10_probe = 1;
        else if (strstr(report->results[index].detail, "chunkDigits=15") != NULL) saw_parse_chunk15_probe = 1;
        else if (strstr(report->results[index].detail, "chunkDigits=18") != NULL) saw_parse_chunk18_probe = 1;
        else if (strstr(report->results[index].detail, "chunkDigits=19") != NULL) saw_parse_chunk19_probe = 1;
        else CHECK(0);
        if (report->results[index].digits == 40) saw_parse_chunk40_probe = 1;
        else if (report->results[index].digits == 150) saw_parse_chunk150_probe = 1;
        else if (report->results[index].digits == 512) saw_parse_chunk512_probe = 1;
        else if (report->results[index].digits == 1000) saw_parse_chunk1000_probe = 1;
        else if (report->results[index].digits == 2048) saw_parse_chunk2048_probe = 1;
        else if (report->results[index].digits == 4096) saw_parse_chunk4096_probe = 1;
        else if (report->results[index].digits == 8192) saw_parse_chunk8192_probe = 1;
        else if (report->results[index].digits == 16384) saw_parse_chunk16384_probe = 1;
        else CHECK(0);
        CHECK(strstr(report->results[index].detail, "candidate=decimal-parse-chunk") != NULL);
        CHECK(strstr(report->results[index].detail, "baseline=current-scratch-parse") != NULL);
        CHECK(strstr(report->results[index].detail, "featureGate=decimal-parse-chunk") != NULL);
        CHECK(strstr(report->results[index].detail, "operandFamilies=1") != NULL);
      }
      if (strcmp(report->results[index].operation, "divmod-dc-power") == 0) {
        saw_divmod_dc_power_probe = 1;
        if (report->results[index].digits == 4096) {
          saw_divmod_dc_power4096_probe = 1;
          CHECK(strstr(report->results[index].detail, "powerChunks=107") != NULL);
          CHECK(strstr(report->results[index].detail, "divisorDigits=2034") != NULL);
        } else if (report->results[index].digits == 8192) {
          saw_divmod_dc_power8192_probe = 1;
          CHECK(strstr(report->results[index].detail, "powerChunks=215") != NULL);
          CHECK(strstr(report->results[index].detail, "divisorDigits=4086") != NULL);
        } else if (report->results[index].digits == 16384) {
          saw_divmod_dc_power16384_probe = 1;
          CHECK(strstr(report->results[index].detail, "powerChunks=431") != NULL);
          CHECK(strstr(report->results[index].detail, "divisorDigits=8190") != NULL);
        } else {
          CHECK(0);
        }
        CHECK(strstr(report->results[index].detail, "candidate=scratch-knuth-divmod") != NULL);
        CHECK(strstr(report->results[index].detail, "baseline=mpz_tdiv_qr") != NULL);
        CHECK(strstr(report->results[index].detail, "featureGate=bigint-division-dc-power") != NULL);
        CHECK(strstr(report->results[index].detail, "gmpClue=mpn_tdiv_qr") != NULL);
        CHECK(strstr(report->results[index].detail, "operandFamilies=1") != NULL);
      }
      if (strcmp(report->results[index].operation, "divmod-precomputed") == 0) {
        saw_divmod_precomputed_probe = 1;
        if (report->results[index].digits == 4096) {
          saw_divmod_precomputed4096_probe = 1;
          CHECK(strstr(report->results[index].detail, "powerChunks=107") != NULL);
          CHECK(strstr(report->results[index].detail, "divisorDigits=2034") != NULL);
        } else if (report->results[index].digits == 8192) {
          saw_divmod_precomputed8192_probe = 1;
          CHECK(strstr(report->results[index].detail, "powerChunks=215") != NULL);
          CHECK(strstr(report->results[index].detail, "divisorDigits=4086") != NULL);
        } else if (report->results[index].digits == 16384) {
          saw_divmod_precomputed16384_probe = 1;
          CHECK(strstr(report->results[index].detail, "powerChunks=431") != NULL);
          CHECK(strstr(report->results[index].detail, "divisorDigits=8190") != NULL);
        } else {
          CHECK(0);
        }
        CHECK(strstr(report->results[index].detail, "candidate=scratch-divmod-precomputed") != NULL);
        CHECK(strstr(report->results[index].detail, "baseline=current-scratch-divmod") != NULL);
        CHECK(strstr(report->results[index].detail, "oracle=mpz_tdiv_qr") != NULL);
        CHECK(strstr(report->results[index].detail, "featureGate=bigint-division-context") != NULL);
        CHECK(strstr(report->results[index].detail, "gmpClue=mpn_tdiv_qr-precomputed-divisor") != NULL);
        CHECK(strstr(report->results[index].detail, "thresholdSafety=explicit-context") != NULL);
        CHECK(strstr(report->results[index].detail, "setupSamples=5") != NULL);
        CHECK(strstr(report->results[index].detail, "setupIterations=") != NULL);
        CHECK(strstr(report->results[index].detail, "setupUs=") != NULL);
        CHECK(strstr(report->results[index].detail, "setupPolicy=reported-not-scored") != NULL);
        CHECK(strstr(report->results[index].detail, "cacheRole=divisor-context") != NULL);
        CHECK(strstr(report->results[index].detail, "noAutoRoute=1") != NULL);
        CHECK(strstr(report->results[index].detail, "operandFamilies=1") != NULL);
        CHECK(!report->results[index].replacement_ready);
        CHECK(strcmp(report->results[index].adoption, "observe-only") == 0);
      }
      if (strcmp(report->results[index].operation, "divmod-workspace") == 0) {
        saw_divmod_workspace_probe = 1;
        if (report->results[index].digits == 4096) {
          saw_divmod_workspace4096_probe = 1;
          CHECK(strstr(report->results[index].detail, "powerChunks=107") != NULL);
          CHECK(strstr(report->results[index].detail, "divisorDigits=2034") != NULL);
        } else if (report->results[index].digits == 8192) {
          saw_divmod_workspace8192_probe = 1;
          CHECK(strstr(report->results[index].detail, "powerChunks=215") != NULL);
          CHECK(strstr(report->results[index].detail, "divisorDigits=4086") != NULL);
        } else if (report->results[index].digits == 16384) {
          saw_divmod_workspace16384_probe = 1;
          CHECK(strstr(report->results[index].detail, "powerChunks=431") != NULL);
          CHECK(strstr(report->results[index].detail, "divisorDigits=8190") != NULL);
        } else {
          CHECK(0);
        }
        CHECK(strstr(report->results[index].detail, "candidate=scratch-divmod-context-workspace") != NULL);
        CHECK(strstr(report->results[index].detail, "baseline=scratch-divmod-precomputed") != NULL);
        CHECK(strstr(report->results[index].detail, "oracle=mpz_tdiv_qr") != NULL);
        CHECK(strstr(report->results[index].detail, "featureGate=bigint-division-workspace") != NULL);
        CHECK(strstr(report->results[index].detail, "gmpClue=mpn_tdiv_qr-scratch-reuse") != NULL);
        CHECK(strstr(report->results[index].detail, "thresholdSafety=explicit-workspace") != NULL);
        CHECK(strstr(report->results[index].detail, "setupSamples=5") != NULL);
        CHECK(strstr(report->results[index].detail, "setupIterations=") != NULL);
        CHECK(strstr(report->results[index].detail, "setupUs=") != NULL);
        CHECK(strstr(report->results[index].detail, "setupPolicy=reported-not-scored") != NULL);
        CHECK(strstr(report->results[index].detail, "cacheRole=divisor-context") != NULL);
        CHECK(strstr(report->results[index].detail, "noAutoRoute=1") != NULL);
        CHECK(strstr(report->results[index].detail, "operandFamilies=1") != NULL);
        CHECK(!report->results[index].replacement_ready);
        CHECK(strcmp(report->results[index].adoption, "observe-only") == 0);
      }
      if (strcmp(report->results[index].operation, "divmod-preinv-qhat") == 0) {
        saw_divmod_preinv_qhat_probe = 1;
        if (report->results[index].digits == 4096) {
          saw_divmod_preinv_qhat4096_probe = 1;
          CHECK(strstr(report->results[index].detail, "powerChunks=107") != NULL);
          CHECK(strstr(report->results[index].detail, "divisorDigits=2034") != NULL);
        } else if (report->results[index].digits == 8192) {
          saw_divmod_preinv_qhat8192_probe = 1;
          CHECK(strstr(report->results[index].detail, "powerChunks=215") != NULL);
          CHECK(strstr(report->results[index].detail, "divisorDigits=4086") != NULL);
        } else if (report->results[index].digits == 16384) {
          saw_divmod_preinv_qhat16384_probe = 1;
          CHECK(strstr(report->results[index].detail, "powerChunks=431") != NULL);
          CHECK(strstr(report->results[index].detail, "divisorDigits=8190") != NULL);
        } else {
          CHECK(0);
        }
        CHECK(strstr(report->results[index].detail, "candidate=scratch-divmod-preinv-qhat") != NULL);
        CHECK(strstr(report->results[index].detail, "baseline=scratch-divmod-context-workspace") != NULL);
        CHECK(strstr(report->results[index].detail, "oracle=mpz_tdiv_qr") != NULL);
        CHECK(strstr(report->results[index].detail, "featureGate=bigint-division-preinv-qhat") != NULL);
        CHECK(strstr(report->results[index].detail, "gmpClue=mpn_sbpi1_div_qr-qhat") != NULL);
        CHECK(strstr(report->results[index].detail, "precomputeScope=per-divisor") != NULL);
        CHECK(strstr(report->results[index].detail, "setupSamples=5") != NULL);
        CHECK(strstr(report->results[index].detail, "setupIterations=") != NULL);
        CHECK(strstr(report->results[index].detail, "setupUs=") != NULL);
        CHECK(strstr(report->results[index].detail, "setupPolicy=reported-not-scored") != NULL);
        CHECK(strstr(report->results[index].detail, "cacheRole=divisor-context") != NULL);
        CHECK(strstr(report->results[index].detail, "thresholdSafety=explicit-probe") != NULL);
        CHECK(strstr(report->results[index].detail, "noAutoRoute=1") != NULL);
        CHECK(strstr(report->results[index].detail, "operandFamilies=1") != NULL);
        CHECK(!report->results[index].replacement_ready);
        CHECK(strcmp(report->results[index].adoption, "observe-only") == 0);
      }
      if (strcmp(report->results[index].operation, "format-threshold") == 0) {
        saw_format_threshold_probe = 1;
        if (strstr(report->results[index].detail, "threshold=8 ") != NULL) saw_format_threshold8_probe = 1;
        else if (strstr(report->results[index].detail, "threshold=16 ") != NULL) saw_format_threshold16_probe = 1;
        else if (strstr(report->results[index].detail, "threshold=24 ") != NULL) saw_format_threshold24_probe = 1;
        else if (strstr(report->results[index].detail, "threshold=32 ") != NULL) saw_format_threshold32_probe = 1;
        else if (strstr(report->results[index].detail, "threshold=40 ") != NULL) saw_format_threshold40_probe = 1;
        else if (strstr(report->results[index].detail, "threshold=44 ") != NULL) saw_format_threshold44_probe = 1;
        else if (strstr(report->results[index].detail, "threshold=48 ") != NULL) saw_format_threshold48_probe = 1;
        else if (strstr(report->results[index].detail, "threshold=52 ") != NULL) saw_format_threshold52_probe = 1;
        else if (strstr(report->results[index].detail, "threshold=56 ") != NULL) saw_format_threshold56_probe = 1;
        else if (strstr(report->results[index].detail, "threshold=64 ") != NULL) saw_format_threshold64_probe = 1;
        else if (strstr(report->results[index].detail, "threshold=80 ") != NULL) saw_format_threshold80_probe = 1;
        else if (strstr(report->results[index].detail, "threshold=96 ") != NULL) saw_format_threshold96_probe = 1;
        else if (strstr(report->results[index].detail, "threshold=128 ") != NULL) saw_format_threshold128_probe = 1;
        else if (strstr(report->results[index].detail, "threshold=160 ") != NULL) saw_format_threshold160_probe = 1;
        else if (strstr(report->results[index].detail, "threshold=192 ") != NULL) saw_format_threshold192_probe = 1;
        else if (strstr(report->results[index].detail, "threshold=256 ") != NULL) saw_format_threshold256limb_probe = 1;
        else CHECK(0);
        if (report->results[index].digits == 256) saw_format_threshold256_probe = 1;
        else if (report->results[index].digits == 512) saw_format_threshold512_probe = 1;
        else if (report->results[index].digits == 1000) saw_format_threshold1000_probe = 1;
        else if (report->results[index].digits == 2048) saw_format_threshold2048_probe = 1;
        else if (report->results[index].digits == 4096) saw_format_threshold4096_probe = 1;
        else if (report->results[index].digits == 8192) saw_format_threshold8192_probe = 1;
        else if (report->results[index].digits == 16384) saw_format_threshold16384_probe = 1;
        else CHECK(0);
        CHECK(strstr(report->results[index].detail, "candidate=decimal-horner") != NULL);
        CHECK(strstr(report->results[index].detail, "baseline=mpz_get_str") != NULL);
        CHECK(strstr(report->results[index].detail, "featureGate=decimal-format-handoff") != NULL);
        CHECK(strstr(report->results[index].detail, "operandFamilies=1") != NULL);
      }
      if (strcmp(report->results[index].operation, "format-divider") == 0) {
        saw_format_divider_probe = 1;
        if (report->results[index].digits == 1000) saw_format_divider1000_probe = 1;
        else if (report->results[index].digits == 4096) saw_format_divider4096_probe = 1;
        else if (report->results[index].digits == 8192) saw_format_divider8192_probe = 1;
        else CHECK(0);
        CHECK(strstr(report->results[index].detail, "mode=direct128") != NULL);
        CHECK(strstr(report->results[index].detail, "candidate=decimal-horner-direct-divider") != NULL);
        CHECK(strstr(report->results[index].detail, "baseline=current-scratch-format") != NULL);
        CHECK(strstr(report->results[index].detail, "featureGate=decimal-format-divider") != NULL);
        CHECK(strstr(report->results[index].detail, "operandFamilies=1") != NULL);
      }
      if (strcmp(report->results[index].operation, "format-folded") == 0) {
        saw_format_folded_probe = 1;
        if (report->results[index].digits == 1000) saw_format_folded1000_probe = 1;
        else if (report->results[index].digits == 4096) saw_format_folded4096_probe = 1;
        else if (report->results[index].digits == 8192) saw_format_folded8192_probe = 1;
        else CHECK(0);
        CHECK(strstr(report->results[index].detail, "chunkDigits=9") != NULL);
        CHECK(strstr(report->results[index].detail, "candidate=decimal-folded-2p64") != NULL);
        CHECK(strstr(report->results[index].detail, "baseline=current-scratch-format") != NULL);
        CHECK(strstr(report->results[index].detail, "featureGate=decimal-format-folded") != NULL);
        CHECK(strstr(report->results[index].detail, "operandFamilies=1") != NULL);
      }
      if (strcmp(report->results[index].operation, "format-pair-writer") == 0) {
        saw_format_pair_writer_probe = 1;
        if (report->results[index].digits == 40) saw_format_pair_writer40_probe = 1;
        else if (report->results[index].digits == 150) saw_format_pair_writer150_probe = 1;
        else if (report->results[index].digits == 256) saw_format_pair_writer256_probe = 1;
        else if (report->results[index].digits == 512) saw_format_pair_writer512_probe = 1;
        else if (report->results[index].digits == 1000) saw_format_pair_writer1000_probe = 1;
        else if (report->results[index].digits == 2048) saw_format_pair_writer2048_probe = 1;
        else if (report->results[index].digits == 4096) saw_format_pair_writer4096_probe = 1;
        else if (report->results[index].digits == 8192) saw_format_pair_writer8192_probe = 1;
        else if (report->results[index].digits == 16384) saw_format_pair_writer16384_probe = 1;
        else CHECK(0);
        if (strstr(report->results[index].detail, "mode=production-chunks") != NULL) saw_format_pair_writer_current_probe = 1;
        else if (strstr(report->results[index].detail, "mode=folded-chunks") != NULL) saw_format_pair_writer_folded_probe = 1;
        else CHECK(0);
        CHECK(strstr(report->results[index].detail, "chunkDigits=9") != NULL);
        CHECK(strstr(report->results[index].detail, "candidate=decimal-pair-writer") != NULL);
        CHECK(strstr(report->results[index].detail, "baseline=legacy-scratch-format") != NULL);
        CHECK(strstr(report->results[index].detail, "featureGate=decimal-format-pair-writer") != NULL);
        CHECK(strstr(report->results[index].detail, "operandFamilies=1") != NULL);
      }
      if (strcmp(report->results[index].operation, "format-wide") == 0) {
        saw_format_wide_probe = 1;
        if (report->results[index].digits == 1000) saw_format_wide1000_probe = 1;
        else if (report->results[index].digits == 4096) saw_format_wide4096_probe = 1;
        else if (report->results[index].digits == 8192) saw_format_wide8192_probe = 1;
        else CHECK(0);
        CHECK(strstr(report->results[index].detail, "wideChunkDigits=19") != NULL);
        CHECK(strstr(report->results[index].detail, "candidate=decimal-wide-chunks") != NULL);
        CHECK(strstr(report->results[index].detail, "baseline=current-scratch-format") != NULL);
        CHECK(strstr(report->results[index].detail, "featureGate=decimal-format-wide") != NULL);
        CHECK(strstr(report->results[index].detail, "operandFamilies=1") != NULL);
      }
      if (strcmp(report->results[index].operation, "format-mixed-writer") == 0 ||
          strcmp(report->results[index].operation, "format-folded-hwdiv") == 0 ||
          strcmp(report->results[index].operation, "format-hwdiv-mixed") == 0 ||
          strcmp(report->results[index].operation, "format-divide-1e19") == 0 ||
          strcmp(report->results[index].operation, "format-divide-1e19-pairs") == 0 ||
          strcmp(report->results[index].operation, "format-divide-1e19-preinv") == 0 ||
          strcmp(report->results[index].operation, "format-divide-1e19-preinv-pairs") == 0 ||
          strcmp(report->results[index].operation, "format-dc") == 0 ||
          strcmp(report->results[index].operation, "format-dc-ladder") == 0 ||
          strcmp(report->results[index].operation, "format-dc-direct") == 0 ||
          strcmp(report->results[index].operation, "format-dc-workspace") == 0 ||
          strcmp(report->results[index].operation, "format-dc-preinv-qhat") == 0 ||
          strcmp(report->results[index].operation, "format-dc-static-ladder") == 0 ||
          strcmp(report->results[index].operation, "format-dc-static-direct") == 0 ||
          strcmp(report->results[index].operation, "format-dc-route") == 0) {
        if (report->results[index].digits == 1000) saw_format_strategy1000_probe = 1;
        else if (report->results[index].digits == 4096) saw_format_strategy4096_probe = 1;
        else if (report->results[index].digits == 8192) saw_format_strategy8192_probe = 1;
        else if (report->results[index].digits == 16384) saw_format_strategy16384_probe = 1;
        else CHECK(0);
        if (strcmp(report->results[index].operation, "format-dc-route") == 0) {
          CHECK(strstr(report->results[index].detail, "baseline=decimal-dc-pow2-ladder-leaf8") != NULL);
        } else {
          CHECK(strstr(report->results[index].detail, "baseline=current-scratch-format") != NULL);
        }
        CHECK(strstr(report->results[index].detail, "operandFamilies=1") != NULL);
        if (strcmp(report->results[index].operation, "format-mixed-writer") == 0) {
          saw_format_mixed_writer_probe = 1;
          CHECK(strstr(report->results[index].detail, "mode=mixed-production-chunks") != NULL);
          CHECK(strstr(report->results[index].detail, "chunkDigits=9") != NULL);
          CHECK(strstr(report->results[index].detail, "candidate=decimal-mixed-pair-writer") != NULL);
          CHECK(strstr(report->results[index].detail, "featureGate=decimal-format-mixed-writer") != NULL);
        } else if (strcmp(report->results[index].operation, "format-folded-hwdiv") == 0) {
          saw_format_folded_hwdiv_probe = 1;
          CHECK(strstr(report->results[index].detail, "mode=folded-direct128") != NULL);
          CHECK(strstr(report->results[index].detail, "chunkDigits=9") != NULL);
          CHECK(strstr(report->results[index].detail, "candidate=decimal-folded-hwdiv") != NULL);
          CHECK(strstr(report->results[index].detail, "featureGate=decimal-format-folded-hwdiv") != NULL);
        } else if (strcmp(report->results[index].operation, "format-hwdiv-mixed") == 0) {
          saw_format_hwdiv_mixed_probe = 1;
          CHECK(strstr(report->results[index].detail, "mode=folded-direct128-mixed") != NULL);
          CHECK(strstr(report->results[index].detail, "chunkDigits=9") != NULL);
          CHECK(strstr(report->results[index].detail, "candidate=decimal-folded-hwdiv-mixed") != NULL);
          CHECK(strstr(report->results[index].detail, "featureGate=decimal-format-hwdiv-mixed") != NULL);
        } else if (strcmp(report->results[index].operation, "format-divide-1e19") == 0) {
          saw_format_divide_1e19_probe = 1;
          CHECK(strstr(report->results[index].detail, "mode=divide-copy-by-1e19") != NULL);
          CHECK(strstr(report->results[index].detail, "chunkDigits=19") != NULL);
          CHECK(strstr(report->results[index].detail, "candidate=decimal-divide-1e19") != NULL);
          CHECK(strstr(report->results[index].detail, "featureGate=decimal-format-divide-1e19") != NULL);
          CHECK(strstr(report->results[index].detail, "gmpClue=mpn_sb_get_str-largest-decimal-power") != NULL);
        } else if (strcmp(report->results[index].operation, "format-divide-1e19-pairs") == 0) {
          saw_format_divide_1e19_pairs_probe = 1;
          CHECK(strstr(report->results[index].detail, "mode=divide-copy-by-1e19-pair-writer") != NULL);
          CHECK(strstr(report->results[index].detail, "chunkDigits=19") != NULL);
          CHECK(strstr(report->results[index].detail, "candidate=decimal-divide-1e19-pair-writer") != NULL);
          CHECK(strstr(report->results[index].detail, "featureGate=decimal-format-divide-1e19-pairs") != NULL);
          CHECK(strstr(report->results[index].detail, "gmpClue=mpn_sb_get_str-largest-decimal-power+digit-emission") != NULL);
        } else if (strcmp(report->results[index].operation, "format-divide-1e19-preinv") == 0) {
          saw_format_divide_1e19_preinv_probe = 1;
          CHECK(strstr(report->results[index].detail, "mode=divide-copy-by-1e19-preinv") != NULL);
          CHECK(strstr(report->results[index].detail, "chunkDigits=19") != NULL);
          CHECK(strstr(report->results[index].detail, "candidate=decimal-divide-1e19-preinv") != NULL);
          CHECK(strstr(report->results[index].detail, "featureGate=decimal-format-divide-1e19-preinv") != NULL);
          CHECK(strstr(report->results[index].detail, "gmpClue=mpn_sb_get_str-preinverted-divrem-1") != NULL);
        } else if (strcmp(report->results[index].operation, "format-divide-1e19-preinv-pairs") == 0) {
          saw_format_divide_1e19_preinv_pairs_probe = 1;
          CHECK(strstr(report->results[index].detail, "mode=divide-copy-by-1e19-preinv-pair-writer") != NULL);
          CHECK(strstr(report->results[index].detail, "chunkDigits=19") != NULL);
          CHECK(strstr(report->results[index].detail, "candidate=decimal-divide-1e19-preinv-pair-writer") != NULL);
          CHECK(strstr(report->results[index].detail, "featureGate=decimal-format-divide-1e19-preinv-pairs") != NULL);
          CHECK(strstr(report->results[index].detail, "gmpClue=mpn_sb_get_str-preinverted-divrem-1+digit-emission") != NULL);
        } else if (strcmp(report->results[index].operation, "format-dc") == 0) {
          saw_format_dc_probe = 1;
          CHECK(strstr(report->results[index].detail, "mode=divide-conquer") != NULL);
          CHECK(strstr(report->results[index].detail, "chunkDigits=19") != NULL);
          CHECK(strstr(report->results[index].detail, "candidate=decimal-dc-powers") != NULL);
          CHECK(strstr(report->results[index].detail, "featureGate=decimal-format-dc") != NULL);
          CHECK(strstr(report->results[index].detail, "gmpClue=mpn_dc_get_str-powtab") != NULL);
          if (strstr(report->results[index].detail, "leafThreshold=8") != NULL) saw_format_dc_leaf8_probe = 1;
          else if (strstr(report->results[index].detail, "leafThreshold=16") != NULL) saw_format_dc_leaf16_probe = 1;
          else if (strstr(report->results[index].detail, "leafThreshold=32") != NULL) saw_format_dc_leaf32_probe = 1;
          else if (strstr(report->results[index].detail, "leafThreshold=64") != NULL) saw_format_dc_leaf64_probe = 1;
          else CHECK(0);
        } else if (strcmp(report->results[index].operation, "format-dc-ladder") == 0) {
          saw_format_dc_ladder_probe = 1;
          CHECK(strstr(report->results[index].detail, "mode=divide-conquer-ladder") != NULL);
          CHECK(strstr(report->results[index].detail, "chunkDigits=19") != NULL);
          CHECK(strstr(report->results[index].detail, "candidate=decimal-dc-pow2-ladder") != NULL);
          CHECK(strstr(report->results[index].detail, "featureGate=decimal-format-dc-ladder") != NULL);
          CHECK(strstr(report->results[index].detail, "gmpClue=mpn_dc_get_str-powtab-squares") != NULL);
          if (strstr(report->results[index].detail, "leafThreshold=8") != NULL) saw_format_dc_ladder_leaf8_probe = 1;
          else if (strstr(report->results[index].detail, "leafThreshold=16") != NULL) saw_format_dc_ladder_leaf16_probe = 1;
          else if (strstr(report->results[index].detail, "leafThreshold=32") != NULL) saw_format_dc_ladder_leaf32_probe = 1;
          else if (strstr(report->results[index].detail, "leafThreshold=64") != NULL) saw_format_dc_ladder_leaf64_probe = 1;
          else CHECK(0);
        } else if (strcmp(report->results[index].operation, "format-dc-direct") == 0) {
          saw_format_dc_direct_probe = 1;
          CHECK(strstr(report->results[index].detail, "mode=divide-conquer-direct") != NULL);
          CHECK(strstr(report->results[index].detail, "chunkDigits=19") != NULL);
          CHECK(strstr(report->results[index].detail, "candidate=decimal-dc-direct-writer") != NULL);
          CHECK(strstr(report->results[index].detail, "featureGate=decimal-format-dc-direct") != NULL);
          CHECK(strstr(report->results[index].detail, "gmpClue=mpn_dc_get_str-output-buffer") != NULL);
          if (strstr(report->results[index].detail, "leafThreshold=8") != NULL) saw_format_dc_direct_leaf8_probe = 1;
          else if (strstr(report->results[index].detail, "leafThreshold=16") != NULL) saw_format_dc_direct_leaf16_probe = 1;
          else if (strstr(report->results[index].detail, "leafThreshold=32") != NULL) saw_format_dc_direct_leaf32_probe = 1;
          else if (strstr(report->results[index].detail, "leafThreshold=64") != NULL) saw_format_dc_direct_leaf64_probe = 1;
          else CHECK(0);
        } else if (strcmp(report->results[index].operation, "format-dc-workspace") == 0) {
          saw_format_dc_workspace_probe = 1;
          CHECK(strstr(report->results[index].detail, "mode=dc-direct-workspace") != NULL);
          CHECK(strstr(report->results[index].detail, "timing=interleaved-alternating-batch") != NULL);
          CHECK(strstr(report->results[index].detail, "chunkDigits=19") != NULL);
          CHECK(strstr(report->results[index].detail, "candidate=decimal-dc-direct-workspace") != NULL);
          CHECK(strstr(report->results[index].detail, "featureGate=decimal-format-dc-workspace") != NULL);
          CHECK(strstr(report->results[index].detail, "gmpClue=mpn_dc_get_str-divisor-context") != NULL);
          if (strstr(report->results[index].detail, "leafThreshold=8") != NULL) saw_format_dc_workspace_leaf8_probe = 1;
          else if (strstr(report->results[index].detail, "leafThreshold=16") != NULL) saw_format_dc_workspace_leaf16_probe = 1;
          else CHECK(0);
        } else if (strcmp(report->results[index].operation, "format-dc-preinv-qhat") == 0) {
          saw_format_dc_preinv_qhat_probe = 1;
          CHECK(strstr(report->results[index].detail, "mode=dc-direct-preinv-qhat") != NULL);
          CHECK(strstr(report->results[index].detail, "timing=interleaved-alternating-batch") != NULL);
          CHECK(strstr(report->results[index].detail, "chunkDigits=19") != NULL);
          CHECK(strstr(report->results[index].detail, "candidate=decimal-dc-direct-preinv-qhat") != NULL);
          CHECK(strstr(report->results[index].detail, "featureGate=decimal-format-dc-preinv-qhat") != NULL);
          CHECK(strstr(report->results[index].detail, "gmpClue=mpn_dc_get_str-preinverted-qhat") != NULL);
          if (strstr(report->results[index].detail, "leafThreshold=8") != NULL) saw_format_dc_preinv_qhat_leaf8_probe = 1;
          else if (strstr(report->results[index].detail, "leafThreshold=16") != NULL) saw_format_dc_preinv_qhat_leaf16_probe = 1;
          else CHECK(0);
        } else if (strcmp(report->results[index].operation, "format-dc-static-ladder") == 0) {
          saw_format_dc_static_ladder_probe = 1;
          CHECK(strstr(report->results[index].detail, "mode=dc-static-ladder") != NULL);
          CHECK(strstr(report->results[index].detail, "chunkDigits=19") != NULL);
          CHECK(strstr(report->results[index].detail, "candidate=dc-static-pow2") != NULL);
          CHECK(strstr(report->results[index].detail, "featureGate=format-dc-static-ladder") != NULL);
          CHECK(strstr(report->results[index].detail, "gmpClue=static-powtab") != NULL);
          if (strstr(report->results[index].detail, "leafThreshold=8") != NULL) saw_format_dc_static_ladder_leaf8_probe = 1;
          else if (strstr(report->results[index].detail, "leafThreshold=16") != NULL) saw_format_dc_static_ladder_leaf16_probe = 1;
          else if (strstr(report->results[index].detail, "leafThreshold=32") != NULL) saw_format_dc_static_ladder_leaf32_probe = 1;
          else if (strstr(report->results[index].detail, "leafThreshold=64") != NULL) saw_format_dc_static_ladder_leaf64_probe = 1;
          else CHECK(0);
        } else if (strcmp(report->results[index].operation, "format-dc-route") == 0) {
          saw_format_dc_route_probe = 1;
          CHECK(strstr(report->results[index].detail, "mode=direct16-vs-ladder8") != NULL);
          CHECK(strstr(report->results[index].detail, "timing=interleaved-alternating-batch") != NULL);
          CHECK(strstr(report->results[index].detail, "chunkDigits=19") != NULL);
          CHECK(strstr(report->results[index].detail, "candidate=decimal-dc-direct-writer-leaf16") != NULL);
          CHECK(strstr(report->results[index].detail, "featureGate=decimal-format-dc-route") != NULL);
          CHECK(strstr(report->results[index].detail, "gmpClue=mpn_dc_get_str-output-buffer") != NULL);
          if (report->results[index].digits == 1000) saw_format_dc_route1000_probe = 1;
          else if (report->results[index].digits == 4096) saw_format_dc_route4096_probe = 1;
          else if (report->results[index].digits == 8192) saw_format_dc_route8192_probe = 1;
          else if (report->results[index].digits == 16384) saw_format_dc_route16384_probe = 1;
          else CHECK(0);
        } else {
          saw_format_dc_static_direct_probe = 1;
          CHECK(strstr(report->results[index].detail, "mode=dc-static-direct") != NULL);
          CHECK(strstr(report->results[index].detail, "chunkDigits=19") != NULL);
          CHECK(strstr(report->results[index].detail, "candidate=dc-static-direct") != NULL);
          CHECK(strstr(report->results[index].detail, "featureGate=format-dc-static-direct") != NULL);
          CHECK(strstr(report->results[index].detail, "gmpClue=static-powtab+buffer") != NULL);
          if (strstr(report->results[index].detail, "leafThreshold=8") != NULL) saw_format_dc_static_direct_leaf8_probe = 1;
          else if (strstr(report->results[index].detail, "leafThreshold=16") != NULL) saw_format_dc_static_direct_leaf16_probe = 1;
          else if (strstr(report->results[index].detail, "leafThreshold=32") != NULL) saw_format_dc_static_direct_leaf32_probe = 1;
          else if (strstr(report->results[index].detail, "leafThreshold=64") != NULL) saw_format_dc_static_direct_leaf64_probe = 1;
          else CHECK(0);
        }
      }
      if (strcmp(report->results[index].operation, "square-vs-mul") == 0) {
        saw_square_vs_mul_probe = 1;
        CHECK(strstr(report->results[index].detail, "routeCandidate=unrouted") != NULL);
        CHECK(strstr(report->results[index].detail, "candidate=specialized-square") != NULL);
        CHECK(strstr(report->results[index].detail, "genericMulUs=") != NULL);
        CHECK(strstr(report->results[index].detail, "baseline=generic-threshold-self-mul") != NULL);
        CHECK(strstr(report->results[index].detail, "featureGate=square-basecase-probe") != NULL);
        CHECK(strstr(report->results[index].detail, "operandFamilies=1") != NULL);
      }
      if (strcmp(report->results[index].operation, "square-karatsuba-vs-mul") == 0 ||
          strcmp(report->results[index].operation, "square-karatsuba-vs-gmp") == 0) {
        if (strcmp(report->results[index].operation, "square-karatsuba-vs-mul") == 0) saw_square_karatsuba_vs_mul_probe = 1;
        else saw_square_karatsuba_vs_gmp_probe = 1;
        CHECK(strstr(report->results[index].detail, "threshold=") != NULL);
        CHECK(strstr(report->results[index].detail, "candidate=karatsuba-square") != NULL);
        if (strcmp(report->results[index].operation, "square-karatsuba-vs-mul") == 0) {
          CHECK(strstr(report->results[index].detail, "baseline=generic-threshold-self-mul") != NULL);
        }
        CHECK(strstr(report->results[index].detail, "featureGate=karatsuba-square-probe") != NULL);
        CHECK(strstr(report->results[index].detail, "operandFamilies=1") != NULL);
      }
      if (strcmp(report->results[index].operation, "square-leaf-order") == 0) {
        saw_square_leaf_order_probe = 1;
        if (report->results[index].digits == 1000) saw_square_leaf_order1000_probe = 1;
        else if (report->results[index].digits == 4096) saw_square_leaf_order4096_probe = 1;
        else if (report->results[index].digits == 8192) saw_square_leaf_order8192_probe = 1;
        else if (report->results[index].digits == 16384) saw_square_leaf_order16384_probe = 1;
        else CHECK(0);
        CHECK(report->results[index].parity_verified);
        CHECK(!report->results[index].replacement_ready);
        CHECK(strcmp(report->results[index].adoption, "observe-only") == 0);
        CHECK(strstr(report->results[index].detail, "threshold=64") != NULL);
        CHECK(strstr(report->results[index].detail, "candidate=fused-diagonal-cross-leaf") != NULL);
        CHECK(strstr(report->results[index].detail, "baseline=current-scratch-square") != NULL);
        CHECK(strstr(report->results[index].detail, "oracle=mpz_mul") != NULL);
        CHECK(strstr(report->results[index].detail, "featureGate=square-leaf-order-pass") != NULL);
        CHECK(strstr(report->results[index].detail, "gmpClue=mfastfermat-wide61-dif-dit-bit-reversal-elision") != NULL);
        CHECK(strstr(report->results[index].detail, "noAutoRoute=1") != NULL);
        CHECK(strstr(report->results[index].detail, "operandFamilies=1") != NULL);
      }
      if (strcmp(report->results[index].operation, "mul-toom3") == 0) {
        saw_toom3_probe = 1;
        CHECK(strstr(report->results[index].detail, "leafThreshold=") != NULL);
        CHECK(strstr(report->results[index].detail, "featureGate=one-level-toom3") != NULL);
        CHECK(strstr(report->results[index].detail, "operandFamilies=2") != NULL);
      }
      if (strcmp(report->results[index].operation, "mul-toom3-vs-scratch") == 0) {
        saw_toom3_vs_scratch_probe = 1;
        CHECK(strstr(report->results[index].detail, "leafThreshold=") != NULL);
        CHECK(strstr(report->results[index].detail, "candidate=one-level-toom3") != NULL);
        CHECK(strstr(report->results[index].detail, "baseline=current-scratch-mul") != NULL);
        CHECK(strstr(report->results[index].detail, "featureGate=internal-promotion") != NULL);
        CHECK(strstr(report->results[index].detail, "operandFamilies=2") != NULL);
      }
      if (strcmp(report->results[index].operation, "mul-toom3-unroll4-vs-scratch") == 0) {
        saw_toom3_unroll4_vs_scratch_probe = 1;
        CHECK(strstr(report->results[index].detail, "leafThreshold=") != NULL);
        CHECK(strstr(report->results[index].detail, "candidate=one-level-toom3+unroll4-leaf") != NULL);
        CHECK(strstr(report->results[index].detail, "baseline=current-scratch-mul") != NULL);
        CHECK(strstr(report->results[index].detail, "featureGate=msvc-x64-toom3-unroll4") != NULL);
        CHECK(strstr(report->results[index].detail, "operandFamilies=2") != NULL);
      }
      if (strcmp(report->results[index].operation, "mul-toom3-unroll4-vs-gmp") == 0) {
        saw_toom3_unroll4_vs_gmp_probe = 1;
        CHECK(strstr(report->results[index].detail, "leafThreshold=") != NULL);
        CHECK(strstr(report->results[index].detail, "candidate=one-level-toom3+unroll4-leaf") != NULL);
        CHECK(strstr(report->results[index].detail, "baseline=mpz_mul") != NULL);
        CHECK(strstr(report->results[index].detail, "featureGate=msvc-x64-toom3-unroll4") != NULL);
        CHECK(strstr(report->results[index].detail, "operandFamilies=2") != NULL);
      }
      if (strcmp(report->results[index].operation, "mul-toom3-unroll4-deep-vs-gmp") == 0) {
        saw_toom3_unroll4_deep_vs_gmp_probe = 1;
        CHECK(strstr(report->results[index].detail, "samples=9") != NULL);
        if (strstr(report->results[index].detail, "leafThreshold=64") != NULL) saw_toom3_unroll4_deep_leaf64_probe = 1;
        else if (strstr(report->results[index].detail, "leafThreshold=96") != NULL) saw_toom3_unroll4_deep_leaf96_probe = 1;
        else CHECK(0);
        CHECK(strstr(report->results[index].detail, "candidate=one-level-toom3+unroll4-leaf") != NULL);
        CHECK(strstr(report->results[index].detail, "baseline=mpz_mul") != NULL);
        CHECK(strstr(report->results[index].detail, "featureGate=msvc-x64-toom3-unroll4") != NULL);
        CHECK(strstr(report->results[index].detail, "operandFamilies=2") != NULL);
      }
      if (strcmp(report->results[index].operation, "mul-toom3-u4-rec-vs-gmp") == 0) {
        saw_toom3_unroll4_recursive_vs_gmp_probe = 1;
        CHECK(strstr(report->results[index].detail, "leafThreshold=") != NULL);
        CHECK(strstr(report->results[index].detail, "depthLimit=2") != NULL);
        CHECK(strstr(report->results[index].detail, "candidate=recursive-toom3+unroll4") != NULL);
        CHECK(strstr(report->results[index].detail, "baseline=mpz_mul") != NULL);
        CHECK(strstr(report->results[index].detail, "featureGate=msvc-x64-recursive-toom3") != NULL);
        CHECK(strstr(report->results[index].detail, "operandFamilies=2") != NULL);
      }
      if (strcmp(report->results[index].operation, "mul-toom3-u4-rec-deep-vs-gmp") == 0) {
        saw_toom3_unroll4_recursive_deep_vs_gmp_probe = 1;
        CHECK(strstr(report->results[index].detail, "samples=9") != NULL);
        if (strstr(report->results[index].detail, "leafThreshold=64") != NULL) saw_toom3_unroll4_recursive_deep_leaf64_probe = 1;
        else if (strstr(report->results[index].detail, "leafThreshold=96") != NULL) saw_toom3_unroll4_recursive_deep_leaf96_probe = 1;
        else CHECK(0);
        CHECK(strstr(report->results[index].detail, "depthLimit=2") != NULL);
        CHECK(strstr(report->results[index].detail, "candidate=recursive-toom3+unroll4") != NULL);
        CHECK(strstr(report->results[index].detail, "baseline=mpz_mul") != NULL);
        CHECK(strstr(report->results[index].detail, "featureGate=msvc-x64-recursive-toom3") != NULL);
        CHECK(strstr(report->results[index].detail, "operandFamilies=2") != NULL);
      }
      if (strcmp(report->results[index].operation, "mul-unroll4-vs-scratch") == 0) {
        saw_mul_unroll4_vs_scratch_probe = 1;
        CHECK(strstr(report->results[index].detail, "leafThreshold=") != NULL);
        CHECK(strstr(report->results[index].detail, "candidate=_umul128+_addcarry_u64-unroll4-full") != NULL);
        CHECK(strstr(report->results[index].detail, "baseline=scalar-threshold-mul") != NULL);
        CHECK(strstr(report->results[index].detail, "featureGate=msvc-x64-full-mul-schedule") != NULL);
        CHECK(strstr(report->results[index].detail, "operandFamilies=2") != NULL);
      }
      if (strcmp(report->results[index].operation, "mul-unroll4-vs-gmp") == 0) {
        saw_mul_unroll4_vs_gmp_probe = 1;
        CHECK(strstr(report->results[index].detail, "leafThreshold=") != NULL);
        CHECK(strstr(report->results[index].detail, "candidate=_umul128+_addcarry_u64-unroll4-full") != NULL);
        CHECK(strstr(report->results[index].detail, "baseline=mpz_mul") != NULL);
        CHECK(strstr(report->results[index].detail, "featureGate=msvc-x64-full-mul-schedule") != NULL);
        CHECK(strstr(report->results[index].detail, "operandFamilies=2") != NULL);
      }
      if (strcmp(report->results[index].operation, "mul-unroll4-deep-vs-gmp") == 0) {
        saw_mul_unroll4_deep_vs_gmp_probe = 1;
        CHECK(strstr(report->results[index].detail, "samples=9") != NULL);
        CHECK(strstr(report->results[index].detail, "leafThreshold=") != NULL);
        CHECK(strstr(report->results[index].detail, "candidate=_umul128+_addcarry_u64-unroll4-full") != NULL);
        CHECK(strstr(report->results[index].detail, "baseline=mpz_mul") != NULL);
        CHECK(strstr(report->results[index].detail, "featureGate=msvc-x64-full-mul-schedule") != NULL);
        CHECK(strstr(report->results[index].detail, "operandFamilies=2") != NULL);
      }
      if (strcmp(report->results[index].operation, "muladd-bmi2-adx") == 0) {
        saw_muladd_bmi2_adx_probe = 1;
        CHECK(strstr(report->results[index].detail, "candidate=_mulx_u64+_addcarryx_u64") != NULL);
        CHECK(strstr(report->results[index].detail, "baseline=_umul128+_addcarry_u64") != NULL);
        CHECK(strstr(report->results[index].detail, "featureGate=msvc-x64-bmi2-adx") != NULL);
      }
      if (strcmp(report->results[index].operation, "muladd-unroll4") == 0) {
        saw_muladd_unroll_probe = 1;
        CHECK(strstr(report->results[index].detail, "candidate=_umul128+_addcarry_u64-unroll4") != NULL);
        CHECK(strstr(report->results[index].detail, "baseline=_umul128+_addcarry_u64") != NULL);
        CHECK(strstr(report->results[index].detail, "featureGate=msvc-x64-loop-schedule") != NULL);
      }
      if (strcmp(report->results[index].operation, "muladd-unroll8") == 0) {
        saw_muladd_unroll8_probe = 1;
        CHECK(strstr(report->results[index].detail, "candidate=_umul128+_addcarry_u64-unroll8") != NULL);
        CHECK(strstr(report->results[index].detail, "baseline=_umul128+_addcarry_u64") != NULL);
        CHECK(strstr(report->results[index].detail, "featureGate=msvc-x64-loop-schedule") != NULL);
      }
      if (strcmp(report->results[index].adoption, "promote-candidate") == 0) {
        size_t required_stable = report->results[index].sample_count > 5 ?
          report->results[index].sample_count - 1 :
          (report->results[index].sample_count < 4 ? report->results[index].sample_count : 4);
        CHECK(report->results[index].stable_sample_count >= required_stable);
        CHECK(report->results[index].speed_ratio <= report->results[index].max_allowed_speed_ratio);
        CHECK(report->results[index].worst_pair_ratio <= 1.0);
      }
      CHECK(strstr(report->results[index].adoption, "promote-candidate") != NULL ||
        strstr(report->results[index].adoption, "observe-only") != NULL ||
        strstr(report->results[index].adoption, "blocked-output-mismatch") != NULL);
    } else if (strcmp(report->results[index].category, "policy-probe") == 0) {
      saw_policy_probe = 1;
      CHECK(report->results[index].passed);
      CHECK(report->results[index].parity_verified);
      CHECK(report->results[index].scratch_us > 0);
      CHECK(report->results[index].gmp_us > 0);
      CHECK(report->results[index].speed_ratio > 0.0);
      CHECK(report->results[index].max_allowed_speed_ratio == 1.0);
      CHECK(report->results[index].sample_count == 5);
      CHECK(report->results[index].stable_sample_count <= report->results[index].sample_count);
      CHECK(strstr(report->results[index].detail, "-policy") != NULL);
      CHECK(strstr(report->results[index].detail, "ratioMethod=paired-median") != NULL);
      CHECK(strstr(report->results[index].detail, "stablePairs=") != NULL);
      CHECK(report->results[index].worst_pair_ratio > 0.0);
      CHECK(strstr(report->results[index].detail, "featureGate=") != NULL);
      CHECK(strstr(report->results[index].detail, "gmpClue=") != NULL);
      CHECK(strstr(report->results[index].detail, "activeCandidate=") != NULL);
      CHECK(strstr(report->results[index].detail, "thresholdSafety=") != NULL);
      CHECK(strstr(report->results[index].detail, "noAutoRoute=") != NULL);
      if (strcmp(report->results[index].operation, "format-route-tournament") == 0) {
        saw_format_route_tournament_probe = 1;
        if (report->results[index].digits == 768) saw_format_route_tournament768_probe = 1;
        else if (report->results[index].digits == 896) saw_format_route_tournament896_probe = 1;
        else if (report->results[index].digits == 1000) saw_format_route_tournament1000_probe = 1;
        else if (report->results[index].digits == 4096) saw_format_route_tournament4096_probe = 1;
        else if (report->results[index].digits == 8192) saw_format_route_tournament8192_probe = 1;
        else CHECK(0);
        CHECK(report->results[index].parity_verified);
        CHECK(!report->results[index].replacement_ready);
        CHECK(strcmp(report->results[index].adoption, "observe-only") == 0);
        CHECK(strstr(report->results[index].detail, "op=format-route-tournament") != NULL);
        CHECK(strstr(report->results[index].detail, "policy=tournament") != NULL);
        CHECK(strstr(report->results[index].detail, "winner=") != NULL);
        CHECK(strstr(report->results[index].detail, "current=current-default") != NULL);
        CHECK(strstr(report->results[index].detail, "routes=current-default,divide1e19-preinv,divide1e19-preinv-pairs,dc-ladder8,dc-direct16,dc-preinv-qhat16") != NULL);
        CHECK(strstr(report->results[index].detail, "routesTested=6") != NULL);
        CHECK(strstr(report->results[index].detail, "requiredStablePairs=4/5") != NULL);
        CHECK(strstr(report->results[index].detail, "winnerCurrentRatio=") != NULL);
        CHECK(strstr(report->results[index].detail, "winnerGmpRatio=") != NULL);
        CHECK(strstr(report->results[index].detail, "currentGmpRatio=") != NULL);
        CHECK(strstr(report->results[index].detail, "ratioMethod=paired-median") != NULL);
        CHECK(strstr(report->results[index].detail, "tournamentMethod=same-run") != NULL);
        CHECK(strstr(report->results[index].detail, "hashSafe=5/5") != NULL);
        CHECK(strstr(report->results[index].detail, "hashGate=matched") != NULL);
        CHECK(strstr(report->results[index].detail, "candidate=best-format-route") != NULL);
        CHECK(strstr(report->results[index].detail, "activeCandidate=") != NULL);
        CHECK(strstr(report->results[index].detail, "baseline=current-scratch-format") != NULL);
        CHECK(strstr(report->results[index].detail, "oracle=mpz_get_str") != NULL);
        CHECK(strstr(report->results[index].detail, "featureGate=decimal-format-route-policy-tournament") != NULL);
        CHECK(strstr(report->results[index].detail, "gmpClue=mfast-factor64pre-precompute") != NULL);
        CHECK(strstr(report->results[index].detail, "thresholdSafety=tournament-observe") != NULL);
        CHECK(strstr(report->results[index].detail, "sameInput=yes") != NULL);
        CHECK(strstr(report->results[index].detail, "sameRunTournament=yes") != NULL);
        CHECK(strstr(report->results[index].detail, "noAutoRoute=1") != NULL);
      } else if (strcmp(report->results[index].operation, "format-route-tournament-detail") == 0) {
        saw_format_route_tournament_detail = 1;
        format_route_tournament_detail_rows++;
        if (strstr(report->results[index].detail, "route=current-default") != NULL) {
          saw_format_route_tournament_detail_current = 1;
          CHECK(strcmp(report->results[index].status, "tournament-baseline") == 0);
          CHECK(report->results[index].speed_ratio == 1.0);
          CHECK(report->results[index].worst_pair_ratio == 1.0);
        }
        if (strstr(report->results[index].detail, "route=divide1e19-preinv") != NULL) {
          saw_format_route_tournament_detail_preinv = 1;
        }
        CHECK(report->results[index].parity_verified);
        CHECK(!report->results[index].replacement_ready);
        CHECK(strcmp(report->results[index].adoption, "observe-only") == 0);
        CHECK(strstr(report->results[index].detail, "op=format-route-tournament-detail") != NULL);
        CHECK(strstr(report->results[index].detail, "policy=tournament") != NULL);
        CHECK(strstr(report->results[index].detail, "tournamentDetail=1") != NULL);
        CHECK(strstr(report->results[index].detail, "controlSafety=tournament-detail") != NULL);
        CHECK(strstr(report->results[index].detail, "routeIndex=") != NULL);
        CHECK(strstr(report->results[index].detail, "isWinner=") != NULL);
        CHECK(strstr(report->results[index].detail, "winner=") != NULL);
        CHECK(strstr(report->results[index].detail, "current=current-default") != NULL);
        CHECK(strstr(report->results[index].detail, "candidate=") != NULL);
        CHECK(strstr(report->results[index].detail, "baseline=current-scratch-format") != NULL);
        CHECK(strstr(report->results[index].detail, "oracle=mpz_get_str") != NULL);
        CHECK(strstr(report->results[index].detail, "routeCurrentRatio=") != NULL);
        CHECK(strstr(report->results[index].detail, "routeGmpRatio=") != NULL);
        CHECK(strstr(report->results[index].detail, "currentGmpRatio=") != NULL);
        CHECK(strstr(report->results[index].detail, "ratioMethod=paired-median") != NULL);
        CHECK(strstr(report->results[index].detail, "tournamentMethod=same-run") != NULL);
        CHECK(strstr(report->results[index].detail, "hashSafe=5/5") != NULL);
        CHECK(strstr(report->results[index].detail, "hashGate=matched") != NULL);
        CHECK(strstr(report->results[index].detail, "featureGate=decimal-format-route-policy-tournament") != NULL);
        CHECK(strstr(report->results[index].detail, "gmpClue=mfast-cuda-tournament-detail") != NULL);
        CHECK(strstr(report->results[index].detail, "thresholdSafety=tournament-observe") != NULL);
        CHECK(strstr(report->results[index].detail, "sameInput=yes") != NULL);
        CHECK(strstr(report->results[index].detail, "sameRunTournament=yes") != NULL);
        CHECK(strstr(report->results[index].detail, "noAutoRoute=1") != NULL);
      } else if (strcmp(report->results[index].operation, "format-policy") == 0) {
        CHECK(strstr(report->results[index].detail, "op=format-policy") != NULL);
        CHECK(strstr(report->results[index].detail, "baseline=mpz_get_str") != NULL);
        CHECK(strstr(report->results[index].detail, "featureGate=decimal-format-policy") != NULL);
        if (strstr(report->results[index].detail, "policy=current-default") != NULL) {
          saw_format_policy_current = 1;
          CHECK(strstr(report->results[index].detail, "candidate=current-scratch-format") != NULL);
          CHECK(strstr(report->results[index].detail, "activeCandidate=current-scratch-format") != NULL);
          CHECK(strstr(report->results[index].detail, "thresholdSafety=direct-row") != NULL);
          CHECK(strstr(report->results[index].detail, "noAutoRoute=0") != NULL);
        } else if (strstr(report->results[index].detail, "policy=direct-ge4096-leaf8") != NULL) {
          saw_format_policy_direct4096 = 1;
          CHECK(strstr(report->results[index].detail, "minDigits=4096") != NULL);
          CHECK(strstr(report->results[index].detail, "leafThreshold=8") != NULL);
          CHECK(strstr(report->results[index].detail, "candidate=decimal-dc-direct-writer") != NULL);
          CHECK(strstr(report->results[index].detail, "thresholdSafety=requires-forced-neighbor") != NULL);
          CHECK(strstr(report->results[index].detail, "noAutoRoute=1") != NULL);
          CHECK(strcmp(report->results[index].adoption, "observe-only") == 0);
          CHECK(!report->results[index].replacement_ready);
          if (report->results[index].digits < 4096) {
            CHECK(strstr(report->results[index].detail, "activeCandidate=current-scratch-format") != NULL);
          } else {
            CHECK(strstr(report->results[index].detail, "activeCandidate=decimal-dc-direct-writer") != NULL);
          }
        } else if (strstr(report->results[index].detail, "policy=direct-ge8192-leaf16") != NULL) {
          saw_format_policy_direct8192 = 1;
          CHECK(strstr(report->results[index].detail, "minDigits=8192") != NULL);
          CHECK(strstr(report->results[index].detail, "leafThreshold=16") != NULL);
          CHECK(strstr(report->results[index].detail, "candidate=decimal-dc-direct-writer") != NULL);
          CHECK(strstr(report->results[index].detail, "thresholdSafety=requires-forced-neighbor") != NULL);
          CHECK(strstr(report->results[index].detail, "noAutoRoute=1") != NULL);
          CHECK(strcmp(report->results[index].adoption, "observe-only") == 0);
          CHECK(!report->results[index].replacement_ready);
          if (report->results[index].digits < 8192) {
            CHECK(strstr(report->results[index].detail, "activeCandidate=current-scratch-format") != NULL);
          } else {
            CHECK(strstr(report->results[index].detail, "activeCandidate=decimal-dc-direct-writer") != NULL);
          }
        } else if (strstr(report->results[index].detail, "policy=static-ge4096-l16") != NULL) {
          saw_format_policy_static4096 = 1;
          CHECK(strstr(report->results[index].detail, "minDigits=4096") != NULL);
          CHECK(strstr(report->results[index].detail, "leafThreshold=16") != NULL);
          CHECK(strstr(report->results[index].detail, "candidate=dc-static-direct") != NULL);
          CHECK(strstr(report->results[index].detail, "featureGate=decimal-format-policy-static-direct") != NULL);
          CHECK(strstr(report->results[index].detail, "gmpClue=static-powtab+buffer") != NULL);
          CHECK(strstr(report->results[index].detail, "thresholdSafety=requires-forced-neighbor") != NULL);
          CHECK(strstr(report->results[index].detail, "noAutoRoute=1") != NULL);
          CHECK(strcmp(report->results[index].adoption, "observe-only") == 0);
          CHECK(!report->results[index].replacement_ready);
          if (report->results[index].digits < 4096) {
            CHECK(strstr(report->results[index].detail, "activeCandidate=current-scratch-format") != NULL);
          } else {
            CHECK(strstr(report->results[index].detail, "activeCandidate=dc-static-direct") != NULL);
          }
        } else if (strstr(report->results[index].detail, "policy=static-ge8192-l8") != NULL) {
          saw_format_policy_static8192 = 1;
          CHECK(strstr(report->results[index].detail, "minDigits=8192") != NULL);
          CHECK(strstr(report->results[index].detail, "leafThreshold=8") != NULL);
          CHECK(strstr(report->results[index].detail, "candidate=dc-static-direct") != NULL);
          CHECK(strstr(report->results[index].detail, "featureGate=decimal-format-policy-static-direct") != NULL);
          CHECK(strstr(report->results[index].detail, "gmpClue=static-powtab+buffer") != NULL);
          CHECK(strstr(report->results[index].detail, "thresholdSafety=requires-forced-neighbor") != NULL);
          CHECK(strstr(report->results[index].detail, "noAutoRoute=1") != NULL);
          CHECK(strcmp(report->results[index].adoption, "observe-only") == 0);
          CHECK(!report->results[index].replacement_ready);
          if (report->results[index].digits < 8192) {
            CHECK(strstr(report->results[index].detail, "activeCandidate=current-scratch-format") != NULL);
          } else {
            CHECK(strstr(report->results[index].detail, "activeCandidate=dc-static-direct") != NULL);
          }
        } else if (strstr(report->results[index].detail, "policy=workspace-ge4096-leaf16") != NULL) {
          saw_format_policy_workspace4096 = 1;
          CHECK(strstr(report->results[index].detail, "minDigits=4096") != NULL);
          CHECK(strstr(report->results[index].detail, "leafThreshold=16") != NULL);
          CHECK(strstr(report->results[index].detail, "candidate=decimal-dc-direct-workspace") != NULL);
          CHECK(strstr(report->results[index].detail, "featureGate=decimal-format-policy-workspace") != NULL);
          CHECK(strstr(report->results[index].detail, "gmpClue=mpn_dc_get_str-divisor-context") != NULL);
          CHECK(strstr(report->results[index].detail, "thresholdSafety=requires-forced-neighbor") != NULL);
          CHECK(strstr(report->results[index].detail, "noAutoRoute=1") != NULL);
          CHECK(strcmp(report->results[index].adoption, "observe-only") == 0);
          CHECK(!report->results[index].replacement_ready);
          if (report->results[index].digits < 4096) {
            CHECK(strstr(report->results[index].detail, "activeCandidate=current-scratch-format") != NULL);
          } else {
            CHECK(strstr(report->results[index].detail, "activeCandidate=decimal-dc-direct-workspace") != NULL);
          }
        } else if (strstr(report->results[index].detail, "policy=preinv-ge4096-leaf8") != NULL) {
          saw_format_policy_preinv4096 = 1;
          CHECK(strstr(report->results[index].detail, "minDigits=4096") != NULL);
          CHECK(strstr(report->results[index].detail, "leafThreshold=8") != NULL);
          CHECK(strstr(report->results[index].detail, "candidate=decimal-dc-direct-preinv-qhat") != NULL);
          CHECK(strstr(report->results[index].detail, "featureGate=decimal-format-policy-preinv-qhat") != NULL);
          CHECK(strstr(report->results[index].detail, "gmpClue=mpn_dc_get_str-preinverted-qhat") != NULL);
          CHECK(strstr(report->results[index].detail, "thresholdSafety=requires-forced-neighbor") != NULL);
          CHECK(strstr(report->results[index].detail, "noAutoRoute=1") != NULL);
          CHECK(strcmp(report->results[index].adoption, "observe-only") == 0);
          CHECK(!report->results[index].replacement_ready);
          if (report->results[index].digits < 4096) {
            CHECK(strstr(report->results[index].detail, "activeCandidate=current-scratch-format") != NULL);
          } else {
            CHECK(strstr(report->results[index].detail, "activeCandidate=decimal-dc-direct-preinv-qhat") != NULL);
          }
        } else if (strstr(report->results[index].detail, "policy=preinv-ge8192-leaf16") != NULL) {
          saw_format_policy_preinv8192 = 1;
          CHECK(strstr(report->results[index].detail, "minDigits=8192") != NULL);
          CHECK(strstr(report->results[index].detail, "leafThreshold=16") != NULL);
          CHECK(strstr(report->results[index].detail, "candidate=decimal-dc-direct-preinv-qhat") != NULL);
          CHECK(strstr(report->results[index].detail, "featureGate=decimal-format-policy-preinv-qhat") != NULL);
          CHECK(strstr(report->results[index].detail, "gmpClue=mpn_dc_get_str-preinverted-qhat") != NULL);
          CHECK(strstr(report->results[index].detail, "thresholdSafety=requires-forced-neighbor") != NULL);
          CHECK(strstr(report->results[index].detail, "noAutoRoute=1") != NULL);
          CHECK(strcmp(report->results[index].adoption, "observe-only") == 0);
          CHECK(!report->results[index].replacement_ready);
          if (report->results[index].digits < 8192) {
            CHECK(strstr(report->results[index].detail, "activeCandidate=current-scratch-format") != NULL);
          } else {
            CHECK(strstr(report->results[index].detail, "activeCandidate=decimal-dc-direct-preinv-qhat") != NULL);
          }
        } else if (strstr(report->results[index].detail, "policy=preinv-ge16384-leaf16") != NULL) {
          saw_format_policy_preinv16384 = 1;
          CHECK(strstr(report->results[index].detail, "minDigits=16384") != NULL);
          CHECK(strstr(report->results[index].detail, "leafThreshold=16") != NULL);
          CHECK(strstr(report->results[index].detail, "candidate=decimal-dc-direct-preinv-qhat") != NULL);
          CHECK(strstr(report->results[index].detail, "featureGate=decimal-format-policy-preinv-qhat") != NULL);
          CHECK(strstr(report->results[index].detail, "gmpClue=mpn_dc_get_str-preinverted-qhat") != NULL);
          CHECK(strstr(report->results[index].detail, "thresholdSafety=requires-forced-neighbor") != NULL);
          CHECK(strstr(report->results[index].detail, "noAutoRoute=1") != NULL);
          CHECK(strcmp(report->results[index].adoption, "observe-only") == 0);
          CHECK(!report->results[index].replacement_ready);
          if (report->results[index].digits < 16384) {
            CHECK(strstr(report->results[index].detail, "activeCandidate=current-scratch-format") != NULL);
          } else {
            CHECK(strstr(report->results[index].detail, "activeCandidate=decimal-dc-direct-preinv-qhat") != NULL);
          }
        } else if (strstr(report->results[index].detail, "policy=preinv10e19-window768-1000") != NULL) {
          saw_format_policy_preinv10e19_window = 1;
          CHECK(strstr(report->results[index].detail, "minDigits=768") != NULL);
          CHECK(strstr(report->results[index].detail, "maxDigits=1000") != NULL);
          CHECK(strstr(report->results[index].detail, "leafThreshold=0") != NULL);
          CHECK(strstr(report->results[index].detail, "candidate=decimal-divide-1e19-preinv") != NULL);
          CHECK(strstr(report->results[index].detail, "featureGate=decimal-format-policy-divide-1e19-preinv") != NULL);
          CHECK(strstr(report->results[index].detail, "gmpClue=mpn_sb_get_str-preinverted-divrem-1") != NULL);
          CHECK(strstr(report->results[index].detail, "thresholdSafety=requires-forced-neighbor") != NULL);
          CHECK(strstr(report->results[index].detail, "noAutoRoute=1") != NULL);
          CHECK(strcmp(report->results[index].adoption, "observe-only") == 0);
          CHECK(!report->results[index].replacement_ready);
          if (report->results[index].digits == 1000) {
            CHECK(strstr(report->results[index].detail, "activeCandidate=decimal-divide-1e19-preinv") != NULL);
          } else {
            CHECK(strstr(report->results[index].detail, "activeCandidate=current-scratch-format") != NULL);
          }
        } else if (strstr(report->results[index].detail, "policy=preinv10e19-pairs-window768-1000") != NULL) {
          saw_format_policy_preinv10e19_pairs_window = 1;
          CHECK(strstr(report->results[index].detail, "minDigits=768") != NULL);
          CHECK(strstr(report->results[index].detail, "maxDigits=1000") != NULL);
          CHECK(strstr(report->results[index].detail, "leafThreshold=0") != NULL);
          CHECK(strstr(report->results[index].detail, "candidate=decimal-divide-1e19-preinv-pair-writer") != NULL);
          CHECK(strstr(report->results[index].detail, "featureGate=decimal-format-policy-divide-1e19-preinv-pairs") != NULL);
          CHECK(strstr(report->results[index].detail, "gmpClue=mpn_sb_get_str-preinverted-divrem-1+digit-emission") != NULL);
          CHECK(strstr(report->results[index].detail, "thresholdSafety=requires-forced-neighbor") != NULL);
          CHECK(strstr(report->results[index].detail, "noAutoRoute=1") != NULL);
          CHECK(strcmp(report->results[index].adoption, "observe-only") == 0);
          CHECK(!report->results[index].replacement_ready);
          if (report->results[index].digits == 1000) {
            CHECK(strstr(report->results[index].detail, "activeCandidate=decimal-divide-1e19-preinv-pair-writer") != NULL);
          } else {
            CHECK(strstr(report->results[index].detail, "activeCandidate=current-scratch-format") != NULL);
          }
        } else if (strstr(report->results[index].detail, "policy=preinv10e19-window768-896") != NULL) {
          saw_format_policy_preinv10e19_window768_896 = 1;
          CHECK(strstr(report->results[index].detail, "minDigits=768") != NULL);
          CHECK(strstr(report->results[index].detail, "maxDigits=896") != NULL);
          CHECK(strstr(report->results[index].detail, "leafThreshold=0") != NULL);
          CHECK(strstr(report->results[index].detail, "candidate=decimal-divide-1e19-preinv") != NULL);
          CHECK(strstr(report->results[index].detail, "featureGate=decimal-format-policy-divide-1e19-preinv") != NULL);
          CHECK(strstr(report->results[index].detail, "gmpClue=mpn_sb_get_str-preinverted-divrem-1") != NULL);
          CHECK(strstr(report->results[index].detail, "thresholdSafety=requires-forced-neighbor") != NULL);
          CHECK(strstr(report->results[index].detail, "noAutoRoute=1") != NULL);
          CHECK(strcmp(report->results[index].adoption, "observe-only") == 0);
          CHECK(!report->results[index].replacement_ready);
          CHECK(strstr(report->results[index].detail, "activeCandidate=decimal-divide-1e19-preinv") != NULL);
        } else if (strstr(report->results[index].detail, "policy=preinv10e19-pairs-window768-896") != NULL) {
          saw_format_policy_preinv10e19_pairs_window768_896 = 1;
          CHECK(strstr(report->results[index].detail, "minDigits=768") != NULL);
          CHECK(strstr(report->results[index].detail, "maxDigits=896") != NULL);
          CHECK(strstr(report->results[index].detail, "leafThreshold=0") != NULL);
          CHECK(strstr(report->results[index].detail, "candidate=decimal-divide-1e19-preinv-pair-writer") != NULL);
          CHECK(strstr(report->results[index].detail, "featureGate=decimal-format-policy-divide-1e19-preinv-pairs") != NULL);
          CHECK(strstr(report->results[index].detail, "gmpClue=mpn_sb_get_str-preinverted-divrem-1+digit-emission") != NULL);
          CHECK(strstr(report->results[index].detail, "thresholdSafety=requires-forced-neighbor") != NULL);
          CHECK(strstr(report->results[index].detail, "noAutoRoute=1") != NULL);
          CHECK(strcmp(report->results[index].adoption, "observe-only") == 0);
          CHECK(!report->results[index].replacement_ready);
          CHECK(strstr(report->results[index].detail, "activeCandidate=decimal-divide-1e19-preinv-pair-writer") != NULL);
        } else if (strstr(report->results[index].detail, "policy=preinv10e19-window768-960") != NULL) {
          saw_format_policy_preinv10e19_window768_960 = 1;
          CHECK(strstr(report->results[index].detail, "minDigits=768") != NULL);
          CHECK(strstr(report->results[index].detail, "maxDigits=960") != NULL);
          CHECK(strstr(report->results[index].detail, "leafThreshold=0") != NULL);
          CHECK(strstr(report->results[index].detail, "candidate=decimal-divide-1e19-preinv") != NULL);
          CHECK(strstr(report->results[index].detail, "featureGate=decimal-format-policy-divide-1e19-preinv") != NULL);
          CHECK(strstr(report->results[index].detail, "gmpClue=mpn_sb_get_str-preinverted-divrem-1") != NULL);
          CHECK(strstr(report->results[index].detail, "thresholdSafety=requires-forced-neighbor") != NULL);
          CHECK(strstr(report->results[index].detail, "noAutoRoute=1") != NULL);
          CHECK(strcmp(report->results[index].adoption, "observe-only") == 0);
          CHECK(!report->results[index].replacement_ready);
          CHECK(strstr(report->results[index].detail, "activeCandidate=decimal-divide-1e19-preinv") != NULL);
        } else if (strstr(report->results[index].detail, "policy=preinv10e19-pairs-window768-960") != NULL) {
          saw_format_policy_preinv10e19_pairs_window768_960 = 1;
          CHECK(strstr(report->results[index].detail, "minDigits=768") != NULL);
          CHECK(strstr(report->results[index].detail, "maxDigits=960") != NULL);
          CHECK(strstr(report->results[index].detail, "leafThreshold=0") != NULL);
          CHECK(strstr(report->results[index].detail, "candidate=decimal-divide-1e19-preinv-pair-writer") != NULL);
          CHECK(strstr(report->results[index].detail, "featureGate=decimal-format-policy-divide-1e19-preinv-pairs") != NULL);
          CHECK(strstr(report->results[index].detail, "gmpClue=mpn_sb_get_str-preinverted-divrem-1+digit-emission") != NULL);
          CHECK(strstr(report->results[index].detail, "thresholdSafety=requires-forced-neighbor") != NULL);
          CHECK(strstr(report->results[index].detail, "noAutoRoute=1") != NULL);
          CHECK(strcmp(report->results[index].adoption, "observe-only") == 0);
          CHECK(!report->results[index].replacement_ready);
          CHECK(strstr(report->results[index].detail, "activeCandidate=decimal-divide-1e19-preinv-pair-writer") != NULL);
        } else if (strstr(report->results[index].detail, "policy=preinv10e19-window896-1000") != NULL) {
          saw_format_policy_preinv10e19_window896_1000 = 1;
          CHECK(strstr(report->results[index].detail, "minDigits=896") != NULL);
          CHECK(strstr(report->results[index].detail, "maxDigits=1000") != NULL);
          CHECK(strstr(report->results[index].detail, "leafThreshold=0") != NULL);
          CHECK(strstr(report->results[index].detail, "candidate=decimal-divide-1e19-preinv") != NULL);
          CHECK(strstr(report->results[index].detail, "featureGate=decimal-format-policy-divide-1e19-preinv") != NULL);
          CHECK(strstr(report->results[index].detail, "gmpClue=mpn_sb_get_str-preinverted-divrem-1") != NULL);
          CHECK(strstr(report->results[index].detail, "thresholdSafety=requires-forced-neighbor") != NULL);
          CHECK(strstr(report->results[index].detail, "noAutoRoute=1") != NULL);
          CHECK(strcmp(report->results[index].adoption, "observe-only") == 0);
          CHECK(!report->results[index].replacement_ready);
          CHECK(strstr(report->results[index].detail, "activeCandidate=decimal-divide-1e19-preinv") != NULL);
        } else if (strstr(report->results[index].detail, "policy=preinv10e19-pairs-window896-1000") != NULL) {
          saw_format_policy_preinv10e19_pairs_window896_1000 = 1;
          CHECK(strstr(report->results[index].detail, "minDigits=896") != NULL);
          CHECK(strstr(report->results[index].detail, "maxDigits=1000") != NULL);
          CHECK(strstr(report->results[index].detail, "leafThreshold=0") != NULL);
          CHECK(strstr(report->results[index].detail, "candidate=decimal-divide-1e19-preinv-pair-writer") != NULL);
          CHECK(strstr(report->results[index].detail, "featureGate=decimal-format-policy-divide-1e19-preinv-pairs") != NULL);
          CHECK(strstr(report->results[index].detail, "gmpClue=mpn_sb_get_str-preinverted-divrem-1+digit-emission") != NULL);
          CHECK(strstr(report->results[index].detail, "thresholdSafety=requires-forced-neighbor") != NULL);
          CHECK(strstr(report->results[index].detail, "noAutoRoute=1") != NULL);
          CHECK(strcmp(report->results[index].adoption, "observe-only") == 0);
          CHECK(!report->results[index].replacement_ready);
          CHECK(strstr(report->results[index].detail, "activeCandidate=decimal-divide-1e19-preinv-pair-writer") != NULL);
        } else {
          CHECK(0);
        }
        if (report->results[index].digits == 768) saw_format_policy768_probe = 1;
        else if (report->results[index].digits == 896) saw_format_policy896_probe = 1;
        else if (report->results[index].digits == 960) saw_format_policy960_probe = 1;
        else if (report->results[index].digits == 1000) saw_format_policy1000_probe = 1;
        else if (report->results[index].digits == 4096) saw_format_policy4096_probe = 1;
        else if (report->results[index].digits == 8192) saw_format_policy8192_probe = 1;
        else if (report->results[index].digits == 16384) saw_format_policy16384_probe = 1;
        else CHECK(0);
      } else if (strcmp(report->results[index].operation, "square-policy") == 0) {
        saw_square_policy_probe = 1;
        CHECK(strstr(report->results[index].detail, "op=square-policy") != NULL);
        CHECK(strstr(report->results[index].detail, "baseline=mpz_mul") != NULL);
        CHECK(strstr(report->results[index].detail, "operandFamilies=1") != NULL);
        CHECK(strstr(report->results[index].detail, "featureGate=square-policy") != NULL);
        if (strstr(report->results[index].detail, "policy=current-default") != NULL) {
          saw_square_policy_current = 1;
          CHECK(strstr(report->results[index].detail, "candidate=current-scratch-square") != NULL);
          CHECK(strstr(report->results[index].detail, "activeCandidate=current-scratch-square") != NULL);
          CHECK(strstr(report->results[index].detail, "candidateAvailable=yes") != NULL);
          CHECK(strstr(report->results[index].detail, "thresholdSafety=direct-row") != NULL);
          CHECK(strstr(report->results[index].detail, "noAutoRoute=0") != NULL);
        } else if (strstr(report->results[index].detail, "policy=karatsuba-thr96") != NULL) {
          saw_square_policy_thr96 = 1;
          CHECK(strstr(report->results[index].detail, "leafThreshold=96") != NULL);
          CHECK(strstr(report->results[index].detail, "candidate=karatsuba-square") != NULL);
          CHECK(strstr(report->results[index].detail, "activeCandidate=karatsuba-square") != NULL);
          CHECK(strstr(report->results[index].detail, "candidateAvailable=yes") != NULL);
          CHECK(strstr(report->results[index].detail, "thresholdSafety=requires-forced-neighbor") != NULL);
          CHECK(strstr(report->results[index].detail, "noAutoRoute=1") != NULL);
          CHECK(strcmp(report->results[index].adoption, "observe-only") == 0);
          CHECK(!report->results[index].replacement_ready);
        } else {
          CHECK(0);
        }
        if (report->results[index].digits == 1000) saw_square_policy1000_probe = 1;
        else if (report->results[index].digits == 4096) saw_square_policy4096_probe = 1;
        else if (report->results[index].digits == 8192) saw_square_policy8192_probe = 1;
        else if (report->results[index].digits == 16384) saw_square_policy16384_probe = 1;
        else CHECK(0);
      } else if (strcmp(report->results[index].operation, "mul-policy") == 0) {
        saw_mul_policy_probe = 1;
        CHECK(strstr(report->results[index].detail, "op=mul-policy") != NULL);
        CHECK(strstr(report->results[index].detail, "baseline=mpz_mul") != NULL);
        CHECK(strstr(report->results[index].detail, "operandFamilies=2") != NULL);
        CHECK(strstr(report->results[index].detail, "featureGate=mul-policy") != NULL);
        if (strstr(report->results[index].detail, "policy=current-default") != NULL) {
          saw_mul_policy_current = 1;
          CHECK(strstr(report->results[index].detail, "candidate=current-scratch-mul") != NULL);
          CHECK(strstr(report->results[index].detail, "activeCandidate=current-scratch-mul") != NULL);
          CHECK(strstr(report->results[index].detail, "candidateAvailable=yes") != NULL);
          CHECK(strstr(report->results[index].detail, "thresholdSafety=direct-row") != NULL);
          CHECK(strstr(report->results[index].detail, "noAutoRoute=0") != NULL);
        } else if (strstr(report->results[index].detail, "policy=threshold96-ge8192") != NULL) {
          saw_mul_policy_threshold96 = 1;
          CHECK(strstr(report->results[index].detail, "minDigits=8192") != NULL);
          CHECK(strstr(report->results[index].detail, "leafThreshold=96") != NULL);
          CHECK(strstr(report->results[index].detail, "depthLimit=0") != NULL);
          CHECK(strstr(report->results[index].detail, "candidate=karatsuba-threshold96") != NULL);
          CHECK(strstr(report->results[index].detail, "candidateAvailable=yes") != NULL);
          CHECK(strstr(report->results[index].detail, "featureGate=mul-policy-threshold96-ge8192") != NULL);
          CHECK(strstr(report->results[index].detail, "gmpClue=mpn_mul-threshold-table+root-size-gate") != NULL);
          CHECK(strstr(report->results[index].detail, "thresholdSafety=requires-forced-neighbor") != NULL);
          CHECK(strstr(report->results[index].detail, "noAutoRoute=1") != NULL);
          CHECK(strcmp(report->results[index].adoption, "observe-only") == 0);
          CHECK(!report->results[index].replacement_ready);
          if (report->results[index].digits < 8192) {
            CHECK(strstr(report->results[index].detail, "activeCandidate=current-scratch-mul") != NULL);
          } else {
            CHECK(strstr(report->results[index].detail, "activeCandidate=karatsuba-threshold96") != NULL);
          }
        } else if (strstr(report->results[index].detail, "policy=toom3-u4-ge8192-leaf48") != NULL) {
          saw_mul_policy_toom_leaf48 = 1;
          CHECK(strstr(report->results[index].detail, "minDigits=8192") != NULL);
          CHECK(strstr(report->results[index].detail, "leafThreshold=48") != NULL);
          CHECK(strstr(report->results[index].detail, "candidate=one-level-toom3+unroll4-leaf") != NULL);
          CHECK(strstr(report->results[index].detail, "thresholdSafety=requires-forced-neighbor") != NULL);
          CHECK(strstr(report->results[index].detail, "noAutoRoute=1") != NULL);
          CHECK(strcmp(report->results[index].adoption, "observe-only") == 0);
          CHECK(!report->results[index].replacement_ready);
#if defined(_MSC_VER) && defined(_M_X64)
          CHECK(strstr(report->results[index].detail, "candidateAvailable=yes") != NULL);
          if (report->results[index].digits < 8192) {
            CHECK(strstr(report->results[index].detail, "activeCandidate=current-scratch-mul") != NULL);
          } else {
            CHECK(strstr(report->results[index].detail, "activeCandidate=one-level-toom3+unroll4-leaf") != NULL);
          }
#else
          CHECK(strstr(report->results[index].detail, "candidateAvailable=no") != NULL);
          CHECK(strstr(report->results[index].detail, "activeCandidate=current-scratch-mul") != NULL);
          CHECK(strcmp(report->results[index].adoption, "observe-only") == 0);
          CHECK(!report->results[index].replacement_ready);
#endif
        } else if (strstr(report->results[index].detail, "policy=toom3-u4-rec-ge16384-leaf64-depth2") != NULL) {
          saw_mul_policy_toom_rec = 1;
          CHECK(strstr(report->results[index].detail, "minDigits=16384") != NULL);
          CHECK(strstr(report->results[index].detail, "leafThreshold=64") != NULL);
          CHECK(strstr(report->results[index].detail, "depthLimit=2") != NULL);
          CHECK(strstr(report->results[index].detail, "candidate=recursive-toom3+unroll4") != NULL);
          CHECK(strstr(report->results[index].detail, "thresholdSafety=requires-forced-neighbor") != NULL);
          CHECK(strstr(report->results[index].detail, "noAutoRoute=1") != NULL);
          CHECK(strcmp(report->results[index].adoption, "observe-only") == 0);
          CHECK(!report->results[index].replacement_ready);
#if defined(_MSC_VER) && defined(_M_X64)
          CHECK(strstr(report->results[index].detail, "candidateAvailable=yes") != NULL);
          if (report->results[index].digits < 16384) {
            CHECK(strstr(report->results[index].detail, "activeCandidate=current-scratch-mul") != NULL);
          } else {
            CHECK(strstr(report->results[index].detail, "activeCandidate=recursive-toom3+unroll4") != NULL);
          }
#else
          CHECK(strstr(report->results[index].detail, "candidateAvailable=no") != NULL);
          CHECK(strstr(report->results[index].detail, "activeCandidate=current-scratch-mul") != NULL);
          CHECK(strcmp(report->results[index].adoption, "observe-only") == 0);
          CHECK(!report->results[index].replacement_ready);
#endif
        } else {
          CHECK(0);
        }
        if (report->results[index].digits == 1000) saw_mul_policy1000_probe = 1;
        else if (report->results[index].digits == 4096) saw_mul_policy4096_probe = 1;
        else if (report->results[index].digits == 8192) saw_mul_policy8192_probe = 1;
        else if (report->results[index].digits == 16384) saw_mul_policy16384_probe = 1;
        else CHECK(0);
      } else {
        CHECK(0);
      }
      if (strcmp(report->results[index].adoption, "promotion-ready") == 0) {
        CHECK(report->results[index].stable_sample_count >= 4);
        CHECK(report->results[index].speed_ratio <= report->results[index].max_allowed_speed_ratio);
        CHECK(report->results[index].worst_pair_ratio <= 1.0);
      }
      CHECK(strstr(report->results[index].adoption, "promotion-ready") != NULL ||
        strstr(report->results[index].adoption, "observe-only") != NULL ||
        strstr(report->results[index].adoption, "blocked-output-mismatch") != NULL);
    } else if (strcmp(report->results[index].category, "policy-gate") == 0) {
      saw_policy_gate = 1;
      CHECK(report->results[index].passed);
      CHECK(report->results[index].parity_verified);
      CHECK(report->results[index].scratch_us > 0);
      CHECK(report->results[index].gmp_us > 0);
      CHECK(report->results[index].speed_ratio > 0.0);
      CHECK(report->results[index].max_allowed_speed_ratio == 1.0);
      CHECK(report->results[index].stable_sample_count <= report->results[index].sample_count);
      CHECK(report->results[index].worst_pair_ratio > 0.0);
      CHECK(strstr(report->results[index].detail, "thresholdSafety=forced-neighbor") != NULL);
      CHECK(strstr(report->results[index].detail, "forcedCandidate=yes") != NULL);
      CHECK(strstr(report->results[index].detail, "ratioMethod=paired-median") != NULL);
      CHECK(strstr(report->results[index].detail, "adoption=") != NULL);
      if (strcmp(report->results[index].operation, "format-policy-safety") == 0 ||
          strcmp(report->results[index].operation, "format-policy-deep-safety") == 0) {
        int is_deep_format_gate = strcmp(report->results[index].operation, "format-policy-deep-safety") == 0;
        CHECK(report->results[index].sample_count == 2);
        CHECK(strstr(
          report->results[index].detail,
          is_deep_format_gate ? "op=format-policy-deep-safety" : "op=format-policy-safety") != NULL);
        CHECK(strstr(report->results[index].detail, "neighborStable=") != NULL);
        CHECK(strstr(report->results[index].detail, "gateStable=") != NULL);
        CHECK(strstr(report->results[index].detail, is_deep_format_gate ? "requiredStablePairs=8/9" : "requiredStablePairs=4/5") != NULL);
        CHECK(strstr(report->results[index].detail, "neighborRatio=") != NULL);
        CHECK(strstr(report->results[index].detail, "gateRatio=") != NULL);
        CHECK(strstr(report->results[index].detail, "baseline=mpz_get_str") != NULL);
        CHECK(strstr(report->results[index].detail, "featureGate=threshold-neighbor") != NULL);
        CHECK(strstr(report->results[index].detail, "gmpClue=product-codegen") != NULL);
        int is_shallow_preinv10e19_gate =
          !is_deep_format_gate &&
          strstr(report->results[index].detail, "policy=preinv10e19") != NULL;
        CHECK(strstr(
          report->results[index].detail,
          is_shallow_preinv10e19_gate ? "deepConfirmation=required" : "deepConfirmation=not-required") != NULL);
        if (is_shallow_preinv10e19_gate) {
          CHECK(!report->results[index].replacement_ready);
          CHECK(strcmp(report->results[index].adoption, "observe-only") == 0);
          CHECK(strcmp(report->results[index].status, "policy-ready") != 0);
        }
        if (strstr(report->results[index].detail, "policy=deep-preinv10e19-window768-1000") != NULL) {
          saw_format_policy_deep_gate_preinv10e19_window768_1000 = 1;
          CHECK(is_deep_format_gate);
          CHECK(strstr(report->results[index].detail, "neighbor=768") != NULL);
          CHECK(strstr(report->results[index].detail, "gate=1000") != NULL);
          CHECK(strstr(report->results[index].detail, "min=768") != NULL);
          CHECK(strstr(report->results[index].detail, "max=1000") != NULL);
          CHECK(strstr(report->results[index].detail, "leaf=0") != NULL);
          CHECK(strstr(report->results[index].detail, "neighborStable=") != NULL);
          CHECK(strstr(report->results[index].detail, "/9") != NULL);
          CHECK(strstr(report->results[index].detail, "candidate=decimal-divide-1e19-preinv") != NULL);
        } else if (strstr(report->results[index].detail, "policy=deep-preinv10e19-window768-896") != NULL) {
          saw_format_policy_deep_gate_preinv10e19_window768_896 = 1;
          CHECK(is_deep_format_gate);
          CHECK(strstr(report->results[index].detail, "neighbor=768") != NULL);
          CHECK(strstr(report->results[index].detail, "gate=896") != NULL);
          CHECK(strstr(report->results[index].detail, "min=768") != NULL);
          CHECK(strstr(report->results[index].detail, "max=896") != NULL);
          CHECK(strstr(report->results[index].detail, "leaf=0") != NULL);
          CHECK(strstr(report->results[index].detail, "neighborStable=") != NULL);
          CHECK(strstr(report->results[index].detail, "/9") != NULL);
          CHECK(strstr(report->results[index].detail, "candidate=decimal-divide-1e19-preinv") != NULL);
        } else if (strstr(report->results[index].detail, "policy=deep-preinv10e19-window768-960") != NULL) {
          saw_format_policy_deep_gate_preinv10e19_window768_960 = 1;
          CHECK(is_deep_format_gate);
          CHECK(strstr(report->results[index].detail, "neighbor=768") != NULL);
          CHECK(strstr(report->results[index].detail, "gate=960") != NULL);
          CHECK(strstr(report->results[index].detail, "min=768") != NULL);
          CHECK(strstr(report->results[index].detail, "max=960") != NULL);
          CHECK(strstr(report->results[index].detail, "leaf=0") != NULL);
          CHECK(strstr(report->results[index].detail, "neighborStable=") != NULL);
          CHECK(strstr(report->results[index].detail, "/9") != NULL);
          CHECK(strstr(report->results[index].detail, "candidate=decimal-divide-1e19-preinv") != NULL);
        } else if (strstr(report->results[index].detail, "policy=deep-preinv10e19-window896-1000") != NULL) {
          saw_format_policy_deep_gate_preinv10e19_window896_1000 = 1;
          CHECK(is_deep_format_gate);
          CHECK(strstr(report->results[index].detail, "neighbor=896") != NULL);
          CHECK(strstr(report->results[index].detail, "gate=1000") != NULL);
          CHECK(strstr(report->results[index].detail, "min=896") != NULL);
          CHECK(strstr(report->results[index].detail, "max=1000") != NULL);
          CHECK(strstr(report->results[index].detail, "leaf=0") != NULL);
          CHECK(strstr(report->results[index].detail, "neighborStable=") != NULL);
          CHECK(strstr(report->results[index].detail, "/9") != NULL);
          CHECK(strstr(report->results[index].detail, "candidate=decimal-divide-1e19-preinv") != NULL);
        } else if (strstr(report->results[index].detail, "policy=deep-preinv10e19-pairs-window768-896") != NULL) {
          saw_format_policy_deep_gate_preinv10e19_pairs_window768_896 = 1;
          CHECK(is_deep_format_gate);
          CHECK(strstr(report->results[index].detail, "neighbor=768") != NULL);
          CHECK(strstr(report->results[index].detail, "gate=896") != NULL);
          CHECK(strstr(report->results[index].detail, "min=768") != NULL);
          CHECK(strstr(report->results[index].detail, "max=896") != NULL);
          CHECK(strstr(report->results[index].detail, "leaf=0") != NULL);
          CHECK(strstr(report->results[index].detail, "neighborStable=") != NULL);
          CHECK(strstr(report->results[index].detail, "/9") != NULL);
          CHECK(strstr(report->results[index].detail, "candidate=decimal-divide-1e19-preinv-pair-writer") != NULL);
        } else if (strstr(report->results[index].detail, "policy=deep-preinv10e19-pairs-window768-960") != NULL) {
          saw_format_policy_deep_gate_preinv10e19_pairs_window768_960 = 1;
          CHECK(is_deep_format_gate);
          CHECK(strstr(report->results[index].detail, "neighbor=768") != NULL);
          CHECK(strstr(report->results[index].detail, "gate=960") != NULL);
          CHECK(strstr(report->results[index].detail, "min=768") != NULL);
          CHECK(strstr(report->results[index].detail, "max=960") != NULL);
          CHECK(strstr(report->results[index].detail, "leaf=0") != NULL);
          CHECK(strstr(report->results[index].detail, "neighborStable=") != NULL);
          CHECK(strstr(report->results[index].detail, "/9") != NULL);
          CHECK(strstr(report->results[index].detail, "candidate=decimal-divide-1e19-preinv-pair-writer") != NULL);
        } else if (strstr(report->results[index].detail, "policy=deep-preinv10e19-pairs-window896-1000") != NULL) {
          saw_format_policy_deep_gate_preinv10e19_pairs_window896_1000 = 1;
          CHECK(is_deep_format_gate);
          CHECK(strstr(report->results[index].detail, "neighbor=896") != NULL);
          CHECK(strstr(report->results[index].detail, "gate=1000") != NULL);
          CHECK(strstr(report->results[index].detail, "min=896") != NULL);
          CHECK(strstr(report->results[index].detail, "max=1000") != NULL);
          CHECK(strstr(report->results[index].detail, "leaf=0") != NULL);
          CHECK(strstr(report->results[index].detail, "neighborStable=") != NULL);
          CHECK(strstr(report->results[index].detail, "/9") != NULL);
          CHECK(strstr(report->results[index].detail, "candidate=decimal-divide-1e19-preinv-pair-writer") != NULL);
        } else if (strstr(report->results[index].detail, "policy=direct-ge4096-leaf8") != NULL) {
          CHECK(!is_deep_format_gate);
          saw_format_policy_gate_direct4096 = 1;
          CHECK(strstr(report->results[index].detail, "neighbor=3072") != NULL);
          CHECK(strstr(report->results[index].detail, "gate=4096") != NULL);
          CHECK(strstr(report->results[index].detail, "min=4096") != NULL);
          CHECK(strstr(report->results[index].detail, "leaf=8") != NULL);
          CHECK(strstr(report->results[index].detail, "candidate=decimal-dc-direct-writer") != NULL);
        } else if (strstr(report->results[index].detail, "policy=direct-ge8192-leaf16") != NULL) {
          CHECK(!is_deep_format_gate);
          saw_format_policy_gate_direct8192 = 1;
          CHECK(strstr(report->results[index].detail, "neighbor=6144") != NULL);
          CHECK(strstr(report->results[index].detail, "gate=8192") != NULL);
          CHECK(strstr(report->results[index].detail, "min=8192") != NULL);
          CHECK(strstr(report->results[index].detail, "leaf=16") != NULL);
          CHECK(strstr(report->results[index].detail, "candidate=decimal-dc-direct-writer") != NULL);
        } else if (strstr(report->results[index].detail, "policy=static-ge4096-l16") != NULL) {
          CHECK(!is_deep_format_gate);
          saw_format_policy_gate_static4096 = 1;
          CHECK(strstr(report->results[index].detail, "neighbor=3072") != NULL);
          CHECK(strstr(report->results[index].detail, "gate=4096") != NULL);
          CHECK(strstr(report->results[index].detail, "min=4096") != NULL);
          CHECK(strstr(report->results[index].detail, "leaf=16") != NULL);
          CHECK(strstr(report->results[index].detail, "candidate=dc-static-direct") != NULL);
        } else if (strstr(report->results[index].detail, "policy=static-ge8192-l8") != NULL) {
          CHECK(!is_deep_format_gate);
          saw_format_policy_gate_static8192 = 1;
          CHECK(strstr(report->results[index].detail, "neighbor=6144") != NULL);
          CHECK(strstr(report->results[index].detail, "gate=8192") != NULL);
          CHECK(strstr(report->results[index].detail, "min=8192") != NULL);
          CHECK(strstr(report->results[index].detail, "leaf=8") != NULL);
          CHECK(strstr(report->results[index].detail, "candidate=dc-static-direct") != NULL);
        } else if (strstr(report->results[index].detail, "policy=workspace-ge4096-leaf16") != NULL) {
          CHECK(!is_deep_format_gate);
          saw_format_policy_gate_workspace4096 = 1;
          CHECK(strstr(report->results[index].detail, "neighbor=3072") != NULL);
          CHECK(strstr(report->results[index].detail, "gate=4096") != NULL);
          CHECK(strstr(report->results[index].detail, "min=4096") != NULL);
          CHECK(strstr(report->results[index].detail, "leaf=16") != NULL);
          CHECK(strstr(report->results[index].detail, "candidate=decimal-dc-direct-workspace") != NULL);
        } else if (strstr(report->results[index].detail, "policy=preinv-ge4096-leaf8") != NULL) {
          CHECK(!is_deep_format_gate);
          saw_format_policy_gate_preinv4096 = 1;
          CHECK(strstr(report->results[index].detail, "neighbor=3072") != NULL);
          CHECK(strstr(report->results[index].detail, "gate=4096") != NULL);
          CHECK(strstr(report->results[index].detail, "min=4096") != NULL);
          CHECK(strstr(report->results[index].detail, "leaf=8") != NULL);
          CHECK(strstr(report->results[index].detail, "candidate=decimal-dc-direct-preinv-qhat") != NULL);
        } else if (strstr(report->results[index].detail, "policy=preinv-ge8192-leaf16") != NULL) {
          CHECK(!is_deep_format_gate);
          saw_format_policy_gate_preinv8192 = 1;
          CHECK(strstr(report->results[index].detail, "neighbor=6144") != NULL);
          CHECK(strstr(report->results[index].detail, "gate=8192") != NULL);
          CHECK(strstr(report->results[index].detail, "min=8192") != NULL);
          CHECK(strstr(report->results[index].detail, "leaf=16") != NULL);
          CHECK(strstr(report->results[index].detail, "candidate=decimal-dc-direct-preinv-qhat") != NULL);
        } else if (strstr(report->results[index].detail, "policy=preinv-ge16384-leaf16") != NULL) {
          CHECK(!is_deep_format_gate);
          saw_format_policy_gate_preinv16384 = 1;
          CHECK(strstr(report->results[index].detail, "neighbor=12288") != NULL);
          CHECK(strstr(report->results[index].detail, "gate=16384") != NULL);
          CHECK(strstr(report->results[index].detail, "min=16384") != NULL);
          CHECK(strstr(report->results[index].detail, "leaf=16") != NULL);
          CHECK(strstr(report->results[index].detail, "candidate=decimal-dc-direct-preinv-qhat") != NULL);
        } else if (strstr(report->results[index].detail, "policy=preinv10e19-window768-1000") != NULL) {
          CHECK(!is_deep_format_gate);
          saw_format_policy_gate_preinv10e19_window = 1;
          CHECK(strstr(report->results[index].detail, "neighbor=768") != NULL);
          CHECK(strstr(report->results[index].detail, "gate=1000") != NULL);
          CHECK(strstr(report->results[index].detail, "min=768") != NULL);
          CHECK(strstr(report->results[index].detail, "max=1000") != NULL);
          CHECK(strstr(report->results[index].detail, "leaf=0") != NULL);
          CHECK(strstr(report->results[index].detail, "candidate=decimal-divide-1e19-preinv") != NULL);
        } else if (strstr(report->results[index].detail, "policy=preinv10e19-pairs-window768-1000") != NULL) {
          CHECK(!is_deep_format_gate);
          saw_format_policy_gate_preinv10e19_pairs_window = 1;
          CHECK(strstr(report->results[index].detail, "neighbor=768") != NULL);
          CHECK(strstr(report->results[index].detail, "gate=1000") != NULL);
          CHECK(strstr(report->results[index].detail, "min=768") != NULL);
          CHECK(strstr(report->results[index].detail, "max=1000") != NULL);
          CHECK(strstr(report->results[index].detail, "leaf=0") != NULL);
          CHECK(strstr(report->results[index].detail, "candidate=decimal-divide-1e19-preinv-pair-writer") != NULL);
        } else if (strstr(report->results[index].detail, "policy=preinv10e19-window768-896") != NULL) {
          CHECK(!is_deep_format_gate);
          saw_format_policy_gate_preinv10e19_window768_896 = 1;
          CHECK(strstr(report->results[index].detail, "neighbor=768") != NULL);
          CHECK(strstr(report->results[index].detail, "gate=896") != NULL);
          CHECK(strstr(report->results[index].detail, "min=768") != NULL);
          CHECK(strstr(report->results[index].detail, "max=896") != NULL);
          CHECK(strstr(report->results[index].detail, "leaf=0") != NULL);
          CHECK(strstr(report->results[index].detail, "candidate=decimal-divide-1e19-preinv") != NULL);
        } else if (strstr(report->results[index].detail, "policy=preinv10e19-pairs-window768-896") != NULL) {
          CHECK(!is_deep_format_gate);
          saw_format_policy_gate_preinv10e19_pairs_window768_896 = 1;
          CHECK(strstr(report->results[index].detail, "neighbor=768") != NULL);
          CHECK(strstr(report->results[index].detail, "gate=896") != NULL);
          CHECK(strstr(report->results[index].detail, "min=768") != NULL);
          CHECK(strstr(report->results[index].detail, "max=896") != NULL);
          CHECK(strstr(report->results[index].detail, "leaf=0") != NULL);
          CHECK(strstr(report->results[index].detail, "candidate=decimal-divide-1e19-preinv-pair-writer") != NULL);
        } else if (strstr(report->results[index].detail, "policy=preinv10e19-window768-960") != NULL) {
          CHECK(!is_deep_format_gate);
          saw_format_policy_gate_preinv10e19_window768_960 = 1;
          CHECK(strstr(report->results[index].detail, "neighbor=768") != NULL);
          CHECK(strstr(report->results[index].detail, "gate=960") != NULL);
          CHECK(strstr(report->results[index].detail, "min=768") != NULL);
          CHECK(strstr(report->results[index].detail, "max=960") != NULL);
          CHECK(strstr(report->results[index].detail, "leaf=0") != NULL);
          CHECK(strstr(report->results[index].detail, "candidate=decimal-divide-1e19-preinv") != NULL);
        } else if (strstr(report->results[index].detail, "policy=preinv10e19-pairs-window768-960") != NULL) {
          CHECK(!is_deep_format_gate);
          saw_format_policy_gate_preinv10e19_pairs_window768_960 = 1;
          CHECK(strstr(report->results[index].detail, "neighbor=768") != NULL);
          CHECK(strstr(report->results[index].detail, "gate=960") != NULL);
          CHECK(strstr(report->results[index].detail, "min=768") != NULL);
          CHECK(strstr(report->results[index].detail, "max=960") != NULL);
          CHECK(strstr(report->results[index].detail, "leaf=0") != NULL);
          CHECK(strstr(report->results[index].detail, "candidate=decimal-divide-1e19-preinv-pair-writer") != NULL);
        } else if (strstr(report->results[index].detail, "policy=preinv10e19-window896-1000") != NULL) {
          CHECK(!is_deep_format_gate);
          saw_format_policy_gate_preinv10e19_window896_1000 = 1;
          CHECK(strstr(report->results[index].detail, "neighbor=896") != NULL);
          CHECK(strstr(report->results[index].detail, "gate=1000") != NULL);
          CHECK(strstr(report->results[index].detail, "min=896") != NULL);
          CHECK(strstr(report->results[index].detail, "max=1000") != NULL);
          CHECK(strstr(report->results[index].detail, "leaf=0") != NULL);
          CHECK(strstr(report->results[index].detail, "candidate=decimal-divide-1e19-preinv") != NULL);
        } else if (strstr(report->results[index].detail, "policy=preinv10e19-pairs-window896-1000") != NULL) {
          CHECK(!is_deep_format_gate);
          saw_format_policy_gate_preinv10e19_pairs_window896_1000 = 1;
          CHECK(strstr(report->results[index].detail, "neighbor=896") != NULL);
          CHECK(strstr(report->results[index].detail, "gate=1000") != NULL);
          CHECK(strstr(report->results[index].detail, "min=896") != NULL);
          CHECK(strstr(report->results[index].detail, "max=1000") != NULL);
          CHECK(strstr(report->results[index].detail, "leaf=0") != NULL);
          CHECK(strstr(report->results[index].detail, "candidate=decimal-divide-1e19-preinv-pair-writer") != NULL);
        } else {
          CHECK(0);
        }
      } else if (strcmp(report->results[index].operation, "format-policy-route-audit") == 0) {
        saw_format_policy_route_audit = 1;
        CHECK(report->results[index].sample_count == 4);
        CHECK(strstr(report->results[index].detail, "op=format-policy-route-audit") != NULL);
        CHECK(strstr(report->results[index].detail, "sizes=768,896,960,1000") != NULL);
        CHECK(strstr(report->results[index].detail, "sizeCount=4") != NULL);
        CHECK(strstr(report->results[index].detail, "samples=9") != NULL);
        CHECK(strstr(report->results[index].detail, "requiredStablePairs=8/9") != NULL);
        CHECK(strstr(report->results[index].detail, "safeSizes=") != NULL);
        CHECK(strstr(report->results[index].detail, "hashSafe=") != NULL);
        CHECK(strstr(report->results[index].detail, "hashGate=matched") != NULL);
        CHECK(strstr(report->results[index].detail, "parity=matched") != NULL);
        CHECK(strstr(report->results[index].detail, "forcedCandidate=yes") != NULL);
        CHECK(strstr(report->results[index].detail, "thresholdSafety=forced-neighbor") != NULL);
        CHECK(strstr(report->results[index].detail, "deepConfirmation=done") != NULL);
        CHECK(strstr(report->results[index].detail, "noAutoRoute=1") != NULL);
        CHECK(strstr(report->results[index].detail, "baseline=current-scratch-format") != NULL);
        CHECK(strstr(report->results[index].detail, "oracle=mpz_get_str") != NULL);
        CHECK(strstr(report->results[index].detail, "candCurrentMax=") != NULL);
        CHECK(strstr(report->results[index].detail, "candGmpMax=") != NULL);
        CHECK(strstr(report->results[index].detail, "currentGmpMax=") != NULL);
        CHECK(strstr(report->results[index].detail, "maxWorstPairRatio=") != NULL);
        CHECK(strstr(report->results[index].detail, "ratioMethod=paired-median") != NULL);
        CHECK(strstr(report->results[index].detail, "sameInput=yes") != NULL);
        CHECK(strstr(report->results[index].detail, "sameRunTournament=yes") != NULL);
        CHECK(strstr(report->results[index].detail, "featureGate=decimal-format-window-promotion-audit") != NULL);
        CHECK(strstr(report->results[index].detail, "gmpClue=mfast-factor64pre-precompute") != NULL);
        if (strstr(report->results[index].detail, "policy=audit-preinv10e19-window768-1000") != NULL) {
          saw_format_policy_route_audit_preinv10e19 = 1;
          CHECK(strstr(report->results[index].detail, "candidate=decimal-divide-1e19-preinv") != NULL);
        } else if (strstr(report->results[index].detail, "policy=audit-preinv10e19-pairs-window768-1000") != NULL) {
          saw_format_policy_route_audit_preinv10e19_pairs = 1;
          CHECK(strstr(report->results[index].detail, "candidate=decimal-divide-1e19-preinv-pair-writer") != NULL);
        } else {
          CHECK(0);
        }
      } else if (strcmp(report->results[index].operation, "format-dc-route-safety") == 0) {
        saw_format_dc_route_safety_gate = 1;
        CHECK(report->results[index].sample_count == 3);
        CHECK(!report->results[index].replacement_ready);
        CHECK(strcmp(report->results[index].adoption, "observe-only") == 0);
        CHECK(strstr(report->results[index].detail, "op=format-dc-route-safety") != NULL);
        CHECK(strstr(report->results[index].detail, "noAutoRoute=1") != NULL);
        CHECK(strstr(report->results[index].detail, "deepConfirmation=required") != NULL);
        CHECK(strstr(report->results[index].detail, "policy=direct16-vs-ladder8") != NULL);
        CHECK(strstr(report->results[index].detail, "sizes=4096,8192,16384") != NULL);
        CHECK(strstr(report->results[index].detail, "minDigits=4096") != NULL);
        CHECK(strstr(report->results[index].detail, "safeSizes=") != NULL);
        CHECK(strstr(report->results[index].detail, "samples=9") != NULL);
        CHECK(strstr(report->results[index].detail, "requiredStablePairs=8/9") != NULL);
        CHECK(strstr(report->results[index].detail, "maxWorstPairRatio=") != NULL);
        CHECK(strstr(report->results[index].detail, "ratioMethod=paired-median") != NULL);
        CHECK(strstr(report->results[index].detail, "candidate=decimal-dc-direct-writer-leaf16") != NULL);
        CHECK(strstr(report->results[index].detail, "baseline=decimal-dc-pow2-ladder-leaf8") != NULL);
        CHECK(strstr(report->results[index].detail, "oracle=mpz_get_str") != NULL);
        CHECK(strstr(report->results[index].detail, "featureGate=decimal-format-dc-route-deep") != NULL);
        CHECK(strstr(report->results[index].detail, "hashSafe=") != NULL);
        CHECK(strstr(report->results[index].detail, "hashGate=matched") != NULL);
        CHECK(strstr(report->results[index].detail, "gmpClue=mfast615fe9e-hashgate") != NULL);
        CHECK(strstr(report->results[index].detail, "warmup=not-counted") != NULL);
      } else if (strcmp(report->results[index].operation, "divmod-preinv-qhat-safety") == 0) {
        saw_divmod_preinv_qhat_safety_gate = 1;
        CHECK(report->results[index].sample_count == 3);
        CHECK(!report->results[index].replacement_ready);
        CHECK(strcmp(report->results[index].adoption, "observe-only") == 0);
        CHECK(strstr(report->results[index].detail, "op=divmod-preinv-qhat-safety") != NULL);
        CHECK(strstr(report->results[index].detail, "sizes=4096,8192,16384") != NULL);
        CHECK(strstr(report->results[index].detail, "safeSizes=") != NULL);
        CHECK(strstr(report->results[index].detail, "requiredStablePairs=4/5") != NULL);
        CHECK(strstr(report->results[index].detail, "maxRatio=") != NULL);
        CHECK(strstr(report->results[index].detail, "maxWorstPairRatio=") != NULL);
        CHECK(strstr(report->results[index].detail, "candidate=scratch-divmod-preinv-qhat") != NULL);
        CHECK(strstr(report->results[index].detail, "baseline=scratch-divmod-context-workspace") != NULL);
        CHECK(strstr(report->results[index].detail, "oracle=mpz_tdiv_qr") != NULL);
        CHECK(strstr(report->results[index].detail, "featureGate=bigint-division-preinv-qhat") != NULL);
        CHECK(strstr(report->results[index].detail, "gmpClue=mpn_sbpi1_div_qr-qhat") != NULL);
        CHECK(strstr(report->results[index].detail, "precomputeScope=per-divisor") != NULL);
        CHECK(strstr(report->results[index].detail, "noAutoRoute=1") != NULL);
      } else if (strcmp(report->results[index].operation, "mul-policy-safety") == 0) {
        CHECK(!report->results[index].replacement_ready);
        CHECK(strcmp(report->results[index].adoption, "observe-only") == 0);
        CHECK(strstr(report->results[index].detail, "op=mul-policy-safety") != NULL);
        CHECK(strstr(report->results[index].detail, "requiredStablePairs=4/5") != NULL);
        CHECK(strstr(report->results[index].detail, "maxRatio=") != NULL);
        CHECK(strstr(report->results[index].detail, "maxWorstPairRatio=") != NULL);
        CHECK(strstr(report->results[index].detail, "baseline=mpz_mul") != NULL);
        CHECK(strstr(report->results[index].detail, "oracle=mpz_mul") != NULL);
        CHECK(strstr(report->results[index].detail, "safeSizes=") != NULL);
        CHECK(strstr(report->results[index].detail, "noAutoRoute=1") != NULL);
        if (strstr(report->results[index].detail, "policy=threshold96-ge8192") != NULL) {
          saw_mul_policy_safety_threshold96_gate = 1;
          CHECK(report->results[index].sample_count == 3);
          CHECK(strstr(report->results[index].detail, "sizes=4096,8192,16384") != NULL);
          CHECK(strstr(report->results[index].detail, "minDigits=8192") != NULL);
          CHECK(strstr(report->results[index].detail, "leafThreshold=96") != NULL);
          CHECK(strstr(report->results[index].detail, "depthLimit=0") != NULL);
          CHECK(strstr(report->results[index].detail, "candidate=karatsuba-threshold96") != NULL);
          CHECK(strstr(report->results[index].detail, "candidateAvailable=yes") != NULL);
          CHECK(strstr(report->results[index].detail, "featureGate=mul-policy-threshold96-ge8192") != NULL);
          CHECK(strstr(report->results[index].detail, "gmpClue=mpn_mul-threshold-table+root-size-gate") != NULL);
        } else if (strstr(report->results[index].detail, "policy=toom3-u4-ge8192-leaf48") != NULL) {
          saw_mul_policy_safety_toom_leaf48_gate = 1;
          CHECK(report->results[index].sample_count == 3);
          CHECK(strstr(report->results[index].detail, "sizes=4096,8192,16384") != NULL);
          CHECK(strstr(report->results[index].detail, "minDigits=8192") != NULL);
          CHECK(strstr(report->results[index].detail, "leafThreshold=48") != NULL);
          CHECK(strstr(report->results[index].detail, "depthLimit=1") != NULL);
          CHECK(strstr(report->results[index].detail, "candidate=one-level-toom3+unroll4-leaf") != NULL);
#if defined(_MSC_VER) && defined(_M_X64)
          CHECK(strstr(report->results[index].detail, "candidateAvailable=yes") != NULL);
#else
          CHECK(strstr(report->results[index].detail, "candidateAvailable=no") != NULL);
#endif
          CHECK(strstr(report->results[index].detail, "featureGate=mul-policy-toom3-u4-ge8192-leaf48") != NULL);
          CHECK(strstr(report->results[index].detail, "gmpClue=toom33-leaf-schedule") != NULL);
        } else if (strstr(report->results[index].detail, "policy=toom3-u4-rec-ge16384-leaf64-depth2") != NULL) {
          saw_mul_policy_safety_toom_rec_gate = 1;
          CHECK(report->results[index].sample_count == 2);
          CHECK(strstr(report->results[index].detail, "sizes=8192,16384") != NULL);
          CHECK(strstr(report->results[index].detail, "minDigits=16384") != NULL);
          CHECK(strstr(report->results[index].detail, "leafThreshold=64") != NULL);
          CHECK(strstr(report->results[index].detail, "depthLimit=2") != NULL);
          CHECK(strstr(report->results[index].detail, "candidate=recursive-toom3+unroll4") != NULL);
#if defined(_MSC_VER) && defined(_M_X64)
          CHECK(strstr(report->results[index].detail, "candidateAvailable=yes") != NULL);
#else
          CHECK(strstr(report->results[index].detail, "candidateAvailable=no") != NULL);
#endif
          CHECK(strstr(report->results[index].detail, "featureGate=mul-policy-toom3-u4-rec-ge16384-leaf64-depth2") != NULL);
          CHECK(strstr(report->results[index].detail, "gmpClue=toom33-recursive") != NULL);
        } else {
          CHECK(0);
        }
      } else {
        CHECK(0);
      }
      if (strcmp(report->results[index].adoption, "promotion-ready") == 0) {
        CHECK(report->results[index].stable_sample_count == report->results[index].sample_count);
        CHECK(report->results[index].speed_ratio <= report->results[index].max_allowed_speed_ratio);
        CHECK(report->results[index].worst_pair_ratio <= 1.0);
      }
      CHECK(strstr(report->results[index].adoption, "promotion-ready") != NULL ||
        strstr(report->results[index].adoption, "observe-only") != NULL ||
        strstr(report->results[index].adoption, "blocked-output-mismatch") != NULL);
    }
  }
  XrayBenchmarkResult mismatch;
  memset(&mismatch, 0, sizeof(mismatch));
  CHECK(strcmp(xray_scratch_adoption_for_result(&mismatch), "blocked-output-mismatch") == 0);
  XrayBenchmarkResult unstable;
  memset(&unstable, 0, sizeof(unstable));
  unstable.parity_verified = 1;
  unstable.speed_ratio = 0.90;
  unstable.max_allowed_speed_ratio = 1.0;
  unstable.sample_count = 5;
  unstable.stable_sample_count = 3;
  unstable.worst_pair_ratio = 0.95;
  CHECK(strcmp(xray_scratch_adoption_for_result(&unstable), "oracle-only") == 0);
  unstable.stable_sample_count = 4;
  CHECK(strcmp(xray_scratch_adoption_for_result(&unstable), "allowed") == 0);
  unstable.worst_pair_ratio = 1.01;
  CHECK(strcmp(xray_scratch_adoption_for_result(&unstable), "oracle-only") == 0);
  unstable.worst_pair_ratio = 0.95;
  unstable.speed_ratio = 1.01;
  unstable.stable_sample_count = 5;
  CHECK(strcmp(xray_scratch_adoption_for_result(&unstable), "oracle-only") == 0);
  CHECK(scratch_rows >= 40);
  CHECK(saw_8192_scratch);
  CHECK(saw_16384_scratch_mul);
  CHECK(saw_frontier_scout);
  CHECK(saw_frontier_mul32768);
  CHECK(saw_frontier_mul65536);
  CHECK(saw_frontier_square32768);
  CHECK(saw_frontier_square65536);
  CHECK(saw_scratch_square);
  CHECK(saw_scratch_format);
  CHECK(saw_scratch_format768);
  CHECK(saw_scratch_format896);
  CHECK(saw_8192_kernel_probe);
  CHECK(saw_16384_kernel_probe);
  CHECK(saw_square_vs_mul_probe);
  CHECK(saw_format_threshold_probe);
  CHECK(saw_format_threshold8_probe);
  CHECK(saw_format_threshold16_probe);
  CHECK(saw_format_threshold24_probe);
  CHECK(saw_format_threshold32_probe);
  CHECK(saw_format_threshold40_probe);
  CHECK(saw_format_threshold44_probe);
  CHECK(saw_format_threshold48_probe);
  CHECK(saw_format_threshold52_probe);
  CHECK(saw_format_threshold56_probe);
  CHECK(saw_format_threshold64_probe);
  CHECK(saw_format_threshold80_probe);
  CHECK(saw_format_threshold96_probe);
  CHECK(saw_format_threshold128_probe);
  CHECK(saw_format_threshold160_probe);
  CHECK(saw_format_threshold192_probe);
  CHECK(saw_format_threshold256limb_probe);
  CHECK(saw_format_threshold256_probe);
  CHECK(saw_format_threshold512_probe);
  CHECK(saw_format_threshold1000_probe);
  CHECK(saw_format_threshold2048_probe);
  CHECK(saw_format_threshold4096_probe);
  CHECK(saw_format_threshold8192_probe);
  CHECK(saw_format_threshold16384_probe);
  CHECK(saw_format_divider_probe);
  CHECK(saw_format_divider1000_probe);
  CHECK(saw_format_divider4096_probe);
  CHECK(saw_format_divider8192_probe);
  CHECK(saw_format_folded_probe);
  CHECK(saw_format_folded1000_probe);
  CHECK(saw_format_folded4096_probe);
  CHECK(saw_format_folded8192_probe);
  CHECK(saw_format_pair_writer_probe);
  CHECK(saw_format_pair_writer_current_probe);
  CHECK(saw_format_pair_writer_folded_probe);
  CHECK(saw_format_pair_writer40_probe);
  CHECK(saw_format_pair_writer150_probe);
  CHECK(saw_format_pair_writer256_probe);
  CHECK(saw_format_pair_writer512_probe);
  CHECK(saw_format_pair_writer1000_probe);
  CHECK(saw_format_pair_writer2048_probe);
  CHECK(saw_format_pair_writer4096_probe);
  CHECK(saw_format_pair_writer8192_probe);
  CHECK(saw_format_pair_writer16384_probe);
  CHECK(saw_format_wide_probe);
  CHECK(saw_format_wide1000_probe);
  CHECK(saw_format_wide4096_probe);
  CHECK(saw_format_wide8192_probe);
  CHECK(saw_format_mixed_writer_probe);
  CHECK(saw_format_folded_hwdiv_probe);
  CHECK(saw_format_hwdiv_mixed_probe);
  CHECK(saw_format_divide_1e19_probe);
  CHECK(saw_format_divide_1e19_pairs_probe);
  CHECK(saw_format_divide_1e19_preinv_probe);
  CHECK(saw_format_divide_1e19_preinv_pairs_probe);
  CHECK(saw_format_dc_probe);
  CHECK(saw_format_dc_leaf8_probe);
  CHECK(saw_format_dc_leaf16_probe);
  CHECK(saw_format_dc_leaf32_probe);
  CHECK(saw_format_dc_leaf64_probe);
  CHECK(saw_format_dc_ladder_probe);
  CHECK(saw_format_dc_ladder_leaf8_probe);
  CHECK(saw_format_dc_ladder_leaf16_probe);
  CHECK(saw_format_dc_ladder_leaf32_probe);
  CHECK(saw_format_dc_ladder_leaf64_probe);
  CHECK(saw_format_dc_direct_probe);
  CHECK(saw_format_dc_direct_leaf8_probe);
  CHECK(saw_format_dc_direct_leaf16_probe);
  CHECK(saw_format_dc_direct_leaf32_probe);
  CHECK(saw_format_dc_direct_leaf64_probe);
  CHECK(saw_format_dc_static_ladder_probe);
  CHECK(saw_format_dc_static_ladder_leaf8_probe);
  CHECK(saw_format_dc_static_ladder_leaf16_probe);
  CHECK(saw_format_dc_static_ladder_leaf32_probe);
  CHECK(saw_format_dc_static_ladder_leaf64_probe);
  CHECK(saw_format_dc_static_direct_probe);
  CHECK(saw_format_dc_static_direct_leaf8_probe);
  CHECK(saw_format_dc_static_direct_leaf16_probe);
  CHECK(saw_format_dc_static_direct_leaf32_probe);
  CHECK(saw_format_dc_static_direct_leaf64_probe);
  CHECK(saw_format_dc_workspace_probe);
  CHECK(saw_format_dc_workspace_leaf8_probe);
  CHECK(saw_format_dc_workspace_leaf16_probe);
  CHECK(saw_format_dc_preinv_qhat_probe);
  CHECK(saw_format_dc_preinv_qhat_leaf8_probe);
  CHECK(saw_format_dc_preinv_qhat_leaf16_probe);
  CHECK(saw_format_dc_route_probe);
  CHECK(saw_format_dc_route1000_probe);
  CHECK(saw_format_dc_route4096_probe);
  CHECK(saw_format_dc_route8192_probe);
  CHECK(saw_format_dc_route16384_probe);
  CHECK(saw_format_dc_route_safety_gate);
  CHECK(saw_format_route_tournament_probe);
  CHECK(saw_format_route_tournament768_probe);
  CHECK(saw_format_route_tournament896_probe);
  CHECK(saw_format_route_tournament1000_probe);
  CHECK(saw_format_route_tournament4096_probe);
  CHECK(saw_format_route_tournament8192_probe);
  CHECK(saw_format_route_tournament_detail);
  CHECK(saw_format_route_tournament_detail_current);
  CHECK(saw_format_route_tournament_detail_preinv);
  CHECK(format_route_tournament_detail_rows >= 30);
  CHECK(saw_policy_probe);
  CHECK(saw_format_policy_current);
  CHECK(saw_format_policy_direct4096);
  CHECK(saw_format_policy_direct8192);
  CHECK(saw_format_policy_static4096);
  CHECK(saw_format_policy_static8192);
  CHECK(saw_format_policy_workspace4096);
  CHECK(saw_format_policy_preinv4096);
  CHECK(saw_format_policy_preinv8192);
  CHECK(saw_format_policy_preinv16384);
  CHECK(saw_format_policy_preinv10e19_window);
  CHECK(saw_format_policy_preinv10e19_pairs_window);
  CHECK(saw_format_policy_preinv10e19_window768_896);
  CHECK(saw_format_policy_preinv10e19_pairs_window768_896);
  CHECK(saw_format_policy_preinv10e19_window768_960);
  CHECK(saw_format_policy_preinv10e19_pairs_window768_960);
  CHECK(saw_format_policy_preinv10e19_window896_1000);
  CHECK(saw_format_policy_preinv10e19_pairs_window896_1000);
  CHECK(saw_policy_gate);
  CHECK(saw_format_policy_gate_direct4096);
  CHECK(saw_format_policy_gate_direct8192);
  CHECK(saw_format_policy_gate_static4096);
  CHECK(saw_format_policy_gate_static8192);
  CHECK(saw_format_policy_gate_workspace4096);
  CHECK(saw_format_policy_gate_preinv4096);
  CHECK(saw_format_policy_gate_preinv8192);
  CHECK(saw_format_policy_gate_preinv16384);
  CHECK(saw_format_policy_gate_preinv10e19_window);
  CHECK(saw_format_policy_gate_preinv10e19_pairs_window);
  CHECK(saw_format_policy_gate_preinv10e19_window768_896);
  CHECK(saw_format_policy_gate_preinv10e19_pairs_window768_896);
  CHECK(saw_format_policy_gate_preinv10e19_window768_960);
  CHECK(saw_format_policy_gate_preinv10e19_pairs_window768_960);
  CHECK(saw_format_policy_gate_preinv10e19_window896_1000);
  CHECK(saw_format_policy_gate_preinv10e19_pairs_window896_1000);
  CHECK(saw_format_policy_deep_gate_preinv10e19_window768_1000);
  CHECK(saw_format_policy_deep_gate_preinv10e19_window768_896);
  CHECK(saw_format_policy_deep_gate_preinv10e19_window768_960);
  CHECK(saw_format_policy_deep_gate_preinv10e19_window896_1000);
  CHECK(saw_format_policy_deep_gate_preinv10e19_pairs_window768_896);
  CHECK(saw_format_policy_deep_gate_preinv10e19_pairs_window768_960);
  CHECK(saw_format_policy_deep_gate_preinv10e19_pairs_window896_1000);
  CHECK(saw_format_policy_route_audit);
  CHECK(saw_format_policy_route_audit_preinv10e19);
  CHECK(saw_format_policy_route_audit_preinv10e19_pairs);
  CHECK(saw_divmod_preinv_qhat_safety_gate);
  CHECK(saw_mul_policy_safety_threshold96_gate);
  CHECK(saw_mul_policy_safety_toom_leaf48_gate);
  CHECK(saw_mul_policy_safety_toom_rec_gate);
  CHECK(saw_format_policy768_probe);
  CHECK(saw_format_policy896_probe);
  CHECK(saw_format_policy960_probe);
  CHECK(saw_format_policy1000_probe);
  CHECK(saw_format_policy4096_probe);
  CHECK(saw_format_policy8192_probe);
  CHECK(saw_format_policy16384_probe);
  CHECK(saw_square_policy_probe);
  CHECK(saw_square_policy_current);
  CHECK(saw_square_policy_thr96);
  CHECK(saw_square_policy1000_probe);
  CHECK(saw_square_policy4096_probe);
  CHECK(saw_square_policy8192_probe);
  CHECK(saw_square_policy16384_probe);
  CHECK(saw_mul_policy_probe);
  CHECK(saw_mul_policy_current);
  CHECK(saw_mul_policy_threshold96);
  CHECK(saw_mul_policy_toom_leaf48);
  CHECK(saw_mul_policy_toom_rec);
  CHECK(saw_mul_policy1000_probe);
  CHECK(saw_mul_policy4096_probe);
  CHECK(saw_mul_policy8192_probe);
  CHECK(saw_mul_policy16384_probe);
  CHECK(saw_mul_threshold_tournament_probe);
  CHECK(saw_mul_threshold_tournament512_probe);
  CHECK(saw_mul_threshold_tournament1000_probe);
  CHECK(saw_mul_threshold_tournament2048_probe);
  CHECK(saw_mul_threshold_tournament4096_probe);
  CHECK(saw_mul_threshold_tournament8192_probe);
  CHECK(saw_mul_threshold_tournament16384_probe);
  CHECK(saw_karatsuba_middle_probe);
  CHECK(saw_karatsuba_middle64_probe);
  CHECK(saw_karatsuba_middle96_probe);
  CHECK(saw_karatsuba_middle128_probe);
  CHECK(saw_karatsuba_middle1000_probe);
  CHECK(saw_karatsuba_middle4096_probe);
  CHECK(saw_karatsuba_middle8192_probe);
  CHECK(saw_karatsuba_middle16384_probe);
  CHECK(saw_format_strategy1000_probe);
  CHECK(saw_format_strategy4096_probe);
  CHECK(saw_format_strategy8192_probe);
  CHECK(saw_format_strategy16384_probe);
  CHECK(saw_square_karatsuba_vs_mul_probe);
  CHECK(saw_square_karatsuba_vs_gmp_probe);
  CHECK(saw_square_leaf_order_probe);
  CHECK(saw_square_leaf_order1000_probe);
  CHECK(saw_square_leaf_order4096_probe);
  CHECK(saw_square_leaf_order8192_probe);
  CHECK(saw_square_leaf_order16384_probe);
  CHECK(saw_toom3_probe);
  CHECK(saw_toom3_vs_scratch_probe);
  CHECK(saw_qhat_preinv_probe);
  CHECK(saw_qhat_u32_limb_probe);
  CHECK(saw_u32_precompute_probe);
  CHECK(saw_mod_u32_precompute_probe);
  CHECK(saw_gcd_u32_precompute_probe);
  CHECK(saw_powmod_u32_precompute_probe);
  CHECK(saw_u32_precompute40_probe);
  CHECK(saw_u32_precompute150_probe);
  CHECK(saw_u32_precompute512_probe);
  CHECK(saw_u32_precompute1000_probe);
  CHECK(saw_u32_precompute2048_probe);
  CHECK(saw_u32_precompute4096_probe);
  CHECK(saw_u32_precompute8192_probe);
  CHECK(saw_u32_precompute16384_probe);
  CHECK(saw_parse_chunk_probe);
  CHECK(saw_parse_chunk8_probe);
  CHECK(saw_parse_chunk9_probe);
  CHECK(saw_parse_chunk10_probe);
  CHECK(saw_parse_chunk15_probe);
  CHECK(saw_parse_chunk18_probe);
  CHECK(saw_parse_chunk19_probe);
  CHECK(saw_parse_chunk40_probe);
  CHECK(saw_parse_chunk150_probe);
  CHECK(saw_parse_chunk512_probe);
  CHECK(saw_parse_chunk1000_probe);
  CHECK(saw_parse_chunk2048_probe);
  CHECK(saw_parse_chunk4096_probe);
  CHECK(saw_parse_chunk8192_probe);
  CHECK(saw_parse_chunk16384_probe);
  CHECK(saw_divmod_dc_power_probe);
  CHECK(saw_divmod_dc_power4096_probe);
  CHECK(saw_divmod_dc_power8192_probe);
  CHECK(saw_divmod_dc_power16384_probe);
  CHECK(saw_divmod_precomputed_probe);
  CHECK(saw_divmod_precomputed4096_probe);
  CHECK(saw_divmod_precomputed8192_probe);
  CHECK(saw_divmod_precomputed16384_probe);
  CHECK(saw_divmod_workspace_probe);
  CHECK(saw_divmod_workspace4096_probe);
  CHECK(saw_divmod_workspace8192_probe);
  CHECK(saw_divmod_workspace16384_probe);
  CHECK(saw_divmod_preinv_qhat_probe);
  CHECK(saw_divmod_preinv_qhat4096_probe);
  CHECK(saw_divmod_preinv_qhat8192_probe);
  CHECK(saw_divmod_preinv_qhat16384_probe);
#if defined(_MSC_VER) && defined(_M_X64)
  CHECK(saw_toom3_unroll4_vs_scratch_probe);
  CHECK(saw_toom3_unroll4_vs_gmp_probe);
  CHECK(saw_toom3_unroll4_deep_vs_gmp_probe);
  CHECK(saw_toom3_unroll4_deep_leaf64_probe);
  CHECK(saw_toom3_unroll4_deep_leaf96_probe);
  CHECK(saw_toom3_unroll4_recursive_vs_gmp_probe);
  CHECK(saw_toom3_unroll4_recursive_deep_vs_gmp_probe);
  CHECK(saw_toom3_unroll4_recursive_deep_leaf64_probe);
  CHECK(saw_toom3_unroll4_recursive_deep_leaf96_probe);
  CHECK(saw_muladd_unroll_probe);
  CHECK(saw_muladd_unroll8_probe);
  CHECK(saw_mul_unroll4_vs_scratch_probe);
  CHECK(saw_mul_unroll4_vs_gmp_probe);
  CHECK(saw_mul_unroll4_deep_vs_gmp_probe);
  if (report->cpu.bmi2 && report->cpu.adx) CHECK(saw_muladd_bmi2_adx_probe);
#endif
  CHECK(kernel_rows >= 4);
  CHECK(report->scratch_count == scratch_rows);
  CHECK(report->replacement_ready_count == replacement_ready_rows);
  CHECK(report->oracle_only_count == oracle_only_rows);
  CHECK(report->blocked_count == blocked_rows);
  CHECK(report->scratch_count == report->replacement_ready_count + report->oracle_only_count + report->blocked_count);
  CHECK(report->lanes.promotion_ready_count == lane_promotion_ready_rows);
  CHECK(report->lanes.oracle_only_count == lane_oracle_only_rows);
  CHECK(report->lanes.safety_rejected_count == lane_safety_rejected_rows);
  CHECK(report->lanes.promotion_ready_count >= report->replacement_ready_count);
  CHECK(report->lanes.oracle_only_count >= report->oracle_only_count);
  if (report->lanes.promotion_ready_count) CHECK(strstr(report->lanes.promotion_ready_detail, "d=") != NULL);
  if (report->lanes.oracle_only_count) CHECK(strstr(report->lanes.oracle_only_detail, "r=") != NULL);
  if (report->lanes.safety_rejected_count) CHECK(strstr(report->lanes.safety_rejected_detail, "s=") != NULL);
  char *json = xray_benchmark_report_json(report);
  CHECK(json != NULL);
  CHECK(strstr(json, "\"lanes\"") != NULL);
  CHECK(strstr(json, "\"promotionReady\"") != NULL);
  CHECK(strstr(json, "\"safetyRejected\"") != NULL);
  CHECK(strstr(json, "\"measurableStatus\"") != NULL);
  CHECK(strstr(json, "\"betterNow\"") != NULL);
  CHECK(strstr(json, "\"stillWorking\"") != NULL);
  CHECK(strstr(json, "\"speedup\"") != NULL);
  CHECK(strstr(json, "\"slowdown\"") != NULL);
  CHECK(strstr(json, "\"backendUs\"") != NULL);
  CHECK(strstr(json, "\"comparison\":\"faster\"") != NULL);
  CHECK(strstr(json, "\"comparison\":\"slower\"") != NULL);
  CHECK(strstr(json, "\"replacementReady\"") != NULL);
  CHECK(strstr(json, "\"stableSampleCount\"") != NULL);
  CHECK(strstr(json, "\"sampleCount\"") != NULL);
  CHECK(strstr(json, "\"worstPairRatio\"") != NULL);
  CHECK(strstr(json, "\"baselineBackend\"") != NULL);
  CHECK(strstr(json, "\"baselineBackendVersion\"") != NULL);
  CHECK(strstr(json, "\"baselineBackendLibrary\"") != NULL);
  CHECK(strstr(json, "\"build\"") != NULL);
  CHECK(strstr(json, "\"compiler\"") != NULL);
  CHECK(strstr(json, "\"compilerVersion\"") != NULL);
  CHECK(strstr(json, "\"buildConfig\"") != NULL);
  CHECK(strstr(json, "\"interproceduralOptimization\"") != NULL);
  CHECK(strstr(json, "\"compileTargetAvx2\"") != NULL);
  CHECK(strstr(json, "\"scratchRouteConfig\"") != NULL);
  CHECK(strstr(json, "\"karatsubaThresholdLimbs\"") != NULL);
  CHECK(strstr(json, "\"squareTinySelfMulPolicy\"") != NULL);
  CHECK(strstr(json, "\"decimalHornerMinLimbs\"") != NULL);
  CHECK(strstr(json, "\"decimalWideChunkDigits\"") != NULL);
  CHECK(strstr(json, "\"decimalDcMinWideChunks\"") != NULL);
  CHECK(strstr(json, "\"decimalPairWriterPolicy\"") != NULL);
  CHECK(strstr(json, "\"sparseMulMinProducts\"") != NULL);
  CHECK(strstr(json, "\"productionRoutes\"") != NULL);
  CHECK(strstr(json, "\"diagnosticProbeFamilies\"") != NULL);
  CHECK(strstr(json, "\"mulUnroll4RouteEnabled\"") != NULL);
  CHECK(strstr(json, "\"cpu\"") != NULL);
  CHECK(strstr(json, "kernel-probe") != NULL);
  CHECK(strstr(json, "policy-probe") != NULL);
  CHECK(strstr(json, "policy-gate") != NULL);
  CHECK(strstr(json, "format-policy-safety") != NULL);
  CHECK(strstr(json, "format-policy-deep-safety") != NULL);
  CHECK(strstr(json, "deepConfirmation=required") != NULL);
  CHECK(strstr(json, "deepConfirmation=not-required") != NULL);
  CHECK(strstr(json, "format-policy-route-audit") != NULL);
  CHECK(strstr(json, "decimal-format-window-promotion-audit") != NULL);
  CHECK(strstr(json, "audit-preinv10e19-pairs-window768-1000") != NULL);
  CHECK(strstr(json, "candCurrentMax=") != NULL);
  CHECK(strstr(json, "\"avx\"") != NULL);
  CHECK(strstr(json, "\"avx2\"") != NULL);
  CHECK(strstr(json, "\"scratchRows\"") != NULL);
  CHECK(strstr(json, "\"replacementReadyRows\"") != NULL);
  CHECK(strstr(json, "\"oracleOnlyRows\"") != NULL);
  CHECK(strstr(json, "\"blockedRows\"") != NULL);
  CHECK(strstr(json, "\"adoption\"") != NULL);
  CHECK(strstr(json, "\"maxAllowedSpeedRatio\"") != NULL);
  CHECK(strstr(json, "\"scratchUs\"") != NULL);
  CHECK(strstr(json, "mul-toom3") != NULL);
  CHECK(strstr(json, "divmod-dc-power") != NULL);
  CHECK(strstr(json, "divmod-precomputed") != NULL);
  CHECK(strstr(json, "bigint-division-context") != NULL);
  CHECK(strstr(json, "setupPolicy=reported-not-scored") != NULL);
  CHECK(strstr(json, "cacheRole=divisor-context") != NULL);
  CHECK(strstr(json, "divmod-workspace") != NULL);
  CHECK(strstr(json, "bigint-division-workspace") != NULL);
  CHECK(strstr(json, "divmod-preinv-qhat") != NULL);
  CHECK(strstr(json, "divmod-preinv-qhat-safety") != NULL);
  CHECK(strstr(json, "bigint-division-preinv-qhat") != NULL);
  CHECK(strstr(json, "qhat-u32-limb") != NULL);
  CHECK(strstr(json, "qhat-preinv") != NULL);
  CHECK(strstr(json, "preinverted-limb-qhat") != NULL);
  CHECK(strstr(json, "division-qhat-estimator") != NULL);
  CHECK(strstr(json, "mul-threshold-tournament") != NULL);
  CHECK(strstr(json, "root-size-threshold-tournament") != NULL);
  CHECK(strstr(json, "candidateThresholds=32,48,64,80,96,128,160") != NULL);
  CHECK(strstr(json, "format-route-tournament") != NULL);
  CHECK(strstr(json, "format-route-tournament-detail") != NULL);
  CHECK(strstr(json, "decimal-format-route-policy-tournament") != NULL);
  CHECK(strstr(json, "controlSafety=tournament-detail") != NULL);
  CHECK(strstr(json, "routeCurrentRatio=") != NULL);
  CHECK(strstr(json, "routeGmpRatio=") != NULL);
  CHECK(strstr(json, "sameRunTournament=yes") != NULL);
  CHECK(strstr(json, "mfast-factor64pre-precompute") != NULL);
  CHECK(strstr(json, "mfast-cuda-tournament-detail") != NULL);
  CHECK(strstr(json, "mul-karatsuba-middle") != NULL);
  CHECK(strstr(json, "karatsuba-sum-middle") != NULL);
  CHECK(strstr(json, "karatsuba-difference-middle") != NULL);
  CHECK(strstr(json, "mod-u32-precompute") != NULL);
  CHECK(strstr(json, "gcd-u32-precompute") != NULL);
  CHECK(strstr(json, "powmod-u32-precompute") != NULL);
  CHECK(strstr(json, "parse-chunk") != NULL);
  CHECK(strstr(json, "\"operation\":\"format\"") != NULL);
  CHECK(strstr(json, "format-threshold") != NULL);
  CHECK(strstr(json, "format-divider") != NULL);
  CHECK(strstr(json, "format-folded") != NULL);
  CHECK(strstr(json, "format-pair-writer") != NULL);
  CHECK(strstr(json, "format-wide") != NULL);
  CHECK(strstr(json, "format-mixed-writer") != NULL);
  CHECK(strstr(json, "format-folded-hwdiv") != NULL);
  CHECK(strstr(json, "format-hwdiv-mixed") != NULL);
  CHECK(strstr(json, "format-divide-1e19") != NULL);
  CHECK(strstr(json, "format-divide-1e19-pairs") != NULL);
  CHECK(strstr(json, "decimal-divide-1e19-pair-writer") != NULL);
  CHECK(strstr(json, "decimal-format-divide-1e19-pairs") != NULL);
  CHECK(strstr(json, "format-divide-1e19-preinv") != NULL);
  CHECK(strstr(json, "format-divide-1e19-preinv-pairs") != NULL);
  CHECK(strstr(json, "decimal-divide-1e19-preinv") != NULL);
  CHECK(strstr(json, "decimal-divide-1e19-preinv-pair-writer") != NULL);
  CHECK(strstr(json, "decimal-format-divide-1e19-preinv") != NULL);
  CHECK(strstr(json, "decimal-format-divide-1e19-preinv-pairs") != NULL);
  CHECK(strstr(json, "format-dc") != NULL);
  CHECK(strstr(json, "format-dc-ladder") != NULL);
  CHECK(strstr(json, "format-dc-direct") != NULL);
  CHECK(strstr(json, "format-dc-workspace") != NULL);
  CHECK(strstr(json, "format-dc-preinv-qhat") != NULL);
  CHECK(strstr(json, "format-dc-route") != NULL);
  CHECK(strstr(json, "format-dc-route-safety") != NULL);
  CHECK(strstr(json, "format-dc-static-ladder") != NULL);
  CHECK(strstr(json, "format-dc-static-direct") != NULL);
  CHECK(strstr(json, "direct16-vs-ladder8") != NULL);
  CHECK(strstr(json, "hashGate=matched") != NULL);
  CHECK(strstr(json, "hashSafe=") != NULL);
  CHECK(strstr(json, "mfast615fe9e-hashgate") != NULL);
  CHECK(strstr(json, "decimal-dc-direct-workspace") != NULL);
  CHECK(strstr(json, "decimal-dc-direct-preinv-qhat") != NULL);
  CHECK(strstr(json, "dc-static-pow2") != NULL);
  CHECK(strstr(json, "dc-static-direct") != NULL);
  CHECK(strstr(json, "format-policy") != NULL);
  CHECK(strstr(json, "direct-ge4096-leaf8") != NULL);
  CHECK(strstr(json, "direct-ge8192-leaf16") != NULL);
  CHECK(strstr(json, "static-ge4096-l16") != NULL);
  CHECK(strstr(json, "static-ge8192-l8") != NULL);
  CHECK(strstr(json, "workspace-ge4096-leaf16") != NULL);
  CHECK(strstr(json, "preinv-ge4096-leaf8") != NULL);
  CHECK(strstr(json, "preinv-ge8192-leaf16") != NULL);
  CHECK(strstr(json, "preinv-ge16384-leaf16") != NULL);
  CHECK(strstr(json, "preinv10e19-window768-1000") != NULL);
  CHECK(strstr(json, "preinv10e19-pairs-window768-1000") != NULL);
  CHECK(strstr(json, "deep-preinv10e19-window768-1000") != NULL);
  CHECK(strstr(json, "deep-preinv10e19-window768-896") != NULL);
  CHECK(strstr(json, "deep-preinv10e19-window768-960") != NULL);
  CHECK(strstr(json, "deep-preinv10e19-window896-1000") != NULL);
  CHECK(strstr(json, "deep-preinv10e19-pairs-window768-896") != NULL);
  CHECK(strstr(json, "deep-preinv10e19-pairs-window768-960") != NULL);
  CHECK(strstr(json, "deep-preinv10e19-pairs-window896-1000") != NULL);
  CHECK(strstr(json, "decimal-format-policy-workspace") != NULL);
  CHECK(strstr(json, "decimal-format-policy-preinv-qhat") != NULL);
  CHECK(strstr(json, "decimal-format-policy-divide-1e19-preinv") != NULL);
  CHECK(strstr(json, "square-leaf-order") != NULL);
  CHECK(strstr(json, "fused-diagonal-cross-leaf") != NULL);
  CHECK(strstr(json, "mfastfermat-wide61-dif-dit-bit-reversal-elision") != NULL);
  CHECK(strstr(json, "decimal-format-policy-divide-1e19-preinv-pairs") != NULL);
  CHECK(strstr(json, "square-policy") != NULL);
  CHECK(strstr(json, "karatsuba-thr96") != NULL);
  CHECK(strstr(json, "mul-policy") != NULL);
  CHECK(strstr(json, "mul-policy-safety") != NULL);
  CHECK(strstr(json, "frontier-scout") != NULL);
  CHECK(strstr(json, "mul-frontier") != NULL);
  CHECK(strstr(json, "square-frontier") != NULL);
  CHECK(strstr(json, "mfast8m-difdit24-complete") != NULL);
  CHECK(strstr(json, "duplicateControl=default") != NULL);
  CHECK(strstr(json, "controlPlacement=tail") != NULL);
  CHECK(strstr(json, "controlSafety=") != NULL);
  CHECK(strstr(json, "controlRatio=") != NULL);
  CHECK(strstr(json, "mfastKnob=difdit24k") != NULL);
  CHECK(strstr(json, "mfastPG=1.612") != NULL);
  CHECK(strstr(json, "mfastCW=1.078") != NULL);
  CHECK(strstr(json, "mfastPocket=difdit98304") != NULL);
  CHECK(strstr(json, "mfastPocketFloor=98304") != NULL);
  CHECK(strstr(json, "mfastPocket8192PG=1.320") != NULL);
  CHECK(strstr(json, "mfastPocketGate=noisy-control") != NULL);
  CHECK(strstr(json, "mfast1mGate=noisy") != NULL);
  CHECK(strstr(json, "mfast1mBest=difdit16000PG1.480") != NULL);
  CHECK(strstr(json, "mfast16mBest=difdit24k") != NULL);
  CHECK(strstr(json, "mfast16mD98304=0.404") != NULL);
  CHECK(strstr(json, "threshold96-ge8192") != NULL);
  CHECK(strstr(json, "karatsuba-threshold96") != NULL);
  CHECK(strstr(json, "toom3-u4-ge8192-leaf48") != NULL);
  CHECK(strstr(json, "toom3-u4-rec-ge16384-leaf64-depth2") != NULL);
  CHECK(strstr(json, "\"operation\":\"square\"") != NULL);
  CHECK(strstr(json, "square-vs-mul") != NULL);
  CHECK(strstr(json, "square-karatsuba-vs-mul") != NULL);
  CHECK(strstr(json, "square-karatsuba-vs-gmp") != NULL);
  CHECK(strstr(json, "mul-toom3-vs-scratch") != NULL);
#if defined(_MSC_VER) && defined(_M_X64)
  CHECK(strstr(json, "mul-toom3-unroll4-vs-scratch") != NULL);
  CHECK(strstr(json, "mul-toom3-unroll4-vs-gmp") != NULL);
  CHECK(strstr(json, "mul-toom3-unroll4-deep-vs-gmp") != NULL);
  CHECK(strstr(json, "mul-toom3-u4-rec-vs-gmp") != NULL);
  CHECK(strstr(json, "mul-toom3-u4-rec-deep-vs-gmp") != NULL);
  CHECK(strstr(json, "muladd-unroll4") != NULL);
  CHECK(strstr(json, "muladd-unroll8") != NULL);
  CHECK(strstr(json, "mul-unroll4-vs-scratch") != NULL);
  CHECK(strstr(json, "mul-unroll4-vs-gmp") != NULL);
  CHECK(strstr(json, "mul-unroll4-deep-vs-gmp") != NULL);
  if (report->cpu.bmi2 && report->cpu.adx) CHECK(strstr(json, "muladd-bmi2-adx") != NULL);
#endif
  free(json);
  char *tsv = xray_benchmark_report_tsv(report);
  CHECK(tsv != NULL);
  CHECK(strstr(tsv, "category\tname\toperation") != NULL);
  CHECK(strstr(tsv, "buildConfig\tipo\tcompiler\tcompilerVersion") != NULL);
  CHECK(strstr(tsv, "factor-benchmark") != NULL);
  CHECK(strstr(tsv, "cyclotomic-benchmark") != NULL);
  CHECK(strstr(tsv, "scratch-vs-gmp") != NULL);
  CHECK(strstr(tsv, "kernel-probe") != NULL);
  CHECK(strstr(tsv, "policy-probe") != NULL);
  CHECK(strstr(tsv, "policy-gate") != NULL);
  CHECK(strstr(tsv, "format-policy-deep-safety") != NULL);
  CHECK(strstr(tsv, "deepConfirmation=required") != NULL);
  CHECK(strstr(tsv, "deepConfirmation=not-required") != NULL);
  CHECK(strstr(tsv, "format-policy-route-audit") != NULL);
  CHECK(strstr(tsv, "decimal-format-window-promotion-audit") != NULL);
  CHECK(strstr(tsv, "audit-preinv10e19-pairs-window768-1000") != NULL);
  CHECK(strstr(tsv, "candCurrentMax=") != NULL);
  CHECK(strstr(tsv, "format-route-tournament-detail") != NULL);
  CHECK(strstr(tsv, "controlSafety=tournament-detail") != NULL);
  CHECK(strstr(tsv, "routeCurrentRatio=") != NULL);
  CHECK(strstr(tsv, "routeGmpRatio=") != NULL);
  CHECK(strstr(tsv, "mfast-cuda-tournament-detail") != NULL);
  CHECK(strstr(tsv, "gmpClue=") != NULL);
  CHECK(strstr(tsv, "mul-toom3") != NULL);
  CHECK(strstr(tsv, "mod-u32-precompute") != NULL);
  CHECK(strstr(tsv, "gcd-u32-precompute") != NULL);
  CHECK(strstr(tsv, "powmod-u32-precompute") != NULL);
  CHECK(strstr(tsv, "parse-chunk") != NULL);
  CHECK(strstr(tsv, "divmod-dc-power") != NULL);
  CHECK(strstr(tsv, "setupPolicy=reported-not-scored") != NULL);
  CHECK(strstr(tsv, "cacheRole=divisor-context") != NULL);
  CHECK(strstr(tsv, "format") != NULL);
  CHECK(strstr(tsv, "format-threshold") != NULL);
  CHECK(strstr(tsv, "format-divider") != NULL);
  CHECK(strstr(tsv, "format-folded") != NULL);
  CHECK(strstr(tsv, "format-pair-writer") != NULL);
  CHECK(strstr(tsv, "format-wide") != NULL);
  CHECK(strstr(tsv, "format-mixed-writer") != NULL);
  CHECK(strstr(tsv, "format-folded-hwdiv") != NULL);
  CHECK(strstr(tsv, "format-hwdiv-mixed") != NULL);
  CHECK(strstr(tsv, "format-divide-1e19") != NULL);
  CHECK(strstr(tsv, "format-divide-1e19-pairs") != NULL);
  CHECK(strstr(tsv, "decimal-divide-1e19-pair-writer") != NULL);
  CHECK(strstr(tsv, "decimal-format-divide-1e19-pairs") != NULL);
  CHECK(strstr(tsv, "format-divide-1e19-preinv") != NULL);
  CHECK(strstr(tsv, "format-divide-1e19-preinv-pairs") != NULL);
  CHECK(strstr(tsv, "decimal-divide-1e19-preinv") != NULL);
  CHECK(strstr(tsv, "decimal-divide-1e19-preinv-pair-writer") != NULL);
  CHECK(strstr(tsv, "decimal-format-divide-1e19-preinv") != NULL);
  CHECK(strstr(tsv, "decimal-format-divide-1e19-preinv-pairs") != NULL);
  CHECK(strstr(tsv, "format-dc") != NULL);
  CHECK(strstr(tsv, "format-dc-ladder") != NULL);
  CHECK(strstr(tsv, "format-dc-direct") != NULL);
  CHECK(strstr(tsv, "format-dc-workspace") != NULL);
  CHECK(strstr(tsv, "format-dc-preinv-qhat") != NULL);
  CHECK(strstr(tsv, "format-dc-route") != NULL);
  CHECK(strstr(tsv, "format-dc-route-safety") != NULL);
  CHECK(strstr(tsv, "format-dc-static-ladder") != NULL);
  CHECK(strstr(tsv, "format-dc-static-direct") != NULL);
  CHECK(strstr(tsv, "direct16-vs-ladder8") != NULL);
  CHECK(strstr(tsv, "hashGate=matched") != NULL);
  CHECK(strstr(tsv, "hashSafe=") != NULL);
  CHECK(strstr(tsv, "mfast615fe9e-hashgate") != NULL);
  CHECK(strstr(tsv, "decimal-dc-direct-workspace") != NULL);
  CHECK(strstr(tsv, "decimal-dc-direct-preinv-qhat") != NULL);
  CHECK(strstr(tsv, "dc-static-pow2") != NULL);
  CHECK(strstr(tsv, "dc-static-direct") != NULL);
  CHECK(strstr(tsv, "format-policy") != NULL);
  CHECK(strstr(tsv, "format-policy-safety") != NULL);
  CHECK(strstr(tsv, "format-route-tournament") != NULL);
  CHECK(strstr(tsv, "decimal-format-route-policy-tournament") != NULL);
  CHECK(strstr(tsv, "sameRunTournament=yes") != NULL);
  CHECK(strstr(tsv, "mfast-factor64pre-precompute") != NULL);
  CHECK(strstr(tsv, "direct-ge4096-leaf8") != NULL);
  CHECK(strstr(tsv, "direct-ge8192-leaf16") != NULL);
  CHECK(strstr(tsv, "static-ge4096-l16") != NULL);
  CHECK(strstr(tsv, "static-ge8192-l8") != NULL);
  CHECK(strstr(tsv, "workspace-ge4096-leaf16") != NULL);
  CHECK(strstr(tsv, "preinv-ge4096-leaf8") != NULL);
  CHECK(strstr(tsv, "preinv-ge8192-leaf16") != NULL);
  CHECK(strstr(tsv, "preinv-ge16384-leaf16") != NULL);
  CHECK(strstr(tsv, "preinv10e19-window768-1000") != NULL);
  CHECK(strstr(tsv, "preinv10e19-pairs-window768-1000") != NULL);
  CHECK(strstr(tsv, "square-leaf-order") != NULL);
  CHECK(strstr(tsv, "fused-diagonal-cross-leaf") != NULL);
  CHECK(strstr(tsv, "decimal-format-policy-workspace") != NULL);
  CHECK(strstr(tsv, "decimal-format-policy-preinv-qhat") != NULL);
  CHECK(strstr(tsv, "decimal-format-policy-divide-1e19-preinv") != NULL);
  CHECK(strstr(tsv, "decimal-format-policy-divide-1e19-preinv-pairs") != NULL);
  CHECK(strstr(tsv, "square-policy") != NULL);
  CHECK(strstr(tsv, "karatsuba-thr96") != NULL);
  CHECK(strstr(tsv, "mul-policy") != NULL);
  CHECK(strstr(tsv, "mul-policy-safety") != NULL);
  CHECK(strstr(tsv, "frontier-scout") != NULL);
  CHECK(strstr(tsv, "mul-frontier") != NULL);
  CHECK(strstr(tsv, "square-frontier") != NULL);
  CHECK(strstr(tsv, "very-large-frontier-scout") != NULL);
  CHECK(strstr(tsv, "mfast8m-difdit24-complete") != NULL);
  CHECK(strstr(tsv, "duplicateControl=default") != NULL);
  CHECK(strstr(tsv, "controlPlacement=tail") != NULL);
  CHECK(strstr(tsv, "controlSafety=") != NULL);
  CHECK(strstr(tsv, "controlRatio=") != NULL);
  CHECK(strstr(tsv, "mfastKnob=difdit24k") != NULL);
  CHECK(strstr(tsv, "mfastPG=1.612") != NULL);
  CHECK(strstr(tsv, "mfastCW=1.078") != NULL);
  CHECK(strstr(tsv, "mfastPocket=difdit98304") != NULL);
  CHECK(strstr(tsv, "mfastPocketFloor=98304") != NULL);
  CHECK(strstr(tsv, "mfastPocket6144=1.019") != NULL);
  CHECK(strstr(tsv, "mfastPocket8192PG=1.320") != NULL);
  CHECK(strstr(tsv, "mfastPocketGate=noisy-control") != NULL);
  CHECK(strstr(tsv, "mfast1mGate=noisy") != NULL);
  CHECK(strstr(tsv, "mfast1mBest=difdit16000PG1.480") != NULL);
  CHECK(strstr(tsv, "mfast16mBest=difdit24k") != NULL);
  CHECK(strstr(tsv, "mfast16mD98304=0.404") != NULL);
  CHECK(strstr(tsv, "threshold96-ge8192") != NULL);
  CHECK(strstr(tsv, "karatsuba-threshold96") != NULL);
  CHECK(strstr(tsv, "mul-policy-threshold96-ge8192") != NULL);
  CHECK(strstr(tsv, "mul-threshold-tournament") != NULL);
  CHECK(strstr(tsv, "root-size-threshold-tournament") != NULL);
  CHECK(strstr(tsv, "candidateThresholds=32,48,64,80,96,128,160") != NULL);
  CHECK(strstr(tsv, "toom3-u4-ge8192-leaf48") != NULL);
  CHECK(strstr(tsv, "toom3-u4-rec-ge16384-leaf64-depth2") != NULL);
  CHECK(strstr(tsv, "square") != NULL);
  CHECK(strstr(tsv, "square-vs-mul") != NULL);
  CHECK(strstr(tsv, "square-karatsuba-vs-mul") != NULL);
  CHECK(strstr(tsv, "square-karatsuba-vs-gmp") != NULL);
  CHECK(strstr(tsv, "mul-toom3-vs-scratch") != NULL);
  CHECK(strstr(tsv, "mul-karatsuba-middle") != NULL);
  CHECK(strstr(tsv, "karatsuba-sum-middle") != NULL);
  CHECK(strstr(tsv, "karatsuba-difference-middle") != NULL);
#if defined(_MSC_VER) && defined(_M_X64)
  CHECK(strstr(tsv, "mul-toom3-unroll4-vs-scratch") != NULL);
  CHECK(strstr(tsv, "mul-toom3-unroll4-vs-gmp") != NULL);
  CHECK(strstr(tsv, "mul-toom3-unroll4-deep-vs-gmp") != NULL);
  CHECK(strstr(tsv, "mul-toom3-u4-rec-vs-gmp") != NULL);
  CHECK(strstr(tsv, "mul-toom3-u4-rec-deep-vs-gmp") != NULL);
  CHECK(strstr(tsv, "muladd-unroll4") != NULL);
  CHECK(strstr(tsv, "muladd-unroll8") != NULL);
  CHECK(strstr(tsv, "mul-unroll4-vs-scratch") != NULL);
  CHECK(strstr(tsv, "mul-unroll4-vs-gmp") != NULL);
  CHECK(strstr(tsv, "mul-unroll4-deep-vs-gmp") != NULL);
  if (report->cpu.bmi2 && report->cpu.adx) CHECK(strstr(tsv, "muladd-bmi2-adx") != NULL);
#endif
  CHECK(strstr(tsv, "replacement-ready") != NULL || strstr(tsv, "parity") != NULL);
  free(tsv);

  CHECK(workbench.run_dir != NULL);
  CHECK(workbench.events_jsonl != NULL);
  CHECK(workbench.input_path != NULL);
  CHECK(workbench.normalized_path != NULL);
  CHECK(workbench.config_path != NULL);
  CHECK(workbench.cpu_features_path != NULL);
  CHECK(workbench.report_json_path != NULL);
  CHECK(workbench.events_jsonl_path != NULL);
  CHECK(workbench.benchmark_json_path != NULL);
  CHECK(workbench.benchmark_tsv_path != NULL);
  CHECK(workbench.benchmark_frontier_path != NULL);
  CHECK(workbench.benchmark_progress_path != NULL);
  CHECK(workbench.benchmark_progress_tsv_path != NULL);
  CHECK(strstr(workbench.json, "\"artifactPaths\"") != NULL);
  CHECK(strstr(workbench.json, "\"benchmarkFrontier\"") != NULL);
  CHECK(strstr(workbench.json, "\"benchmarkProgress\"") != NULL);
  CHECK(strstr(workbench.json, "\"benchmarkProgressTsv\"") != NULL);
  CHECK(strstr(workbench.benchmark_frontier_path, workbench.run_dir) != NULL);
  CHECK(strstr(workbench.benchmark_progress_path, workbench.run_dir) != NULL);
  CHECK(strstr(workbench.benchmark_progress_tsv_path, workbench.run_dir) != NULL);
  CHECK(strstr(workbench.events_jsonl, "\"stage\":\"benchmark\"") != NULL);
  CHECK(strstr(workbench.events_jsonl, "lanePromotionReady=") != NULL);
  CHECK(strstr(workbench.events_jsonl, "laneSafetyRejected=") != NULL);
  CHECK(strstr(workbench.events_jsonl, "\"stage\":\"cpu\"") != NULL);
  char *benchmark_json = read_text_file(workbench.benchmark_json_path);
  char *benchmark_tsv = read_text_file(workbench.benchmark_tsv_path);
  char *benchmark_frontier = read_text_file(workbench.benchmark_frontier_path);
  char *benchmark_progress = read_text_file(workbench.benchmark_progress_path);
  char *benchmark_progress_tsv = read_text_file(workbench.benchmark_progress_tsv_path);
  char *cpu_text = read_text_file(workbench.cpu_features_path);
  CHECK(strstr(benchmark_json, "\"benchmarkReport\"") != NULL);
  CHECK(strstr(benchmark_json, "\"cpu\"") != NULL);
  CHECK(strstr(benchmark_json, "\"build\"") != NULL);
  CHECK(strstr(benchmark_json, "\"compiler\"") != NULL);
  CHECK(strstr(benchmark_json, "\"interproceduralOptimization\"") != NULL);
  CHECK(strstr(benchmark_json, "\"baselineBackend\"") != NULL);
  CHECK(strstr(benchmark_json, "\"baselineBackendVersion\"") != NULL);
  CHECK(strstr(benchmark_json, "\"baselineBackendLibrary\"") != NULL);
  CHECK(strstr(benchmark_json, "\"scratchRouteConfig\"") != NULL);
  CHECK(strstr(benchmark_json, "\"lanes\"") != NULL);
  CHECK(strstr(benchmark_json, "\"promotionReady\"") != NULL);
  CHECK(strstr(benchmark_json, "\"oracleOnly\"") != NULL);
  CHECK(strstr(benchmark_json, "\"safetyRejected\"") != NULL);
  CHECK(strstr(benchmark_json, "\"measurableStatus\"") != NULL);
  CHECK(strstr(benchmark_json, "\"betterNow\"") != NULL);
  CHECK(strstr(benchmark_json, "\"stillWorking\"") != NULL);
  CHECK(strstr(benchmark_json, "\"speedup\"") != NULL);
  CHECK(strstr(benchmark_json, "\"slowdown\"") != NULL);
  CHECK(strstr(benchmark_json, "\"squareTinySelfMulPolicy\"") != NULL);
  CHECK(strstr(benchmark_json, "\"mulUnroll4RouteMaxLimbs\"") != NULL);
  CHECK(strstr(benchmark_json, "\"decimalWideChunkDigits\"") != NULL);
  CHECK(strstr(benchmark_json, "\"parseLargeMinDigits\":2048") != NULL);
  CHECK(strstr(benchmark_json, "\"parseLargeChunkDigits\":15") != NULL);
  CHECK(strstr(benchmark_json, "\"decimalDcMinWideChunks\"") != NULL);
  CHECK(strstr(benchmark_json, "\"decimalPairWriterPolicy\"") != NULL);
  CHECK(strstr(benchmark_json, "\"sparseMulMinProducts\"") != NULL);
  CHECK(strstr(benchmark_json, "\"productionRoutes\"") != NULL);
  CHECK(strstr(benchmark_json, "\"diagnosticProbeFamilies\"") != NULL);
  CHECK(strstr(benchmark_json, "\"decimal-parse-large\"") != NULL);
  CHECK(strstr(benchmark_json, "\"msvcUint128Helpers\"") != NULL);
  CHECK(strstr(benchmark_json, "\"scratchRows\"") != NULL);
  CHECK(strstr(benchmark_tsv, "scratch-vs-gmp") != NULL);
  CHECK(strstr(benchmark_tsv, "buildConfig\tipo\tcompiler\tcompilerVersion") != NULL);
  CHECK(strstr(benchmark_tsv, "kernel-probe") != NULL);
  CHECK(strstr(benchmark_tsv, "policy-probe") != NULL);
  CHECK(strstr(benchmark_tsv, "policy-gate") != NULL);
  CHECK(strstr(benchmark_tsv, "format-policy-safety") != NULL);
  CHECK(strstr(benchmark_tsv, "format-policy-deep-safety") != NULL);
  CHECK(strstr(benchmark_tsv, "deepConfirmation=required") != NULL);
  CHECK(strstr(benchmark_tsv, "deepConfirmation=not-required") != NULL);
  CHECK(strstr(benchmark_tsv, "format-policy-route-audit") != NULL);
  CHECK(strstr(benchmark_tsv, "decimal-format-window-promotion-audit") != NULL);
  CHECK(strstr(benchmark_tsv, "audit-preinv10e19-pairs-window768-1000") != NULL);
  CHECK(strstr(benchmark_tsv, "candCurrentMax=") != NULL);
  CHECK(strstr(benchmark_tsv, "mul-toom3") != NULL);
  CHECK(strstr(benchmark_tsv, "mul-threshold-tournament") != NULL);
  CHECK(strstr(benchmark_tsv, "root-size-threshold-tournament") != NULL);
  CHECK(strstr(benchmark_tsv, "candidateThresholds=32,48,64,80,96,128,160") != NULL);
  CHECK(strstr(benchmark_tsv, "format-route-tournament") != NULL);
  CHECK(strstr(benchmark_tsv, "format-route-tournament-detail") != NULL);
  CHECK(strstr(benchmark_tsv, "decimal-format-route-policy-tournament") != NULL);
  CHECK(strstr(benchmark_tsv, "controlSafety=tournament-detail") != NULL);
  CHECK(strstr(benchmark_tsv, "routeCurrentRatio=") != NULL);
  CHECK(strstr(benchmark_tsv, "routeGmpRatio=") != NULL);
  CHECK(strstr(benchmark_tsv, "sameRunTournament=yes") != NULL);
  CHECK(strstr(benchmark_tsv, "mfast-factor64pre-precompute") != NULL);
  CHECK(strstr(benchmark_tsv, "mfast-cuda-tournament-detail") != NULL);
  CHECK(strstr(benchmark_tsv, "mul-karatsuba-middle") != NULL);
  CHECK(strstr(benchmark_tsv, "karatsuba-sum-middle") != NULL);
  CHECK(strstr(benchmark_tsv, "karatsuba-difference-middle") != NULL);
  CHECK(strstr(benchmark_tsv, "mod-u32-precompute") != NULL);
  CHECK(strstr(benchmark_tsv, "gcd-u32-precompute") != NULL);
  CHECK(strstr(benchmark_tsv, "powmod-u32-precompute") != NULL);
  CHECK(strstr(benchmark_tsv, "parse-chunk") != NULL);
  CHECK(strstr(benchmark_tsv, "divmod-dc-power") != NULL);
  CHECK(strstr(benchmark_tsv, "format") != NULL);
  CHECK(strstr(benchmark_tsv, "format-threshold") != NULL);
  CHECK(strstr(benchmark_tsv, "format-divider") != NULL);
  CHECK(strstr(benchmark_tsv, "format-folded") != NULL);
  CHECK(strstr(benchmark_tsv, "format-pair-writer") != NULL);
  CHECK(strstr(benchmark_tsv, "format-wide") != NULL);
  CHECK(strstr(benchmark_tsv, "format-mixed-writer") != NULL);
  CHECK(strstr(benchmark_tsv, "format-folded-hwdiv") != NULL);
  CHECK(strstr(benchmark_tsv, "format-hwdiv-mixed") != NULL);
  CHECK(strstr(benchmark_tsv, "format-divide-1e19") != NULL);
  CHECK(strstr(benchmark_tsv, "format-divide-1e19-pairs") != NULL);
  CHECK(strstr(benchmark_tsv, "decimal-divide-1e19-pair-writer") != NULL);
  CHECK(strstr(benchmark_tsv, "decimal-format-divide-1e19-pairs") != NULL);
  CHECK(strstr(benchmark_tsv, "format-divide-1e19-preinv") != NULL);
  CHECK(strstr(benchmark_tsv, "format-divide-1e19-preinv-pairs") != NULL);
  CHECK(strstr(benchmark_tsv, "decimal-divide-1e19-preinv") != NULL);
  CHECK(strstr(benchmark_tsv, "decimal-divide-1e19-preinv-pair-writer") != NULL);
  CHECK(strstr(benchmark_tsv, "decimal-format-divide-1e19-preinv") != NULL);
  CHECK(strstr(benchmark_tsv, "decimal-format-divide-1e19-preinv-pairs") != NULL);
  CHECK(strstr(benchmark_tsv, "format-dc") != NULL);
  CHECK(strstr(benchmark_tsv, "format-dc-ladder") != NULL);
  CHECK(strstr(benchmark_tsv, "format-dc-direct") != NULL);
  CHECK(strstr(benchmark_tsv, "format-dc-workspace") != NULL);
  CHECK(strstr(benchmark_tsv, "format-dc-preinv-qhat") != NULL);
  CHECK(strstr(benchmark_tsv, "format-dc-route") != NULL);
  CHECK(strstr(benchmark_tsv, "format-dc-route-safety") != NULL);
  CHECK(strstr(benchmark_tsv, "format-dc-static-ladder") != NULL);
  CHECK(strstr(benchmark_tsv, "format-dc-static-direct") != NULL);
  CHECK(strstr(benchmark_tsv, "direct16-vs-ladder8") != NULL);
  CHECK(strstr(benchmark_tsv, "hashGate=matched") != NULL);
  CHECK(strstr(benchmark_tsv, "hashSafe=") != NULL);
  CHECK(strstr(benchmark_tsv, "mfast615fe9e-hashgate") != NULL);
  CHECK(strstr(benchmark_tsv, "decimal-dc-direct-workspace") != NULL);
  CHECK(strstr(benchmark_tsv, "decimal-dc-direct-preinv-qhat") != NULL);
  CHECK(strstr(benchmark_tsv, "dc-static-pow2") != NULL);
  CHECK(strstr(benchmark_tsv, "dc-static-direct") != NULL);
  CHECK(strstr(benchmark_tsv, "format-policy") != NULL);
  CHECK(strstr(benchmark_tsv, "format-policy-safety") != NULL);
  CHECK(strstr(benchmark_tsv, "divmod-precomputed") != NULL);
  CHECK(strstr(benchmark_tsv, "divmod-workspace") != NULL);
  CHECK(strstr(benchmark_tsv, "setupPolicy=reported-not-scored") != NULL);
  CHECK(strstr(benchmark_tsv, "cacheRole=divisor-context") != NULL);
  CHECK(strstr(benchmark_tsv, "divmod-preinv-qhat") != NULL);
  CHECK(strstr(benchmark_tsv, "divmod-preinv-qhat-safety") != NULL);
  CHECK(strstr(benchmark_tsv, "scratch-divmod-preinv-qhat") != NULL);
  CHECK(strstr(benchmark_tsv, "qhat-u32-limb") != NULL);
  CHECK(strstr(benchmark_tsv, "qhat-preinv") != NULL);
  CHECK(strstr(benchmark_tsv, "preinverted-limb-qhat") != NULL);
  CHECK(strstr(benchmark_tsv, "direct-udiv128-qhat") != NULL);
  CHECK(strstr(benchmark_tsv, "noAutoRoute=1") != NULL);
  CHECK(strstr(benchmark_tsv, "direct-ge4096-leaf8") != NULL);
  CHECK(strstr(benchmark_tsv, "direct-ge8192-leaf16") != NULL);
  CHECK(strstr(benchmark_tsv, "static-ge4096-l16") != NULL);
  CHECK(strstr(benchmark_tsv, "static-ge8192-l8") != NULL);
  CHECK(strstr(benchmark_tsv, "workspace-ge4096-leaf16") != NULL);
  CHECK(strstr(benchmark_tsv, "preinv-ge4096-leaf8") != NULL);
  CHECK(strstr(benchmark_tsv, "preinv-ge8192-leaf16") != NULL);
  CHECK(strstr(benchmark_tsv, "preinv-ge16384-leaf16") != NULL);
  CHECK(strstr(benchmark_tsv, "preinv10e19-window768-1000") != NULL);
  CHECK(strstr(benchmark_tsv, "preinv10e19-pairs-window768-1000") != NULL);
  CHECK(strstr(benchmark_tsv, "square-leaf-order") != NULL);
  CHECK(strstr(benchmark_tsv, "mfastfermat-wide61-dif-dit-bit-reversal-elision") != NULL);
  CHECK(strstr(benchmark_tsv, "deep-preinv10e19-window768-1000") != NULL);
  CHECK(strstr(benchmark_tsv, "deep-preinv10e19-window768-896") != NULL);
  CHECK(strstr(benchmark_tsv, "deep-preinv10e19-window768-960") != NULL);
  CHECK(strstr(benchmark_tsv, "deep-preinv10e19-window896-1000") != NULL);
  CHECK(strstr(benchmark_tsv, "deep-preinv10e19-pairs-window768-896") != NULL);
  CHECK(strstr(benchmark_tsv, "deep-preinv10e19-pairs-window768-960") != NULL);
  CHECK(strstr(benchmark_tsv, "deep-preinv10e19-pairs-window896-1000") != NULL);
  CHECK(strstr(benchmark_tsv, "requiredStablePairs=8/9") != NULL);
  CHECK(strstr(benchmark_tsv, "decimal-format-policy-workspace") != NULL);
  CHECK(strstr(benchmark_tsv, "decimal-format-policy-preinv-qhat") != NULL);
  CHECK(strstr(benchmark_tsv, "decimal-format-policy-divide-1e19-preinv") != NULL);
  CHECK(strstr(benchmark_tsv, "decimal-format-policy-divide-1e19-preinv-pairs") != NULL);
  CHECK(strstr(benchmark_tsv, "square-policy") != NULL);
  CHECK(strstr(benchmark_tsv, "karatsuba-thr96") != NULL);
  CHECK(strstr(benchmark_tsv, "mul-policy") != NULL);
  CHECK(strstr(benchmark_tsv, "mul-policy-safety") != NULL);
  CHECK(strstr(benchmark_tsv, "frontier-scout") != NULL);
  CHECK(strstr(benchmark_tsv, "mul-frontier") != NULL);
  CHECK(strstr(benchmark_tsv, "square-frontier") != NULL);
  CHECK(strstr(benchmark_tsv, "very-large-frontier-scout") != NULL);
  CHECK(strstr(benchmark_tsv, "mfast8m-difdit24-complete") != NULL);
  CHECK(strstr(benchmark_tsv, "duplicateControl=default") != NULL);
  CHECK(strstr(benchmark_tsv, "controlPlacement=tail") != NULL);
  CHECK(strstr(benchmark_tsv, "controlSafety=") != NULL);
  CHECK(strstr(benchmark_tsv, "controlRatio=") != NULL);
  CHECK(strstr(benchmark_tsv, "mfastKnob=difdit24k") != NULL);
  CHECK(strstr(benchmark_tsv, "mfastPG=1.612") != NULL);
  CHECK(strstr(benchmark_tsv, "mfastCW=1.078") != NULL);
  CHECK(strstr(benchmark_tsv, "mfastPocket=difdit98304") != NULL);
  CHECK(strstr(benchmark_tsv, "mfastPocketFloor=98304") != NULL);
  CHECK(strstr(benchmark_tsv, "mfastPocket8192PG=1.320") != NULL);
  CHECK(strstr(benchmark_tsv, "mfastPocketGate=noisy-control") != NULL);
  CHECK(strstr(benchmark_tsv, "mfast1mGate=noisy") != NULL);
  CHECK(strstr(benchmark_tsv, "mfast1mBest=difdit16000PG1.480") != NULL);
  CHECK(strstr(benchmark_tsv, "mfast16mBest=difdit24k") != NULL);
  CHECK(strstr(benchmark_tsv, "mfast16mD98304=0.404") != NULL);
  CHECK(strstr(benchmark_tsv, "threshold96-ge8192") != NULL);
  CHECK(strstr(benchmark_tsv, "karatsuba-threshold96") != NULL);
  CHECK(strstr(benchmark_tsv, "mul-policy-threshold96-ge8192") != NULL);
  CHECK(strstr(benchmark_tsv, "toom3-u4-ge8192-leaf48") != NULL);
  CHECK(strstr(benchmark_tsv, "toom3-u4-rec-ge16384-leaf64-depth2") != NULL);
  CHECK(strstr(benchmark_tsv, "square") != NULL);
  CHECK(strstr(benchmark_tsv, "square-vs-mul") != NULL);
  CHECK(strstr(benchmark_tsv, "square-karatsuba-vs-mul") != NULL);
  CHECK(strstr(benchmark_tsv, "square-karatsuba-vs-gmp") != NULL);
  CHECK(strstr(benchmark_tsv, "sparse-zero-square") != NULL);
  CHECK(strstr(benchmark_tsv, "sparse-zero-mul") != NULL);
  CHECK(strstr(benchmark_tsv, "sparse-pair-product") != NULL);
  CHECK(strstr(benchmark_tsv, "mfastFeedback=zero-scalar-row-addmul") != NULL);
#if defined(_MSC_VER) && defined(_M_X64)
  CHECK(strstr(benchmark_tsv, "mul-toom3-unroll4-vs-scratch") != NULL);
  CHECK(strstr(benchmark_tsv, "mul-toom3-unroll4-vs-gmp") != NULL);
  CHECK(strstr(benchmark_tsv, "mul-toom3-unroll4-deep-vs-gmp") != NULL);
  CHECK(strstr(benchmark_tsv, "mul-toom3-u4-rec-vs-gmp") != NULL);
  CHECK(strstr(benchmark_tsv, "mul-toom3-u4-rec-deep-vs-gmp") != NULL);
#endif
  CHECK(strstr(benchmark_tsv, "speedRatio") != NULL);
  CHECK(strstr(benchmark_tsv, "stableSampleCount") != NULL);
  CHECK(strstr(benchmark_tsv, "worstPairRatio") != NULL);
  CHECK(strstr(benchmark_tsv, "ratioMethod=paired-median") != NULL);
  CHECK(strstr(benchmark_frontier, "BENCHMARK FRONTIER") != NULL);
  CHECK(strstr(benchmark_frontier, "Build: compiler=") != NULL);
  CHECK(strstr(benchmark_frontier, "ipo=") != NULL);
  CHECK(strstr(benchmark_frontier, "Baseline backend:") != NULL);
  CHECK(strstr(benchmark_frontier, "Bigint route:") != NULL);
  CHECK(strstr(benchmark_frontier, "square-self-mul<=8 limbs") != NULL);
  CHECK(strstr(benchmark_frontier, "format-pair-writer=small<=8 or horner 48..54 limbs") != NULL);
  CHECK(strstr(benchmark_frontier, "format-dc-ladder>=4096 digits leaf=8") != NULL);
  CHECK(strstr(benchmark_frontier, "FRONTIER SUMMARY") != NULL);
  CHECK(strstr(benchmark_frontier, "MEASURABLE STATUS") != NULL);
  CHECK(strstr(benchmark_frontier, "Better now (scratch rows allowed)") != NULL);
  CHECK(strstr(benchmark_frontier, "Still working (scratch gaps)") != NULL);
  CHECK(strstr(benchmark_frontier, "faster than backend") != NULL);
  CHECK(strstr(benchmark_frontier, "slower than backend") != NULL);
  CHECK(strstr(benchmark_frontier, "Largest scratch gaps") != NULL);
  CHECK(strstr(benchmark_frontier, "Median wins rejected by worst-pair safety") != NULL);
  CHECK(strstr(benchmark_frontier, "SCRATCH VS ") != NULL);
  CHECK(strstr(benchmark_frontier, "PRODUCT POLICY PROBES") != NULL);
  CHECK(strstr(benchmark_frontier, "PRODUCT POLICY THRESHOLD GATES") != NULL);
  CHECK(strstr(benchmark_frontier, "LARGE FRONTIER SCOUTS") != NULL);
  CHECK(strstr(benchmark_frontier, "Control") != NULL);
  CHECK(strstr(benchmark_frontier, "CtlSafety") != NULL);
  CHECK(strstr(benchmark_frontier, "Worst") != NULL);
  CHECK(strstr(benchmark_frontier, "mul-threshold thr=") != NULL);
  CHECK(strstr(benchmark_frontier, "mul-threshold-tournament thr=") != NULL);
  CHECK(strstr(benchmark_frontier, "mod-u32-precompute") != NULL);
  CHECK(strstr(benchmark_frontier, "gcd-u32-precompute") != NULL);
  CHECK(strstr(benchmark_frontier, "powmod-u32-precompute") != NULL);
  CHECK(strstr(benchmark_frontier, "parse-chunk chunk=") != NULL);
  CHECK(strstr(benchmark_frontier, "qhat-preinv") != NULL);
  CHECK(strstr(benchmark_frontier, "qhat-u32-limb") != NULL);
  CHECK(strstr(benchmark_frontier, "divmod-dc-power chunks=") != NULL);
  CHECK(strstr(benchmark_frontier, "divmod-precomputed chunks=") != NULL);
  CHECK(strstr(benchmark_frontier, "divmod-workspace chunks=") != NULL);
  CHECK(strstr(benchmark_frontier, "divmod-preinv-qhat chunks=") != NULL);
  CHECK(strstr(benchmark_frontier, "divmod-preinv-qhat-safety") != NULL);
  CHECK(strstr(benchmark_frontier, "format-threshold thr=16") != NULL);
  CHECK(strstr(benchmark_frontier, "format-threshold thr=32") != NULL);
  CHECK(strstr(benchmark_frontier, "format-threshold thr=40") != NULL);
  CHECK(strstr(benchmark_frontier, "format-threshold thr=44") != NULL);
  CHECK(strstr(benchmark_frontier, "format-threshold thr=48") != NULL);
  CHECK(strstr(benchmark_frontier, "format-threshold thr=52") != NULL);
  CHECK(strstr(benchmark_frontier, "format-threshold thr=56") != NULL);
  CHECK(strstr(benchmark_frontier, "format-threshold thr=64") != NULL);
  CHECK(strstr(benchmark_frontier, "format-threshold thr=80") != NULL);
  CHECK(strstr(benchmark_frontier, "format-threshold thr=96") != NULL);
  CHECK(strstr(benchmark_frontier, "format-threshold thr=128") != NULL);
  CHECK(strstr(benchmark_frontier, "format-threshold thr=160") != NULL);
  CHECK(strstr(benchmark_frontier, "format-threshold thr=256") != NULL);
  CHECK(strstr(benchmark_frontier, "format-divider mode=direct128") != NULL);
  CHECK(strstr(benchmark_frontier, "format-folded") != NULL);
  CHECK(strstr(benchmark_frontier, "format-pair-writer") != NULL);
  CHECK(strstr(benchmark_frontier, "format-wide") != NULL);
  CHECK(strstr(benchmark_frontier, "format-mixed-writer") != NULL);
  CHECK(strstr(benchmark_frontier, "format-folded-hwdiv") != NULL);
  CHECK(strstr(benchmark_frontier, "format-hwdiv-mixed") != NULL);
  CHECK(strstr(benchmark_frontier, "format-divide-1e19") != NULL);
  CHECK(strstr(benchmark_frontier, "format-divide-1e19-pairs") != NULL);
  CHECK(strstr(benchmark_frontier, "format-divide-1e19-preinv") != NULL);
  CHECK(strstr(benchmark_frontier, "format-divide-1e19-preinv-pairs") != NULL);
  CHECK(strstr(benchmark_frontier, "format-dc") != NULL);
  CHECK(strstr(benchmark_frontier, "format-dc-ladder") != NULL);
  CHECK(strstr(benchmark_frontier, "format-dc-direct") != NULL);
  CHECK(strstr(benchmark_frontier, "format-dc-workspace") != NULL);
  CHECK(strstr(benchmark_frontier, "format-dc-preinv-qhat") != NULL);
  CHECK(strstr(benchmark_frontier, "format-dc-route") != NULL);
  CHECK(strstr(benchmark_frontier, "format-dc-route-safety") != NULL);
  CHECK(strstr(benchmark_frontier, "format-dc-static-ladder") != NULL);
  CHECK(strstr(benchmark_frontier, "format-dc-static-direct") != NULL);
  CHECK(strstr(benchmark_frontier, "format-policy current-default") != NULL);
  CHECK(strstr(benchmark_frontier, "format-policy direct-ge4096-leaf8") != NULL);
  CHECK(strstr(benchmark_frontier, "format-policy direct-ge8192-leaf16") != NULL);
  CHECK(strstr(benchmark_frontier, "format-policy static-ge4096-l16") != NULL);
  CHECK(strstr(benchmark_frontier, "format-policy static-ge8192-l8") != NULL);
  CHECK(strstr(benchmark_frontier, "format-policy workspace-ge4096-leaf16") != NULL);
  CHECK(strstr(benchmark_frontier, "format-policy preinv-ge4096-leaf8") != NULL);
  CHECK(strstr(benchmark_frontier, "format-policy preinv-ge8192-leaf16") != NULL);
  CHECK(strstr(benchmark_frontier, "format-policy preinv-ge16384-leaf16") != NULL);
  CHECK(strstr(benchmark_frontier, "format-policy preinv10e19-window768-1000") != NULL);
  CHECK(strstr(benchmark_frontier, "format-policy preinv10e19-pairs-window768-1000") != NULL);
  CHECK(strstr(benchmark_frontier, "format-route-tournament tournament") != NULL);
  CHECK(strstr(benchmark_frontier, "format-route-tournament-detail") != NULL);
  CHECK(strstr(benchmark_frontier, "format-policy-safety direct-ge4096-leaf8") != NULL);
  CHECK(strstr(benchmark_frontier, "format-policy-safety direct-ge8192-leaf16") != NULL);
  CHECK(strstr(benchmark_frontier, "format-policy-safety static-ge4096-l16") != NULL);
  CHECK(strstr(benchmark_frontier, "format-policy-safety static-ge8192-l8") != NULL);
  CHECK(strstr(benchmark_frontier, "format-policy-safety workspace-ge4096-leaf16") != NULL);
  CHECK(strstr(benchmark_frontier, "format-policy-safety preinv-ge4096-leaf8") != NULL);
  CHECK(strstr(benchmark_frontier, "format-policy-safety preinv-ge8192-leaf16") != NULL);
  CHECK(strstr(benchmark_frontier, "format-policy-safety preinv-ge16384-leaf16") != NULL);
  CHECK(strstr(benchmark_frontier, "format-policy-safety preinv10e19-window768-1000") != NULL);
  CHECK(strstr(benchmark_frontier, "format-policy-safety preinv10e19-pairs-window768-1000") != NULL);
  CHECK(strstr(benchmark_frontier, "format-policy-deep-safety deep-preinv10e19-window768-1000") != NULL);
  CHECK(strstr(benchmark_frontier, "format-policy-deep-safety deep-preinv10e19-window768-896") != NULL);
  CHECK(strstr(benchmark_frontier, "format-policy-deep-safety deep-preinv10e19-window768-960") != NULL);
  CHECK(strstr(benchmark_frontier, "format-policy-deep-safety deep-preinv10e19-window896-1000") != NULL);
  CHECK(strstr(benchmark_frontier, "format-policy-deep-safety deep-preinv10e19-pairs-window768-896") != NULL);
  CHECK(strstr(benchmark_frontier, "format-policy-deep-safety deep-preinv10e19-pairs-window768-960") != NULL);
  CHECK(strstr(benchmark_frontier, "format-policy-deep-safety deep-preinv10e19-pairs-window896-1000") != NULL);
  CHECK(strstr(benchmark_frontier, "format-policy-route-audit audit-preinv10e19-window768-1000") != NULL);
  CHECK(strstr(benchmark_frontier, "format-policy-route-audit audit-preinv10e19-pairs-window768-1000") != NULL);
  CHECK(strstr(benchmark_frontier, "square-policy current-default") != NULL);
  CHECK(strstr(benchmark_frontier, "square-policy karatsuba-thr96") != NULL);
  CHECK(strstr(benchmark_frontier, "square-leaf-order") != NULL);
  CHECK(strstr(benchmark_frontier, "sparse-zero-square") != NULL);
  CHECK(strstr(benchmark_frontier, "sparse-zero-mul") != NULL);
  CHECK(strstr(benchmark_frontier, "sparse-pair-product") != NULL);
  CHECK(strstr(benchmark_frontier, "base=current-scratch-square") != NULL);
  CHECK(strstr(benchmark_frontier, "mul-policy current-default") != NULL);
  CHECK(strstr(benchmark_frontier, "mul-policy threshold96-ge8192") != NULL);
  CHECK(strstr(benchmark_frontier, "mul-policy toom3-u4-ge8192-leaf48") != NULL);
  CHECK(strstr(benchmark_frontier, "mul-policy toom3-u4-rec-ge16384-leaf64-depth2") != NULL);
  CHECK(strstr(benchmark_frontier, "mul-policy-safety threshold96-ge8192") != NULL);
  CHECK(strstr(benchmark_frontier, "mul-policy-safety toom3-u4-ge8192-leaf48") != NULL);
  CHECK(strstr(benchmark_frontier, "mul-policy-safety toom3-u4-rec-ge16384-leaf64-depth2") != NULL);
  CHECK(strstr(benchmark_frontier, "mul-frontier") != NULL);
  CHECK(strstr(benchmark_frontier, "square-frontier") != NULL);
  CHECK(strstr(benchmark_frontier, "mul-karatsuba-middle") != NULL);
  CHECK(strstr(benchmark_frontier, "mode=sum-vs-difference") != NULL);
  CHECK(strstr(benchmark_frontier, "base=karatsuba-difference-") != NULL);
  CHECK(strstr(benchmark_frontier, "leaf=64") != NULL);
  CHECK(strstr(benchmark_frontier, "base=") != NULL);
#if defined(_MSC_VER) && defined(_M_X64)
  CHECK(strstr(benchmark_frontier, "depth=2") != NULL);
#endif
  CHECK(strstr(benchmark_frontier, "format") != NULL);
  CHECK(strstr(benchmark_frontier, "flags=") != NULL);
  CHECK(strstr(benchmark_frontier, "no worst-pair regression above 1.0") != NULL);
  CHECK(strstr(benchmark_progress, "BENCHMARK PROGRESS DIGEST") != NULL);
  CHECK(strstr(benchmark_progress, "Product/backend route candidate rows observed") != NULL);
  CHECK(strstr(benchmark_progress, "Open/noisy route rows observed") != NULL);
  CHECK(strstr(benchmark_progress, "Product-gated route rows observed") != NULL);
  CHECK(strstr(benchmark_progress, "Setup/warmup context rows observed") != NULL);
  CHECK(strstr(benchmark_progress, "Warmup-review rows observed") != NULL);
  CHECK(strstr(benchmark_progress, "Safety-rejected rows observed") != NULL);
  CHECK(strstr(benchmark_progress, "Baseline/current rows observed") != NULL);
  CHECK(strstr(benchmark_progress, "Control/noise rows observed") != NULL);
  CHECK(strstr(benchmark_progress, "productGatedOpen=") != NULL);
  CHECK(strstr(benchmark_progress, "warmupReviewRows=") != NULL);
  CHECK(strstr(benchmark_progress, "setupContextRows=") != NULL);
  CHECK(strstr(benchmark_progress, "baselineExcluded=") != NULL);
  CHECK(strstr(benchmark_progress, "controlsExcluded=") != NULL);
  CHECK(strstr(benchmark_progress, "product-gated rows") != NULL);
  CHECK(strstr(benchmark_progress_tsv, "primaryLane\trouteCandidate\trouteCompleted\trouteOpen\tproductGated\thasSetupContext\tsetupSeconds\twarmupReview\tlowerBound\trunFailed\tattemptedRuns\tcompletedRuns") != NULL);
  CHECK(strstr(benchmark_progress_tsv, "compilerVersion\tdigitBand\tworkloadShape\tpolicy\tcandidate\tactiveCandidate\tbaseline\tfeatureGate\tgmpClue\tcontrolSafety\tthresholdSafety\thashGate") != NULL);
  CHECK(strstr(benchmark_progress_tsv, "hasSetupContext") != NULL);
  CHECK(strstr(benchmark_progress_tsv, "setupSeconds") != NULL);
  CHECK(strstr(benchmark_progress_tsv, "runFailed") != NULL);
  CHECK(strstr(benchmark_progress_tsv, "\tlarge\tdecimal-format\t") != NULL);
  CHECK(strstr(benchmark_progress_tsv, "\tfrontier\tfrontier-scout\t") != NULL);
  CHECK(strstr(benchmark_progress_tsv, "divmod-precomputed") != NULL);
  CHECK(strstr(benchmark_progress_tsv, "format-route-tournament-detail") != NULL);
  CHECK(strstr(benchmark_progress_tsv, "controlSafety=tournament-detail") != NULL);
  CHECK(strstr(benchmark_progress_tsv, "\ttrue\tfalse\ttrue\ttrue\ttrue") != NULL);
  CHECK(strstr(cpu_text, "CPU:") != NULL);
  CHECK(strstr(cpu_text, "flags=") != NULL);
  free(benchmark_json);
  free(benchmark_tsv);
  free(benchmark_frontier);
  free(benchmark_progress);
  free(benchmark_progress_tsv);
  free(cpu_text);
  xray_workbench_report_clear(&workbench);
}

int main(void) {
  test_parse_messy_input();
  test_public_allocator_contract();
  test_runtime_version_contract();
  test_benchmark_tsv_comparison();
  test_benchmark_progress_digest();
  test_exact_expression_parser();
  test_cpu_feature_detection();
  test_scratch_bigint_oracle();
  test_decimal_ffi_helpers();
  test_scratch_bigint_oracle_sweep();
  test_scratch_bigint_large_mul_oracle();
  test_scratch_bigint_square_oracle();
  test_scratch_bigint_sparse_zero_limb_oracle();
  test_scratch_bigint_karatsuba_middle_signs();
  test_scratch_bigint_mul_thresholds();
  test_scratch_bigint_karatsuba_sum_probe_oracle();
  test_scratch_bigint_toom3_probe_oracle();
  test_scratch_bigint_toom3_recursive_probe_oracle();
  test_scratch_bigint_unroll4_probe_oracle();
  test_scratch_bigint_toom3_minus_one_signs();
  test_ambiguous_input_rejected();
  test_factor_solver_exact();
  test_factor_solver_unresolved_budget();
  test_rho_and_prime_power();
  test_stronger_factor_methods();
  test_cyclotomic_known_values();
  test_cyclotomic_scan_exact();
  test_workspace_and_gnfs_artifacts();
  test_report_json_ffi_helpers();
  test_large_nonhit_does_not_false_solve();
  test_benchmarks();
  puts("native xray tests passed");
  return 0;
}
