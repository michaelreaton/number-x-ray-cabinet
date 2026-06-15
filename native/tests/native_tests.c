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

  const size_t format_roundtrip_sizes[] = {40U, 150U, 1000U, 4096U, 8192U};
  for (size_t index = 0; index < sizeof(format_roundtrip_sizes) / sizeof(format_roundtrip_sizes[0]); ++index) {
    char *roundtrip_input = make_pattern_decimal(format_roundtrip_sizes[index], "97531864208642135790");
    CHECK(roundtrip_input != NULL);
    CHECK(xray_bigint_set_decimal(&a, roundtrip_input));
    CHECK(mpz_set_str(ga, roundtrip_input, 10) == 0);
    char *roundtrip_text = xray_bigint_get_decimal(&a);
    char *roundtrip_folded = xray_bigint_get_decimal_folded_probe(&a);
    char *roundtrip_pair = xray_bigint_get_decimal_pair_writer_probe(&a);
    char *roundtrip_folded_pair = xray_bigint_get_decimal_folded_pair_writer_probe(&a);
    char *roundtrip_wide = xray_bigint_get_decimal_wide_probe(&a);
    char *roundtrip_oracle = mpz_get_str(NULL, 10, ga);
    CHECK(roundtrip_text != NULL);
    CHECK(roundtrip_folded != NULL);
    CHECK(roundtrip_pair != NULL);
    CHECK(roundtrip_folded_pair != NULL);
    CHECK(roundtrip_wide != NULL);
    CHECK(roundtrip_oracle != NULL);
    CHECK(strcmp(roundtrip_text, roundtrip_oracle) == 0);
    CHECK(strcmp(roundtrip_folded, roundtrip_oracle) == 0);
    CHECK(strcmp(roundtrip_pair, roundtrip_oracle) == 0);
    CHECK(strcmp(roundtrip_folded_pair, roundtrip_oracle) == 0);
    CHECK(strcmp(roundtrip_wide, roundtrip_oracle) == 0);
    free(roundtrip_input);
    free(roundtrip_text);
    free(roundtrip_folded);
    free(roundtrip_pair);
    free(roundtrip_folded_pair);
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
    XrayScratchBigInt value, square;
    xray_bigint_init(&value);
    xray_bigint_init(&square);
    mpz_t gvalue, gsquare;
    mpz_inits(gvalue, gsquare, NULL);

    CHECK(xray_bigint_set_decimal(&value, small_cases[index]));
    CHECK(mpz_set_str(gvalue, small_cases[index], 10) == 0);
    mpz_mul(gsquare, gvalue, gvalue);
    CHECK(xray_bigint_square(&square, &value));
    check_scratch_matches_mpz(&square, gsquare);

    CHECK(xray_bigint_square(&value, &value));
    check_scratch_matches_mpz(&value, gsquare);

    mpz_clears(gvalue, gsquare, NULL);
    xray_bigint_clear(&value);
    xray_bigint_clear(&square);
  }

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

  CHECK(xray_bigint_square(&large, &large));
  check_scratch_matches_mpz(&large, gsquare);
  CHECK(xray_bigint_set_decimal(&large, large_text));
  CHECK(xray_bigint_square_karatsuba_probe(&large, &large, 16));
  check_scratch_matches_mpz(&large, gsquare);

  mpz_clears(glarge, gsquare, NULL);
  xray_bigint_clear(&large);
  xray_bigint_clear(&square);
  free(large_text);
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
  XrayWorkbenchReport workbench;
  CHECK(xray_workbench_run("10403", &config, &workbench));
  XrayBenchmarkReport *report = &workbench.benchmark;
  CHECK(workbench.cpu.logical_cpus >= 1);
  CHECK(report->cpu.logical_cpus >= 1);
  CHECK(report->result_count >= 40);
  CHECK(report->passed_count == report->result_count);
  size_t scratch_rows = 0;
  size_t kernel_rows = 0;
  size_t replacement_ready_rows = 0;
  size_t oracle_only_rows = 0;
  size_t blocked_rows = 0;
  int saw_8192_scratch = 0;
  int saw_16384_scratch_mul = 0;
  int saw_scratch_square = 0;
  int saw_scratch_format = 0;
  int saw_8192_kernel_probe = 0;
  int saw_16384_kernel_probe = 0;
  int saw_square_vs_mul_probe = 0;
  int saw_format_threshold_probe = 0;
  int saw_format_threshold16_probe = 0;
  int saw_format_threshold32_probe = 0;
  int saw_format_threshold48_probe = 0;
  int saw_format_threshold64_probe = 0;
  int saw_format_threshold96_probe = 0;
  int saw_format_threshold128_probe = 0;
  int saw_format_threshold1000_probe = 0;
  int saw_format_threshold4096_probe = 0;
  int saw_format_threshold8192_probe = 0;
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
  int saw_format_pair_writer1000_probe = 0;
  int saw_format_pair_writer4096_probe = 0;
  int saw_format_pair_writer8192_probe = 0;
  int saw_format_wide_probe = 0;
  int saw_format_wide1000_probe = 0;
  int saw_format_wide4096_probe = 0;
  int saw_format_wide8192_probe = 0;
  int saw_square_karatsuba_vs_mul_probe = 0;
  int saw_square_karatsuba_vs_gmp_probe = 0;
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
  int saw_u32_precompute_probe = 0;
  int saw_mod_u32_precompute_probe = 0;
  int saw_gcd_u32_precompute_probe = 0;
  int saw_powmod_u32_precompute_probe = 0;
  int saw_u32_precompute40_probe = 0;
  int saw_u32_precompute1000_probe = 0;
  int saw_u32_precompute8192_probe = 0;
  int saw_mul_unroll4_vs_scratch_probe = 0;
  int saw_mul_unroll4_vs_gmp_probe = 0;
  int saw_mul_unroll4_deep_vs_gmp_probe = 0;
  for (size_t index = 0; index < report->result_count; ++index) {
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
      if (strcmp(report->results[index].operation, "format") == 0) saw_scratch_format = 1;
      CHECK(strstr(report->results[index].detail, "ratioMethod=paired-median") != NULL);
      CHECK(strstr(report->results[index].detail, "stablePairs=") != NULL);
      CHECK(strstr(report->results[index].detail, "worstPairRatio=") != NULL);
      CHECK(strstr(report->results[index].detail, strcmp(report->results[index].operation, "mul") == 0 ? "operandFamilies=2" : "operandFamilies=1") != NULL);
      const char *adoption = xray_scratch_adoption_for_result(&report->results[index]);
      CHECK(strcmp(report->results[index].adoption, adoption) == 0);
      CHECK(report->results[index].replacement_ready == (strcmp(adoption, "allowed") == 0));
      if (strcmp(adoption, "allowed") == 0) {
        CHECK(report->results[index].stable_sample_count >= 4);
        replacement_ready_rows++;
      }
      else if (strcmp(adoption, "oracle-only") == 0) oracle_only_rows++;
      else blocked_rows++;
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
      if (strcmp(report->results[index].operation, "mod-u32-precompute") == 0 ||
          strcmp(report->results[index].operation, "gcd-u32-precompute") == 0 ||
          strcmp(report->results[index].operation, "powmod-u32-precompute") == 0) {
        saw_u32_precompute_probe = 1;
        if (strcmp(report->results[index].operation, "mod-u32-precompute") == 0) saw_mod_u32_precompute_probe = 1;
        else if (strcmp(report->results[index].operation, "gcd-u32-precompute") == 0) saw_gcd_u32_precompute_probe = 1;
        else saw_powmod_u32_precompute_probe = 1;
        if (report->results[index].digits == 40) saw_u32_precompute40_probe = 1;
        else if (report->results[index].digits == 1000) saw_u32_precompute1000_probe = 1;
        else if (report->results[index].digits == 8192) saw_u32_precompute8192_probe = 1;
        else CHECK(0);
        CHECK(strstr(report->results[index].detail, "candidate=u32-mod-context") != NULL);
        CHECK(strstr(report->results[index].detail, "baseline=one-shot-u32") != NULL);
        CHECK(strstr(report->results[index].detail, "featureGate=u32-mod-context") != NULL);
        CHECK(strstr(report->results[index].detail, "modulus=1000000007") != NULL);
      }
      if (strcmp(report->results[index].operation, "format-threshold") == 0) {
        saw_format_threshold_probe = 1;
        if (strstr(report->results[index].detail, "threshold=16") != NULL) saw_format_threshold16_probe = 1;
        else if (strstr(report->results[index].detail, "threshold=32") != NULL) saw_format_threshold32_probe = 1;
        else if (strstr(report->results[index].detail, "threshold=48") != NULL) saw_format_threshold48_probe = 1;
        else if (strstr(report->results[index].detail, "threshold=64") != NULL) saw_format_threshold64_probe = 1;
        else if (strstr(report->results[index].detail, "threshold=96") != NULL) saw_format_threshold96_probe = 1;
        else if (strstr(report->results[index].detail, "threshold=128") != NULL) saw_format_threshold128_probe = 1;
        else CHECK(0);
        if (report->results[index].digits == 1000) saw_format_threshold1000_probe = 1;
        else if (report->results[index].digits == 4096) saw_format_threshold4096_probe = 1;
        else if (report->results[index].digits == 8192) saw_format_threshold8192_probe = 1;
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
        if (report->results[index].digits == 1000) saw_format_pair_writer1000_probe = 1;
        else if (report->results[index].digits == 4096) saw_format_pair_writer4096_probe = 1;
        else if (report->results[index].digits == 8192) saw_format_pair_writer8192_probe = 1;
        else CHECK(0);
        if (strstr(report->results[index].detail, "mode=production-chunks") != NULL) saw_format_pair_writer_current_probe = 1;
        else if (strstr(report->results[index].detail, "mode=folded-chunks") != NULL) saw_format_pair_writer_folded_probe = 1;
        else CHECK(0);
        CHECK(strstr(report->results[index].detail, "chunkDigits=9") != NULL);
        CHECK(strstr(report->results[index].detail, "candidate=decimal-pair-writer") != NULL);
        CHECK(strstr(report->results[index].detail, "baseline=current-scratch-format") != NULL);
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
      if (strcmp(report->results[index].operation, "square-vs-mul") == 0) {
        saw_square_vs_mul_probe = 1;
        CHECK(strstr(report->results[index].detail, "routeCandidate=unrouted") != NULL);
        CHECK(strstr(report->results[index].detail, "candidate=specialized-square") != NULL);
        CHECK(strstr(report->results[index].detail, "baseline=current-scratch-self-mul") != NULL);
        CHECK(strstr(report->results[index].detail, "featureGate=square-basecase-probe") != NULL);
        CHECK(strstr(report->results[index].detail, "operandFamilies=1") != NULL);
      }
      if (strcmp(report->results[index].operation, "square-karatsuba-vs-mul") == 0 ||
          strcmp(report->results[index].operation, "square-karatsuba-vs-gmp") == 0) {
        if (strcmp(report->results[index].operation, "square-karatsuba-vs-mul") == 0) saw_square_karatsuba_vs_mul_probe = 1;
        else saw_square_karatsuba_vs_gmp_probe = 1;
        CHECK(strstr(report->results[index].detail, "threshold=") != NULL);
        CHECK(strstr(report->results[index].detail, "candidate=karatsuba-square") != NULL);
        CHECK(strstr(report->results[index].detail, "featureGate=karatsuba-square-probe") != NULL);
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
      }
      CHECK(strstr(report->results[index].adoption, "promote-candidate") != NULL ||
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
  CHECK(strcmp(xray_scratch_adoption_for_result(&unstable), "oracle-only") == 0);
  unstable.stable_sample_count = 4;
  CHECK(strcmp(xray_scratch_adoption_for_result(&unstable), "allowed") == 0);
  unstable.speed_ratio = 1.01;
  unstable.stable_sample_count = 5;
  CHECK(strcmp(xray_scratch_adoption_for_result(&unstable), "oracle-only") == 0);
  CHECK(scratch_rows >= 40);
  CHECK(saw_8192_scratch);
  CHECK(saw_16384_scratch_mul);
  CHECK(saw_scratch_square);
  CHECK(saw_scratch_format);
  CHECK(saw_8192_kernel_probe);
  CHECK(saw_16384_kernel_probe);
  CHECK(saw_square_vs_mul_probe);
  CHECK(saw_format_threshold_probe);
  CHECK(saw_format_threshold16_probe);
  CHECK(saw_format_threshold32_probe);
  CHECK(saw_format_threshold48_probe);
  CHECK(saw_format_threshold64_probe);
  CHECK(saw_format_threshold96_probe);
  CHECK(saw_format_threshold128_probe);
  CHECK(saw_format_threshold1000_probe);
  CHECK(saw_format_threshold4096_probe);
  CHECK(saw_format_threshold8192_probe);
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
  CHECK(saw_format_pair_writer1000_probe);
  CHECK(saw_format_pair_writer4096_probe);
  CHECK(saw_format_pair_writer8192_probe);
  CHECK(saw_format_wide_probe);
  CHECK(saw_format_wide1000_probe);
  CHECK(saw_format_wide4096_probe);
  CHECK(saw_format_wide8192_probe);
  CHECK(saw_square_karatsuba_vs_mul_probe);
  CHECK(saw_square_karatsuba_vs_gmp_probe);
  CHECK(saw_toom3_probe);
  CHECK(saw_toom3_vs_scratch_probe);
  CHECK(saw_u32_precompute_probe);
  CHECK(saw_mod_u32_precompute_probe);
  CHECK(saw_gcd_u32_precompute_probe);
  CHECK(saw_powmod_u32_precompute_probe);
  CHECK(saw_u32_precompute40_probe);
  CHECK(saw_u32_precompute1000_probe);
  CHECK(saw_u32_precompute8192_probe);
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
  char *json = xray_benchmark_report_json(report);
  CHECK(json != NULL);
  CHECK(strstr(json, "\"replacementReady\"") != NULL);
  CHECK(strstr(json, "\"stableSampleCount\"") != NULL);
  CHECK(strstr(json, "\"sampleCount\"") != NULL);
  CHECK(strstr(json, "\"worstPairRatio\"") != NULL);
  CHECK(strstr(json, "\"baselineBackend\"") != NULL);
  CHECK(strstr(json, "\"baselineBackendVersion\"") != NULL);
  CHECK(strstr(json, "\"baselineBackendLibrary\"") != NULL);
  CHECK(strstr(json, "\"scratchRouteConfig\"") != NULL);
  CHECK(strstr(json, "\"karatsubaThresholdLimbs\"") != NULL);
  CHECK(strstr(json, "\"decimalHornerMinLimbs\"") != NULL);
  CHECK(strstr(json, "\"mulUnroll4RouteEnabled\"") != NULL);
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
  CHECK(strstr(json, "mul-toom3") != NULL);
  CHECK(strstr(json, "mod-u32-precompute") != NULL);
  CHECK(strstr(json, "gcd-u32-precompute") != NULL);
  CHECK(strstr(json, "powmod-u32-precompute") != NULL);
  CHECK(strstr(json, "\"operation\":\"format\"") != NULL);
  CHECK(strstr(json, "format-threshold") != NULL);
  CHECK(strstr(json, "format-divider") != NULL);
  CHECK(strstr(json, "format-folded") != NULL);
  CHECK(strstr(json, "format-pair-writer") != NULL);
  CHECK(strstr(json, "format-wide") != NULL);
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
  CHECK(strstr(tsv, "factor-benchmark") != NULL);
  CHECK(strstr(tsv, "cyclotomic-benchmark") != NULL);
  CHECK(strstr(tsv, "scratch-vs-gmp") != NULL);
  CHECK(strstr(tsv, "kernel-probe") != NULL);
  CHECK(strstr(tsv, "gmpClue=") != NULL);
  CHECK(strstr(tsv, "mul-toom3") != NULL);
  CHECK(strstr(tsv, "mod-u32-precompute") != NULL);
  CHECK(strstr(tsv, "gcd-u32-precompute") != NULL);
  CHECK(strstr(tsv, "powmod-u32-precompute") != NULL);
  CHECK(strstr(tsv, "format") != NULL);
  CHECK(strstr(tsv, "format-threshold") != NULL);
  CHECK(strstr(tsv, "format-divider") != NULL);
  CHECK(strstr(tsv, "format-folded") != NULL);
  CHECK(strstr(tsv, "format-pair-writer") != NULL);
  CHECK(strstr(tsv, "format-wide") != NULL);
  CHECK(strstr(tsv, "square") != NULL);
  CHECK(strstr(tsv, "square-vs-mul") != NULL);
  CHECK(strstr(tsv, "square-karatsuba-vs-mul") != NULL);
  CHECK(strstr(tsv, "square-karatsuba-vs-gmp") != NULL);
  CHECK(strstr(tsv, "mul-toom3-vs-scratch") != NULL);
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
  CHECK(strstr(workbench.events_jsonl, "\"stage\":\"benchmark\"") != NULL);
  CHECK(strstr(workbench.events_jsonl, "\"stage\":\"cpu\"") != NULL);
  char *benchmark_json_path = test_path_join(workbench.run_dir, "benchmark.json");
  char *benchmark_tsv_path = test_path_join(workbench.run_dir, "benchmark.tsv");
  char *benchmark_frontier_path = test_path_join(workbench.run_dir, "benchmark_frontier.txt");
  char *cpu_path = test_path_join(workbench.run_dir, "cpu_features.txt");
  char *benchmark_json = read_text_file(benchmark_json_path);
  char *benchmark_tsv = read_text_file(benchmark_tsv_path);
  char *benchmark_frontier = read_text_file(benchmark_frontier_path);
  char *cpu_text = read_text_file(cpu_path);
  CHECK(strstr(benchmark_json, "\"benchmarkReport\"") != NULL);
  CHECK(strstr(benchmark_json, "\"cpu\"") != NULL);
  CHECK(strstr(benchmark_json, "\"baselineBackend\"") != NULL);
  CHECK(strstr(benchmark_json, "\"baselineBackendVersion\"") != NULL);
  CHECK(strstr(benchmark_json, "\"baselineBackendLibrary\"") != NULL);
  CHECK(strstr(benchmark_json, "\"scratchRouteConfig\"") != NULL);
  CHECK(strstr(benchmark_json, "\"mulUnroll4RouteMaxLimbs\"") != NULL);
  CHECK(strstr(benchmark_json, "\"msvcUint128Helpers\"") != NULL);
  CHECK(strstr(benchmark_json, "\"scratchRows\"") != NULL);
  CHECK(strstr(benchmark_tsv, "scratch-vs-gmp") != NULL);
  CHECK(strstr(benchmark_tsv, "kernel-probe") != NULL);
  CHECK(strstr(benchmark_tsv, "mul-toom3") != NULL);
  CHECK(strstr(benchmark_tsv, "mod-u32-precompute") != NULL);
  CHECK(strstr(benchmark_tsv, "gcd-u32-precompute") != NULL);
  CHECK(strstr(benchmark_tsv, "powmod-u32-precompute") != NULL);
  CHECK(strstr(benchmark_tsv, "format") != NULL);
  CHECK(strstr(benchmark_tsv, "format-threshold") != NULL);
  CHECK(strstr(benchmark_tsv, "format-divider") != NULL);
  CHECK(strstr(benchmark_tsv, "format-folded") != NULL);
  CHECK(strstr(benchmark_tsv, "format-pair-writer") != NULL);
  CHECK(strstr(benchmark_tsv, "format-wide") != NULL);
  CHECK(strstr(benchmark_tsv, "square") != NULL);
  CHECK(strstr(benchmark_tsv, "square-vs-mul") != NULL);
  CHECK(strstr(benchmark_tsv, "square-karatsuba-vs-mul") != NULL);
  CHECK(strstr(benchmark_tsv, "square-karatsuba-vs-gmp") != NULL);
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
  CHECK(strstr(benchmark_frontier, "Baseline backend:") != NULL);
  CHECK(strstr(benchmark_frontier, "Bigint route:") != NULL);
  CHECK(strstr(benchmark_frontier, "FRONTIER SUMMARY") != NULL);
  CHECK(strstr(benchmark_frontier, "Largest scratch gaps") != NULL);
  CHECK(strstr(benchmark_frontier, "SCRATCH VS ") != NULL);
  CHECK(strstr(benchmark_frontier, "mul-threshold thr=") != NULL);
  CHECK(strstr(benchmark_frontier, "mod-u32-precompute") != NULL);
  CHECK(strstr(benchmark_frontier, "gcd-u32-precompute") != NULL);
  CHECK(strstr(benchmark_frontier, "powmod-u32-precompute") != NULL);
  CHECK(strstr(benchmark_frontier, "format-threshold thr=16") != NULL);
  CHECK(strstr(benchmark_frontier, "format-threshold thr=32") != NULL);
  CHECK(strstr(benchmark_frontier, "format-threshold thr=48") != NULL);
  CHECK(strstr(benchmark_frontier, "format-threshold thr=64") != NULL);
  CHECK(strstr(benchmark_frontier, "format-threshold thr=96") != NULL);
  CHECK(strstr(benchmark_frontier, "format-threshold thr=128") != NULL);
  CHECK(strstr(benchmark_frontier, "format-divider mode=direct128") != NULL);
  CHECK(strstr(benchmark_frontier, "format-folded") != NULL);
  CHECK(strstr(benchmark_frontier, "format-pair-writer") != NULL);
  CHECK(strstr(benchmark_frontier, "format-wide") != NULL);
  CHECK(strstr(benchmark_frontier, "leaf=64") != NULL);
  CHECK(strstr(benchmark_frontier, "base=") != NULL);
#if defined(_MSC_VER) && defined(_M_X64)
  CHECK(strstr(benchmark_frontier, "depth=2") != NULL);
#endif
  CHECK(strstr(benchmark_frontier, "format") != NULL);
  CHECK(strstr(benchmark_frontier, "flags=") != NULL);
  CHECK(strstr(cpu_text, "CPU:") != NULL);
  CHECK(strstr(cpu_text, "flags=") != NULL);
  free(benchmark_json_path);
  free(benchmark_tsv_path);
  free(benchmark_frontier_path);
  free(cpu_path);
  free(benchmark_json);
  free(benchmark_tsv);
  free(benchmark_frontier);
  free(cpu_text);
  xray_workbench_report_clear(&workbench);
}

int main(void) {
  test_parse_messy_input();
  test_public_allocator_contract();
  test_runtime_version_contract();
  test_exact_expression_parser();
  test_cpu_feature_detection();
  test_scratch_bigint_oracle();
  test_decimal_ffi_helpers();
  test_scratch_bigint_oracle_sweep();
  test_scratch_bigint_large_mul_oracle();
  test_scratch_bigint_square_oracle();
  test_scratch_bigint_karatsuba_middle_signs();
  test_scratch_bigint_mul_thresholds();
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
