#include "xray_workbench.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
#include <intrin.h>
#define XRAY_HAS_MSVC_CARRY_INTRINSICS 1
#else
#define XRAY_HAS_MSVC_CARRY_INTRINSICS 0
#endif

#if defined(_MSC_VER) && defined(_M_X64)
#include <immintrin.h>
#define XRAY_HAS_MSVC_BMI2_ADX_INTRINSICS 1
#else
#define XRAY_HAS_MSVC_BMI2_ADX_INTRINSICS 0
#endif

#if defined(_MSC_VER) && defined(_M_X64)
#define XRAY_HAS_MUL_UNROLL4_POLICY_PROBES 1
#else
#define XRAY_HAS_MUL_UNROLL4_POLICY_PROBES 0
#endif

#define XRAY_BENCH_SAMPLES 5
#define XRAY_BENCH_DEEP_SAMPLES 9
#define XRAY_BENCH_MAX_SAMPLES 9
#define XRAY_KERNEL_SAMPLES 5
#define XRAY_MUL_OPERAND_FAMILIES 2
#define XRAY_SCRATCH_REQUIRED_STABLE_SAMPLES 4
#define XRAY_KERNEL_REQUIRED_STABLE_SAMPLES 4

static volatile unsigned long long kernel_probe_sink = 0;

typedef struct XrayMulOperandFamily {
  unsigned int left_seed;
  unsigned int right_seed;
  int left_high_lead;
  int right_high_lead;
} XrayMulOperandFamily;

static const XrayMulOperandFamily mul_operand_families[XRAY_MUL_OPERAND_FAMILIES] = {
  {5, 11, 1, 0},
  {29, 37, 1, 1}
};

static int text_has_token(const char *text, const char *token) {
  return text && token && strstr(text, token) != NULL;
}

int xray_benchmark_result_is_promotion_ready(const XrayBenchmarkResult *result) {
  if (!result) return 0;
  return result->replacement_ready ||
    strcmp(result->adoption, "allowed") == 0 ||
    strcmp(result->adoption, "promotion-ready") == 0 ||
    text_has_token(result->adoption, "promote") ||
    strcmp(result->status, "replacement-ready") == 0 ||
    strcmp(result->status, "policy-ready") == 0 ||
    text_has_token(result->status, "promote");
}

int xray_benchmark_result_is_oracle_only(const XrayBenchmarkResult *result) {
  if (!result) return 0;
  return strcmp(result->adoption, "oracle-only") == 0 ||
    strcmp(result->status, "oracle-only") == 0;
}

int xray_benchmark_result_is_safety_rejected(const XrayBenchmarkResult *result) {
  if (!result) return 0;
  return text_has_token(result->status, "safety-rejected") ||
    text_has_token(result->status, "safety-blocked") ||
    text_has_token(result->status, "regression") ||
    text_has_token(result->status, "neighbor") ||
    text_has_token(result->status, "blocked") ||
    text_has_token(result->status, "mismatch") ||
    text_has_token(result->adoption, "safety-rejected") ||
    text_has_token(result->adoption, "safety-blocked") ||
    text_has_token(result->adoption, "regression") ||
    text_has_token(result->adoption, "neighbor") ||
    text_has_token(result->adoption, "blocked") ||
    text_has_token(result->adoption, "mismatch");
}

void xray_benchmark_result_brief(const XrayBenchmarkResult *result, size_t result_index, char *out, size_t out_size) {
  if (!out || out_size == 0) return;
  if (!result) {
    snprintf(out, out_size, "No benchmark row.");
    return;
  }
  const char *category = result->category[0] ? result->category : "benchmark";
  const char *operation = result->operation[0] ? result->operation : (result->name[0] ? result->name : "unknown");
  if (result_index) {
    snprintf(out, out_size, "#%zu %s %s d=%zu r=%.3f s=%zu/%zu",
      result_index,
      category,
      operation,
      result->digits,
      result->speed_ratio,
      result->stable_sample_count,
      result->sample_count);
  } else {
    snprintf(out, out_size, "%s %s d=%zu r=%.3f s=%zu/%zu",
      category,
      operation,
      result->digits,
      result->speed_ratio,
      result->stable_sample_count,
      result->sample_count);
  }
}

static void note_lane_result(XrayBenchmarkReport *report, const XrayBenchmarkResult *result, size_t result_index) {
  if (!report || !result) return;
  if (xray_benchmark_result_is_promotion_ready(result)) {
    report->lanes.promotion_ready_count++;
    xray_benchmark_result_brief(result, result_index, report->lanes.promotion_ready_detail, sizeof(report->lanes.promotion_ready_detail));
  }
  if (xray_benchmark_result_is_oracle_only(result)) {
    report->lanes.oracle_only_count++;
    xray_benchmark_result_brief(result, result_index, report->lanes.oracle_only_detail, sizeof(report->lanes.oracle_only_detail));
  }
  if (xray_benchmark_result_is_safety_rejected(result)) {
    report->lanes.safety_rejected_count++;
    xray_benchmark_result_brief(result, result_index, report->lanes.safety_rejected_detail, sizeof(report->lanes.safety_rejected_detail));
  }
}

static void append_result(XrayBenchmarkReport *report, const XrayBenchmarkResult *result) {
  XrayBenchmarkResult *next = (XrayBenchmarkResult *)realloc(report->results, sizeof(XrayBenchmarkResult) * (report->result_count + 1));
  if (!next) return;
  report->results = next;
  size_t row_index = report->result_count;
  report->results[report->result_count++] = *result;
  XrayBenchmarkResult *row = &report->results[row_index];
  if (result->passed) report->passed_count++;
  if (strcmp(result->category, "scratch-vs-gmp") == 0) {
    report->scratch_count++;
    if (strcmp(result->adoption, "allowed") == 0) report->replacement_ready_count++;
    else if (strcmp(result->adoption, "oracle-only") == 0) report->oracle_only_count++;
    else report->blocked_count++;
  }
  note_lane_result(report, row, row_index + 1U);
  if (report->result_callback) {
    report->result_callback(row, row_index + 1U, report->result_callback_user_data);
  }
}

const char *xray_scratch_adoption_for_result(const XrayBenchmarkResult *result) {
  if (!result || !result->parity_verified) return "blocked-output-mismatch";
  double limit = result->max_allowed_speed_ratio > 0.0 ? result->max_allowed_speed_ratio : 1.0;
  size_t required = result->sample_count < XRAY_SCRATCH_REQUIRED_STABLE_SAMPLES ?
    result->sample_count :
    XRAY_SCRATCH_REQUIRED_STABLE_SAMPLES;
  if (result->speed_ratio <= limit &&
      (result->worst_pair_ratio <= 0.0 || result->worst_pair_ratio <= 1.0) &&
      (required == 0 || result->stable_sample_count >= required)) return "allowed";
  return "oracle-only";
}

static int xray_no_worst_pair_regression(double worst_pair_ratio) {
  return worst_pair_ratio <= 0.0 || worst_pair_ratio <= 1.0;
}

static int xray_benchmark_readiness_gate(
  double speed_ratio,
  double max_allowed_speed_ratio,
  size_t stable_sample_count,
  size_t required_stable,
  double worst_pair_ratio) {
  double limit = max_allowed_speed_ratio > 0.0 ? max_allowed_speed_ratio : 1.0;
  return speed_ratio <= limit &&
    xray_no_worst_pair_regression(worst_pair_ratio) &&
    stable_sample_count >= required_stable;
}

static size_t kernel_required_stable_samples(size_t sample_count) {
  if (sample_count == 0) return 0;
  if (sample_count > XRAY_BENCH_SAMPLES) return sample_count - 1U;
  return sample_count < XRAY_KERNEL_REQUIRED_STABLE_SAMPLES ?
    sample_count :
    XRAY_KERNEL_REQUIRED_STABLE_SAMPLES;
}

static unsigned long long median_samples(const unsigned long long *samples, size_t count) {
  unsigned long long sorted[XRAY_BENCH_MAX_SAMPLES] = {0};
  if (!samples || count == 0 || count > XRAY_BENCH_MAX_SAMPLES) return 0;
  for (size_t index = 0; index < count; ++index) sorted[index] = samples[index];
  for (size_t index = 1; index < count; ++index) {
    unsigned long long value = sorted[index];
    size_t pos = index;
    while (pos > 0 && sorted[pos - 1] > value) {
      sorted[pos] = sorted[pos - 1];
      pos--;
    }
    sorted[pos] = value;
  }
  return sorted[count / 2];
}

static double median_paired_ratio(const unsigned long long *numerator, const unsigned long long *denominator, size_t count) {
  double ratios[XRAY_BENCH_MAX_SAMPLES] = {0.0};
  if (!numerator || !denominator || count == 0 || count > XRAY_BENCH_MAX_SAMPLES) return 0.0;
  for (size_t index = 0; index < count; ++index) {
    ratios[index] = (double)(numerator[index] ? numerator[index] : 1ULL) /
      (double)(denominator[index] ? denominator[index] : 1ULL);
  }
  for (size_t index = 1; index < count; ++index) {
    double value = ratios[index];
    size_t pos = index;
    while (pos > 0 && ratios[pos - 1] > value) {
      ratios[pos] = ratios[pos - 1];
      pos--;
    }
    ratios[pos] = value;
  }
  return ratios[count / 2];
}

static double max_paired_ratio(const unsigned long long *numerator, const unsigned long long *denominator, size_t count) {
  double worst = 0.0;
  if (!numerator || !denominator || count == 0) return 0.0;
  for (size_t index = 0; index < count; ++index) {
    double ratio = (double)(numerator[index] ? numerator[index] : 1ULL) /
      (double)(denominator[index] ? denominator[index] : 1ULL);
    if (ratio > worst) worst = ratio;
  }
  return worst;
}

static size_t paired_ratio_wins(
  const unsigned long long *numerator,
  const unsigned long long *denominator,
  size_t count,
  double limit) {
  size_t wins = 0;
  if (!numerator || !denominator || count == 0) return 0;
  for (size_t index = 0; index < count; ++index) {
    double ratio = (double)(numerator[index] ? numerator[index] : 1ULL) /
      (double)(denominator[index] ? denominator[index] : 1ULL);
    if (ratio <= limit) wins++;
  }
  return wins;
}

static void fill_kernel_u32(uint32_t *words, size_t count, uint32_t seed) {
  uint32_t state = seed ? seed : 1U;
  for (size_t index = 0; index < count; ++index) {
    state = state * 1664525U + 1013904223U;
    words[index] = state ^ (uint32_t)(index * 2654435761U);
  }
  if (count > 4) {
    words[1] = 0xffffffffU;
    words[2] = 0xffffffffU;
    words[count / 2] ^= 0x80000000U;
    words[count - 1] |= 0x40000000U;
  }
}

static void fill_kernel_u64(uint64_t *words, size_t count, uint64_t seed) {
  uint64_t state = seed ? seed : UINT64_C(0x9e3779b97f4a7c15);
  for (size_t index = 0; index < count; ++index) {
    state = state * UINT64_C(2862933555777941757) + UINT64_C(3037000493);
    words[index] = state ^ (UINT64_C(0xbf58476d1ce4e5b9) * (uint64_t)(index + 1));
  }
  if (count > 4) {
    words[1] = UINT64_MAX;
    words[2] ^= UINT64_C(0x8000000000000000);
    words[count / 2] |= UINT64_C(0x4000000000000000);
    words[count - 1] |= UINT64_C(0x2000000000000000);
  }
}

static void pack_kernel_u64(uint64_t *out, const uint32_t *words, size_t word_count) {
  size_t out_count = word_count / 2;
  for (size_t index = 0; index < out_count; ++index) {
    out[index] = (uint64_t)words[index * 2] | ((uint64_t)words[index * 2 + 1] << 32);
  }
}

static int compare_u32_to_u64_words(
  const uint32_t *words32,
  uint32_t carry32,
  const uint64_t *words64,
  uint64_t carry64,
  size_t word_count32) {
  if ((carry32 ? 1U : 0U) != (carry64 ? 1U : 0U)) return 0;
  for (size_t index = 0; index < word_count32 / 2; ++index) {
    uint32_t low = (uint32_t)words64[index];
    uint32_t high = (uint32_t)(words64[index] >> 32);
    if (words32[index * 2] != low || words32[index * 2 + 1] != high) return 0;
  }
  return 1;
}

static uint32_t kernel_add32_scalar(uint32_t *out, const uint32_t *left, const uint32_t *right, size_t count) {
  uint64_t carry = 0;
  for (size_t index = 0; index < count; ++index) {
    uint64_t sum = (uint64_t)left[index] + right[index] + carry;
    out[index] = (uint32_t)sum;
    carry = sum >> 32;
  }
  return (uint32_t)carry;
}

static uint32_t kernel_sub32_scalar(uint32_t *out, const uint32_t *left, const uint32_t *right, size_t count) {
  uint64_t borrow = 0;
  for (size_t index = 0; index < count; ++index) {
    uint64_t lhs = left[index];
    uint64_t rhs = (uint64_t)right[index] + borrow;
    out[index] = (uint32_t)(lhs - rhs);
    borrow = lhs < rhs;
  }
  return (uint32_t)borrow;
}

static uint64_t kernel_add64_scalar(uint64_t *out, const uint64_t *left, const uint64_t *right, size_t count) {
  uint64_t carry = 0;
  for (size_t index = 0; index < count; ++index) {
    uint64_t sum = left[index] + right[index];
    uint64_t carry_from_sum = sum < left[index];
    uint64_t with_carry = sum + carry;
    out[index] = with_carry;
    carry = carry_from_sum || (with_carry < sum);
  }
  return carry;
}

static uint64_t kernel_sub64_scalar(uint64_t *out, const uint64_t *left, const uint64_t *right, size_t count) {
  uint64_t borrow = 0;
  for (size_t index = 0; index < count; ++index) {
    uint64_t subtrahend = right[index] + borrow;
    uint64_t borrow_from_add = subtrahend < right[index];
    out[index] = left[index] - subtrahend;
    borrow = borrow_from_add || (left[index] < subtrahend);
  }
  return borrow;
}

#if XRAY_HAS_MSVC_CARRY_INTRINSICS
static uint32_t kernel_add32_intrinsic(uint32_t *out, const uint32_t *left, const uint32_t *right, size_t count) {
  unsigned char carry = 0;
  for (size_t index = 0; index < count; ++index) {
    unsigned int word = 0;
    carry = _addcarry_u32(carry, left[index], right[index], &word);
    out[index] = (uint32_t)word;
  }
  return (uint32_t)carry;
}

static uint32_t kernel_sub32_intrinsic(uint32_t *out, const uint32_t *left, const uint32_t *right, size_t count) {
  unsigned char borrow = 0;
  for (size_t index = 0; index < count; ++index) {
    unsigned int word = 0;
    borrow = _subborrow_u32(borrow, left[index], right[index], &word);
    out[index] = (uint32_t)word;
  }
  return (uint32_t)borrow;
}
#endif

static unsigned int kernel_iterations(size_t bits) {
  if (bits <= 2048) return 220000;
  if (bits <= 16384) return 26000;
  return 6000;
}

static void append_kernel_probe_result(
  XrayBenchmarkReport *report,
  const char *name,
  const char *operation,
  size_t bits,
  int parity,
  unsigned long long candidate_us,
  unsigned long long baseline_us,
  double paired_ratio,
  size_t stable_sample_count,
  size_t sample_count,
  double worst_pair_ratio,
  const char *candidate,
  const char *baseline,
  const char *feature_gate,
  const char *gmp_clue) {
  XrayBenchmarkResult result;
  memset(&result, 0, sizeof(result));
  snprintf(result.name, sizeof(result.name), "%s", name);
  snprintf(result.category, sizeof(result.category), "kernel-probe");
  snprintf(result.operation, sizeof(result.operation), "%s", operation);
  result.digits = (bits * 30103U + 99999U) / 100000U;
  result.scratch_us = candidate_us ? candidate_us : 1;
  result.gmp_us = baseline_us ? baseline_us : 1;
  result.speed_ratio = paired_ratio > 0.0 ? paired_ratio : (double)result.scratch_us / (double)result.gmp_us;
  result.max_allowed_speed_ratio = 0.98;
  result.stable_sample_count = stable_sample_count;
  result.sample_count = sample_count;
  result.worst_pair_ratio = worst_pair_ratio;
  result.parity_verified = parity;
  size_t required_stable = kernel_required_stable_samples(sample_count);
  result.replacement_ready = parity &&
    xray_benchmark_readiness_gate(
      result.speed_ratio,
      result.max_allowed_speed_ratio,
      result.stable_sample_count,
      required_stable,
      result.worst_pair_ratio);
  snprintf(result.adoption, sizeof(result.adoption), "%s",
    !parity ? "blocked-output-mismatch" : (result.replacement_ready ? "promote-candidate" : "observe-only"));
  snprintf(result.status, sizeof(result.status), "%s",
    !parity ? "mismatch" : (result.replacement_ready ? "candidate-faster" : (result.speed_ratio < 1.0 ? "candidate-no-margin" : "baseline-faster")));
  result.passed = 1;
  result.elapsed_ms = (unsigned long)((result.scratch_us + result.gmp_us + 999ULL) / 1000ULL);
  snprintf(result.detail, sizeof(result.detail),
    "op=%s bits=%zu samples=%zu stablePairs=%zu/%zu candidate=%s baseline=%s candUs=%llu baseUs=%llu ratio=%.3f worstPairRatio=%.3f ratioMethod=paired-median max=%.2f featureGate=%s gmpClue=%s adoption=%s",
    operation,
    bits,
    sample_count,
    stable_sample_count,
    sample_count,
    candidate,
    baseline,
    result.scratch_us,
    result.gmp_us,
    result.speed_ratio,
    result.worst_pair_ratio,
    result.max_allowed_speed_ratio,
    feature_gate,
    gmp_clue,
    result.adoption);
  append_result(report, &result);
}

typedef struct XrayQhatCase {
  uint64_t high;
  uint64_t low;
  uint64_t divisor_top;
  uint64_t divisor_next;
  uint64_t numerator_next;
  uint64_t divisor_inverse;
  uint64_t direct_qhat;
  uint64_t direct_rhat;
} XrayQhatCase;

static uint64_t bench_mul_high_u64(uint64_t left, uint64_t right) {
#if defined(_MSC_VER) && defined(_M_X64)
  unsigned __int64 high = 0;
  (void)_umul128(left, right, &high);
  return (uint64_t)high;
#else
  return (uint64_t)(((__uint128_t)left * (__uint128_t)right) >> 64);
#endif
}

static int bench_product_gt_two_limb(uint64_t left, uint64_t right, uint64_t high, uint64_t low) {
  uint64_t product_high = bench_mul_high_u64(left, right);
  uint64_t product_low = left * right;
  return product_high > high || (product_high == high && product_low > low);
}

static uint64_t bench_divmod_u64_direct(uint64_t high, uint64_t low, uint64_t divisor, uint64_t *remainder) {
#if defined(_MSC_VER) && defined(_M_X64)
  unsigned __int64 rem = 0;
  unsigned __int64 quotient = _udiv128(
    (unsigned __int64)high,
    (unsigned __int64)low,
    (unsigned __int64)divisor,
    &rem);
  if (remainder) *remainder = (uint64_t)rem;
  return (uint64_t)quotient;
#else
  __uint128_t numerator = ((__uint128_t)high << 64U) | (__uint128_t)low;
  if (remainder) *remainder = (uint64_t)(numerator % divisor);
  return (uint64_t)(numerator / divisor);
#endif
}

static uint64_t bench_invert_limb_u64(uint64_t divisor) {
  return bench_divmod_u64_direct(UINT64_MAX - divisor, UINT64_MAX, divisor, NULL);
}

static uint64_t bench_divmod_u64_preinv(
  uint64_t high,
  uint64_t low,
  uint64_t divisor,
  uint64_t inverse,
  uint64_t *remainder) {
  uint64_t qhat = high + bench_mul_high_u64(high, inverse);
  uint64_t product_high = 0;
  uint64_t product_low = 0;

  for (;;) {
    product_high = bench_mul_high_u64(qhat, divisor);
    product_low = qhat * divisor;
    if (product_high < high || (product_high == high && product_low <= low)) break;
    qhat--;
  }

  uint64_t borrow = low < product_low ? 1U : 0U;
  uint64_t rem_low = low - product_low;
  uint64_t rem_high = high - product_high - borrow;
  while (rem_high || rem_low >= divisor) {
    uint64_t old_low = rem_low;
    rem_low -= divisor;
    if (old_low < divisor) rem_high--;
    qhat++;
  }

  if (remainder) *remainder = rem_low;
  return qhat;
}

static uint32_t bench_divmod_u32_direct(uint32_t high, uint32_t low, uint32_t divisor, uint32_t *remainder) {
  uint64_t numerator = ((uint64_t)high << 32U) | low;
  uint32_t quotient = (uint32_t)(numerator / divisor);
  if (remainder) *remainder = (uint32_t)(numerator - (uint64_t)quotient * divisor);
  return quotient;
}

static int bench_product_gt_two_word32(uint32_t left, uint32_t right, uint32_t high, uint32_t low) {
  uint64_t product = (uint64_t)left * right;
  uint32_t product_high = (uint32_t)(product >> 32U);
  uint32_t product_low = (uint32_t)product;
  return product_high > high || (product_high == high && product_low > low);
}

static uint64_t bench_divmod_u64_via_u32(uint64_t high, uint64_t low, uint64_t divisor, uint64_t *remainder) {
  uint32_t v0 = (uint32_t)divisor;
  uint32_t v1 = (uint32_t)(divisor >> 32U);
  uint32_t u[4] = {
    (uint32_t)low,
    (uint32_t)(low >> 32U),
    (uint32_t)high,
    (uint32_t)(high >> 32U)
  };
  uint32_t q[2] = {0, 0};

  for (size_t jj = 2U; jj > 0; --jj) {
    size_t j = jj - 1U;
    uint32_t qhat = 0;
    uint32_t rhat = 0;
    int rhat_overflow = 0;
    if (u[j + 2U] == v1) {
      qhat = UINT32_MAX;
      uint64_t r = (uint64_t)u[j + 1U] + v1;
      rhat = (uint32_t)r;
      rhat_overflow = r > UINT32_MAX;
    } else {
      qhat = bench_divmod_u32_direct(u[j + 2U], u[j + 1U], v1, &rhat);
    }

    while (!rhat_overflow && bench_product_gt_two_word32(qhat, v0, rhat, u[j])) {
      qhat--;
      uint32_t old_rhat = rhat;
      rhat += v1;
      rhat_overflow = rhat < old_rhat;
    }

    uint64_t carry = 0;
    uint64_t borrow = 0;
    for (size_t index = 0; index < 2U; ++index) {
      uint64_t product = (uint64_t)(index == 0 ? v0 : v1) * qhat + carry;
      uint32_t product_low = (uint32_t)product;
      carry = product >> 32U;
      uint64_t subtrahend = (uint64_t)product_low + borrow;
      uint64_t word = u[j + index];
      u[j + index] = (uint32_t)(word - subtrahend);
      borrow = word < subtrahend;
    }
    uint64_t top_subtrahend = carry + borrow;
    uint64_t top_word = u[j + 2U];
    u[j + 2U] = (uint32_t)(top_word - top_subtrahend);
    if (top_word < top_subtrahend) {
      qhat--;
      uint64_t sum = (uint64_t)u[j] + v0;
      u[j] = (uint32_t)sum;
      uint64_t carry_back = sum >> 32U;
      sum = (uint64_t)u[j + 1U] + v1 + carry_back;
      u[j + 1U] = (uint32_t)sum;
      carry_back = sum >> 32U;
      u[j + 2U] = (uint32_t)((uint64_t)u[j + 2U] + carry_back);
    }
    q[j] = qhat;
  }

  if (remainder) *remainder = ((uint64_t)u[1] << 32U) | u[0];
  return ((uint64_t)q[1] << 32U) | q[0];
}

static void bench_qhat_apply_second_limb_correction(
  uint64_t *qhat,
  uint64_t *rhat,
  uint64_t divisor_top,
  uint64_t divisor_next,
  uint64_t numerator_next) {
  int rhat_overflow = 0;
  while (!rhat_overflow && bench_product_gt_two_limb(*qhat, divisor_next, *rhat, numerator_next)) {
    (*qhat)--;
    uint64_t old_rhat = *rhat;
    *rhat += divisor_top;
    rhat_overflow = *rhat < old_rhat;
  }
}

static void bench_qhat_direct(const XrayQhatCase *item, uint64_t *qhat, uint64_t *rhat) {
  *qhat = bench_divmod_u64_direct(item->high, item->low, item->divisor_top, rhat);
  bench_qhat_apply_second_limb_correction(qhat, rhat, item->divisor_top, item->divisor_next, item->numerator_next);
}

static void bench_qhat_preinv(const XrayQhatCase *item, uint64_t *qhat, uint64_t *rhat) {
  *qhat = bench_divmod_u64_preinv(item->high, item->low, item->divisor_top, item->divisor_inverse, rhat);
  bench_qhat_apply_second_limb_correction(qhat, rhat, item->divisor_top, item->divisor_next, item->numerator_next);
}

static void bench_qhat_u32_limb(const XrayQhatCase *item, uint64_t *qhat, uint64_t *rhat) {
  *qhat = bench_divmod_u64_via_u32(item->high, item->low, item->divisor_top, rhat);
  bench_qhat_apply_second_limb_correction(qhat, rhat, item->divisor_top, item->divisor_next, item->numerator_next);
}

static void fill_qhat_cases(XrayQhatCase *items, size_t count) {
  uint64_t state = UINT64_C(0x243f6a8885a308d3);
  for (size_t index = 0; index < count; ++index) {
    state = state * UINT64_C(2862933555777941757) + UINT64_C(3037000493);
    uint64_t divisor_top = state | UINT64_C(0x8000000000000000);
    state = state * UINT64_C(2862933555777941757) + UINT64_C(3037000493);
    uint64_t high = state % divisor_top;
    state = state * UINT64_C(2862933555777941757) + UINT64_C(3037000493);
    uint64_t low = state ^ (UINT64_C(0x9e3779b97f4a7c15) * (uint64_t)(index + 1U));
    state = state * UINT64_C(2862933555777941757) + UINT64_C(3037000493);
    uint64_t divisor_next = state | (UINT64_C(1) << 63U);
    state = state * UINT64_C(2862933555777941757) + UINT64_C(3037000493);
    uint64_t numerator_next = state;
    if ((index % 17U) == 0) high = divisor_top - 1U;
    if ((index % 31U) == 0) low = UINT64_MAX;

    items[index].high = high;
    items[index].low = low;
    items[index].divisor_top = divisor_top;
    items[index].divisor_next = divisor_next;
    items[index].numerator_next = numerator_next;
    items[index].divisor_inverse = bench_invert_limb_u64(divisor_top);
    bench_qhat_direct(&items[index], &items[index].direct_qhat, &items[index].direct_rhat);
  }
}

static void append_qhat_probe_result(
  XrayBenchmarkReport *report,
  const char *operation,
  const char *candidate,
  size_t case_count,
  unsigned int passes_per_sample,
  int parity,
  unsigned long long candidate_us,
  unsigned long long baseline_us,
  double paired_ratio,
  size_t stable_sample_count,
  size_t sample_count,
  double worst_pair_ratio) {
  XrayBenchmarkResult result;
  memset(&result, 0, sizeof(result));
  snprintf(result.name, sizeof(result.name), "kernel qhat estimator %s", candidate);
  snprintf(result.category, sizeof(result.category), "kernel-probe");
  snprintf(result.operation, sizeof(result.operation), "%s", operation);
  result.digits = 0;
  result.scratch_us = candidate_us ? candidate_us : 1;
  result.gmp_us = baseline_us ? baseline_us : 1;
  result.speed_ratio = paired_ratio > 0.0 ? paired_ratio : (double)result.scratch_us / (double)result.gmp_us;
  result.max_allowed_speed_ratio = 0.98;
  result.stable_sample_count = stable_sample_count;
  result.sample_count = sample_count;
  result.worst_pair_ratio = worst_pair_ratio;
  result.parity_verified = parity;
  int candidate_cleared_gate = parity &&
    xray_benchmark_readiness_gate(
      result.speed_ratio,
      result.max_allowed_speed_ratio,
      result.stable_sample_count,
      kernel_required_stable_samples(sample_count),
      result.worst_pair_ratio);
  result.replacement_ready = 0;
  snprintf(result.adoption, sizeof(result.adoption), "%s",
    !parity ? "blocked-output-mismatch" : "observe-only");
  snprintf(result.status, sizeof(result.status), "%s",
    !parity ? "mismatch" : (candidate_cleared_gate ? "candidate-faster" : (result.speed_ratio < 1.0 ? "candidate-no-margin" : "baseline-faster")));
  result.passed = parity;
  result.elapsed_ms = (unsigned long)((result.scratch_us + result.gmp_us + 999ULL) / 1000ULL);
  snprintf(result.detail, sizeof(result.detail),
    "op=%s cases=%zu passesPerSample=%u samples=%zu stablePairs=%zu/%zu candidateUs=%llu baselineUs=%llu ratio=%.3f worstPairRatio=%.3f ratioMethod=paired-median max=%.2f candidate=%s baseline=direct-udiv128-qhat parityTarget=qhat+rhat featureGate=division-qhat-estimator gmpClue=mpn_sbpi1_div_qr-qhat precomputeScope=per-divisor noAutoRoute=1 adoption=%s",
    operation,
    case_count,
    passes_per_sample,
    sample_count,
    stable_sample_count,
    sample_count,
    result.scratch_us,
    result.gmp_us,
    result.speed_ratio,
    result.worst_pair_ratio,
    result.max_allowed_speed_ratio,
    candidate,
    result.adoption);
  append_result(report, &result);
}

static void run_qhat_estimator_probe_case(XrayBenchmarkReport *report) {
  enum { qhat_case_count = 4096, passes_per_sample = 96 };
  XrayQhatCase *items = (XrayQhatCase *)calloc(qhat_case_count, sizeof(XrayQhatCase));
  if (!items) return;
  fill_qhat_cases(items, qhat_case_count);

  unsigned long long candidate_samples[XRAY_KERNEL_SAMPLES] = {0};
  unsigned long long preinv_samples[XRAY_KERNEL_SAMPLES] = {0};
  unsigned long long baseline_samples[XRAY_KERNEL_SAMPLES] = {0};
  int u32_parity = 1;
  int preinv_parity = 1;
  for (unsigned int sample = 0; sample < XRAY_KERNEL_SAMPLES; ++sample) {
    unsigned long long baseline_started = xray_now_us();
    for (unsigned int pass = 0; pass < passes_per_sample; ++pass) {
      for (size_t index = 0; index < qhat_case_count; ++index) {
        uint64_t qhat = 0;
        uint64_t rhat = 0;
        bench_qhat_direct(&items[index], &qhat, &rhat);
        kernel_probe_sink ^= qhat ^ rhat;
      }
    }
    baseline_samples[sample] = xray_now_us() - baseline_started;

    unsigned long long preinv_started = xray_now_us();
    for (unsigned int pass = 0; pass < passes_per_sample; ++pass) {
      for (size_t index = 0; index < qhat_case_count; ++index) {
        uint64_t qhat = 0;
        uint64_t rhat = 0;
        bench_qhat_preinv(&items[index], &qhat, &rhat);
        preinv_parity = preinv_parity &&
          qhat == items[index].direct_qhat &&
          rhat == items[index].direct_rhat;
        kernel_probe_sink ^= qhat ^ rhat;
      }
    }
    preinv_samples[sample] = xray_now_us() - preinv_started;

    unsigned long long candidate_started = xray_now_us();
    for (unsigned int pass = 0; pass < passes_per_sample; ++pass) {
      for (size_t index = 0; index < qhat_case_count; ++index) {
        uint64_t qhat = 0;
        uint64_t rhat = 0;
        bench_qhat_u32_limb(&items[index], &qhat, &rhat);
        u32_parity = u32_parity &&
          qhat == items[index].direct_qhat &&
          rhat == items[index].direct_rhat;
        kernel_probe_sink ^= qhat ^ rhat;
      }
    }
    candidate_samples[sample] = xray_now_us() - candidate_started;
  }

  append_qhat_probe_result(
    report,
    "qhat-u32-limb",
    "u32-limb-knuth-qhat",
    qhat_case_count,
    passes_per_sample,
    u32_parity,
    median_samples(candidate_samples, XRAY_KERNEL_SAMPLES),
    median_samples(baseline_samples, XRAY_KERNEL_SAMPLES),
    median_paired_ratio(candidate_samples, baseline_samples, XRAY_KERNEL_SAMPLES),
    paired_ratio_wins(candidate_samples, baseline_samples, XRAY_KERNEL_SAMPLES, 0.98),
    XRAY_KERNEL_SAMPLES,
    max_paired_ratio(candidate_samples, baseline_samples, XRAY_KERNEL_SAMPLES));
  append_qhat_probe_result(
    report,
    "qhat-preinv",
    "preinverted-limb-qhat",
    qhat_case_count,
    passes_per_sample,
    preinv_parity,
    median_samples(preinv_samples, XRAY_KERNEL_SAMPLES),
    median_samples(baseline_samples, XRAY_KERNEL_SAMPLES),
    median_paired_ratio(preinv_samples, baseline_samples, XRAY_KERNEL_SAMPLES),
    paired_ratio_wins(preinv_samples, baseline_samples, XRAY_KERNEL_SAMPLES, 0.98),
    XRAY_KERNEL_SAMPLES,
    max_paired_ratio(preinv_samples, baseline_samples, XRAY_KERNEL_SAMPLES));

  free(items);
}

static void run_kernel_probe64_case(XrayBenchmarkReport *report, const char *operation, size_t bits) {
  size_t limbs32 = bits / 32U;
  size_t limbs64 = bits / 64U;
  uint32_t *left32 = (uint32_t *)calloc(limbs32, sizeof(uint32_t));
  uint32_t *right32 = (uint32_t *)calloc(limbs32, sizeof(uint32_t));
  uint32_t *baseline32 = (uint32_t *)calloc(limbs32, sizeof(uint32_t));
  uint32_t *check32 = (uint32_t *)calloc(limbs32, sizeof(uint32_t));
  uint64_t *left64 = (uint64_t *)calloc(limbs64, sizeof(uint64_t));
  uint64_t *right64 = (uint64_t *)calloc(limbs64, sizeof(uint64_t));
  uint64_t *candidate64 = (uint64_t *)calloc(limbs64, sizeof(uint64_t));
  if (!left32 || !right32 || !baseline32 || !check32 || !left64 || !right64 || !candidate64) {
    free(left32); free(right32); free(baseline32); free(check32); free(left64); free(right64); free(candidate64);
    return;
  }

  fill_kernel_u32(left32, limbs32, 0x159a55e5U);
  fill_kernel_u32(right32, limbs32, 0x5eed1234U);
  if (strcmp(operation, "sub-carry") == 0) {
    for (size_t index = 0; index < limbs32; ++index) {
      left32[index] |= right32[index] & 0x7fffffffU;
    }
  }
  pack_kernel_u64(left64, left32, limbs32);
  pack_kernel_u64(right64, right32, limbs32);

  uint32_t baseline_carry = 0;
  uint64_t candidate_carry = 0;
  if (strcmp(operation, "add-carry") == 0) {
    baseline_carry = kernel_add32_scalar(baseline32, left32, right32, limbs32);
    candidate_carry = kernel_add64_scalar(candidate64, left64, right64, limbs64);
  } else {
    baseline_carry = kernel_sub32_scalar(baseline32, left32, right32, limbs32);
    candidate_carry = kernel_sub64_scalar(candidate64, left64, right64, limbs64);
  }
  int parity = compare_u32_to_u64_words(baseline32, baseline_carry, candidate64, candidate_carry, limbs32);

  unsigned int iterations = kernel_iterations(bits);
  unsigned long long candidate_samples[XRAY_KERNEL_SAMPLES] = {0};
  unsigned long long baseline_samples[XRAY_KERNEL_SAMPLES] = {0};
  for (unsigned int sample = 0; sample < XRAY_KERNEL_SAMPLES; ++sample) {
    unsigned long long baseline_started = xray_now_us();
    for (unsigned int index = 0; index < iterations; ++index) {
      if (strcmp(operation, "add-carry") == 0) baseline_carry = kernel_add32_scalar(check32, left32, right32, limbs32);
      else baseline_carry = kernel_sub32_scalar(check32, left32, right32, limbs32);
      kernel_probe_sink ^= (unsigned long long)check32[(index + sample) % limbs32] ^ baseline_carry;
    }
    baseline_samples[sample] = xray_now_us() - baseline_started;

    unsigned long long candidate_started = xray_now_us();
    for (unsigned int index = 0; index < iterations; ++index) {
      if (strcmp(operation, "add-carry") == 0) candidate_carry = kernel_add64_scalar(candidate64, left64, right64, limbs64);
      else candidate_carry = kernel_sub64_scalar(candidate64, left64, right64, limbs64);
      kernel_probe_sink ^= candidate64[(index + sample) % limbs64] ^ candidate_carry;
    }
    candidate_samples[sample] = xray_now_us() - candidate_started;
  }

  char name[64];
  snprintf(name, sizeof(name), "kernel %s 64-bit limbs %zu-bit", operation, bits);
  append_kernel_probe_result(
    report,
    name,
    operation,
    bits,
    parity,
    median_samples(candidate_samples, XRAY_KERNEL_SAMPLES),
    median_samples(baseline_samples, XRAY_KERNEL_SAMPLES),
    median_paired_ratio(candidate_samples, baseline_samples, XRAY_KERNEL_SAMPLES),
    paired_ratio_wins(candidate_samples, baseline_samples, XRAY_KERNEL_SAMPLES, 0.98),
    XRAY_KERNEL_SAMPLES,
    max_paired_ratio(candidate_samples, baseline_samples, XRAY_KERNEL_SAMPLES),
    "scalar64-limb",
    "scalar32-limb",
    "portable-c",
    "gmp64limbs");

  free(left32); free(right32); free(baseline32); free(check32); free(left64); free(right64); free(candidate64);
}

#if XRAY_HAS_MSVC_CARRY_INTRINSICS
static void run_kernel_probe_intrinsic_case(XrayBenchmarkReport *report, const char *operation, size_t bits) {
  size_t limbs = bits / 32U;
  uint32_t *left = (uint32_t *)calloc(limbs, sizeof(uint32_t));
  uint32_t *right = (uint32_t *)calloc(limbs, sizeof(uint32_t));
  uint32_t *baseline = (uint32_t *)calloc(limbs, sizeof(uint32_t));
  uint32_t *candidate = (uint32_t *)calloc(limbs, sizeof(uint32_t));
  if (!left || !right || !baseline || !candidate) {
    free(left); free(right); free(baseline); free(candidate);
    return;
  }

  fill_kernel_u32(left, limbs, 0x243f6a88U);
  fill_kernel_u32(right, limbs, 0x85a308d3U);
  if (strcmp(operation, "sub-carry") == 0) {
    for (size_t index = 0; index < limbs; ++index) {
      left[index] |= right[index] & 0x7fffffffU;
    }
  }

  uint32_t baseline_carry = 0;
  uint32_t candidate_carry = 0;
  if (strcmp(operation, "add-carry") == 0) {
    baseline_carry = kernel_add32_scalar(baseline, left, right, limbs);
    candidate_carry = kernel_add32_intrinsic(candidate, left, right, limbs);
  } else {
    baseline_carry = kernel_sub32_scalar(baseline, left, right, limbs);
    candidate_carry = kernel_sub32_intrinsic(candidate, left, right, limbs);
  }
  int parity = baseline_carry == candidate_carry && memcmp(baseline, candidate, sizeof(uint32_t) * limbs) == 0;

  unsigned int iterations = kernel_iterations(bits);
  unsigned long long candidate_samples[XRAY_KERNEL_SAMPLES] = {0};
  unsigned long long baseline_samples[XRAY_KERNEL_SAMPLES] = {0};
  for (unsigned int sample = 0; sample < XRAY_KERNEL_SAMPLES; ++sample) {
    unsigned long long baseline_started = xray_now_us();
    for (unsigned int index = 0; index < iterations; ++index) {
      if (strcmp(operation, "add-carry") == 0) baseline_carry = kernel_add32_scalar(baseline, left, right, limbs);
      else baseline_carry = kernel_sub32_scalar(baseline, left, right, limbs);
      kernel_probe_sink ^= (unsigned long long)baseline[(index + sample) % limbs] ^ baseline_carry;
    }
    baseline_samples[sample] = xray_now_us() - baseline_started;

    unsigned long long candidate_started = xray_now_us();
    for (unsigned int index = 0; index < iterations; ++index) {
      if (strcmp(operation, "add-carry") == 0) candidate_carry = kernel_add32_intrinsic(candidate, left, right, limbs);
      else candidate_carry = kernel_sub32_intrinsic(candidate, left, right, limbs);
      kernel_probe_sink ^= (unsigned long long)candidate[(index + sample) % limbs] ^ candidate_carry;
    }
    candidate_samples[sample] = xray_now_us() - candidate_started;
  }

  char name[64];
  snprintf(name, sizeof(name), "kernel %s MSVC carry %zu-bit", operation, bits);
  append_kernel_probe_result(
    report,
    name,
    operation,
    bits,
    parity,
    median_samples(candidate_samples, XRAY_KERNEL_SAMPLES),
    median_samples(baseline_samples, XRAY_KERNEL_SAMPLES),
    median_paired_ratio(candidate_samples, baseline_samples, XRAY_KERNEL_SAMPLES),
    paired_ratio_wins(candidate_samples, baseline_samples, XRAY_KERNEL_SAMPLES, 0.98),
    XRAY_KERNEL_SAMPLES,
    max_paired_ratio(candidate_samples, baseline_samples, XRAY_KERNEL_SAMPLES),
    strcmp(operation, "add-carry") == 0 ? "_addcarry_u32" : "_subborrow_u32",
    "scalar32-limb",
    "msvc-x86-intrinsic",
    "gmpCpuKernelsLocalWin");

  free(left); free(right); free(baseline); free(candidate);
}
#endif

#if XRAY_HAS_MSVC_BMI2_ADX_INTRINSICS
static uint64_t kernel_muladd_u64_msvc_carry(uint64_t *out, const uint64_t *existing, const uint64_t *right, uint64_t multiplier, size_t limbs) {
  uint64_t carry = 0;
  for (size_t index = 0; index < limbs; ++index) {
    unsigned __int64 high = 0;
    unsigned __int64 low = _umul128(right[index], multiplier, &high);
    unsigned __int64 sum = 0;
    unsigned char carry_out = _addcarry_u64(0, low, existing[index], &sum);
    high += carry_out;
    carry_out = _addcarry_u64(0, sum, carry, &sum);
    high += carry_out;
    out[index] = (uint64_t)sum;
    carry = (uint64_t)high;
  }
  return carry;
}

static uint64_t kernel_muladd_u64_msvc_carry_unroll4(uint64_t *out, const uint64_t *existing, const uint64_t *right, uint64_t multiplier, size_t limbs) {
  uint64_t carry = 0;
  size_t index = 0;
  for (; index + 4U <= limbs; index += 4U) {
    unsigned __int64 high0 = 0;
    unsigned __int64 low0 = _umul128(right[index], multiplier, &high0);
    unsigned __int64 sum0 = 0;
    unsigned char carry_out0 = _addcarry_u64(0, low0, existing[index], &sum0);
    high0 += carry_out0;
    carry_out0 = _addcarry_u64(0, sum0, carry, &sum0);
    high0 += carry_out0;
    out[index] = (uint64_t)sum0;
    carry = (uint64_t)high0;

    unsigned __int64 high1 = 0;
    unsigned __int64 low1 = _umul128(right[index + 1U], multiplier, &high1);
    unsigned __int64 sum1 = 0;
    unsigned char carry_out1 = _addcarry_u64(0, low1, existing[index + 1U], &sum1);
    high1 += carry_out1;
    carry_out1 = _addcarry_u64(0, sum1, carry, &sum1);
    high1 += carry_out1;
    out[index + 1U] = (uint64_t)sum1;
    carry = (uint64_t)high1;

    unsigned __int64 high2 = 0;
    unsigned __int64 low2 = _umul128(right[index + 2U], multiplier, &high2);
    unsigned __int64 sum2 = 0;
    unsigned char carry_out2 = _addcarry_u64(0, low2, existing[index + 2U], &sum2);
    high2 += carry_out2;
    carry_out2 = _addcarry_u64(0, sum2, carry, &sum2);
    high2 += carry_out2;
    out[index + 2U] = (uint64_t)sum2;
    carry = (uint64_t)high2;

    unsigned __int64 high3 = 0;
    unsigned __int64 low3 = _umul128(right[index + 3U], multiplier, &high3);
    unsigned __int64 sum3 = 0;
    unsigned char carry_out3 = _addcarry_u64(0, low3, existing[index + 3U], &sum3);
    high3 += carry_out3;
    carry_out3 = _addcarry_u64(0, sum3, carry, &sum3);
    high3 += carry_out3;
    out[index + 3U] = (uint64_t)sum3;
    carry = (uint64_t)high3;
  }
  for (; index < limbs; ++index) {
    unsigned __int64 high = 0;
    unsigned __int64 low = _umul128(right[index], multiplier, &high);
    unsigned __int64 sum = 0;
    unsigned char carry_out = _addcarry_u64(0, low, existing[index], &sum);
    high += carry_out;
    carry_out = _addcarry_u64(0, sum, carry, &sum);
    high += carry_out;
    out[index] = (uint64_t)sum;
    carry = (uint64_t)high;
  }
  return carry;
}

static uint64_t kernel_muladd_u64_msvc_carry_unroll8(uint64_t *out, const uint64_t *existing, const uint64_t *right, uint64_t multiplier, size_t limbs) {
  uint64_t carry = 0;
  size_t index = 0;
#define XRAY_MULADD_UNROLL8_STEP(offset) do { \
    unsigned __int64 high = 0; \
    unsigned __int64 low = _umul128(right[index + (offset)], multiplier, &high); \
    unsigned __int64 sum = 0; \
    unsigned char carry_out = _addcarry_u64(0, low, existing[index + (offset)], &sum); \
    high += carry_out; \
    carry_out = _addcarry_u64(0, sum, carry, &sum); \
    high += carry_out; \
    out[index + (offset)] = (uint64_t)sum; \
    carry = (uint64_t)high; \
  } while (0)
  for (; index + 8U <= limbs; index += 8U) {
    XRAY_MULADD_UNROLL8_STEP(0U);
    XRAY_MULADD_UNROLL8_STEP(1U);
    XRAY_MULADD_UNROLL8_STEP(2U);
    XRAY_MULADD_UNROLL8_STEP(3U);
    XRAY_MULADD_UNROLL8_STEP(4U);
    XRAY_MULADD_UNROLL8_STEP(5U);
    XRAY_MULADD_UNROLL8_STEP(6U);
    XRAY_MULADD_UNROLL8_STEP(7U);
  }
#undef XRAY_MULADD_UNROLL8_STEP
  for (; index < limbs; ++index) {
    unsigned __int64 high = 0;
    unsigned __int64 low = _umul128(right[index], multiplier, &high);
    unsigned __int64 sum = 0;
    unsigned char carry_out = _addcarry_u64(0, low, existing[index], &sum);
    high += carry_out;
    carry_out = _addcarry_u64(0, sum, carry, &sum);
    high += carry_out;
    out[index] = (uint64_t)sum;
    carry = (uint64_t)high;
  }
  return carry;
}

static uint64_t kernel_muladd_u64_bmi2_adx(uint64_t *out, const uint64_t *existing, const uint64_t *right, uint64_t multiplier, size_t limbs) {
  uint64_t carry = 0;
  for (size_t index = 0; index < limbs; ++index) {
    unsigned __int64 high = 0;
    unsigned __int64 low = _mulx_u64(right[index], multiplier, &high);
    unsigned __int64 sum = 0;
    unsigned char carry_out = _addcarryx_u64(0, low, existing[index], &sum);
    high += carry_out;
    carry_out = _addcarryx_u64(0, sum, carry, &sum);
    high += carry_out;
    out[index] = (uint64_t)sum;
    carry = (uint64_t)high;
  }
  return carry;
}

static void run_kernel_probe_muladd_unroll_case(XrayBenchmarkReport *report, size_t bits, unsigned int unroll) {
  size_t limbs = bits / 64U;
  uint64_t *existing = (uint64_t *)calloc(limbs, sizeof(uint64_t));
  uint64_t *right = (uint64_t *)calloc(limbs, sizeof(uint64_t));
  uint64_t *baseline = (uint64_t *)calloc(limbs, sizeof(uint64_t));
  uint64_t *candidate = (uint64_t *)calloc(limbs, sizeof(uint64_t));
  if (!existing || !right || !baseline || !candidate) {
    free(existing); free(right); free(baseline); free(candidate);
    return;
  }

  fill_kernel_u64(existing, limbs, UINT64_C(0x8cb92ba72f3d8dd7));
  fill_kernel_u64(right, limbs, UINT64_C(0x589965cc75374cc3));
  uint64_t multiplier = UINT64_C(0x9e3779b97f4a7c15);
  uint64_t baseline_carry = kernel_muladd_u64_msvc_carry(baseline, existing, right, multiplier, limbs);
  uint64_t candidate_carry = unroll == 8U ?
    kernel_muladd_u64_msvc_carry_unroll8(candidate, existing, right, multiplier, limbs) :
    kernel_muladd_u64_msvc_carry_unroll4(candidate, existing, right, multiplier, limbs);
  int parity = baseline_carry == candidate_carry && memcmp(baseline, candidate, sizeof(uint64_t) * limbs) == 0;

  unsigned int iterations = kernel_iterations(bits);
  unsigned long long candidate_samples[XRAY_KERNEL_SAMPLES] = {0};
  unsigned long long baseline_samples[XRAY_KERNEL_SAMPLES] = {0};
  for (unsigned int sample = 0; sample < XRAY_KERNEL_SAMPLES; ++sample) {
    unsigned long long baseline_started = xray_now_us();
    for (unsigned int index = 0; index < iterations; ++index) {
      baseline_carry = kernel_muladd_u64_msvc_carry(baseline, existing, right, multiplier + index + sample, limbs);
      kernel_probe_sink ^= baseline[(index + sample) % limbs] ^ baseline_carry;
    }
    baseline_samples[sample] = xray_now_us() - baseline_started;

    unsigned long long candidate_started = xray_now_us();
    for (unsigned int index = 0; index < iterations; ++index) {
      candidate_carry = unroll == 8U ?
        kernel_muladd_u64_msvc_carry_unroll8(candidate, existing, right, multiplier + index + sample, limbs) :
        kernel_muladd_u64_msvc_carry_unroll4(candidate, existing, right, multiplier + index + sample, limbs);
      kernel_probe_sink ^= candidate[(index + sample) % limbs] ^ candidate_carry;
    }
    candidate_samples[sample] = xray_now_us() - candidate_started;
  }

  char name[72];
  snprintf(name, sizeof(name), "kernel muladd unroll%u %zu-bit", unroll == 8U ? 8U : 4U, bits);
  char operation[32];
  snprintf(operation, sizeof(operation), "muladd-unroll%u", unroll == 8U ? 8U : 4U);
  char candidate_name[64];
  snprintf(candidate_name, sizeof(candidate_name), "_umul128+_addcarry_u64-unroll%u", unroll == 8U ? 8U : 4U);
  append_kernel_probe_result(
    report,
    name,
    operation,
    bits,
    parity,
    median_samples(candidate_samples, XRAY_KERNEL_SAMPLES),
    median_samples(baseline_samples, XRAY_KERNEL_SAMPLES),
    median_paired_ratio(candidate_samples, baseline_samples, XRAY_KERNEL_SAMPLES),
    paired_ratio_wins(candidate_samples, baseline_samples, XRAY_KERNEL_SAMPLES, 0.98),
    XRAY_KERNEL_SAMPLES,
    max_paired_ratio(candidate_samples, baseline_samples, XRAY_KERNEL_SAMPLES),
    candidate_name,
    "_umul128+_addcarry_u64",
    "msvc-x64-loop-schedule",
    "gmpAddmulScheduling");

  free(existing); free(right); free(baseline); free(candidate);
}

static void run_kernel_probe_muladd_bmi2_adx_case(XrayBenchmarkReport *report, size_t bits) {
  if (!report || !report->cpu.bmi2 || !report->cpu.adx) return;
  size_t limbs = bits / 64U;
  uint64_t *existing = (uint64_t *)calloc(limbs, sizeof(uint64_t));
  uint64_t *right = (uint64_t *)calloc(limbs, sizeof(uint64_t));
  uint64_t *baseline = (uint64_t *)calloc(limbs, sizeof(uint64_t));
  uint64_t *candidate = (uint64_t *)calloc(limbs, sizeof(uint64_t));
  if (!existing || !right || !baseline || !candidate) {
    free(existing); free(right); free(baseline); free(candidate);
    return;
  }

  fill_kernel_u64(existing, limbs, UINT64_C(0xd1b54a32d192ed03));
  fill_kernel_u64(right, limbs, UINT64_C(0xabc98388fb8fac03));
  uint64_t multiplier = UINT64_C(0xfedcba9876543211);
  uint64_t baseline_carry = kernel_muladd_u64_msvc_carry(baseline, existing, right, multiplier, limbs);
  uint64_t candidate_carry = kernel_muladd_u64_bmi2_adx(candidate, existing, right, multiplier, limbs);
  int parity = baseline_carry == candidate_carry && memcmp(baseline, candidate, sizeof(uint64_t) * limbs) == 0;

  unsigned int iterations = kernel_iterations(bits);
  unsigned long long candidate_samples[XRAY_KERNEL_SAMPLES] = {0};
  unsigned long long baseline_samples[XRAY_KERNEL_SAMPLES] = {0};
  for (unsigned int sample = 0; sample < XRAY_KERNEL_SAMPLES; ++sample) {
    unsigned long long baseline_started = xray_now_us();
    for (unsigned int index = 0; index < iterations; ++index) {
      baseline_carry = kernel_muladd_u64_msvc_carry(baseline, existing, right, multiplier + index + sample, limbs);
      kernel_probe_sink ^= baseline[(index + sample) % limbs] ^ baseline_carry;
    }
    baseline_samples[sample] = xray_now_us() - baseline_started;

    unsigned long long candidate_started = xray_now_us();
    for (unsigned int index = 0; index < iterations; ++index) {
      candidate_carry = kernel_muladd_u64_bmi2_adx(candidate, existing, right, multiplier + index + sample, limbs);
      kernel_probe_sink ^= candidate[(index + sample) % limbs] ^ candidate_carry;
    }
    candidate_samples[sample] = xray_now_us() - candidate_started;
  }

  char name[72];
  snprintf(name, sizeof(name), "kernel muladd BMI2 ADX %zu-bit", bits);
  append_kernel_probe_result(
    report,
    name,
    "muladd-bmi2-adx",
    bits,
    parity,
    median_samples(candidate_samples, XRAY_KERNEL_SAMPLES),
    median_samples(baseline_samples, XRAY_KERNEL_SAMPLES),
    median_paired_ratio(candidate_samples, baseline_samples, XRAY_KERNEL_SAMPLES),
    paired_ratio_wins(candidate_samples, baseline_samples, XRAY_KERNEL_SAMPLES, 0.98),
    XRAY_KERNEL_SAMPLES,
    max_paired_ratio(candidate_samples, baseline_samples, XRAY_KERNEL_SAMPLES),
    "_mulx_u64+_addcarryx_u64",
    "_umul128+_addcarry_u64",
    "msvc-x64-bmi2-adx",
    "gmpAddmulKernels");

  free(existing); free(right); free(baseline); free(candidate);
}
#endif

static void run_factor_case(XrayBenchmarkReport *report, const char *name, const char *input, const char *expected_status, unsigned long budget_ms) {
  XrayBenchmarkResult result;
  memset(&result, 0, sizeof(result));
  snprintf(result.name, sizeof(result.name), "%s", name);
  snprintf(result.category, sizeof(result.category), "factor-benchmark");
  snprintf(result.operation, sizeof(result.operation), "factor-solve");
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
  snprintf(result.category, sizeof(result.category), "cyclotomic-benchmark");
  snprintf(result.operation, sizeof(result.operation), "cyclotomic-eval");
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
    if (digits > 4096) return 80;
    return 160;
  }
  if (strcmp(operation, "format") == 0) {
    if (digits <= 40) return 4000;
    if (digits <= 150) return 1200;
    if (digits > 4096) return 80;
    return 160;
  }
  if (strcmp(operation, "mul") == 0) {
    if (digits <= 40) return 2000;
    if (digits <= 150) return 180;
    if (digits <= 1000) return 240;
    if (digits > 4096) return 32;
    return 80;
  }
  if (strcmp(operation, "powmod-u32") == 0) {
    if (digits <= 40) return 12000;
    if (digits <= 150) return 8000;
    if (digits > 4096) return 1200;
    return 2200;
  }
  if (strcmp(operation, "mod-u32") == 0 || strcmp(operation, "gcd-u32") == 0) {
    if (digits <= 40) return 30000;
    if (digits <= 150) return 10000;
    if (digits > 4096) return 1200;
    return 2200;
  }
  if (strcmp(operation, "divmod-bigint") == 0) {
    if (digits > 8192) return 8;
    if (digits > 4096) return 16;
    return 32;
  }
  if (strcmp(operation, "add") == 0 || strcmp(operation, "sub") == 0) {
    if (digits <= 40) return 20000;
    if (digits <= 150) return 8000;
    return 6400;
  }
  if (digits <= 40) return 20000;
  if (digits <= 150) return 8000;
  if (digits > 4096) return 800;
  return 1600;
}

static void append_perf_result(
  XrayBenchmarkReport *report,
  const char *operation,
  size_t digits,
  size_t operand_families,
  int parity,
  unsigned long long scratch_us,
  unsigned long long gmp_us,
  double paired_ratio,
  size_t stable_sample_count,
  size_t sample_count,
  double worst_pair_ratio) {
  XrayBenchmarkResult result;
  memset(&result, 0, sizeof(result));
  snprintf(result.name, sizeof(result.name), "scratch %s %zu digits", operation, digits);
  snprintf(result.category, sizeof(result.category), "scratch-vs-gmp");
  snprintf(result.operation, sizeof(result.operation), "%s", operation);
  result.digits = digits;
  result.scratch_us = scratch_us ? scratch_us : 1;
  result.gmp_us = gmp_us ? gmp_us : 1;
  result.speed_ratio = paired_ratio > 0.0 ? paired_ratio : (double)result.scratch_us / (double)result.gmp_us;
  result.max_allowed_speed_ratio = 1.0;
  result.stable_sample_count = stable_sample_count;
  result.sample_count = sample_count;
  result.worst_pair_ratio = worst_pair_ratio;
  result.parity_verified = parity;
  const char *adoption = xray_scratch_adoption_for_result(&result);
  result.replacement_ready = strcmp(adoption, "allowed") == 0;
  snprintf(result.adoption, sizeof(result.adoption), "%s", adoption);
  result.elapsed_ms = (unsigned long)((result.scratch_us + result.gmp_us + 999ULL) / 1000ULL);
  result.passed = parity;
  snprintf(result.status, sizeof(result.status), "%s",
    !parity ? "failed" : (result.replacement_ready ? "replacement-ready" : "parity"));
  snprintf(result.detail, sizeof(result.detail),
    "operation=%s digits=%zu operandFamilies=%zu samples=%zu stablePairs=%zu/%zu scratchUs=%llu gmpUs=%llu ratio=%.3f worstPairRatio=%.3f ratioMethod=paired-median maxAllowedRatio=%.1f adoption=%s",
    operation,
    digits,
    operand_families,
    sample_count,
    stable_sample_count,
    sample_count,
    result.scratch_us,
    result.gmp_us,
    result.speed_ratio,
    result.worst_pair_ratio,
    result.max_allowed_speed_ratio,
    result.adoption);
  append_result(report, &result);
}

static size_t policy_required_stable_samples(size_t sample_count) {
  if (sample_count == 0) return 0;
  if (sample_count > XRAY_BENCH_SAMPLES) return sample_count - 1U;
  return sample_count < XRAY_SCRATCH_REQUIRED_STABLE_SAMPLES ?
    sample_count :
    XRAY_SCRATCH_REQUIRED_STABLE_SAMPLES;
}

static void append_format_policy_probe_result(
  XrayBenchmarkReport *report,
  size_t digits,
  const char *policy,
  const char *candidate,
  const char *feature_gate,
  const char *gmp_clue,
  size_t min_digits,
  size_t max_digits,
  size_t leaf_chunks,
  int parity,
  unsigned long long candidate_us,
  unsigned long long gmp_us,
  double paired_ratio,
  size_t stable_sample_count,
  size_t sample_count,
  double worst_pair_ratio) {
  XrayBenchmarkResult result;
  memset(&result, 0, sizeof(result));
  snprintf(result.name, sizeof(result.name), "policy format %s %zu digits", policy, digits);
  snprintf(result.category, sizeof(result.category), "policy-probe");
  snprintf(result.operation, sizeof(result.operation), "format-policy");
  result.digits = digits;
  result.scratch_us = candidate_us ? candidate_us : 1;
  result.gmp_us = gmp_us ? gmp_us : 1;
  result.speed_ratio = paired_ratio > 0.0 ? paired_ratio : (double)result.scratch_us / (double)result.gmp_us;
  result.max_allowed_speed_ratio = 1.0;
  result.stable_sample_count = stable_sample_count;
  result.sample_count = sample_count;
  result.worst_pair_ratio = worst_pair_ratio;
  result.parity_verified = parity;
  size_t required_stable = policy_required_stable_samples(sample_count);
  int threshold_policy = min_digits > 0 || max_digits > 0 || leaf_chunks > 0;
  int row_cleared_gate = parity &&
    xray_benchmark_readiness_gate(
      result.speed_ratio,
      result.max_allowed_speed_ratio,
      result.stable_sample_count,
      required_stable,
      result.worst_pair_ratio);
  result.replacement_ready = !threshold_policy && row_cleared_gate;
  snprintf(result.adoption, sizeof(result.adoption), "%s",
    !parity ? "blocked-output-mismatch" : (result.replacement_ready ? "promotion-ready" : "observe-only"));
  snprintf(result.status, sizeof(result.status), "%s",
    !parity ? "mismatch" : (result.replacement_ready ? "policy-ready" : (threshold_policy && row_cleared_gate ? "needs-safety-gate" : (result.speed_ratio <= 1.0 ? "needs-stability" : "backend-faster"))));
  result.passed = parity;
  result.elapsed_ms = (unsigned long)((result.scratch_us + result.gmp_us + 999ULL) / 1000ULL);
  const char *active_candidate = (digits >= min_digits && (max_digits == 0 || digits <= max_digits)) ? candidate : "current-scratch-format";
  snprintf(result.detail, sizeof(result.detail),
    "op=format-policy digits=%zu policy=%s minDigits=%zu maxDigits=%zu leafThreshold=%zu samples=%zu stablePairs=%zu/%zu ratioMethod=paired-median candidate=%s activeCandidate=%s baseline=mpz_get_str featureGate=%s gmpClue=%s thresholdSafety=%s noAutoRoute=%d adoption=%s",
    digits,
    policy,
    min_digits,
    max_digits,
    leaf_chunks,
    sample_count,
    stable_sample_count,
    sample_count,
    candidate,
    active_candidate,
    feature_gate,
    gmp_clue,
    threshold_policy ? "requires-forced-neighbor" : "direct-row",
    threshold_policy ? 1 : 0,
    result.adoption);
  append_result(report, &result);
}

static void append_arithmetic_policy_probe_result(
  XrayBenchmarkReport *report,
  const char *operation,
  size_t digits,
  size_t operand_families,
  const char *policy,
  const char *candidate,
  const char *feature_gate,
  const char *gmp_clue,
  size_t min_digits,
  size_t leaf_threshold,
  size_t depth_limit,
  int candidate_available,
  int parity,
  unsigned long long candidate_us,
  unsigned long long gmp_us,
  double paired_ratio,
  size_t stable_sample_count,
  size_t sample_count,
  double worst_pair_ratio) {
  XrayBenchmarkResult result;
  memset(&result, 0, sizeof(result));
  snprintf(result.name, sizeof(result.name), "policy %s %s %zu digits", operation, policy, digits);
  snprintf(result.category, sizeof(result.category), "policy-probe");
  snprintf(result.operation, sizeof(result.operation), "%s-policy", operation);
  result.digits = digits;
  result.scratch_us = candidate_us ? candidate_us : 1;
  result.gmp_us = gmp_us ? gmp_us : 1;
  result.speed_ratio = paired_ratio > 0.0 ? paired_ratio : (double)result.scratch_us / (double)result.gmp_us;
  result.max_allowed_speed_ratio = 1.0;
  result.stable_sample_count = stable_sample_count;
  result.sample_count = sample_count;
  result.worst_pair_ratio = worst_pair_ratio;
  result.parity_verified = parity;
  size_t required_stable = policy_required_stable_samples(sample_count);
  int threshold_policy = min_digits > 0 || leaf_threshold > 0 || depth_limit > 0;
  int row_cleared_gate = candidate_available &&
    parity &&
    xray_benchmark_readiness_gate(
      result.speed_ratio,
      result.max_allowed_speed_ratio,
      result.stable_sample_count,
      required_stable,
      result.worst_pair_ratio);
  result.replacement_ready = !threshold_policy && row_cleared_gate;
  snprintf(result.adoption, sizeof(result.adoption), "%s",
    !parity ? "blocked-output-mismatch" : (result.replacement_ready ? "promotion-ready" : "observe-only"));
  snprintf(result.status, sizeof(result.status), "%s",
    !parity ? "mismatch" : (!candidate_available ? "candidate-unavailable" : (result.replacement_ready ? "policy-ready" : (threshold_policy && row_cleared_gate ? "needs-safety-gate" : (result.speed_ratio <= 1.0 ? "needs-stability" : "backend-faster")))));
  result.passed = parity;
  result.elapsed_ms = (unsigned long)((result.scratch_us + result.gmp_us + 999ULL) / 1000ULL);
  const char *fallback_candidate = strcmp(operation, "mul") == 0 ? "current-scratch-mul" : "current-scratch-square";
  const char *active_candidate = (candidate_available && (min_digits == 0 || digits >= min_digits)) ? candidate : fallback_candidate;
  snprintf(result.detail, sizeof(result.detail),
    "op=%s-policy digits=%zu policy=%s minDigits=%zu leafThreshold=%zu depthLimit=%zu operandFamilies=%zu samples=%zu stablePairs=%zu/%zu ratioMethod=paired-median candidate=%s activeCandidate=%s candidateAvailable=%s baseline=mpz_mul featureGate=%s gmpClue=%s thresholdSafety=%s noAutoRoute=%d adoption=%s",
    operation,
    digits,
    policy,
    min_digits,
    leaf_threshold,
    depth_limit,
    operand_families,
    sample_count,
    stable_sample_count,
    sample_count,
    candidate,
    active_candidate,
    candidate_available ? "yes" : "no",
    feature_gate,
    gmp_clue,
    threshold_policy ? "requires-forced-neighbor" : "direct-row",
    threshold_policy ? 1 : 0,
    result.adoption);
  append_result(report, &result);
}

static void append_format_policy_safety_result(
  XrayBenchmarkReport *report,
  const char *operation,
  const char *policy,
  const char *candidate,
  size_t neighbor_digits,
  size_t gate_digits,
  size_t min_digits,
  size_t max_digits,
  size_t leaf_chunks,
  int parity,
  unsigned long long candidate_us,
  unsigned long long gmp_us,
  double neighbor_ratio,
  double gate_ratio,
  size_t neighbor_stable,
  size_t gate_stable,
  size_t sample_count,
  double worst_pair_ratio) {
  XrayBenchmarkResult result;
  memset(&result, 0, sizeof(result));
  snprintf(result.name, sizeof(result.name), "policy gate format %s", policy);
  snprintf(result.category, sizeof(result.category), "policy-gate");
  snprintf(result.operation, sizeof(result.operation), "%s", operation ? operation : "format-policy-safety");
  result.digits = gate_digits;
  result.scratch_us = candidate_us ? candidate_us : 1;
  result.gmp_us = gmp_us ? gmp_us : 1;
  result.speed_ratio = neighbor_ratio > gate_ratio ? neighbor_ratio : gate_ratio;
  result.max_allowed_speed_ratio = 1.0;
  result.stable_sample_count =
    (neighbor_stable >= policy_required_stable_samples(sample_count) ? 1U : 0U) +
    (gate_stable >= policy_required_stable_samples(sample_count) ? 1U : 0U);
  result.sample_count = 2;
  result.worst_pair_ratio = worst_pair_ratio;
  result.parity_verified = parity;
  int neighbor_safe = neighbor_ratio <= result.max_allowed_speed_ratio &&
    neighbor_stable >= policy_required_stable_samples(sample_count);
  int gate_safe = gate_ratio <= result.max_allowed_speed_ratio &&
    gate_stable >= policy_required_stable_samples(sample_count);
  int worst_pair_safe = xray_no_worst_pair_regression(result.worst_pair_ratio);
  result.replacement_ready = parity && neighbor_safe && gate_safe && worst_pair_safe;
  snprintf(result.adoption, sizeof(result.adoption), "%s",
    !parity ? "blocked-output-mismatch" : (result.replacement_ready ? "promotion-ready" : "observe-only"));
  snprintf(result.status, sizeof(result.status), "%s",
    !parity ? "mismatch" :
    (!neighbor_safe ? "neighbor-regression" :
    (!gate_safe ? "gate-regression" :
    (!worst_pair_safe ? "worst-pair-regression" :
    (result.replacement_ready ? "policy-ready" : "needs-stability")))));
  result.passed = parity;
  result.elapsed_ms = (unsigned long)((result.scratch_us + result.gmp_us + 999ULL) / 1000ULL);
  size_t required_stable = policy_required_stable_samples(sample_count);
  snprintf(result.detail, sizeof(result.detail),
    "op=%s policy=%s gate=%zu neighbor=%zu min=%zu max=%zu leaf=%zu forcedCandidate=yes thresholdSafety=forced-neighbor neighborStable=%zu/%zu gateStable=%zu/%zu requiredStablePairs=%zu/%zu neighborRatio=%.3f gateRatio=%.3f ratioMethod=paired-median candidate=%s baseline=mpz_get_str featureGate=threshold-neighbor gmpClue=product-codegen adoption=%s",
    result.operation,
    policy,
    gate_digits,
    neighbor_digits,
    min_digits,
    max_digits,
    leaf_chunks,
    neighbor_stable,
    sample_count,
    gate_stable,
    sample_count,
    required_stable,
    sample_count,
    neighbor_ratio,
    gate_ratio,
    candidate,
    result.adoption);
  append_result(report, &result);
}

typedef struct {
  size_t digits;
  int candidate_available;
  int parity;
  unsigned long long candidate_us;
  unsigned long long gmp_us;
  double paired_ratio;
  size_t stable_sample_count;
  size_t sample_count;
  double worst_pair_ratio;
} XrayMulPolicySafetyPoint;

static void append_mul_policy_safety_result(
  XrayBenchmarkReport *report,
  const char *policy,
  const char *candidate,
  const char *feature_gate,
  const char *gmp_clue,
  size_t min_digits,
  size_t leaf_threshold,
  size_t depth_limit,
  int candidate_available,
  const XrayMulPolicySafetyPoint *points,
  size_t point_count) {
  if (!points || point_count == 0) return;

  int parity = 1;
  double max_ratio = 0.0;
  double max_worst_pair_ratio = 0.0;
  size_t safe_size_count = 0;
  size_t measured_size_count = 0;
  size_t max_digits = 0;
  size_t min_measured_digits = SIZE_MAX;
  size_t required_stable = policy_required_stable_samples(XRAY_BENCH_SAMPLES);
  unsigned long long candidate_us = 0;
  unsigned long long gmp_us = 0;
  char sizes[128] = {0};

  for (size_t index = 0; index < point_count; ++index) {
    const XrayMulPolicySafetyPoint *point = &points[index];
    if (point->digits == 0 || point->sample_count == 0) continue;
    measured_size_count++;
    parity = parity && point->parity;
    if (point->candidate_us > candidate_us) candidate_us = point->candidate_us;
    if (point->gmp_us > gmp_us) gmp_us = point->gmp_us;
    if (point->paired_ratio > max_ratio) max_ratio = point->paired_ratio;
    if (point->worst_pair_ratio > max_worst_pair_ratio) max_worst_pair_ratio = point->worst_pair_ratio;
    if (point->digits > max_digits) max_digits = point->digits;
    if (point->digits < min_measured_digits) min_measured_digits = point->digits;
    if (candidate_available &&
        point->candidate_available &&
        point->parity &&
        point->paired_ratio <= 1.0 &&
        point->worst_pair_ratio <= 1.0 &&
        point->stable_sample_count >= required_stable) {
      safe_size_count++;
    }
    size_t used = strlen(sizes);
    if (used < sizeof(sizes)) {
      snprintf(sizes + used, sizeof(sizes) - used, "%s%zu", sizes[0] ? "," : "", point->digits);
    }
  }
  if (measured_size_count == 0) return;

  XrayBenchmarkResult result;
  memset(&result, 0, sizeof(result));
  snprintf(result.name, sizeof(result.name), "policy gate mul %s %zu-%zu digits", policy, min_measured_digits, max_digits);
  snprintf(result.category, sizeof(result.category), "policy-gate");
  snprintf(result.operation, sizeof(result.operation), "mul-policy-safety");
  result.digits = max_digits;
  result.scratch_us = candidate_us ? candidate_us : 1;
  result.gmp_us = gmp_us ? gmp_us : 1;
  result.speed_ratio = max_ratio > 0.0 ? max_ratio : 1.0;
  result.max_allowed_speed_ratio = 1.0;
  result.stable_sample_count = safe_size_count;
  result.sample_count = measured_size_count;
  result.worst_pair_ratio = max_worst_pair_ratio > 0.0 ? max_worst_pair_ratio : 1.0;
  result.parity_verified = parity;
  result.replacement_ready = 0;
  snprintf(result.adoption, sizeof(result.adoption), "%s",
    !parity ? "blocked-output-mismatch" : "observe-only");
  snprintf(result.status, sizeof(result.status), "%s",
    !parity ? "mismatch" :
    (!candidate_available ? "candidate-unavailable" :
    (safe_size_count == measured_size_count ? "safety-window-clean" : "neighbor-regression")));
  result.passed = parity;
  result.elapsed_ms = (unsigned long)((result.scratch_us + result.gmp_us + 999ULL) / 1000ULL);
  snprintf(result.detail, sizeof(result.detail),
    "op=mul-policy-safety policy=%s sizes=%s minDigits=%zu leafThreshold=%zu depthLimit=%zu forcedCandidate=yes thresholdSafety=forced-neighbor safeSizes=%zu/%zu requiredStablePairs=%zu/%u maxRatio=%.3f maxWorstPairRatio=%.3f ratioMethod=paired-median candidate=%s candidateAvailable=%s baseline=mpz_mul oracle=mpz_mul featureGate=%s gmpClue=%s noAutoRoute=1 adoption=%s",
    policy,
    sizes,
    min_digits,
    leaf_threshold,
    depth_limit,
    safe_size_count,
    measured_size_count,
    required_stable,
    XRAY_BENCH_SAMPLES,
    result.speed_ratio,
    result.worst_pair_ratio,
    candidate,
    candidate_available ? "yes" : "no",
    feature_gate,
    gmp_clue,
    result.adoption);
  append_result(report, &result);
}

static void append_parse_chunk_probe_result(
  XrayBenchmarkReport *report,
  size_t digits,
  unsigned int chunk_digits,
  int parity,
  unsigned long long candidate_us,
  unsigned long long baseline_us,
  double paired_ratio,
  size_t stable_sample_count,
  size_t sample_count,
  double worst_pair_ratio) {
  XrayBenchmarkResult result;
  memset(&result, 0, sizeof(result));
  snprintf(result.name, sizeof(result.name), "kernel parse chunk %u digits %zu digits", chunk_digits, digits);
  snprintf(result.category, sizeof(result.category), "kernel-probe");
  snprintf(result.operation, sizeof(result.operation), "parse-chunk");
  result.digits = digits;
  result.scratch_us = candidate_us ? candidate_us : 1;
  result.gmp_us = baseline_us ? baseline_us : 1;
  result.speed_ratio = paired_ratio > 0.0 ? paired_ratio : (double)result.scratch_us / (double)result.gmp_us;
  result.max_allowed_speed_ratio = 0.98;
  result.stable_sample_count = stable_sample_count;
  result.sample_count = sample_count;
  result.worst_pair_ratio = worst_pair_ratio;
  result.parity_verified = parity;
  size_t required_stable = kernel_required_stable_samples(sample_count);
  result.replacement_ready = parity &&
    xray_benchmark_readiness_gate(
      result.speed_ratio,
      result.max_allowed_speed_ratio,
      result.stable_sample_count,
      required_stable,
      result.worst_pair_ratio);
  snprintf(result.adoption, sizeof(result.adoption), "%s",
    !parity ? "blocked-output-mismatch" : (result.replacement_ready ? "promote-candidate" : "observe-only"));
  snprintf(result.status, sizeof(result.status), "%s",
    !parity ? "mismatch" : (result.replacement_ready ? "candidate-faster" : (result.speed_ratio < 1.0 ? "candidate-no-margin" : "baseline-faster")));
  result.passed = parity;
  result.elapsed_ms = (unsigned long)((result.scratch_us + result.gmp_us + 999ULL) / 1000ULL);
  snprintf(result.detail, sizeof(result.detail),
    "op=parse-chunk digits=%zu chunkDigits=%u operandFamilies=1 samples=%zu stablePairs=%zu/%zu candidateUs=%llu baselineUs=%llu ratio=%.3f worstPairRatio=%.3f ratioMethod=paired-median max=%.2f candidate=decimal-parse-chunk baseline=current-scratch-parse featureGate=decimal-parse-chunk gmpClue=mpz_set_str-chunking adoption=%s",
    digits,
    chunk_digits,
    sample_count,
    stable_sample_count,
    sample_count,
    result.scratch_us,
    result.gmp_us,
    result.speed_ratio,
    result.worst_pair_ratio,
    result.max_allowed_speed_ratio,
    result.adoption);
  append_result(report, &result);
}

static void append_divmod_dc_power_probe_result(
  XrayBenchmarkReport *report,
  size_t digits,
  size_t power_chunks,
  int parity,
  unsigned long long candidate_us,
  unsigned long long baseline_us,
  double paired_ratio,
  size_t stable_sample_count,
  size_t sample_count,
  double worst_pair_ratio) {
  XrayBenchmarkResult result;
  memset(&result, 0, sizeof(result));
  snprintf(result.name, sizeof(result.name), "kernel divmod D&C power %zu chunks %zu digits", power_chunks, digits);
  snprintf(result.category, sizeof(result.category), "kernel-probe");
  snprintf(result.operation, sizeof(result.operation), "divmod-dc-power");
  result.digits = digits;
  result.scratch_us = candidate_us ? candidate_us : 1;
  result.gmp_us = baseline_us ? baseline_us : 1;
  result.speed_ratio = paired_ratio > 0.0 ? paired_ratio : (double)result.scratch_us / (double)result.gmp_us;
  result.max_allowed_speed_ratio = 0.98;
  result.stable_sample_count = stable_sample_count;
  result.sample_count = sample_count;
  result.worst_pair_ratio = worst_pair_ratio;
  result.parity_verified = parity;
  size_t required_stable = kernel_required_stable_samples(sample_count);
  result.replacement_ready = parity &&
    xray_benchmark_readiness_gate(
      result.speed_ratio,
      result.max_allowed_speed_ratio,
      result.stable_sample_count,
      required_stable,
      result.worst_pair_ratio);
  snprintf(result.adoption, sizeof(result.adoption), "%s",
    !parity ? "blocked-output-mismatch" : (result.replacement_ready ? "promote-candidate" : "observe-only"));
  snprintf(result.status, sizeof(result.status), "%s",
    !parity ? "mismatch" : (result.replacement_ready ? "candidate-faster" : (result.speed_ratio < 1.0 ? "candidate-no-margin" : "baseline-faster")));
  result.passed = parity;
  result.elapsed_ms = (unsigned long)((result.scratch_us + result.gmp_us + 999ULL) / 1000ULL);
  snprintf(result.detail, sizeof(result.detail),
    "op=divmod-dc-power digits=%zu powerChunks=%zu divisorDigits=%zu operandFamilies=1 samples=%zu stablePairs=%zu/%zu candidateUs=%llu baselineUs=%llu ratio=%.3f worstPairRatio=%.3f ratioMethod=paired-median max=%.2f candidate=scratch-knuth-divmod baseline=mpz_tdiv_qr featureGate=bigint-division-dc-power gmpClue=mpn_tdiv_qr adoption=%s",
    digits,
    power_chunks,
    power_chunks * 19U + 1U,
    sample_count,
    stable_sample_count,
    sample_count,
    result.scratch_us,
    result.gmp_us,
    result.speed_ratio,
    result.worst_pair_ratio,
    result.max_allowed_speed_ratio,
    result.adoption);
  append_result(report, &result);
}

static void append_divmod_precomputed_probe_result(
  XrayBenchmarkReport *report,
  size_t digits,
  size_t power_chunks,
  int parity,
  unsigned long long candidate_us,
  unsigned long long baseline_us,
  double paired_ratio,
  size_t stable_sample_count,
  size_t sample_count,
  double worst_pair_ratio) {
  XrayBenchmarkResult result;
  memset(&result, 0, sizeof(result));
  snprintf(result.name, sizeof(result.name), "kernel divmod precomputed divisor %zu chunks %zu digits", power_chunks, digits);
  snprintf(result.category, sizeof(result.category), "kernel-probe");
  snprintf(result.operation, sizeof(result.operation), "divmod-precomputed");
  result.digits = digits;
  result.scratch_us = candidate_us ? candidate_us : 1;
  result.gmp_us = baseline_us ? baseline_us : 1;
  result.speed_ratio = paired_ratio > 0.0 ? paired_ratio : (double)result.scratch_us / (double)result.gmp_us;
  result.max_allowed_speed_ratio = 0.98;
  result.stable_sample_count = stable_sample_count;
  result.sample_count = sample_count;
  result.worst_pair_ratio = worst_pair_ratio;
  result.parity_verified = parity;
  size_t required_stable = kernel_required_stable_samples(sample_count);
  int candidate_cleared_gate = parity &&
    xray_benchmark_readiness_gate(
      result.speed_ratio,
      result.max_allowed_speed_ratio,
      result.stable_sample_count,
      required_stable,
      result.worst_pair_ratio);
  result.replacement_ready = 0;
  snprintf(result.adoption, sizeof(result.adoption), "%s",
    !parity ? "blocked-output-mismatch" : "observe-only");
  snprintf(result.status, sizeof(result.status), "%s",
    !parity ? "mismatch" : (candidate_cleared_gate ? "candidate-faster" : (result.speed_ratio < 1.0 ? "candidate-no-margin" : "baseline-faster")));
  result.passed = parity;
  result.elapsed_ms = (unsigned long)((result.scratch_us + result.gmp_us + 999ULL) / 1000ULL);
  snprintf(result.detail, sizeof(result.detail),
    "op=divmod-precomputed digits=%zu powerChunks=%zu divisorDigits=%zu operandFamilies=1 samples=%zu stablePairs=%zu/%zu candidateUs=%llu baselineUs=%llu ratio=%.3f worstPairRatio=%.3f ratioMethod=paired-median max=%.2f candidate=scratch-divmod-precomputed baseline=current-scratch-divmod oracle=mpz_tdiv_qr featureGate=bigint-division-context gmpClue=mpn_tdiv_qr-precomputed-divisor thresholdSafety=explicit-context noAutoRoute=1 adoption=%s",
    digits,
    power_chunks,
    power_chunks * 19U + 1U,
    sample_count,
    stable_sample_count,
    sample_count,
    result.scratch_us,
    result.gmp_us,
    result.speed_ratio,
    result.worst_pair_ratio,
    result.max_allowed_speed_ratio,
    result.adoption);
  append_result(report, &result);
}

static void append_divmod_workspace_probe_result(
  XrayBenchmarkReport *report,
  size_t digits,
  size_t power_chunks,
  int parity,
  unsigned long long candidate_us,
  unsigned long long baseline_us,
  double paired_ratio,
  size_t stable_sample_count,
  size_t sample_count,
  double worst_pair_ratio) {
  XrayBenchmarkResult result;
  memset(&result, 0, sizeof(result));
  snprintf(result.name, sizeof(result.name), "kernel divmod reusable workspace %zu chunks %zu digits", power_chunks, digits);
  snprintf(result.category, sizeof(result.category), "kernel-probe");
  snprintf(result.operation, sizeof(result.operation), "divmod-workspace");
  result.digits = digits;
  result.scratch_us = candidate_us ? candidate_us : 1;
  result.gmp_us = baseline_us ? baseline_us : 1;
  result.speed_ratio = paired_ratio > 0.0 ? paired_ratio : (double)result.scratch_us / (double)result.gmp_us;
  result.max_allowed_speed_ratio = 0.98;
  result.stable_sample_count = stable_sample_count;
  result.sample_count = sample_count;
  result.worst_pair_ratio = worst_pair_ratio;
  result.parity_verified = parity;
  size_t required_stable = kernel_required_stable_samples(sample_count);
  int candidate_cleared_gate = parity &&
    xray_benchmark_readiness_gate(
      result.speed_ratio,
      result.max_allowed_speed_ratio,
      result.stable_sample_count,
      required_stable,
      result.worst_pair_ratio);
  result.replacement_ready = 0;
  snprintf(result.adoption, sizeof(result.adoption), "%s",
    !parity ? "blocked-output-mismatch" : "observe-only");
  snprintf(result.status, sizeof(result.status), "%s",
    !parity ? "mismatch" : (candidate_cleared_gate ? "candidate-faster" : (result.speed_ratio < 1.0 ? "candidate-no-margin" : "baseline-faster")));
  result.passed = parity;
  result.elapsed_ms = (unsigned long)((result.scratch_us + result.gmp_us + 999ULL) / 1000ULL);
  snprintf(result.detail, sizeof(result.detail),
    "op=divmod-workspace digits=%zu powerChunks=%zu divisorDigits=%zu operandFamilies=1 samples=%zu stablePairs=%zu/%zu candidateUs=%llu baselineUs=%llu ratio=%.3f worstPairRatio=%.3f ratioMethod=paired-median max=%.2f candidate=scratch-divmod-context-workspace baseline=scratch-divmod-precomputed oracle=mpz_tdiv_qr featureGate=bigint-division-workspace gmpClue=mpn_tdiv_qr-scratch-reuse thresholdSafety=explicit-workspace noAutoRoute=1 adoption=%s",
    digits,
    power_chunks,
    power_chunks * 19U + 1U,
    sample_count,
    stable_sample_count,
    sample_count,
    result.scratch_us,
    result.gmp_us,
    result.speed_ratio,
    result.worst_pair_ratio,
    result.max_allowed_speed_ratio,
    result.adoption);
  append_result(report, &result);
}

static void append_divmod_preinv_qhat_probe_result(
  XrayBenchmarkReport *report,
  size_t digits,
  size_t power_chunks,
  int parity,
  unsigned long long candidate_us,
  unsigned long long baseline_us,
  double paired_ratio,
  size_t stable_sample_count,
  size_t sample_count,
  double worst_pair_ratio) {
  XrayBenchmarkResult result;
  memset(&result, 0, sizeof(result));
  snprintf(result.name, sizeof(result.name), "kernel divmod preinverted qhat %zu chunks %zu digits", power_chunks, digits);
  snprintf(result.category, sizeof(result.category), "kernel-probe");
  snprintf(result.operation, sizeof(result.operation), "divmod-preinv-qhat");
  result.digits = digits;
  result.scratch_us = candidate_us ? candidate_us : 1;
  result.gmp_us = baseline_us ? baseline_us : 1;
  result.speed_ratio = paired_ratio > 0.0 ? paired_ratio : (double)result.scratch_us / (double)result.gmp_us;
  result.max_allowed_speed_ratio = 0.98;
  result.stable_sample_count = stable_sample_count;
  result.sample_count = sample_count;
  result.worst_pair_ratio = worst_pair_ratio;
  result.parity_verified = parity;
  size_t required_stable = kernel_required_stable_samples(sample_count);
  int candidate_cleared_gate = parity &&
    xray_benchmark_readiness_gate(
      result.speed_ratio,
      result.max_allowed_speed_ratio,
      result.stable_sample_count,
      required_stable,
      result.worst_pair_ratio);
  result.replacement_ready = 0;
  snprintf(result.adoption, sizeof(result.adoption), "%s",
    !parity ? "blocked-output-mismatch" : "observe-only");
  snprintf(result.status, sizeof(result.status), "%s",
    !parity ? "mismatch" : (candidate_cleared_gate ? "candidate-faster" : (result.speed_ratio < 1.0 ? "candidate-no-margin" : "baseline-faster")));
  result.passed = parity;
  result.elapsed_ms = (unsigned long)((result.scratch_us + result.gmp_us + 999ULL) / 1000ULL);
  snprintf(result.detail, sizeof(result.detail),
    "op=divmod-preinv-qhat digits=%zu powerChunks=%zu divisorDigits=%zu operandFamilies=1 samples=%zu stablePairs=%zu/%zu candidateUs=%llu baselineUs=%llu ratio=%.3f worstPairRatio=%.3f ratioMethod=paired-median max=%.2f candidate=scratch-divmod-preinv-qhat baseline=scratch-divmod-context-workspace oracle=mpz_tdiv_qr featureGate=bigint-division-preinv-qhat gmpClue=mpn_sbpi1_div_qr-qhat precomputeScope=per-divisor thresholdSafety=explicit-probe noAutoRoute=1 adoption=%s",
    digits,
    power_chunks,
    power_chunks * 19U + 1U,
    sample_count,
    stable_sample_count,
    sample_count,
    result.scratch_us,
    result.gmp_us,
    result.speed_ratio,
    result.worst_pair_ratio,
    result.max_allowed_speed_ratio,
    result.adoption);
  append_result(report, &result);
}

typedef struct {
  size_t digits;
  size_t power_chunks;
  int parity;
  double paired_ratio;
  size_t stable_sample_count;
  size_t sample_count;
  double worst_pair_ratio;
} XrayDivmodPreinvQhatSafetyPoint;

static void append_divmod_preinv_qhat_safety_result(
  XrayBenchmarkReport *report,
  const XrayDivmodPreinvQhatSafetyPoint *points,
  size_t point_count) {
  if (!points || point_count == 0) return;

  int parity = 1;
  double max_ratio = 0.0;
  double max_worst_pair_ratio = 0.0;
  size_t safe_size_count = 0;
  size_t measured_size_count = 0;
  size_t max_digits = 0;
  size_t min_digits = SIZE_MAX;
  size_t required_stable = kernel_required_stable_samples(XRAY_BENCH_SAMPLES);
  char sizes[96] = {0};

  for (size_t index = 0; index < point_count; ++index) {
    const XrayDivmodPreinvQhatSafetyPoint *point = &points[index];
    if (point->digits == 0 || point->sample_count == 0) continue;
    measured_size_count++;
    parity = parity && point->parity;
    if (point->paired_ratio > max_ratio) max_ratio = point->paired_ratio;
    if (point->worst_pair_ratio > max_worst_pair_ratio) max_worst_pair_ratio = point->worst_pair_ratio;
    if (point->digits > max_digits) max_digits = point->digits;
    if (point->digits < min_digits) min_digits = point->digits;
    if (point->parity &&
        point->paired_ratio <= 1.0 &&
        point->worst_pair_ratio <= 1.0 &&
        point->stable_sample_count >= required_stable) {
      safe_size_count++;
    }
    size_t used = strlen(sizes);
    if (used < sizeof(sizes)) {
      snprintf(sizes + used, sizeof(sizes) - used, "%s%zu", sizes[0] ? "," : "", point->digits);
    }
  }
  if (measured_size_count == 0) return;

  XrayBenchmarkResult result;
  memset(&result, 0, sizeof(result));
  snprintf(result.name, sizeof(result.name), "policy gate divmod preinverted qhat %zu-%zu digits", min_digits, max_digits);
  snprintf(result.category, sizeof(result.category), "policy-gate");
  snprintf(result.operation, sizeof(result.operation), "divmod-preinv-qhat-safety");
  result.digits = max_digits;
  result.scratch_us = 1;
  result.gmp_us = 1;
  result.speed_ratio = max_ratio;
  result.max_allowed_speed_ratio = 1.0;
  result.stable_sample_count = safe_size_count;
  result.sample_count = measured_size_count;
  result.worst_pair_ratio = max_worst_pair_ratio;
  result.parity_verified = parity;
  result.replacement_ready = 0;
  snprintf(result.adoption, sizeof(result.adoption), "%s",
    !parity ? "blocked-output-mismatch" : "observe-only");
  snprintf(result.status, sizeof(result.status), "%s",
    !parity ? "mismatch" :
    (safe_size_count == measured_size_count ? "safety-window-clean" : "neighbor-regression"));
  result.passed = parity;
  result.elapsed_ms = 1;
  snprintf(result.detail, sizeof(result.detail),
    "op=divmod-preinv-qhat-safety sizes=%s forcedCandidate=yes thresholdSafety=forced-neighbor safeSizes=%zu/%zu requiredStablePairs=%zu/%u maxRatio=%.3f maxWorstPairRatio=%.3f ratioMethod=paired-median candidate=scratch-divmod-preinv-qhat baseline=scratch-divmod-context-workspace oracle=mpz_tdiv_qr featureGate=bigint-division-preinv-qhat gmpClue=mpn_sbpi1_div_qr-qhat precomputeScope=per-divisor noAutoRoute=1 adoption=%s",
    sizes,
    safe_size_count,
    measured_size_count,
    required_stable,
    XRAY_BENCH_SAMPLES,
    result.speed_ratio,
    result.worst_pair_ratio,
    result.adoption);
  append_result(report, &result);
}

static void append_square_vs_mul_probe_result(
  XrayBenchmarkReport *report,
  size_t digits,
  int parity,
  unsigned long long square_us,
  unsigned long long baseline_us,
  double paired_ratio,
  size_t stable_sample_count,
  size_t sample_count,
  double worst_pair_ratio) {
  XrayBenchmarkResult result;
  memset(&result, 0, sizeof(result));
  snprintf(result.name, sizeof(result.name), "kernel square versus generic self-mul %zu digits", digits);
  snprintf(result.category, sizeof(result.category), "kernel-probe");
  snprintf(result.operation, sizeof(result.operation), "square-vs-mul");
  result.digits = digits;
  result.scratch_us = square_us ? square_us : 1;
  result.gmp_us = baseline_us ? baseline_us : 1;
  result.speed_ratio = paired_ratio > 0.0 ? paired_ratio : (double)result.scratch_us / (double)result.gmp_us;
  result.max_allowed_speed_ratio = 0.98;
  result.stable_sample_count = stable_sample_count;
  result.sample_count = sample_count;
  result.worst_pair_ratio = worst_pair_ratio;
  result.parity_verified = parity;
  size_t required_stable = kernel_required_stable_samples(sample_count);
  result.replacement_ready = parity &&
    xray_benchmark_readiness_gate(
      result.speed_ratio,
      result.max_allowed_speed_ratio,
      result.stable_sample_count,
      required_stable,
      result.worst_pair_ratio);
  snprintf(result.adoption, sizeof(result.adoption), "%s",
    !parity ? "blocked-output-mismatch" : (result.replacement_ready ? "promote-candidate" : "observe-only"));
  snprintf(result.status, sizeof(result.status), "%s",
    !parity ? "mismatch" : (result.replacement_ready ? "candidate-faster" : (result.speed_ratio < 1.0 ? "candidate-no-margin" : "baseline-faster")));
  result.passed = parity;
  result.elapsed_ms = (unsigned long)((result.scratch_us + result.gmp_us + 999ULL) / 1000ULL);
  snprintf(result.detail, sizeof(result.detail),
    "op=square-vs-mul digits=%zu routeCandidate=unrouted operandFamilies=1 samples=%zu stablePairs=%zu/%zu squareUs=%llu genericMulUs=%llu ratio=%.3f worstPairRatio=%.3f ratioMethod=paired-median max=%.2f candidate=specialized-square baseline=generic-threshold-self-mul featureGate=square-basecase-probe gmpClue=sqr_basecase adoption=%s",
    digits,
    sample_count,
    stable_sample_count,
    sample_count,
    result.scratch_us,
    result.gmp_us,
    result.speed_ratio,
    result.worst_pair_ratio,
    result.max_allowed_speed_ratio,
    result.adoption);
  append_result(report, &result);
}

static void append_u32_precompute_probe_result(
  XrayBenchmarkReport *report,
  const char *operation,
  size_t digits,
  int parity,
  unsigned long long candidate_us,
  unsigned long long baseline_us,
  double paired_ratio,
  size_t stable_sample_count,
  size_t sample_count,
  double worst_pair_ratio) {
  XrayBenchmarkResult result;
  memset(&result, 0, sizeof(result));
  snprintf(result.name, sizeof(result.name), "kernel %s precompute context %zu digits", operation, digits);
  snprintf(result.category, sizeof(result.category), "kernel-probe");
  snprintf(result.operation, sizeof(result.operation), "%s-precompute", operation);
  result.digits = digits;
  result.scratch_us = candidate_us ? candidate_us : 1;
  result.gmp_us = baseline_us ? baseline_us : 1;
  result.speed_ratio = paired_ratio > 0.0 ? paired_ratio : (double)result.scratch_us / (double)result.gmp_us;
  result.max_allowed_speed_ratio = 0.98;
  result.stable_sample_count = stable_sample_count;
  result.sample_count = sample_count;
  result.worst_pair_ratio = worst_pair_ratio;
  result.parity_verified = parity;
  size_t required_stable = kernel_required_stable_samples(sample_count);
  result.replacement_ready = parity &&
    xray_benchmark_readiness_gate(
      result.speed_ratio,
      result.max_allowed_speed_ratio,
      result.stable_sample_count,
      required_stable,
      result.worst_pair_ratio);
  snprintf(result.adoption, sizeof(result.adoption), "%s",
    !parity ? "blocked-output-mismatch" : (result.replacement_ready ? "promote-candidate" : "observe-only"));
  snprintf(result.status, sizeof(result.status), "%s",
    !parity ? "mismatch" : (result.replacement_ready ? "candidate-faster" : (result.speed_ratio < 1.0 ? "candidate-no-margin" : "baseline-faster")));
  result.passed = parity;
  result.elapsed_ms = (unsigned long)((result.scratch_us + result.gmp_us + 999ULL) / 1000ULL);
  snprintf(result.detail, sizeof(result.detail),
    "op=%s-precompute digits=%zu modulus=1000000007 operandFamilies=1 samples=%zu stablePairs=%zu/%zu candidateUs=%llu baselineUs=%llu ratio=%.3f worstPairRatio=%.3f ratioMethod=paired-median max=%.2f candidate=u32-mod-context baseline=one-shot-u32 featureGate=u32-mod-context gmpClue=preinvert-limb-divisor adoption=%s",
    operation,
    digits,
    sample_count,
    stable_sample_count,
    sample_count,
    result.scratch_us,
    result.gmp_us,
    result.speed_ratio,
    result.worst_pair_ratio,
    result.max_allowed_speed_ratio,
    result.adoption);
  append_result(report, &result);
}

static void append_square_karatsuba_probe_result(
  XrayBenchmarkReport *report,
  const char *operation_name,
  const char *baseline_name,
  size_t digits,
  size_t threshold,
  int parity,
  unsigned long long candidate_us,
  unsigned long long baseline_us,
  double paired_ratio,
  size_t stable_sample_count,
  size_t sample_count,
  double worst_pair_ratio) {
  XrayBenchmarkResult result;
  memset(&result, 0, sizeof(result));
  snprintf(result.name, sizeof(result.name), "kernel Karatsuba square versus %s threshold %zu limbs %zu digits", baseline_name, threshold, digits);
  snprintf(result.category, sizeof(result.category), "kernel-probe");
  snprintf(result.operation, sizeof(result.operation), "%s", operation_name);
  result.digits = digits;
  result.scratch_us = candidate_us ? candidate_us : 1;
  result.gmp_us = baseline_us ? baseline_us : 1;
  result.speed_ratio = paired_ratio > 0.0 ? paired_ratio : (double)result.scratch_us / (double)result.gmp_us;
  result.max_allowed_speed_ratio = 0.98;
  result.stable_sample_count = stable_sample_count;
  result.sample_count = sample_count;
  result.worst_pair_ratio = worst_pair_ratio;
  result.parity_verified = parity;
  size_t required_stable = kernel_required_stable_samples(sample_count);
  result.replacement_ready = parity &&
    xray_benchmark_readiness_gate(
      result.speed_ratio,
      result.max_allowed_speed_ratio,
      result.stable_sample_count,
      required_stable,
      result.worst_pair_ratio);
  snprintf(result.adoption, sizeof(result.adoption), "%s",
    !parity ? "blocked-output-mismatch" : (result.replacement_ready ? "promote-candidate" : "observe-only"));
  snprintf(result.status, sizeof(result.status), "%s",
    !parity ? "mismatch" : (result.replacement_ready ? "candidate-faster" : (result.speed_ratio < 1.0 ? "candidate-no-margin" : "baseline-faster")));
  result.passed = parity;
  result.elapsed_ms = (unsigned long)((result.scratch_us + result.gmp_us + 999ULL) / 1000ULL);
  snprintf(result.detail, sizeof(result.detail),
    "op=%s digits=%zu threshold=%zu operandFamilies=1 samples=%zu stablePairs=%zu/%zu candidateUs=%llu baselineUs=%llu ratio=%.3f worstPairRatio=%.3f ratioMethod=paired-median max=%.2f candidate=karatsuba-square baseline=%s featureGate=karatsuba-square-probe gmpClue=sqr_karatsuba adoption=%s",
    operation_name,
    digits,
    threshold,
    sample_count,
    stable_sample_count,
    sample_count,
    result.scratch_us,
    result.gmp_us,
    result.speed_ratio,
    result.worst_pair_ratio,
    result.max_allowed_speed_ratio,
    baseline_name,
    result.adoption);
  append_result(report, &result);
}

static void append_mul_threshold_probe_result(
  XrayBenchmarkReport *report,
  size_t digits,
  size_t threshold,
  size_t operand_families,
  int parity,
  unsigned long long scratch_us,
  unsigned long long gmp_us,
  double paired_ratio,
  size_t stable_sample_count,
  size_t sample_count,
  double worst_pair_ratio) {
  XrayBenchmarkResult result;
  memset(&result, 0, sizeof(result));
  snprintf(result.name, sizeof(result.name), "kernel mul threshold %zu limbs %zu digits", threshold, digits);
  snprintf(result.category, sizeof(result.category), "kernel-probe");
  snprintf(result.operation, sizeof(result.operation), "mul-threshold");
  result.digits = digits;
  result.scratch_us = scratch_us ? scratch_us : 1;
  result.gmp_us = gmp_us ? gmp_us : 1;
  result.speed_ratio = paired_ratio > 0.0 ? paired_ratio : (double)result.scratch_us / (double)result.gmp_us;
  result.max_allowed_speed_ratio = 0.98;
  result.stable_sample_count = stable_sample_count;
  result.sample_count = sample_count;
  result.worst_pair_ratio = worst_pair_ratio;
  result.parity_verified = parity;
  size_t required_stable = kernel_required_stable_samples(sample_count);
  result.replacement_ready = parity &&
    xray_benchmark_readiness_gate(
      result.speed_ratio,
      result.max_allowed_speed_ratio,
      result.stable_sample_count,
      required_stable,
      result.worst_pair_ratio);
  snprintf(result.adoption, sizeof(result.adoption), "%s",
    !parity ? "blocked-output-mismatch" : (result.replacement_ready ? "promote-candidate" : "observe-only"));
  snprintf(result.status, sizeof(result.status), "%s",
    !parity ? "mismatch" : (result.replacement_ready ? "candidate-faster" : (result.speed_ratio < 1.0 ? "candidate-no-margin" : "baseline-faster")));
  result.passed = parity;
  result.elapsed_ms = (unsigned long)((result.scratch_us + result.gmp_us + 999ULL) / 1000ULL);
  snprintf(result.detail, sizeof(result.detail),
    "op=mul-threshold digits=%zu threshold=%zu operandFamilies=%zu samples=%zu stablePairs=%zu/%zu scratchUs=%llu gmpUs=%llu ratio=%.3f worstPairRatio=%.3f ratioMethod=paired-median max=%.2f featureGate=runtime-threshold gmpClue=gmp-thresholds adoption=%s",
    digits,
    threshold,
    operand_families,
    sample_count,
    stable_sample_count,
    sample_count,
    result.scratch_us,
    result.gmp_us,
    result.speed_ratio,
    result.worst_pair_ratio,
    result.max_allowed_speed_ratio,
    result.adoption);
  append_result(report, &result);
}

static void append_mul_karatsuba_middle_probe_result(
  XrayBenchmarkReport *report,
  size_t digits,
  size_t threshold,
  size_t operand_families,
  int parity,
  unsigned long long candidate_us,
  unsigned long long baseline_us,
  double paired_ratio,
  size_t stable_sample_count,
  size_t sample_count,
  double worst_pair_ratio) {
  XrayBenchmarkResult result;
  memset(&result, 0, sizeof(result));
  snprintf(result.name, sizeof(result.name), "kernel mul Karatsuba middle %zu limbs %zu digits", threshold, digits);
  snprintf(result.category, sizeof(result.category), "kernel-probe");
  snprintf(result.operation, sizeof(result.operation), "mul-karatsuba-middle");
  result.digits = digits;
  result.scratch_us = candidate_us ? candidate_us : 1;
  result.gmp_us = baseline_us ? baseline_us : 1;
  result.speed_ratio = paired_ratio > 0.0 ? paired_ratio : (double)result.scratch_us / (double)result.gmp_us;
  result.max_allowed_speed_ratio = 0.98;
  result.stable_sample_count = stable_sample_count;
  result.sample_count = sample_count;
  result.worst_pair_ratio = worst_pair_ratio;
  result.parity_verified = parity;
  size_t required_stable = kernel_required_stable_samples(sample_count);
  result.replacement_ready = parity &&
    xray_benchmark_readiness_gate(
      result.speed_ratio,
      result.max_allowed_speed_ratio,
      result.stable_sample_count,
      required_stable,
      result.worst_pair_ratio);
  snprintf(result.adoption, sizeof(result.adoption), "%s",
    !parity ? "blocked-output-mismatch" : (result.replacement_ready ? "promote-candidate" : "observe-only"));
  snprintf(result.status, sizeof(result.status), "%s",
    !parity ? "mismatch" : (result.replacement_ready ? "candidate-faster" : (result.speed_ratio < 1.0 ? "candidate-no-margin" : "baseline-faster")));
  result.passed = parity;
  result.elapsed_ms = (unsigned long)((result.scratch_us + result.gmp_us + 999ULL) / 1000ULL);
  snprintf(result.detail, sizeof(result.detail),
    "op=mul-karatsuba-middle digits=%zu threshold=%zu mode=sum-vs-difference operandFamilies=%zu samples=%zu stablePairs=%zu/%zu ratio=%.3f worstPairRatio=%.3f ratioMethod=paired-median candidate=karatsuba-sum-middle baseline=karatsuba-difference-middle featureGate=karatsuba-middle-form gmpClue=mpn_mul_n-middle-term adoption=%s",
    digits,
    threshold,
    operand_families,
    sample_count,
    stable_sample_count,
    sample_count,
    result.speed_ratio,
    result.worst_pair_ratio,
    result.adoption);
  append_result(report, &result);
}

static void append_format_threshold_probe_result(
  XrayBenchmarkReport *report,
  size_t digits,
  size_t threshold,
  int parity,
  unsigned long long scratch_us,
  unsigned long long gmp_us,
  double paired_ratio,
  size_t stable_sample_count,
  size_t sample_count,
  double worst_pair_ratio) {
  XrayBenchmarkResult result;
  memset(&result, 0, sizeof(result));
  snprintf(result.name, sizeof(result.name), "kernel format Horner threshold %zu limbs %zu digits", threshold, digits);
  snprintf(result.category, sizeof(result.category), "kernel-probe");
  snprintf(result.operation, sizeof(result.operation), "format-threshold");
  result.digits = digits;
  result.scratch_us = scratch_us ? scratch_us : 1;
  result.gmp_us = gmp_us ? gmp_us : 1;
  result.speed_ratio = paired_ratio > 0.0 ? paired_ratio : (double)result.scratch_us / (double)result.gmp_us;
  result.max_allowed_speed_ratio = 0.98;
  result.stable_sample_count = stable_sample_count;
  result.sample_count = sample_count;
  result.worst_pair_ratio = worst_pair_ratio;
  result.parity_verified = parity;
  size_t required_stable = kernel_required_stable_samples(sample_count);
  result.replacement_ready = parity &&
    xray_benchmark_readiness_gate(
      result.speed_ratio,
      result.max_allowed_speed_ratio,
      result.stable_sample_count,
      required_stable,
      result.worst_pair_ratio);
  snprintf(result.adoption, sizeof(result.adoption), "%s",
    !parity ? "blocked-output-mismatch" : (result.replacement_ready ? "promote-candidate" : "observe-only"));
  snprintf(result.status, sizeof(result.status), "%s",
    !parity ? "mismatch" : (result.replacement_ready ? "candidate-faster" : (result.speed_ratio < 1.0 ? "candidate-no-margin" : "baseline-faster")));
  result.passed = parity;
  result.elapsed_ms = (unsigned long)((result.scratch_us + result.gmp_us + 999ULL) / 1000ULL);
  snprintf(result.detail, sizeof(result.detail),
    "op=format-threshold digits=%zu threshold=%zu operandFamilies=1 samples=%zu stablePairs=%zu/%zu scratchUs=%llu gmpUs=%llu ratio=%.3f worstPairRatio=%.3f ratioMethod=paired-median max=%.2f candidate=decimal-horner baseline=mpz_get_str featureGate=decimal-format-handoff gmpClue=mpn_get_str-thresholds adoption=%s",
    digits,
    threshold,
    sample_count,
    stable_sample_count,
    sample_count,
    result.scratch_us,
    result.gmp_us,
    result.speed_ratio,
    result.worst_pair_ratio,
    result.max_allowed_speed_ratio,
    result.adoption);
  append_result(report, &result);
}

static void append_format_divider_probe_result(
  XrayBenchmarkReport *report,
  size_t digits,
  const char *mode,
  int parity,
  unsigned long long candidate_us,
  unsigned long long baseline_us,
  double paired_ratio,
  size_t stable_sample_count,
  size_t sample_count,
  double worst_pair_ratio) {
  XrayBenchmarkResult result;
  memset(&result, 0, sizeof(result));
  snprintf(result.name, sizeof(result.name), "kernel format divider %s %zu digits", mode, digits);
  snprintf(result.category, sizeof(result.category), "kernel-probe");
  snprintf(result.operation, sizeof(result.operation), "format-divider");
  result.digits = digits;
  result.scratch_us = candidate_us ? candidate_us : 1;
  result.gmp_us = baseline_us ? baseline_us : 1;
  result.speed_ratio = paired_ratio > 0.0 ? paired_ratio : (double)result.scratch_us / (double)result.gmp_us;
  result.max_allowed_speed_ratio = 0.98;
  result.stable_sample_count = stable_sample_count;
  result.sample_count = sample_count;
  result.worst_pair_ratio = worst_pair_ratio;
  result.parity_verified = parity;
  size_t required_stable = kernel_required_stable_samples(sample_count);
  result.replacement_ready = parity &&
    xray_benchmark_readiness_gate(
      result.speed_ratio,
      result.max_allowed_speed_ratio,
      result.stable_sample_count,
      required_stable,
      result.worst_pair_ratio);
  snprintf(result.adoption, sizeof(result.adoption), "%s",
    !parity ? "blocked-output-mismatch" : (result.replacement_ready ? "promote-candidate" : "observe-only"));
  snprintf(result.status, sizeof(result.status), "%s",
    !parity ? "mismatch" : (result.replacement_ready ? "candidate-faster" : (result.speed_ratio < 1.0 ? "candidate-no-margin" : "baseline-faster")));
  result.passed = parity;
  result.elapsed_ms = (unsigned long)((result.scratch_us + result.gmp_us + 999ULL) / 1000ULL);
  snprintf(result.detail, sizeof(result.detail),
    "op=format-divider digits=%zu mode=%s operandFamilies=1 samples=%zu stablePairs=%zu/%zu candidateUs=%llu baselineUs=%llu ratio=%.3f worstPairRatio=%.3f ratioMethod=paired-median max=%.2f candidate=decimal-horner-direct-divider baseline=current-scratch-format featureGate=decimal-format-divider gmpClue=mpn_get_str-division adoption=%s",
    digits,
    mode,
    sample_count,
    stable_sample_count,
    sample_count,
    result.scratch_us,
    result.gmp_us,
    result.speed_ratio,
    result.worst_pair_ratio,
    result.max_allowed_speed_ratio,
    result.adoption);
  append_result(report, &result);
}

static void append_format_wide_probe_result(
  XrayBenchmarkReport *report,
  size_t digits,
  int parity,
  unsigned long long candidate_us,
  unsigned long long baseline_us,
  double paired_ratio,
  size_t stable_sample_count,
  size_t sample_count,
  double worst_pair_ratio) {
  XrayBenchmarkResult result;
  memset(&result, 0, sizeof(result));
  snprintf(result.name, sizeof(result.name), "kernel format wide chunks %zu digits", digits);
  snprintf(result.category, sizeof(result.category), "kernel-probe");
  snprintf(result.operation, sizeof(result.operation), "format-wide");
  result.digits = digits;
  result.scratch_us = candidate_us ? candidate_us : 1;
  result.gmp_us = baseline_us ? baseline_us : 1;
  result.speed_ratio = paired_ratio > 0.0 ? paired_ratio : (double)result.scratch_us / (double)result.gmp_us;
  result.max_allowed_speed_ratio = 0.98;
  result.stable_sample_count = stable_sample_count;
  result.sample_count = sample_count;
  result.worst_pair_ratio = worst_pair_ratio;
  result.parity_verified = parity;
  size_t required_stable = kernel_required_stable_samples(sample_count);
  result.replacement_ready = parity &&
    xray_benchmark_readiness_gate(
      result.speed_ratio,
      result.max_allowed_speed_ratio,
      result.stable_sample_count,
      required_stable,
      result.worst_pair_ratio);
  snprintf(result.adoption, sizeof(result.adoption), "%s",
    !parity ? "blocked-output-mismatch" : (result.replacement_ready ? "promote-candidate" : "observe-only"));
  snprintf(result.status, sizeof(result.status), "%s",
    !parity ? "mismatch" : (result.replacement_ready ? "candidate-faster" : (result.speed_ratio < 1.0 ? "candidate-no-margin" : "baseline-faster")));
  result.passed = parity;
  result.elapsed_ms = (unsigned long)((result.scratch_us + result.gmp_us + 999ULL) / 1000ULL);
  snprintf(result.detail, sizeof(result.detail),
    "op=format-wide digits=%zu wideChunkDigits=19 operandFamilies=1 samples=%zu stablePairs=%zu/%zu candidateUs=%llu baselineUs=%llu ratio=%.3f worstPairRatio=%.3f ratioMethod=paired-median max=%.2f candidate=decimal-wide-chunks baseline=current-scratch-format featureGate=decimal-format-wide gmpClue=mpn_get_str-chunk-size adoption=%s",
    digits,
    sample_count,
    stable_sample_count,
    sample_count,
    result.scratch_us,
    result.gmp_us,
    result.speed_ratio,
    result.worst_pair_ratio,
    result.max_allowed_speed_ratio,
    result.adoption);
  append_result(report, &result);
}

static void append_format_folded_probe_result(
  XrayBenchmarkReport *report,
  size_t digits,
  int parity,
  unsigned long long candidate_us,
  unsigned long long baseline_us,
  double paired_ratio,
  size_t stable_sample_count,
  size_t sample_count,
  double worst_pair_ratio) {
  XrayBenchmarkResult result;
  memset(&result, 0, sizeof(result));
  snprintf(result.name, sizeof(result.name), "kernel format folded chunks %zu digits", digits);
  snprintf(result.category, sizeof(result.category), "kernel-probe");
  snprintf(result.operation, sizeof(result.operation), "format-folded");
  result.digits = digits;
  result.scratch_us = candidate_us ? candidate_us : 1;
  result.gmp_us = baseline_us ? baseline_us : 1;
  result.speed_ratio = paired_ratio > 0.0 ? paired_ratio : (double)result.scratch_us / (double)result.gmp_us;
  result.max_allowed_speed_ratio = 0.98;
  result.stable_sample_count = stable_sample_count;
  result.sample_count = sample_count;
  result.worst_pair_ratio = worst_pair_ratio;
  result.parity_verified = parity;
  size_t required_stable = kernel_required_stable_samples(sample_count);
  result.replacement_ready = parity &&
    xray_benchmark_readiness_gate(
      result.speed_ratio,
      result.max_allowed_speed_ratio,
      result.stable_sample_count,
      required_stable,
      result.worst_pair_ratio);
  snprintf(result.adoption, sizeof(result.adoption), "%s",
    !parity ? "blocked-output-mismatch" : (result.replacement_ready ? "promote-candidate" : "observe-only"));
  snprintf(result.status, sizeof(result.status), "%s",
    !parity ? "mismatch" : (result.replacement_ready ? "candidate-faster" : (result.speed_ratio < 1.0 ? "candidate-no-margin" : "baseline-faster")));
  result.passed = parity;
  result.elapsed_ms = (unsigned long)((result.scratch_us + result.gmp_us + 999ULL) / 1000ULL);
  snprintf(result.detail, sizeof(result.detail),
    "op=format-folded digits=%zu chunkDigits=9 operandFamilies=1 samples=%zu stablePairs=%zu/%zu candidateUs=%llu baselineUs=%llu ratio=%.3f worstPairRatio=%.3f ratioMethod=paired-median max=%.2f candidate=decimal-folded-2p64 baseline=current-scratch-format featureGate=decimal-format-folded gmpClue=mpn_get_str-constant-base adoption=%s",
    digits,
    sample_count,
    stable_sample_count,
    sample_count,
    result.scratch_us,
    result.gmp_us,
    result.speed_ratio,
    result.worst_pair_ratio,
    result.max_allowed_speed_ratio,
    result.adoption);
  append_result(report, &result);
}

static void append_format_pair_writer_probe_result(
  XrayBenchmarkReport *report,
  size_t digits,
  const char *mode,
  int parity,
  unsigned long long candidate_us,
  unsigned long long baseline_us,
  double paired_ratio,
  size_t stable_sample_count,
  size_t sample_count,
  double worst_pair_ratio) {
  XrayBenchmarkResult result;
  memset(&result, 0, sizeof(result));
  snprintf(result.name, sizeof(result.name), "kernel format pair writer %s %zu digits", mode, digits);
  snprintf(result.category, sizeof(result.category), "kernel-probe");
  snprintf(result.operation, sizeof(result.operation), "format-pair-writer");
  result.digits = digits;
  result.scratch_us = candidate_us ? candidate_us : 1;
  result.gmp_us = baseline_us ? baseline_us : 1;
  result.speed_ratio = paired_ratio > 0.0 ? paired_ratio : (double)result.scratch_us / (double)result.gmp_us;
  result.max_allowed_speed_ratio = 0.98;
  result.stable_sample_count = stable_sample_count;
  result.sample_count = sample_count;
  result.worst_pair_ratio = worst_pair_ratio;
  result.parity_verified = parity;
  size_t required_stable = kernel_required_stable_samples(sample_count);
  result.replacement_ready = parity &&
    xray_benchmark_readiness_gate(
      result.speed_ratio,
      result.max_allowed_speed_ratio,
      result.stable_sample_count,
      required_stable,
      result.worst_pair_ratio);
  snprintf(result.adoption, sizeof(result.adoption), "%s",
    !parity ? "blocked-output-mismatch" : (result.replacement_ready ? "promote-candidate" : "observe-only"));
  snprintf(result.status, sizeof(result.status), "%s",
    !parity ? "mismatch" : (result.replacement_ready ? "candidate-faster" : (result.speed_ratio < 1.0 ? "candidate-no-margin" : "baseline-faster")));
  result.passed = parity;
  result.elapsed_ms = (unsigned long)((result.scratch_us + result.gmp_us + 999ULL) / 1000ULL);
  snprintf(result.detail, sizeof(result.detail),
    "op=format-pair-writer digits=%zu mode=%s chunkDigits=9 operandFamilies=1 samples=%zu stablePairs=%zu/%zu candidateUs=%llu baselineUs=%llu ratio=%.3f worstPairRatio=%.3f ratioMethod=paired-median max=%.2f candidate=decimal-pair-writer baseline=legacy-scratch-format featureGate=decimal-format-pair-writer gmpClue=mpn_get_str-output-emission adoption=%s",
    digits,
    mode,
    sample_count,
    stable_sample_count,
    sample_count,
    result.scratch_us,
    result.gmp_us,
    result.speed_ratio,
    result.worst_pair_ratio,
    result.max_allowed_speed_ratio,
    result.adoption);
  append_result(report, &result);
}

static void append_format_variant_probe_result(
  XrayBenchmarkReport *report,
  const char *operation,
  const char *label,
  size_t digits,
  const char *mode,
  const char *timing_mode,
  const char *candidate,
  const char *baseline,
  const char *feature_gate,
  const char *gmp_clue,
  unsigned int chunk_digits,
  int parity,
  unsigned long long candidate_us,
  unsigned long long baseline_us,
  double paired_ratio,
  size_t stable_sample_count,
  size_t sample_count,
  double worst_pair_ratio) {
  XrayBenchmarkResult result;
  memset(&result, 0, sizeof(result));
  snprintf(result.name, sizeof(result.name), "kernel %s %s %zu digits", label, mode, digits);
  snprintf(result.category, sizeof(result.category), "kernel-probe");
  snprintf(result.operation, sizeof(result.operation), "%s", operation);
  result.digits = digits;
  result.scratch_us = candidate_us ? candidate_us : 1;
  result.gmp_us = baseline_us ? baseline_us : 1;
  result.speed_ratio = paired_ratio > 0.0 ? paired_ratio : (double)result.scratch_us / (double)result.gmp_us;
  result.max_allowed_speed_ratio = 0.98;
  result.stable_sample_count = stable_sample_count;
  result.sample_count = sample_count;
  result.worst_pair_ratio = worst_pair_ratio;
  result.parity_verified = parity;
  size_t required_stable = kernel_required_stable_samples(sample_count);
  result.replacement_ready = parity &&
    xray_benchmark_readiness_gate(
      result.speed_ratio,
      result.max_allowed_speed_ratio,
      result.stable_sample_count,
      required_stable,
      result.worst_pair_ratio);
  snprintf(result.adoption, sizeof(result.adoption), "%s",
    !parity ? "blocked-output-mismatch" : (result.replacement_ready ? "promote-candidate" : "observe-only"));
  snprintf(result.status, sizeof(result.status), "%s",
    !parity ? "mismatch" : (result.replacement_ready ? "candidate-faster" : (result.speed_ratio < 1.0 ? "candidate-no-margin" : "baseline-faster")));
  result.passed = parity;
  result.elapsed_ms = (unsigned long)((result.scratch_us + result.gmp_us + 999ULL) / 1000ULL);
  snprintf(result.detail, sizeof(result.detail),
    "op=%s digits=%zu mode=%s timing=%s chunkDigits=%u operandFamilies=1 samples=%zu stablePairs=%zu/%zu ratio=%.3f worstPairRatio=%.3f ratioMethod=paired-median candidate=%s baseline=%s featureGate=%s gmpClue=%s adoption=%s",
    operation,
    digits,
    mode,
    timing_mode ? timing_mode : "block",
    chunk_digits,
    sample_count,
    stable_sample_count,
    sample_count,
    result.speed_ratio,
    result.worst_pair_ratio,
    candidate,
    baseline,
    feature_gate,
    gmp_clue,
    result.adoption);
  append_result(report, &result);
}

static void append_mul_toom3_probe_result(
  XrayBenchmarkReport *report,
  size_t digits,
  size_t leaf_threshold,
  size_t operand_families,
  int parity,
  unsigned long long scratch_us,
  unsigned long long gmp_us,
  double paired_ratio,
  size_t stable_sample_count,
  size_t sample_count,
  double worst_pair_ratio) {
  XrayBenchmarkResult result;
  memset(&result, 0, sizeof(result));
  snprintf(result.name, sizeof(result.name), "kernel mul Toom-3 leaf %zu limbs %zu digits", leaf_threshold, digits);
  snprintf(result.category, sizeof(result.category), "kernel-probe");
  snprintf(result.operation, sizeof(result.operation), "mul-toom3");
  result.digits = digits;
  result.scratch_us = scratch_us ? scratch_us : 1;
  result.gmp_us = gmp_us ? gmp_us : 1;
  result.speed_ratio = paired_ratio > 0.0 ? paired_ratio : (double)result.scratch_us / (double)result.gmp_us;
  result.max_allowed_speed_ratio = 0.98;
  result.stable_sample_count = stable_sample_count;
  result.sample_count = sample_count;
  result.worst_pair_ratio = worst_pair_ratio;
  result.parity_verified = parity;
  size_t required_stable = kernel_required_stable_samples(sample_count);
  result.replacement_ready = parity &&
    xray_benchmark_readiness_gate(
      result.speed_ratio,
      result.max_allowed_speed_ratio,
      result.stable_sample_count,
      required_stable,
      result.worst_pair_ratio);
  snprintf(result.adoption, sizeof(result.adoption), "%s",
    !parity ? "blocked-output-mismatch" : (result.replacement_ready ? "promote-candidate" : "observe-only"));
  snprintf(result.status, sizeof(result.status), "%s",
    !parity ? "mismatch" : (result.replacement_ready ? "candidate-faster" : (result.speed_ratio < 1.0 ? "candidate-no-margin" : "baseline-faster")));
  result.passed = parity;
  result.elapsed_ms = (unsigned long)((result.scratch_us + result.gmp_us + 999ULL) / 1000ULL);
  snprintf(result.detail, sizeof(result.detail),
    "op=mul-toom3 digits=%zu leafThreshold=%zu operandFamilies=%zu samples=%zu stablePairs=%zu/%zu scratchUs=%llu gmpUs=%llu ratio=%.3f worstPairRatio=%.3f ratioMethod=paired-median max=%.2f featureGate=one-level-toom3 gmpClue=toom33-thresholds adoption=%s",
    digits,
    leaf_threshold,
    operand_families,
    sample_count,
    stable_sample_count,
    sample_count,
    result.scratch_us,
    result.gmp_us,
    result.speed_ratio,
    result.worst_pair_ratio,
    result.max_allowed_speed_ratio,
    result.adoption);
  append_result(report, &result);
}

static void append_mul_toom3_vs_scratch_probe_result(
  XrayBenchmarkReport *report,
  size_t digits,
  size_t leaf_threshold,
  size_t operand_families,
  int parity,
  unsigned long long candidate_us,
  unsigned long long baseline_us,
  double paired_ratio,
  size_t stable_sample_count,
  size_t sample_count,
  double worst_pair_ratio) {
  XrayBenchmarkResult result;
  memset(&result, 0, sizeof(result));
  snprintf(result.name, sizeof(result.name), "kernel mul Toom-3 versus scratch leaf %zu limbs %zu digits", leaf_threshold, digits);
  snprintf(result.category, sizeof(result.category), "kernel-probe");
  snprintf(result.operation, sizeof(result.operation), "mul-toom3-vs-scratch");
  result.digits = digits;
  result.scratch_us = candidate_us ? candidate_us : 1;
  result.gmp_us = baseline_us ? baseline_us : 1;
  result.speed_ratio = paired_ratio > 0.0 ? paired_ratio : (double)result.scratch_us / (double)result.gmp_us;
  result.max_allowed_speed_ratio = 0.98;
  result.stable_sample_count = stable_sample_count;
  result.sample_count = sample_count;
  result.worst_pair_ratio = worst_pair_ratio;
  result.parity_verified = parity;
  size_t required_stable = kernel_required_stable_samples(sample_count);
  result.replacement_ready = parity &&
    xray_benchmark_readiness_gate(
      result.speed_ratio,
      result.max_allowed_speed_ratio,
      result.stable_sample_count,
      required_stable,
      result.worst_pair_ratio);
  snprintf(result.adoption, sizeof(result.adoption), "%s",
    !parity ? "blocked-output-mismatch" : (result.replacement_ready ? "promote-candidate" : "observe-only"));
  snprintf(result.status, sizeof(result.status), "%s",
    !parity ? "mismatch" : (result.replacement_ready ? "candidate-faster" : (result.speed_ratio < 1.0 ? "candidate-no-margin" : "baseline-faster")));
  result.passed = parity;
  result.elapsed_ms = (unsigned long)((result.scratch_us + result.gmp_us + 999ULL) / 1000ULL);
  snprintf(result.detail, sizeof(result.detail),
    "op=mul-toom3-vs-scratch digits=%zu leafThreshold=%zu operandFamilies=%zu samples=%zu stablePairs=%zu/%zu toom3Us=%llu currentScratchUs=%llu ratio=%.3f worstPairRatio=%.3f ratioMethod=paired-median max=%.2f candidate=one-level-toom3 baseline=current-scratch-mul featureGate=internal-promotion gmpClue=toom33-thresholds adoption=%s",
    digits,
    leaf_threshold,
    operand_families,
    sample_count,
    stable_sample_count,
    sample_count,
    result.scratch_us,
    result.gmp_us,
    result.speed_ratio,
    result.worst_pair_ratio,
    result.max_allowed_speed_ratio,
    result.adoption);
  append_result(report, &result);
}

#if XRAY_HAS_MSVC_BMI2_ADX_INTRINSICS
static void append_mul_toom3_unroll4_vs_scratch_probe_result(
  XrayBenchmarkReport *report,
  size_t digits,
  size_t leaf_threshold,
  size_t operand_families,
  int parity,
  unsigned long long candidate_us,
  unsigned long long baseline_us,
  double paired_ratio,
  size_t stable_sample_count,
  size_t sample_count,
  double worst_pair_ratio) {
  XrayBenchmarkResult result;
  memset(&result, 0, sizeof(result));
  snprintf(result.name, sizeof(result.name), "kernel mul Toom-3+unroll4 vs scratch %zu digits", digits);
  snprintf(result.category, sizeof(result.category), "kernel-probe");
  snprintf(result.operation, sizeof(result.operation), "mul-toom3-unroll4-vs-scratch");
  result.digits = digits;
  result.scratch_us = candidate_us ? candidate_us : 1;
  result.gmp_us = baseline_us ? baseline_us : 1;
  result.speed_ratio = paired_ratio > 0.0 ? paired_ratio : (double)result.scratch_us / (double)result.gmp_us;
  result.max_allowed_speed_ratio = 0.98;
  result.stable_sample_count = stable_sample_count;
  result.sample_count = sample_count;
  result.worst_pair_ratio = worst_pair_ratio;
  result.parity_verified = parity;
  size_t required_stable = kernel_required_stable_samples(sample_count);
  result.replacement_ready = parity &&
    xray_benchmark_readiness_gate(
      result.speed_ratio,
      result.max_allowed_speed_ratio,
      result.stable_sample_count,
      required_stable,
      result.worst_pair_ratio);
  snprintf(result.adoption, sizeof(result.adoption), "%s",
    !parity ? "blocked-output-mismatch" : (result.replacement_ready ? "promote-candidate" : "observe-only"));
  snprintf(result.status, sizeof(result.status), "%s",
    !parity ? "mismatch" : (result.replacement_ready ? "candidate-faster" : (result.speed_ratio < 1.0 ? "candidate-no-margin" : "baseline-faster")));
  result.passed = parity;
  result.elapsed_ms = (unsigned long)((result.scratch_us + result.gmp_us + 999ULL) / 1000ULL);
  snprintf(result.detail, sizeof(result.detail),
    "op=mul-toom3-unroll4-vs-scratch digits=%zu leafThreshold=%zu operandFamilies=%zu samples=%zu stablePairs=%zu/%zu candidateUs=%llu currentScratchUs=%llu ratio=%.3f worstPairRatio=%.3f ratioMethod=paired-median max=%.2f candidate=one-level-toom3+unroll4-leaf baseline=current-scratch-mul featureGate=msvc-x64-toom3-unroll4 gmpClue=toom33-leaf-schedule adoption=%s",
    digits,
    leaf_threshold,
    operand_families,
    sample_count,
    stable_sample_count,
    sample_count,
    result.scratch_us,
    result.gmp_us,
    result.speed_ratio,
    result.worst_pair_ratio,
    result.max_allowed_speed_ratio,
    result.adoption);
  append_result(report, &result);
}

static void append_mul_toom3_unroll4_vs_gmp_probe_result(
  XrayBenchmarkReport *report,
  const char *operation_name,
  const char *name_prefix,
  size_t digits,
  size_t leaf_threshold,
  size_t operand_families,
  int parity,
  unsigned long long candidate_us,
  unsigned long long gmp_us,
  double paired_ratio,
  size_t stable_sample_count,
  size_t sample_count,
  double worst_pair_ratio) {
  XrayBenchmarkResult result;
  const char *operation = operation_name ? operation_name : "mul-toom3-unroll4-vs-gmp";
  const char *prefix = name_prefix ? name_prefix : "kernel mul Toom-3+unroll4 vs GMP";
  memset(&result, 0, sizeof(result));
  snprintf(result.name, sizeof(result.name), "%s %zu digits", prefix, digits);
  snprintf(result.category, sizeof(result.category), "kernel-probe");
  snprintf(result.operation, sizeof(result.operation), "%s", operation);
  result.digits = digits;
  result.scratch_us = candidate_us ? candidate_us : 1;
  result.gmp_us = gmp_us ? gmp_us : 1;
  result.speed_ratio = paired_ratio > 0.0 ? paired_ratio : (double)result.scratch_us / (double)result.gmp_us;
  result.max_allowed_speed_ratio = 0.98;
  result.stable_sample_count = stable_sample_count;
  result.sample_count = sample_count;
  result.worst_pair_ratio = worst_pair_ratio;
  result.parity_verified = parity;
  size_t required_stable = kernel_required_stable_samples(sample_count);
  result.replacement_ready = parity &&
    xray_benchmark_readiness_gate(
      result.speed_ratio,
      result.max_allowed_speed_ratio,
      result.stable_sample_count,
      required_stable,
      result.worst_pair_ratio);
  snprintf(result.adoption, sizeof(result.adoption), "%s",
    !parity ? "blocked-output-mismatch" : (result.replacement_ready ? "promote-candidate" : "observe-only"));
  snprintf(result.status, sizeof(result.status), "%s",
    !parity ? "mismatch" : (result.replacement_ready ? "candidate-faster" : (result.speed_ratio < 1.0 ? "candidate-no-margin" : "gmp-faster")));
  result.passed = parity;
  result.elapsed_ms = (unsigned long)((result.scratch_us + result.gmp_us + 999ULL) / 1000ULL);
  snprintf(result.detail, sizeof(result.detail),
    "op=%s digits=%zu leafThreshold=%zu operandFamilies=%zu samples=%zu stablePairs=%zu/%zu candidateUs=%llu gmpUs=%llu ratio=%.3f worstPairRatio=%.3f ratioMethod=paired-median max=%.2f candidate=one-level-toom3+unroll4-leaf baseline=mpz_mul featureGate=msvc-x64-toom3-unroll4 gmpClue=toom33-leaf-schedule adoption=%s",
    operation,
    digits,
    leaf_threshold,
    operand_families,
    sample_count,
    stable_sample_count,
    sample_count,
    result.scratch_us,
    result.gmp_us,
    result.speed_ratio,
    result.worst_pair_ratio,
    result.max_allowed_speed_ratio,
    result.adoption);
  append_result(report, &result);
}

static void append_mul_toom3_unroll4_recursive_vs_gmp_probe_result(
  XrayBenchmarkReport *report,
  const char *operation_name,
  const char *name_prefix,
  size_t digits,
  size_t leaf_threshold,
  size_t depth_limit,
  size_t operand_families,
  int parity,
  unsigned long long candidate_us,
  unsigned long long gmp_us,
  double paired_ratio,
  size_t stable_sample_count,
  size_t sample_count,
  double worst_pair_ratio) {
  XrayBenchmarkResult result;
  const char *operation = operation_name ? operation_name : "mul-toom3-u4-rec-vs-gmp";
  const char *prefix = name_prefix ? name_prefix : "kernel recursive Toom-3+unroll4 vs GMP";
  memset(&result, 0, sizeof(result));
  snprintf(result.name, sizeof(result.name), "%s %zu digits", prefix, digits);
  snprintf(result.category, sizeof(result.category), "kernel-probe");
  snprintf(result.operation, sizeof(result.operation), "%s", operation);
  result.digits = digits;
  result.scratch_us = candidate_us ? candidate_us : 1;
  result.gmp_us = gmp_us ? gmp_us : 1;
  result.speed_ratio = paired_ratio > 0.0 ? paired_ratio : (double)result.scratch_us / (double)result.gmp_us;
  result.max_allowed_speed_ratio = 0.98;
  result.stable_sample_count = stable_sample_count;
  result.sample_count = sample_count;
  result.worst_pair_ratio = worst_pair_ratio;
  result.parity_verified = parity;
  size_t required_stable = kernel_required_stable_samples(sample_count);
  result.replacement_ready = parity &&
    xray_benchmark_readiness_gate(
      result.speed_ratio,
      result.max_allowed_speed_ratio,
      result.stable_sample_count,
      required_stable,
      result.worst_pair_ratio);
  snprintf(result.adoption, sizeof(result.adoption), "%s",
    !parity ? "blocked-output-mismatch" : (result.replacement_ready ? "promote-candidate" : "observe-only"));
  snprintf(result.status, sizeof(result.status), "%s",
    !parity ? "mismatch" : (result.replacement_ready ? "candidate-faster" : (result.speed_ratio < 1.0 ? "candidate-no-margin" : "gmp-faster")));
  result.passed = parity;
  result.elapsed_ms = (unsigned long)((result.scratch_us + result.gmp_us + 999ULL) / 1000ULL);
  snprintf(result.detail, sizeof(result.detail),
    "op=%s digits=%zu leafThreshold=%zu depthLimit=%zu operandFamilies=%zu samples=%zu stablePairs=%zu/%zu candidateUs=%llu gmpUs=%llu ratio=%.3f worstPairRatio=%.3f ratioMethod=paired-median max=%.2f candidate=recursive-toom3+unroll4 baseline=mpz_mul featureGate=msvc-x64-recursive-toom3 gmpClue=toom33-recursive adoption=%s",
    operation,
    digits,
    leaf_threshold,
    depth_limit,
    operand_families,
    sample_count,
    stable_sample_count,
    sample_count,
    result.scratch_us,
    result.gmp_us,
    result.speed_ratio,
    result.worst_pair_ratio,
    result.max_allowed_speed_ratio,
    result.adoption);
  append_result(report, &result);
}
#endif

static void append_mul_unroll4_vs_scratch_probe_result(
  XrayBenchmarkReport *report,
  size_t digits,
  size_t leaf_threshold,
  size_t operand_families,
  int parity,
  unsigned long long candidate_us,
  unsigned long long baseline_us,
  double paired_ratio,
  size_t stable_sample_count,
  size_t sample_count,
  double worst_pair_ratio) {
  XrayBenchmarkResult result;
  memset(&result, 0, sizeof(result));
  snprintf(result.name, sizeof(result.name), "kernel mul unroll4 versus scratch leaf %zu limbs %zu digits", leaf_threshold, digits);
  snprintf(result.category, sizeof(result.category), "kernel-probe");
  snprintf(result.operation, sizeof(result.operation), "mul-unroll4-vs-scratch");
  result.digits = digits;
  result.scratch_us = candidate_us ? candidate_us : 1;
  result.gmp_us = baseline_us ? baseline_us : 1;
  result.speed_ratio = paired_ratio > 0.0 ? paired_ratio : (double)result.scratch_us / (double)result.gmp_us;
  result.max_allowed_speed_ratio = 0.98;
  result.stable_sample_count = stable_sample_count;
  result.sample_count = sample_count;
  result.worst_pair_ratio = worst_pair_ratio;
  result.parity_verified = parity;
  size_t required_stable = kernel_required_stable_samples(sample_count);
  result.replacement_ready = parity &&
    xray_benchmark_readiness_gate(
      result.speed_ratio,
      result.max_allowed_speed_ratio,
      result.stable_sample_count,
      required_stable,
      result.worst_pair_ratio);
  snprintf(result.adoption, sizeof(result.adoption), "%s",
    !parity ? "blocked-output-mismatch" : (result.replacement_ready ? "promote-candidate" : "observe-only"));
  snprintf(result.status, sizeof(result.status), "%s",
    !parity ? "mismatch" : (result.replacement_ready ? "candidate-faster" : (result.speed_ratio < 1.0 ? "candidate-no-margin" : "baseline-faster")));
  result.passed = parity;
  result.elapsed_ms = (unsigned long)((result.scratch_us + result.gmp_us + 999ULL) / 1000ULL);
  snprintf(result.detail, sizeof(result.detail),
    "op=mul-unroll4-vs-scratch digits=%zu leafThreshold=%zu operandFamilies=%zu samples=%zu stablePairs=%zu/%zu unroll4Us=%llu scalarScratchUs=%llu ratio=%.3f worstPairRatio=%.3f ratioMethod=paired-median max=%.2f candidate=_umul128+_addcarry_u64-unroll4-full baseline=scalar-threshold-mul featureGate=msvc-x64-full-mul-schedule gmpClue=mpn-addmul-loop-scheduling adoption=%s",
    digits,
    leaf_threshold,
    operand_families,
    sample_count,
    stable_sample_count,
    sample_count,
    result.scratch_us,
    result.gmp_us,
    result.speed_ratio,
    result.worst_pair_ratio,
    result.max_allowed_speed_ratio,
    result.adoption);
  append_result(report, &result);
}

static void append_mul_unroll4_vs_gmp_probe_result(
  XrayBenchmarkReport *report,
  const char *operation_name,
  const char *name_prefix,
  size_t digits,
  size_t leaf_threshold,
  size_t operand_families,
  int parity,
  unsigned long long candidate_us,
  unsigned long long gmp_us,
  double paired_ratio,
  size_t stable_sample_count,
  size_t sample_count,
  double worst_pair_ratio) {
  XrayBenchmarkResult result;
  const char *operation = operation_name ? operation_name : "mul-unroll4-vs-gmp";
  const char *prefix = name_prefix ? name_prefix : "kernel mul unroll4 versus GMP";
  memset(&result, 0, sizeof(result));
  snprintf(result.name, sizeof(result.name), "%s leaf %zu limbs %zu digits", prefix, leaf_threshold, digits);
  snprintf(result.category, sizeof(result.category), "kernel-probe");
  snprintf(result.operation, sizeof(result.operation), "%s", operation);
  result.digits = digits;
  result.scratch_us = candidate_us ? candidate_us : 1;
  result.gmp_us = gmp_us ? gmp_us : 1;
  result.speed_ratio = paired_ratio > 0.0 ? paired_ratio : (double)result.scratch_us / (double)result.gmp_us;
  result.max_allowed_speed_ratio = 0.98;
  result.stable_sample_count = stable_sample_count;
  result.sample_count = sample_count;
  result.worst_pair_ratio = worst_pair_ratio;
  result.parity_verified = parity;
  size_t required_stable = kernel_required_stable_samples(sample_count);
  result.replacement_ready = parity &&
    xray_benchmark_readiness_gate(
      result.speed_ratio,
      result.max_allowed_speed_ratio,
      result.stable_sample_count,
      required_stable,
      result.worst_pair_ratio);
  snprintf(result.adoption, sizeof(result.adoption), "%s",
    !parity ? "blocked-output-mismatch" : (result.replacement_ready ? "promote-candidate" : "observe-only"));
  snprintf(result.status, sizeof(result.status), "%s",
    !parity ? "mismatch" : (result.replacement_ready ? "candidate-faster" : (result.speed_ratio < 1.0 ? "candidate-no-margin" : "gmp-faster")));
  result.passed = parity;
  result.elapsed_ms = (unsigned long)((result.scratch_us + result.gmp_us + 999ULL) / 1000ULL);
  snprintf(result.detail, sizeof(result.detail),
    "op=%s digits=%zu leafThreshold=%zu operandFamilies=%zu samples=%zu stablePairs=%zu/%zu unroll4Us=%llu gmpUs=%llu ratio=%.3f worstPairRatio=%.3f ratioMethod=paired-median max=%.2f candidate=_umul128+_addcarry_u64-unroll4-full baseline=mpz_mul featureGate=msvc-x64-full-mul-schedule gmpClue=mpn-mul/addmul-thresholds adoption=%s",
    operation,
    digits,
    leaf_threshold,
    operand_families,
    sample_count,
    stable_sample_count,
    sample_count,
    result.scratch_us,
    result.gmp_us,
    result.speed_ratio,
    result.worst_pair_ratio,
    result.max_allowed_speed_ratio,
    result.adoption);
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
  unsigned long long scratch_samples[XRAY_BENCH_SAMPLES] = {0};
  unsigned long long gmp_samples[XRAY_BENCH_SAMPLES] = {0};
  int parity = 1;

  for (unsigned int sample = 0; sample < XRAY_BENCH_SAMPLES; ++sample) {
    unsigned long long scratch_started = xray_now_us();
    for (unsigned int index = 0; ok && index < iterations; ++index) {
      ok = xray_bigint_set_decimal(&scratch, text);
    }
    scratch_samples[sample] = xray_now_us() - scratch_started;

    unsigned long long gmp_started = xray_now_us();
    for (unsigned int index = 0; ok && index < iterations; ++index) {
      ok = mpz_set_str(gmp, text, 10) == 0;
    }
    gmp_samples[sample] = xray_now_us() - gmp_started;

    char *scratch_text = xray_bigint_get_decimal(&scratch);
    char *gmp_text = mpz_get_str(NULL, 10, gmp);
    parity = parity && ok && scratch_text && gmp_text && strcmp(scratch_text, gmp_text) == 0;
    free(scratch_text);
    free(gmp_text);
  }
  unsigned long long scratch_us = median_samples(scratch_samples, XRAY_BENCH_SAMPLES);
  unsigned long long gmp_us = median_samples(gmp_samples, XRAY_BENCH_SAMPLES);
  double paired_ratio = median_paired_ratio(scratch_samples, gmp_samples, XRAY_BENCH_SAMPLES);
  append_perf_result(
    report,
    "parse",
    digits,
    1,
    parity,
    scratch_us,
    gmp_us,
    paired_ratio,
    paired_ratio_wins(scratch_samples, gmp_samples, XRAY_BENCH_SAMPLES, 1.0),
    XRAY_BENCH_SAMPLES,
    max_paired_ratio(scratch_samples, gmp_samples, XRAY_BENCH_SAMPLES));

  mpz_clear(gmp);
  xray_bigint_clear(&scratch);
  free(text);
}

static void run_parse_chunk_probe_case(XrayBenchmarkReport *report, size_t digits, unsigned int chunk_digits) {
  char *text = benchmark_decimal(digits, 7U + chunk_digits, 1);
  if (!text) return;
  unsigned int iterations = perf_iterations("parse", digits);
  XrayScratchBigInt candidate, baseline;
  xray_bigint_init(&candidate);
  xray_bigint_init(&baseline);
  mpz_t gmp;
  mpz_init(gmp);
  int ok = mpz_set_str(gmp, text, 10) == 0;
  unsigned long long candidate_samples[XRAY_BENCH_SAMPLES] = {0};
  unsigned long long baseline_samples[XRAY_BENCH_SAMPLES] = {0};
  int parity = 1;

  for (unsigned int sample = 0; sample < XRAY_BENCH_SAMPLES; ++sample) {
    if ((sample % 2U) == 0U) {
      unsigned long long candidate_started = xray_now_us();
      for (unsigned int index = 0; ok && index < iterations; ++index) {
        ok = xray_bigint_set_decimal_chunk_probe(&candidate, text, chunk_digits);
      }
      candidate_samples[sample] = xray_now_us() - candidate_started;

      unsigned long long baseline_started = xray_now_us();
      for (unsigned int index = 0; ok && index < iterations; ++index) {
        ok = xray_bigint_set_decimal(&baseline, text);
      }
      baseline_samples[sample] = xray_now_us() - baseline_started;
    } else {
      unsigned long long baseline_started = xray_now_us();
      for (unsigned int index = 0; ok && index < iterations; ++index) {
        ok = xray_bigint_set_decimal(&baseline, text);
      }
      baseline_samples[sample] = xray_now_us() - baseline_started;

      unsigned long long candidate_started = xray_now_us();
      for (unsigned int index = 0; ok && index < iterations; ++index) {
        ok = xray_bigint_set_decimal_chunk_probe(&candidate, text, chunk_digits);
      }
      candidate_samples[sample] = xray_now_us() - candidate_started;
    }

    char *candidate_text = xray_bigint_get_decimal(&candidate);
    char *baseline_text = xray_bigint_get_decimal(&baseline);
    char *gmp_text = mpz_get_str(NULL, 10, gmp);
    parity = parity && ok && candidate_text && baseline_text && gmp_text &&
      strcmp(candidate_text, baseline_text) == 0 &&
      strcmp(candidate_text, gmp_text) == 0;
    free(candidate_text);
    free(baseline_text);
    free(gmp_text);
  }

  append_parse_chunk_probe_result(
    report,
    digits,
    chunk_digits,
    parity,
    median_samples(candidate_samples, XRAY_BENCH_SAMPLES),
    median_samples(baseline_samples, XRAY_BENCH_SAMPLES),
    median_paired_ratio(candidate_samples, baseline_samples, XRAY_BENCH_SAMPLES),
    paired_ratio_wins(candidate_samples, baseline_samples, XRAY_BENCH_SAMPLES, 0.98),
    XRAY_BENCH_SAMPLES,
    max_paired_ratio(candidate_samples, baseline_samples, XRAY_BENCH_SAMPLES));

  mpz_clear(gmp);
  xray_bigint_clear(&candidate);
  xray_bigint_clear(&baseline);
  free(text);
}

static void run_scratch_format_case(XrayBenchmarkReport *report, size_t digits) {
  char *text = benchmark_decimal(digits, 13, 1);
  if (!text) return;
  unsigned int iterations = perf_iterations("format", digits);
  XrayScratchBigInt scratch;
  xray_bigint_init(&scratch);
  mpz_t gmp;
  mpz_init(gmp);
  int ok = xray_bigint_set_decimal(&scratch, text) && mpz_set_str(gmp, text, 10) == 0;
  unsigned long long scratch_samples[XRAY_BENCH_SAMPLES] = {0};
  unsigned long long gmp_samples[XRAY_BENCH_SAMPLES] = {0};
  int parity = 1;

  for (unsigned int sample = 0; sample < XRAY_BENCH_SAMPLES; ++sample) {
    unsigned long long scratch_started = xray_now_us();
    for (unsigned int index = 0; ok && index < iterations; ++index) {
      char *scratch_text = xray_bigint_get_decimal(&scratch);
      ok = scratch_text != NULL;
      free(scratch_text);
    }
    scratch_samples[sample] = xray_now_us() - scratch_started;

    unsigned long long gmp_started = xray_now_us();
    for (unsigned int index = 0; ok && index < iterations; ++index) {
      char *gmp_text = mpz_get_str(NULL, 10, gmp);
      ok = gmp_text != NULL;
      free(gmp_text);
    }
    gmp_samples[sample] = xray_now_us() - gmp_started;

    char *scratch_text = xray_bigint_get_decimal(&scratch);
    char *gmp_text = mpz_get_str(NULL, 10, gmp);
    parity = parity && ok && scratch_text && gmp_text && strcmp(scratch_text, gmp_text) == 0;
    free(scratch_text);
    free(gmp_text);
  }
  unsigned long long scratch_us = median_samples(scratch_samples, XRAY_BENCH_SAMPLES);
  unsigned long long gmp_us = median_samples(gmp_samples, XRAY_BENCH_SAMPLES);
  double paired_ratio = median_paired_ratio(scratch_samples, gmp_samples, XRAY_BENCH_SAMPLES);
  append_perf_result(
    report,
    "format",
    digits,
    1,
    parity,
    scratch_us,
    gmp_us,
    paired_ratio,
    paired_ratio_wins(scratch_samples, gmp_samples, XRAY_BENCH_SAMPLES, 1.0),
    XRAY_BENCH_SAMPLES,
    max_paired_ratio(scratch_samples, gmp_samples, XRAY_BENCH_SAMPLES));

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

  unsigned long long scratch_samples[XRAY_BENCH_SAMPLES] = {0};
  unsigned long long gmp_samples[XRAY_BENCH_SAMPLES] = {0};
  int parity = 1;
  for (unsigned int sample = 0; sample < XRAY_BENCH_SAMPLES; ++sample) {
    unsigned long long scratch_started = xray_now_us();
    for (unsigned int index = 0; ok && index < iterations; ++index) {
      if (strcmp(operation, "add") == 0) ok = xray_bigint_add(&scratch_out, &a, &b);
      else if (strcmp(operation, "sub") == 0) ok = xray_bigint_sub(&scratch_out, &a, &b);
      else ok = xray_bigint_mul(&scratch_out, &a, &b);
    }
    scratch_samples[sample] = xray_now_us() - scratch_started;

    unsigned long long gmp_started = xray_now_us();
    for (unsigned int index = 0; ok && index < iterations; ++index) {
      if (strcmp(operation, "add") == 0) mpz_add(gout, ga, gb);
      else if (strcmp(operation, "sub") == 0) mpz_sub(gout, ga, gb);
      else mpz_mul(gout, ga, gb);
    }
    gmp_samples[sample] = xray_now_us() - gmp_started;

    char *scratch_text = xray_bigint_get_decimal(&scratch_out);
    char *gmp_text = mpz_get_str(NULL, 10, gout);
    parity = parity && ok && scratch_text && gmp_text && strcmp(scratch_text, gmp_text) == 0;
    free(scratch_text);
    free(gmp_text);
  }
  unsigned long long scratch_us = median_samples(scratch_samples, XRAY_BENCH_SAMPLES);
  unsigned long long gmp_us = median_samples(gmp_samples, XRAY_BENCH_SAMPLES);
  double paired_ratio = median_paired_ratio(scratch_samples, gmp_samples, XRAY_BENCH_SAMPLES);
  append_perf_result(
    report,
    operation,
    digits,
    1,
    parity,
    scratch_us,
    gmp_us,
    paired_ratio,
    paired_ratio_wins(scratch_samples, gmp_samples, XRAY_BENCH_SAMPLES, 1.0),
    XRAY_BENCH_SAMPLES,
    max_paired_ratio(scratch_samples, gmp_samples, XRAY_BENCH_SAMPLES));

  mpz_clears(ga, gb, gout, NULL);
  xray_bigint_clear(&a);
  xray_bigint_clear(&b);
  xray_bigint_clear(&scratch_out);
  free(left_text);
  free(right_text);
}

static void run_mul_threshold_probe_case(XrayBenchmarkReport *report, size_t digits, size_t threshold) {
  char *left_text[XRAY_MUL_OPERAND_FAMILIES] = {0};
  char *right_text[XRAY_MUL_OPERAND_FAMILIES] = {0};
  XrayScratchBigInt a[XRAY_MUL_OPERAND_FAMILIES];
  XrayScratchBigInt b[XRAY_MUL_OPERAND_FAMILIES];
  XrayScratchBigInt scratch_out[XRAY_MUL_OPERAND_FAMILIES];
  mpz_t ga[XRAY_MUL_OPERAND_FAMILIES];
  mpz_t gb[XRAY_MUL_OPERAND_FAMILIES];
  mpz_t gout[XRAY_MUL_OPERAND_FAMILIES];

  unsigned int iterations = perf_iterations("mul", digits);
  int ok = 1;
  for (size_t family = 0; family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
    xray_bigint_init(&a[family]);
    xray_bigint_init(&b[family]);
    xray_bigint_init(&scratch_out[family]);
    mpz_inits(ga[family], gb[family], gout[family], NULL);
    left_text[family] = benchmark_decimal(digits, mul_operand_families[family].left_seed, mul_operand_families[family].left_high_lead);
    right_text[family] = benchmark_decimal(digits, mul_operand_families[family].right_seed, mul_operand_families[family].right_high_lead);
    ok = ok &&
      left_text[family] &&
      right_text[family] &&
      xray_bigint_set_decimal(&a[family], left_text[family]) &&
      xray_bigint_set_decimal(&b[family], right_text[family]) &&
      mpz_set_str(ga[family], left_text[family], 10) == 0 &&
      mpz_set_str(gb[family], right_text[family], 10) == 0;
  }

  unsigned long long scratch_samples[XRAY_BENCH_SAMPLES] = {0};
  unsigned long long gmp_samples[XRAY_BENCH_SAMPLES] = {0};
  int parity = 1;
  for (unsigned int sample = 0; sample < XRAY_BENCH_SAMPLES; ++sample) {
    unsigned long long scratch_started = xray_now_us();
    for (unsigned int index = 0; ok && index < iterations; ++index) {
      for (size_t family = 0; ok && family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
        ok = xray_bigint_mul_with_threshold(&scratch_out[family], &a[family], &b[family], threshold);
      }
    }
    scratch_samples[sample] = xray_now_us() - scratch_started;

    unsigned long long gmp_started = xray_now_us();
    for (unsigned int index = 0; ok && index < iterations; ++index) {
      for (size_t family = 0; family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
        mpz_mul(gout[family], ga[family], gb[family]);
      }
    }
    gmp_samples[sample] = xray_now_us() - gmp_started;

    for (size_t family = 0; family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
      char *scratch_text = xray_bigint_get_decimal(&scratch_out[family]);
      char *gmp_text = mpz_get_str(NULL, 10, gout[family]);
      parity = parity && ok && scratch_text && gmp_text && strcmp(scratch_text, gmp_text) == 0;
      free(scratch_text);
      free(gmp_text);
    }
  }

  append_mul_threshold_probe_result(
    report,
    digits,
    threshold,
    XRAY_MUL_OPERAND_FAMILIES,
    parity,
    median_samples(scratch_samples, XRAY_BENCH_SAMPLES),
    median_samples(gmp_samples, XRAY_BENCH_SAMPLES),
    median_paired_ratio(scratch_samples, gmp_samples, XRAY_BENCH_SAMPLES),
    paired_ratio_wins(scratch_samples, gmp_samples, XRAY_BENCH_SAMPLES, 0.98),
    XRAY_BENCH_SAMPLES,
    max_paired_ratio(scratch_samples, gmp_samples, XRAY_BENCH_SAMPLES));

  for (size_t family = 0; family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
    mpz_clears(ga[family], gb[family], gout[family], NULL);
    xray_bigint_clear(&a[family]);
    xray_bigint_clear(&b[family]);
    xray_bigint_clear(&scratch_out[family]);
    free(left_text[family]);
    free(right_text[family]);
  }
}

static void run_mul_karatsuba_middle_probe_case(XrayBenchmarkReport *report, size_t digits, size_t threshold) {
  char *left_text[XRAY_MUL_OPERAND_FAMILIES] = {0};
  char *right_text[XRAY_MUL_OPERAND_FAMILIES] = {0};
  XrayScratchBigInt a[XRAY_MUL_OPERAND_FAMILIES];
  XrayScratchBigInt b[XRAY_MUL_OPERAND_FAMILIES];
  XrayScratchBigInt candidate_out[XRAY_MUL_OPERAND_FAMILIES];
  XrayScratchBigInt baseline_out[XRAY_MUL_OPERAND_FAMILIES];
  mpz_t ga[XRAY_MUL_OPERAND_FAMILIES];
  mpz_t gb[XRAY_MUL_OPERAND_FAMILIES];
  mpz_t gout[XRAY_MUL_OPERAND_FAMILIES];

  unsigned int iterations = perf_iterations("mul", digits);
  int ok = 1;
  for (size_t family = 0; family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
    xray_bigint_init(&a[family]);
    xray_bigint_init(&b[family]);
    xray_bigint_init(&candidate_out[family]);
    xray_bigint_init(&baseline_out[family]);
    mpz_inits(ga[family], gb[family], gout[family], NULL);
    left_text[family] = benchmark_decimal(digits, mul_operand_families[family].left_seed, mul_operand_families[family].left_high_lead);
    right_text[family] = benchmark_decimal(digits, mul_operand_families[family].right_seed, mul_operand_families[family].right_high_lead);
    ok = ok &&
      left_text[family] &&
      right_text[family] &&
      xray_bigint_set_decimal(&a[family], left_text[family]) &&
      xray_bigint_set_decimal(&b[family], right_text[family]) &&
      mpz_set_str(ga[family], left_text[family], 10) == 0 &&
      mpz_set_str(gb[family], right_text[family], 10) == 0;
  }

  unsigned long long candidate_samples[XRAY_BENCH_SAMPLES] = {0};
  unsigned long long baseline_samples[XRAY_BENCH_SAMPLES] = {0};
  int parity = 1;
  for (unsigned int sample = 0; sample < XRAY_BENCH_SAMPLES; ++sample) {
    unsigned long long candidate_started = xray_now_us();
    for (unsigned int index = 0; ok && index < iterations; ++index) {
      for (size_t family = 0; ok && family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
        ok = xray_bigint_mul_karatsuba_sum_probe(&candidate_out[family], &a[family], &b[family], threshold);
      }
    }
    candidate_samples[sample] = xray_now_us() - candidate_started;

    unsigned long long baseline_started = xray_now_us();
    for (unsigned int index = 0; ok && index < iterations; ++index) {
      for (size_t family = 0; ok && family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
        ok = xray_bigint_mul_with_threshold(&baseline_out[family], &a[family], &b[family], threshold);
      }
    }
    baseline_samples[sample] = xray_now_us() - baseline_started;

    for (size_t family = 0; family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
      mpz_mul(gout[family], ga[family], gb[family]);
      char *candidate_text = xray_bigint_get_decimal(&candidate_out[family]);
      char *baseline_text = xray_bigint_get_decimal(&baseline_out[family]);
      char *gmp_text = mpz_get_str(NULL, 10, gout[family]);
      parity = parity &&
        ok &&
        candidate_text &&
        baseline_text &&
        gmp_text &&
        strcmp(candidate_text, baseline_text) == 0 &&
        strcmp(candidate_text, gmp_text) == 0;
      free(candidate_text);
      free(baseline_text);
      free(gmp_text);
    }
  }

  append_mul_karatsuba_middle_probe_result(
    report,
    digits,
    threshold,
    XRAY_MUL_OPERAND_FAMILIES,
    parity,
    median_samples(candidate_samples, XRAY_BENCH_SAMPLES),
    median_samples(baseline_samples, XRAY_BENCH_SAMPLES),
    median_paired_ratio(candidate_samples, baseline_samples, XRAY_BENCH_SAMPLES),
    paired_ratio_wins(candidate_samples, baseline_samples, XRAY_BENCH_SAMPLES, 0.98),
    XRAY_BENCH_SAMPLES,
    max_paired_ratio(candidate_samples, baseline_samples, XRAY_BENCH_SAMPLES));

  for (size_t family = 0; family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
    mpz_clears(ga[family], gb[family], gout[family], NULL);
    xray_bigint_clear(&a[family]);
    xray_bigint_clear(&b[family]);
    xray_bigint_clear(&candidate_out[family]);
    xray_bigint_clear(&baseline_out[family]);
    free(left_text[family]);
    free(right_text[family]);
  }
}

static void run_format_threshold_probe_case(XrayBenchmarkReport *report, size_t digits, size_t threshold) {
  char *text = benchmark_decimal(digits, 13, 1);
  if (!text) return;
  unsigned int iterations = perf_iterations("format", digits);
  XrayScratchBigInt scratch;
  xray_bigint_init(&scratch);
  mpz_t gmp;
  mpz_init(gmp);
  int ok = xray_bigint_set_decimal(&scratch, text) && mpz_set_str(gmp, text, 10) == 0;
  unsigned long long scratch_samples[XRAY_BENCH_SAMPLES] = {0};
  unsigned long long gmp_samples[XRAY_BENCH_SAMPLES] = {0};
  int parity = 1;

  for (unsigned int sample = 0; sample < XRAY_BENCH_SAMPLES; ++sample) {
    unsigned long long scratch_started = xray_now_us();
    for (unsigned int index = 0; ok && index < iterations; ++index) {
      char *scratch_text = xray_bigint_get_decimal_horner_threshold_probe(&scratch, threshold);
      ok = scratch_text != NULL;
      free(scratch_text);
    }
    scratch_samples[sample] = xray_now_us() - scratch_started;

    unsigned long long gmp_started = xray_now_us();
    for (unsigned int index = 0; ok && index < iterations; ++index) {
      char *gmp_text = mpz_get_str(NULL, 10, gmp);
      ok = gmp_text != NULL;
      free(gmp_text);
    }
    gmp_samples[sample] = xray_now_us() - gmp_started;

    char *scratch_text = xray_bigint_get_decimal_horner_threshold_probe(&scratch, threshold);
    char *gmp_text = mpz_get_str(NULL, 10, gmp);
    parity = parity && ok && scratch_text && gmp_text && strcmp(scratch_text, gmp_text) == 0;
    free(scratch_text);
    free(gmp_text);
  }

  append_format_threshold_probe_result(
    report,
    digits,
    threshold,
    parity,
    median_samples(scratch_samples, XRAY_BENCH_SAMPLES),
    median_samples(gmp_samples, XRAY_BENCH_SAMPLES),
    median_paired_ratio(scratch_samples, gmp_samples, XRAY_BENCH_SAMPLES),
    paired_ratio_wins(scratch_samples, gmp_samples, XRAY_BENCH_SAMPLES, 0.98),
    XRAY_BENCH_SAMPLES,
    max_paired_ratio(scratch_samples, gmp_samples, XRAY_BENCH_SAMPLES));

  mpz_clear(gmp);
  xray_bigint_clear(&scratch);
  free(text);
}

static void run_format_divider_probe_case(XrayBenchmarkReport *report, size_t digits) {
  char *text = benchmark_decimal(digits, 13, 1);
  if (!text) return;
  unsigned int iterations = perf_iterations("format", digits);
  XrayScratchBigInt scratch;
  xray_bigint_init(&scratch);
  mpz_t gmp;
  mpz_init(gmp);
  int ok = xray_bigint_set_decimal(&scratch, text) && mpz_set_str(gmp, text, 10) == 0;
  unsigned long long candidate_samples[XRAY_BENCH_SAMPLES] = {0};
  unsigned long long baseline_samples[XRAY_BENCH_SAMPLES] = {0};
  int parity = 1;

  for (unsigned int sample = 0; sample < XRAY_BENCH_SAMPLES; ++sample) {
    unsigned long long candidate_started = xray_now_us();
    for (unsigned int index = 0; ok && index < iterations; ++index) {
      char *candidate_text = xray_bigint_get_decimal_divider_probe(&scratch, 1);
      ok = candidate_text != NULL;
      free(candidate_text);
    }
    candidate_samples[sample] = xray_now_us() - candidate_started;

    unsigned long long baseline_started = xray_now_us();
    for (unsigned int index = 0; ok && index < iterations; ++index) {
      char *baseline_text = xray_bigint_get_decimal_divider_probe(&scratch, 0);
      ok = baseline_text != NULL;
      free(baseline_text);
    }
    baseline_samples[sample] = xray_now_us() - baseline_started;

    char *candidate_text = xray_bigint_get_decimal_divider_probe(&scratch, 1);
    char *baseline_text = xray_bigint_get_decimal_divider_probe(&scratch, 0);
    char *gmp_text = mpz_get_str(NULL, 10, gmp);
    parity = parity && ok && candidate_text && baseline_text && gmp_text &&
      strcmp(candidate_text, baseline_text) == 0 &&
      strcmp(candidate_text, gmp_text) == 0;
    free(candidate_text);
    free(baseline_text);
    free(gmp_text);
  }

  append_format_divider_probe_result(
    report,
    digits,
    "direct128",
    parity,
    median_samples(candidate_samples, XRAY_BENCH_SAMPLES),
    median_samples(baseline_samples, XRAY_BENCH_SAMPLES),
    median_paired_ratio(candidate_samples, baseline_samples, XRAY_BENCH_SAMPLES),
    paired_ratio_wins(candidate_samples, baseline_samples, XRAY_BENCH_SAMPLES, 0.98),
    XRAY_BENCH_SAMPLES,
    max_paired_ratio(candidate_samples, baseline_samples, XRAY_BENCH_SAMPLES));

  mpz_clear(gmp);
  xray_bigint_clear(&scratch);
  free(text);
}

static void run_format_wide_probe_case(XrayBenchmarkReport *report, size_t digits) {
  char *text = benchmark_decimal(digits, 13, 1);
  if (!text) return;
  unsigned int iterations = perf_iterations("format", digits);
  XrayScratchBigInt scratch;
  xray_bigint_init(&scratch);
  mpz_t gmp;
  mpz_init(gmp);
  int ok = xray_bigint_set_decimal(&scratch, text) && mpz_set_str(gmp, text, 10) == 0;
  unsigned long long candidate_samples[XRAY_BENCH_SAMPLES] = {0};
  unsigned long long baseline_samples[XRAY_BENCH_SAMPLES] = {0};
  int parity = 1;

  for (unsigned int sample = 0; sample < XRAY_BENCH_SAMPLES; ++sample) {
    unsigned long long candidate_started = xray_now_us();
    for (unsigned int index = 0; ok && index < iterations; ++index) {
      char *candidate_text = xray_bigint_get_decimal_wide_probe(&scratch);
      ok = candidate_text != NULL;
      free(candidate_text);
    }
    candidate_samples[sample] = xray_now_us() - candidate_started;

    unsigned long long baseline_started = xray_now_us();
    for (unsigned int index = 0; ok && index < iterations; ++index) {
      char *baseline_text = xray_bigint_get_decimal(&scratch);
      ok = baseline_text != NULL;
      free(baseline_text);
    }
    baseline_samples[sample] = xray_now_us() - baseline_started;

    char *candidate_text = xray_bigint_get_decimal_wide_probe(&scratch);
    char *baseline_text = xray_bigint_get_decimal(&scratch);
    char *gmp_text = mpz_get_str(NULL, 10, gmp);
    parity = parity && ok && candidate_text && baseline_text && gmp_text &&
      strcmp(candidate_text, baseline_text) == 0 &&
      strcmp(candidate_text, gmp_text) == 0;
    free(candidate_text);
    free(baseline_text);
    free(gmp_text);
  }

  append_format_wide_probe_result(
    report,
    digits,
    parity,
    median_samples(candidate_samples, XRAY_BENCH_SAMPLES),
    median_samples(baseline_samples, XRAY_BENCH_SAMPLES),
    median_paired_ratio(candidate_samples, baseline_samples, XRAY_BENCH_SAMPLES),
    paired_ratio_wins(candidate_samples, baseline_samples, XRAY_BENCH_SAMPLES, 0.98),
    XRAY_BENCH_SAMPLES,
    max_paired_ratio(candidate_samples, baseline_samples, XRAY_BENCH_SAMPLES));

  mpz_clear(gmp);
  xray_bigint_clear(&scratch);
  free(text);
}

static void run_format_folded_probe_case(XrayBenchmarkReport *report, size_t digits) {
  char *text = benchmark_decimal(digits, 13, 1);
  if (!text) return;
  unsigned int iterations = perf_iterations("format", digits);
  XrayScratchBigInt scratch;
  xray_bigint_init(&scratch);
  mpz_t gmp;
  mpz_init(gmp);
  int ok = xray_bigint_set_decimal(&scratch, text) && mpz_set_str(gmp, text, 10) == 0;
  unsigned long long candidate_samples[XRAY_BENCH_SAMPLES] = {0};
  unsigned long long baseline_samples[XRAY_BENCH_SAMPLES] = {0};
  int parity = 1;

  for (unsigned int sample = 0; sample < XRAY_BENCH_SAMPLES; ++sample) {
    unsigned long long candidate_started = xray_now_us();
    for (unsigned int index = 0; ok && index < iterations; ++index) {
      char *candidate_text = xray_bigint_get_decimal_folded_probe(&scratch);
      ok = candidate_text != NULL;
      free(candidate_text);
    }
    candidate_samples[sample] = xray_now_us() - candidate_started;

    unsigned long long baseline_started = xray_now_us();
    for (unsigned int index = 0; ok && index < iterations; ++index) {
      char *baseline_text = xray_bigint_get_decimal(&scratch);
      ok = baseline_text != NULL;
      free(baseline_text);
    }
    baseline_samples[sample] = xray_now_us() - baseline_started;

    char *candidate_text = xray_bigint_get_decimal_folded_probe(&scratch);
    char *baseline_text = xray_bigint_get_decimal(&scratch);
    char *gmp_text = mpz_get_str(NULL, 10, gmp);
    parity = parity && ok && candidate_text && baseline_text && gmp_text &&
      strcmp(candidate_text, baseline_text) == 0 &&
      strcmp(candidate_text, gmp_text) == 0;
    free(candidate_text);
    free(baseline_text);
    free(gmp_text);
  }

  append_format_folded_probe_result(
    report,
    digits,
    parity,
    median_samples(candidate_samples, XRAY_BENCH_SAMPLES),
    median_samples(baseline_samples, XRAY_BENCH_SAMPLES),
    median_paired_ratio(candidate_samples, baseline_samples, XRAY_BENCH_SAMPLES),
    paired_ratio_wins(candidate_samples, baseline_samples, XRAY_BENCH_SAMPLES, 0.98),
    XRAY_BENCH_SAMPLES,
    max_paired_ratio(candidate_samples, baseline_samples, XRAY_BENCH_SAMPLES));

  mpz_clear(gmp);
  xray_bigint_clear(&scratch);
  free(text);
}

static void run_format_pair_writer_probe_case(XrayBenchmarkReport *report, size_t digits, int use_folded_chunks) {
  char *text = benchmark_decimal(digits, use_folded_chunks ? 19U : 17U, 1);
  if (!text) return;
  unsigned int iterations = perf_iterations("format", digits);
  XrayBigIntRouteConfig route = xray_bigint_route_config();
  XrayScratchBigInt scratch;
  xray_bigint_init(&scratch);
  mpz_t gmp;
  mpz_init(gmp);
  int ok = xray_bigint_set_decimal(&scratch, text) && mpz_set_str(gmp, text, 10) == 0;
  unsigned long long candidate_samples[XRAY_BENCH_SAMPLES] = {0};
  unsigned long long baseline_samples[XRAY_BENCH_SAMPLES] = {0};
  int parity = 1;

  for (unsigned int sample = 0; sample < XRAY_BENCH_SAMPLES; ++sample) {
    unsigned long long candidate_started = xray_now_us();
    for (unsigned int index = 0; ok && index < iterations; ++index) {
      char *candidate_text = use_folded_chunks ?
        xray_bigint_get_decimal_folded_pair_writer_probe(&scratch) :
        xray_bigint_get_decimal_pair_writer_probe(&scratch);
      ok = candidate_text != NULL;
      free(candidate_text);
    }
    candidate_samples[sample] = xray_now_us() - candidate_started;

    unsigned long long baseline_started = xray_now_us();
    for (unsigned int index = 0; ok && index < iterations; ++index) {
      char *baseline_text = xray_bigint_get_decimal_horner_threshold_probe(&scratch, route.decimal_horner_min_limbs);
      ok = baseline_text != NULL;
      free(baseline_text);
    }
    baseline_samples[sample] = xray_now_us() - baseline_started;

    char *candidate_text = use_folded_chunks ?
      xray_bigint_get_decimal_folded_pair_writer_probe(&scratch) :
      xray_bigint_get_decimal_pair_writer_probe(&scratch);
    char *baseline_text = xray_bigint_get_decimal_horner_threshold_probe(&scratch, route.decimal_horner_min_limbs);
    char *gmp_text = mpz_get_str(NULL, 10, gmp);
    parity = parity && ok && candidate_text && baseline_text && gmp_text &&
      strcmp(candidate_text, baseline_text) == 0 &&
      strcmp(candidate_text, gmp_text) == 0;
    free(candidate_text);
    free(baseline_text);
    free(gmp_text);
  }

  append_format_pair_writer_probe_result(
    report,
    digits,
    use_folded_chunks ? "folded-chunks" : "production-chunks",
    parity,
    median_samples(candidate_samples, XRAY_BENCH_SAMPLES),
    median_samples(baseline_samples, XRAY_BENCH_SAMPLES),
    median_paired_ratio(candidate_samples, baseline_samples, XRAY_BENCH_SAMPLES),
    paired_ratio_wins(candidate_samples, baseline_samples, XRAY_BENCH_SAMPLES, 0.98),
    XRAY_BENCH_SAMPLES,
    max_paired_ratio(candidate_samples, baseline_samples, XRAY_BENCH_SAMPLES));

  mpz_clear(gmp);
  xray_bigint_clear(&scratch);
  free(text);
}

typedef char *(*XrayFormatProbeFn)(const XrayScratchBigInt *value);

static char *format_dc_leaf8_probe(const XrayScratchBigInt *value) {
  return xray_bigint_get_decimal_dc_probe(value, 8U);
}

static char *format_dc_leaf16_probe(const XrayScratchBigInt *value) {
  return xray_bigint_get_decimal_dc_probe(value, 16U);
}

static char *format_dc_leaf32_probe(const XrayScratchBigInt *value) {
  return xray_bigint_get_decimal_dc_probe(value, 32U);
}

static char *format_dc_leaf64_probe(const XrayScratchBigInt *value) {
  return xray_bigint_get_decimal_dc_probe(value, 64U);
}

static char *format_dc_ladder_leaf8_probe(const XrayScratchBigInt *value) {
  return xray_bigint_get_decimal_dc_ladder_probe(value, 8U);
}

static char *format_dc_ladder_leaf16_probe(const XrayScratchBigInt *value) {
  return xray_bigint_get_decimal_dc_ladder_probe(value, 16U);
}

static char *format_dc_static_ladder_leaf8_probe(const XrayScratchBigInt *value) {
  return xray_bigint_get_decimal_dc_static_ladder_probe(value, 8U);
}

static char *format_dc_static_ladder_leaf16_probe(const XrayScratchBigInt *value) {
  return xray_bigint_get_decimal_dc_static_ladder_probe(value, 16U);
}

static char *format_dc_ladder_leaf32_probe(const XrayScratchBigInt *value) {
  return xray_bigint_get_decimal_dc_ladder_probe(value, 32U);
}

static char *format_dc_ladder_leaf64_probe(const XrayScratchBigInt *value) {
  return xray_bigint_get_decimal_dc_ladder_probe(value, 64U);
}

static char *format_dc_direct_leaf8_probe(const XrayScratchBigInt *value) {
  return xray_bigint_get_decimal_dc_direct_probe(value, 8U);
}

static char *format_dc_direct_leaf16_probe(const XrayScratchBigInt *value) {
  return xray_bigint_get_decimal_dc_direct_probe(value, 16U);
}

static char *format_dc_static_direct_leaf8_probe(const XrayScratchBigInt *value) {
  return xray_bigint_get_decimal_dc_static_direct_probe(value, 8U);
}

static char *format_dc_static_direct_leaf16_probe(const XrayScratchBigInt *value) {
  return xray_bigint_get_decimal_dc_static_direct_probe(value, 16U);
}

static char *format_dc_direct_leaf32_probe(const XrayScratchBigInt *value) {
  return xray_bigint_get_decimal_dc_direct_probe(value, 32U);
}

static char *format_dc_direct_leaf64_probe(const XrayScratchBigInt *value) {
  return xray_bigint_get_decimal_dc_direct_probe(value, 64U);
}

static char *format_dc_workspace_leaf8_probe(const XrayScratchBigInt *value) {
  return xray_bigint_get_decimal_dc_workspace_probe(value, 8U);
}

static char *format_dc_workspace_leaf16_probe(const XrayScratchBigInt *value) {
  return xray_bigint_get_decimal_dc_workspace_probe(value, 16U);
}

static char *format_dc_preinv_qhat_leaf8_probe(const XrayScratchBigInt *value) {
  return xray_bigint_get_decimal_dc_preinv_qhat_probe(value, 8U);
}

static char *format_dc_preinv_qhat_leaf16_probe(const XrayScratchBigInt *value) {
  return xray_bigint_get_decimal_dc_preinv_qhat_probe(value, 16U);
}

static char *format_divide_1e19_pair_writer_probe(const XrayScratchBigInt *value) {
  return xray_bigint_get_decimal_divide_1e19_pair_writer_probe(value);
}

static char *format_divide_1e19_preinv_probe(const XrayScratchBigInt *value) {
  return xray_bigint_get_decimal_divide_1e19_preinv_probe(value);
}

static char *format_divide_1e19_preinv_pair_writer_probe(const XrayScratchBigInt *value) {
  return xray_bigint_get_decimal_divide_1e19_preinv_pair_writer_probe(value);
}

static char *format_policy_current_default(
  const XrayScratchBigInt *value,
  size_t digits,
  size_t min_digits,
  size_t max_digits,
  size_t leaf_chunks) {
  (void)digits;
  (void)min_digits;
  (void)max_digits;
  (void)leaf_chunks;
  return xray_bigint_get_decimal(value);
}

static char *format_policy_direct(
  const XrayScratchBigInt *value,
  size_t digits,
  size_t min_digits,
  size_t max_digits,
  size_t leaf_chunks) {
  if (max_digits > 0 && digits > max_digits) return xray_bigint_get_decimal(value);
  if (digits < min_digits) return xray_bigint_get_decimal(value);
  return xray_bigint_get_decimal_dc_direct_probe(value, leaf_chunks);
}

static char *format_policy_static_direct(
  const XrayScratchBigInt *value,
  size_t digits,
  size_t min_digits,
  size_t max_digits,
  size_t leaf_chunks) {
  if (max_digits > 0 && digits > max_digits) return xray_bigint_get_decimal(value);
  if (digits < min_digits) return xray_bigint_get_decimal(value);
  return xray_bigint_get_decimal_dc_static_direct_probe(value, leaf_chunks);
}

static char *format_policy_workspace(
  const XrayScratchBigInt *value,
  size_t digits,
  size_t min_digits,
  size_t max_digits,
  size_t leaf_chunks) {
  if (max_digits > 0 && digits > max_digits) return xray_bigint_get_decimal(value);
  if (digits < min_digits) return xray_bigint_get_decimal(value);
  return xray_bigint_get_decimal_dc_workspace_probe(value, leaf_chunks);
}

static char *format_policy_preinv_qhat(
  const XrayScratchBigInt *value,
  size_t digits,
  size_t min_digits,
  size_t max_digits,
  size_t leaf_chunks) {
  if (max_digits > 0 && digits > max_digits) return xray_bigint_get_decimal(value);
  if (digits < min_digits) return xray_bigint_get_decimal(value);
  return xray_bigint_get_decimal_dc_preinv_qhat_probe(value, leaf_chunks);
}

static char *format_policy_divide_1e19_preinv(
  const XrayScratchBigInt *value,
  size_t digits,
  size_t min_digits,
  size_t max_digits,
  size_t leaf_chunks) {
  (void)leaf_chunks;
  if (max_digits > 0 && digits > max_digits) return xray_bigint_get_decimal(value);
  if (digits < min_digits) return xray_bigint_get_decimal(value);
  return xray_bigint_get_decimal_divide_1e19_preinv_probe(value);
}

static char *format_policy_divide_1e19_preinv_pairs(
  const XrayScratchBigInt *value,
  size_t digits,
  size_t min_digits,
  size_t max_digits,
  size_t leaf_chunks) {
  (void)leaf_chunks;
  if (max_digits > 0 && digits > max_digits) return xray_bigint_get_decimal(value);
  if (digits < min_digits) return xray_bigint_get_decimal(value);
  return xray_bigint_get_decimal_divide_1e19_preinv_pair_writer_probe(value);
}

typedef char *(*XrayFormatPolicyProbeFn)(const XrayScratchBigInt *value, size_t digits, size_t min_digits, size_t max_digits, size_t leaf_chunks);

typedef struct XrayFormatPolicyMeasurement {
  int parity;
  unsigned long long candidate_us;
  unsigned long long gmp_us;
  double paired_ratio;
  double worst_pair_ratio;
  size_t stable_sample_count;
} XrayFormatPolicyMeasurement;

static XrayFormatPolicyMeasurement measure_forced_format_policy_candidate(
  size_t digits,
  unsigned int seed,
  size_t max_digits,
  size_t leaf_chunks,
  size_t sample_count,
  XrayFormatPolicyProbeFn probe) {
  XrayFormatPolicyMeasurement measurement;
  memset(&measurement, 0, sizeof(measurement));
  if (sample_count == 0 || sample_count > XRAY_BENCH_MAX_SAMPLES) sample_count = XRAY_BENCH_SAMPLES;
  char *text = benchmark_decimal(digits, seed, 1);
  if (!text || !probe) {
    free(text);
    return measurement;
  }

  unsigned int iterations = perf_iterations("format", digits);
  XrayScratchBigInt scratch;
  xray_bigint_init(&scratch);
  mpz_t gmp;
  mpz_init(gmp);
  int ok = xray_bigint_set_decimal(&scratch, text) && mpz_set_str(gmp, text, 10) == 0;
  unsigned long long candidate_samples[XRAY_BENCH_MAX_SAMPLES] = {0};
  unsigned long long gmp_samples[XRAY_BENCH_MAX_SAMPLES] = {0};
  int parity = 1;

  for (size_t sample = 0; sample < sample_count; ++sample) {
    unsigned long long candidate_started = xray_now_us();
    for (unsigned int index = 0; ok && index < iterations; ++index) {
      char *candidate_text = probe(&scratch, digits, 0, max_digits, leaf_chunks);
      ok = candidate_text != NULL;
      free(candidate_text);
    }
    candidate_samples[sample] = xray_now_us() - candidate_started;

    unsigned long long gmp_started = xray_now_us();
    for (unsigned int index = 0; ok && index < iterations; ++index) {
      char *gmp_text = mpz_get_str(NULL, 10, gmp);
      ok = gmp_text != NULL;
      free(gmp_text);
    }
    gmp_samples[sample] = xray_now_us() - gmp_started;

    char *candidate_text = probe(&scratch, digits, 0, max_digits, leaf_chunks);
    char *gmp_text = mpz_get_str(NULL, 10, gmp);
    parity = parity && ok && candidate_text && gmp_text && strcmp(candidate_text, gmp_text) == 0;
    free(candidate_text);
    free(gmp_text);
  }

  measurement.parity = parity;
  measurement.candidate_us = median_samples(candidate_samples, sample_count);
  measurement.gmp_us = median_samples(gmp_samples, sample_count);
  measurement.paired_ratio = median_paired_ratio(candidate_samples, gmp_samples, sample_count);
  measurement.worst_pair_ratio = max_paired_ratio(candidate_samples, gmp_samples, sample_count);
  measurement.stable_sample_count = paired_ratio_wins(candidate_samples, gmp_samples, sample_count, 1.0);

  mpz_clear(gmp);
  xray_bigint_clear(&scratch);
  free(text);
  return measurement;
}

static void run_format_policy_safety_case_samples(
  XrayBenchmarkReport *report,
  const char *operation,
  unsigned int seed,
  const char *policy,
  const char *candidate,
  size_t neighbor_digits,
  size_t gate_digits,
  size_t min_digits,
  size_t max_digits,
  size_t leaf_chunks,
  size_t sample_count,
  XrayFormatPolicyProbeFn probe) {
  XrayFormatPolicyMeasurement neighbor = measure_forced_format_policy_candidate(
    neighbor_digits,
    seed,
    max_digits,
    leaf_chunks,
    sample_count,
    probe);
  XrayFormatPolicyMeasurement gate = measure_forced_format_policy_candidate(
    gate_digits,
    seed + 1U,
    max_digits,
    leaf_chunks,
    sample_count,
    probe);
  unsigned long long candidate_us = neighbor.candidate_us > gate.candidate_us ?
    neighbor.candidate_us :
    gate.candidate_us;
  unsigned long long gmp_us = neighbor.gmp_us > gate.gmp_us ? neighbor.gmp_us : gate.gmp_us;
  double worst_pair_ratio = neighbor.worst_pair_ratio > gate.worst_pair_ratio ?
    neighbor.worst_pair_ratio :
    gate.worst_pair_ratio;

  append_format_policy_safety_result(
    report,
    operation,
    policy,
    candidate,
    neighbor_digits,
    gate_digits,
    min_digits,
    max_digits,
    leaf_chunks,
    neighbor.parity && gate.parity,
    candidate_us,
    gmp_us,
    neighbor.paired_ratio,
    gate.paired_ratio,
    neighbor.stable_sample_count,
    gate.stable_sample_count,
    sample_count,
    worst_pair_ratio);
}

static void run_format_policy_safety_case(
  XrayBenchmarkReport *report,
  unsigned int seed,
  const char *policy,
  const char *candidate,
  size_t neighbor_digits,
  size_t gate_digits,
  size_t min_digits,
  size_t max_digits,
  size_t leaf_chunks,
  XrayFormatPolicyProbeFn probe) {
  run_format_policy_safety_case_samples(
    report,
    "format-policy-safety",
    seed,
    policy,
    candidate,
    neighbor_digits,
    gate_digits,
    min_digits,
    max_digits,
    leaf_chunks,
    XRAY_BENCH_SAMPLES,
    probe);
}

static void run_format_policy_deep_safety_case(
  XrayBenchmarkReport *report,
  unsigned int seed,
  const char *policy,
  const char *candidate,
  size_t neighbor_digits,
  size_t gate_digits,
  size_t min_digits,
  size_t max_digits,
  size_t leaf_chunks,
  XrayFormatPolicyProbeFn probe) {
  run_format_policy_safety_case_samples(
    report,
    "format-policy-deep-safety",
    seed,
    policy,
    candidate,
    neighbor_digits,
    gate_digits,
    min_digits,
    max_digits,
    leaf_chunks,
    XRAY_BENCH_DEEP_SAMPLES,
    probe);
}

static void run_format_policy_probe_case(
  XrayBenchmarkReport *report,
  size_t digits,
  unsigned int seed,
  const char *policy,
  const char *candidate,
  const char *feature_gate,
  const char *gmp_clue,
  size_t min_digits,
  size_t max_digits,
  size_t leaf_chunks,
  XrayFormatPolicyProbeFn probe) {
  char *text = benchmark_decimal(digits, seed, 1);
  if (!text || !probe) {
    free(text);
    return;
  }
  unsigned int iterations = perf_iterations("format", digits);
  XrayScratchBigInt scratch;
  xray_bigint_init(&scratch);
  mpz_t gmp;
  mpz_init(gmp);
  int ok = xray_bigint_set_decimal(&scratch, text) && mpz_set_str(gmp, text, 10) == 0;
  unsigned long long candidate_samples[XRAY_BENCH_SAMPLES] = {0};
  unsigned long long gmp_samples[XRAY_BENCH_SAMPLES] = {0};
  int parity = 1;

  for (unsigned int sample = 0; sample < XRAY_BENCH_SAMPLES; ++sample) {
    unsigned long long candidate_started = xray_now_us();
    for (unsigned int index = 0; ok && index < iterations; ++index) {
      char *candidate_text = probe(&scratch, digits, min_digits, max_digits, leaf_chunks);
      ok = candidate_text != NULL;
      free(candidate_text);
    }
    candidate_samples[sample] = xray_now_us() - candidate_started;

    unsigned long long gmp_started = xray_now_us();
    for (unsigned int index = 0; ok && index < iterations; ++index) {
      char *gmp_text = mpz_get_str(NULL, 10, gmp);
      ok = gmp_text != NULL;
      free(gmp_text);
    }
    gmp_samples[sample] = xray_now_us() - gmp_started;

    char *candidate_text = probe(&scratch, digits, min_digits, max_digits, leaf_chunks);
    char *gmp_text = mpz_get_str(NULL, 10, gmp);
    parity = parity && ok && candidate_text && gmp_text && strcmp(candidate_text, gmp_text) == 0;
    free(candidate_text);
    free(gmp_text);
  }

  append_format_policy_probe_result(
    report,
    digits,
    policy,
    candidate,
    feature_gate,
    gmp_clue,
    min_digits,
    max_digits,
    leaf_chunks,
    parity,
    median_samples(candidate_samples, XRAY_BENCH_SAMPLES),
    median_samples(gmp_samples, XRAY_BENCH_SAMPLES),
    median_paired_ratio(candidate_samples, gmp_samples, XRAY_BENCH_SAMPLES),
    paired_ratio_wins(candidate_samples, gmp_samples, XRAY_BENCH_SAMPLES, 1.0),
    XRAY_BENCH_SAMPLES,
    max_paired_ratio(candidate_samples, gmp_samples, XRAY_BENCH_SAMPLES));

  mpz_clear(gmp);
  xray_bigint_clear(&scratch);
  free(text);
}

static void run_format_policy_window_endpoint_probe_cases(
  XrayBenchmarkReport *report,
  unsigned int seed,
  const char *policy,
  const char *candidate,
  const char *feature_gate,
  const char *gmp_clue,
  size_t min_digits,
  size_t max_digits,
  XrayFormatPolicyProbeFn probe) {
  run_format_policy_probe_case(
    report,
    min_digits,
    seed,
    policy,
    candidate,
    feature_gate,
    gmp_clue,
    min_digits,
    max_digits,
    0,
    probe);
  if (max_digits != min_digits) {
    run_format_policy_probe_case(
      report,
      max_digits,
      seed + 1U,
      policy,
      candidate,
      feature_gate,
      gmp_clue,
      min_digits,
      max_digits,
      0,
      probe);
  }
}

static void run_format_variant_probe_case(
  XrayBenchmarkReport *report,
  size_t digits,
  unsigned int seed,
  const char *operation,
  const char *label,
  const char *mode,
  const char *candidate,
  const char *baseline,
  const char *feature_gate,
  const char *gmp_clue,
  unsigned int chunk_digits,
  XrayFormatProbeFn probe) {
  char *text = benchmark_decimal(digits, seed, 1);
  if (!text || !probe) {
    free(text);
    return;
  }
  unsigned int iterations = perf_iterations("format", digits);
  XrayScratchBigInt scratch;
  xray_bigint_init(&scratch);
  mpz_t gmp;
  mpz_init(gmp);
  int ok = xray_bigint_set_decimal(&scratch, text) && mpz_set_str(gmp, text, 10) == 0;
  unsigned long long candidate_samples[XRAY_BENCH_SAMPLES] = {0};
  unsigned long long baseline_samples[XRAY_BENCH_SAMPLES] = {0};
  int parity = 1;

  for (unsigned int sample = 0; sample < XRAY_BENCH_SAMPLES; ++sample) {
    unsigned long long candidate_started = xray_now_us();
    for (unsigned int index = 0; ok && index < iterations; ++index) {
      char *candidate_text = probe(&scratch);
      ok = candidate_text != NULL;
      free(candidate_text);
    }
    candidate_samples[sample] = xray_now_us() - candidate_started;

    unsigned long long baseline_started = xray_now_us();
    for (unsigned int index = 0; ok && index < iterations; ++index) {
      char *baseline_text = xray_bigint_get_decimal(&scratch);
      ok = baseline_text != NULL;
      free(baseline_text);
    }
    baseline_samples[sample] = xray_now_us() - baseline_started;

    char *candidate_text = probe(&scratch);
    char *baseline_text = xray_bigint_get_decimal(&scratch);
    char *gmp_text = mpz_get_str(NULL, 10, gmp);
    parity = parity && ok && candidate_text && baseline_text && gmp_text &&
      strcmp(candidate_text, baseline_text) == 0 &&
      strcmp(candidate_text, gmp_text) == 0;
    free(candidate_text);
    free(baseline_text);
    free(gmp_text);
  }

  append_format_variant_probe_result(
    report,
    operation,
    label,
    digits,
    mode,
    "block",
    candidate,
    baseline,
    feature_gate,
    gmp_clue,
    chunk_digits,
    parity,
    median_samples(candidate_samples, XRAY_BENCH_SAMPLES),
    median_samples(baseline_samples, XRAY_BENCH_SAMPLES),
    median_paired_ratio(candidate_samples, baseline_samples, XRAY_BENCH_SAMPLES),
    paired_ratio_wins(candidate_samples, baseline_samples, XRAY_BENCH_SAMPLES, 0.98),
    XRAY_BENCH_SAMPLES,
    max_paired_ratio(candidate_samples, baseline_samples, XRAY_BENCH_SAMPLES));

  mpz_clear(gmp);
  xray_bigint_clear(&scratch);
  free(text);
}

static int run_format_probe_batch(
  const XrayScratchBigInt *scratch,
  XrayFormatProbeFn probe,
  unsigned int iterations,
  unsigned long long *elapsed_us) {
  if (!scratch || !probe || !elapsed_us) return 0;
  unsigned long long started = xray_now_us();
  int ok = 1;
  for (unsigned int index = 0; index < iterations; ++index) {
    char *text = probe(scratch);
    if (!text) {
      ok = 0;
      break;
    }
    free(text);
  }
  *elapsed_us += xray_now_us() - started;
  return ok;
}

static void run_format_variant_pair_probe_case(
  XrayBenchmarkReport *report,
  size_t digits,
  unsigned int seed,
  const char *operation,
  const char *label,
  const char *mode,
  const char *candidate,
  const char *baseline,
  const char *feature_gate,
  const char *gmp_clue,
  unsigned int chunk_digits,
  XrayFormatProbeFn candidate_probe,
  XrayFormatProbeFn baseline_probe) {
  char *text = benchmark_decimal(digits, seed, 1);
  if (!text || !candidate_probe || !baseline_probe) {
    free(text);
    return;
  }
  unsigned int iterations = perf_iterations("format", digits);
  XrayScratchBigInt scratch;
  xray_bigint_init(&scratch);
  mpz_t gmp;
  mpz_init(gmp);
  int ok = xray_bigint_set_decimal(&scratch, text) && mpz_set_str(gmp, text, 10) == 0;
  unsigned long long candidate_samples[XRAY_BENCH_SAMPLES] = {0};
  unsigned long long baseline_samples[XRAY_BENCH_SAMPLES] = {0};
  int parity = 1;
  unsigned int batch_iterations = iterations >= 64U ? 8U : (iterations >= 16U ? 4U : 1U);
  char timing_mode[64];
  snprintf(
    timing_mode,
    sizeof(timing_mode),
    "interleaved-alternating-batch%u",
    batch_iterations);

  for (unsigned int sample = 0; sample < XRAY_BENCH_SAMPLES; ++sample) {
    unsigned int completed = 0;
    int candidate_first = (sample % 2U) == 0U;
    while (ok && completed < iterations) {
      unsigned int remaining = iterations - completed;
      unsigned int batch = remaining < batch_iterations ? remaining : batch_iterations;
      if (candidate_first) {
        ok = run_format_probe_batch(&scratch, candidate_probe, batch, &candidate_samples[sample]);
        if (ok) ok = run_format_probe_batch(&scratch, baseline_probe, batch, &baseline_samples[sample]);
      } else {
        ok = run_format_probe_batch(&scratch, baseline_probe, batch, &baseline_samples[sample]);
        if (ok) ok = run_format_probe_batch(&scratch, candidate_probe, batch, &candidate_samples[sample]);
      }
      candidate_first = !candidate_first;
      completed += batch;
    }

    char *candidate_text = candidate_probe(&scratch);
    char *baseline_text = baseline_probe(&scratch);
    char *gmp_text = mpz_get_str(NULL, 10, gmp);
    parity = parity && ok && candidate_text && baseline_text && gmp_text &&
      strcmp(candidate_text, baseline_text) == 0 &&
      strcmp(candidate_text, gmp_text) == 0;
    free(candidate_text);
    free(baseline_text);
    free(gmp_text);
  }

  append_format_variant_probe_result(
    report,
    operation,
    label,
    digits,
    mode,
    timing_mode,
    candidate,
    baseline,
    feature_gate,
    gmp_clue,
    chunk_digits,
    parity,
    median_samples(candidate_samples, XRAY_BENCH_SAMPLES),
    median_samples(baseline_samples, XRAY_BENCH_SAMPLES),
    median_paired_ratio(candidate_samples, baseline_samples, XRAY_BENCH_SAMPLES),
    paired_ratio_wins(candidate_samples, baseline_samples, XRAY_BENCH_SAMPLES, 0.98),
    XRAY_BENCH_SAMPLES,
    max_paired_ratio(candidate_samples, baseline_samples, XRAY_BENCH_SAMPLES));

  mpz_clear(gmp);
  xray_bigint_clear(&scratch);
  free(text);
}

typedef int (*XraySquarePolicyProbeFn)(XrayScratchBigInt *out, const XrayScratchBigInt *value, size_t threshold);

static int square_policy_current_default(XrayScratchBigInt *out, const XrayScratchBigInt *value, size_t threshold) {
  (void)threshold;
  return xray_bigint_square(out, value);
}

static int square_policy_karatsuba_threshold(XrayScratchBigInt *out, const XrayScratchBigInt *value, size_t threshold) {
  return xray_bigint_square_karatsuba_probe(out, value, threshold);
}

static void run_square_policy_probe_case(
  XrayBenchmarkReport *report,
  size_t digits,
  unsigned int seed,
  const char *policy,
  const char *candidate,
  const char *feature_gate,
  const char *gmp_clue,
  size_t threshold,
  XraySquarePolicyProbeFn probe) {
  char *text = benchmark_decimal(digits, seed, 1);
  if (!text || !probe) {
    free(text);
    return;
  }
  unsigned int iterations = perf_iterations("mul", digits);
  XrayScratchBigInt value;
  XrayScratchBigInt out;
  xray_bigint_init(&value);
  xray_bigint_init(&out);
  mpz_t gvalue, gout;
  mpz_inits(gvalue, gout, NULL);
  int ok = xray_bigint_set_decimal(&value, text) && mpz_set_str(gvalue, text, 10) == 0;
  unsigned long long candidate_samples[XRAY_BENCH_SAMPLES] = {0};
  unsigned long long gmp_samples[XRAY_BENCH_SAMPLES] = {0};
  int parity = 1;

  for (unsigned int sample = 0; sample < XRAY_BENCH_SAMPLES; ++sample) {
    unsigned long long candidate_started = xray_now_us();
    for (unsigned int index = 0; ok && index < iterations; ++index) {
      ok = probe(&out, &value, threshold);
    }
    candidate_samples[sample] = xray_now_us() - candidate_started;

    unsigned long long gmp_started = xray_now_us();
    for (unsigned int index = 0; ok && index < iterations; ++index) {
      mpz_mul(gout, gvalue, gvalue);
    }
    gmp_samples[sample] = xray_now_us() - gmp_started;

    char *candidate_text = xray_bigint_get_decimal(&out);
    char *gmp_text = mpz_get_str(NULL, 10, gout);
    parity = parity && ok && candidate_text && gmp_text && strcmp(candidate_text, gmp_text) == 0;
    free(candidate_text);
    free(gmp_text);
  }

  append_arithmetic_policy_probe_result(
    report,
    "square",
    digits,
    1,
    policy,
    candidate,
    feature_gate,
    gmp_clue,
    0,
    threshold,
    0,
    1,
    parity,
    median_samples(candidate_samples, XRAY_BENCH_SAMPLES),
    median_samples(gmp_samples, XRAY_BENCH_SAMPLES),
    median_paired_ratio(candidate_samples, gmp_samples, XRAY_BENCH_SAMPLES),
    paired_ratio_wins(candidate_samples, gmp_samples, XRAY_BENCH_SAMPLES, 1.0),
    XRAY_BENCH_SAMPLES,
    max_paired_ratio(candidate_samples, gmp_samples, XRAY_BENCH_SAMPLES));

  mpz_clears(gvalue, gout, NULL);
  xray_bigint_clear(&value);
  xray_bigint_clear(&out);
  free(text);
}

typedef int (*XrayMulPolicyProbeFn)(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *left,
  const XrayScratchBigInt *right,
  size_t digits,
  size_t min_digits,
  size_t leaf_threshold,
  size_t depth_limit);

static int mul_policy_current_default(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *left,
  const XrayScratchBigInt *right,
  size_t digits,
  size_t min_digits,
  size_t leaf_threshold,
  size_t depth_limit) {
  (void)digits;
  (void)min_digits;
  (void)leaf_threshold;
  (void)depth_limit;
  return xray_bigint_mul(out, left, right);
}

static int mul_policy_toom3_unroll4(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *left,
  const XrayScratchBigInt *right,
  size_t digits,
  size_t min_digits,
  size_t leaf_threshold,
  size_t depth_limit) {
  (void)depth_limit;
#if XRAY_HAS_MUL_UNROLL4_POLICY_PROBES
  if (digits < min_digits) return xray_bigint_mul(out, left, right);
  return xray_bigint_mul_toom3_unroll4_probe(out, left, right, leaf_threshold);
#else
  (void)digits;
  (void)min_digits;
  (void)leaf_threshold;
  return xray_bigint_mul(out, left, right);
#endif
}

static int mul_policy_toom3_unroll4_recursive(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *left,
  const XrayScratchBigInt *right,
  size_t digits,
  size_t min_digits,
  size_t leaf_threshold,
  size_t depth_limit) {
#if XRAY_HAS_MUL_UNROLL4_POLICY_PROBES
  if (digits < min_digits) return xray_bigint_mul(out, left, right);
  return xray_bigint_mul_toom3_unroll4_recursive_probe(out, left, right, leaf_threshold, depth_limit);
#else
  (void)digits;
  (void)min_digits;
  (void)leaf_threshold;
  (void)depth_limit;
  return xray_bigint_mul(out, left, right);
#endif
}

static XrayMulPolicySafetyPoint measure_forced_mul_policy_candidate(
  size_t digits,
  size_t leaf_threshold,
  size_t depth_limit,
  int candidate_available,
  XrayMulPolicyProbeFn probe) {
  XrayMulPolicySafetyPoint measurement;
  memset(&measurement, 0, sizeof(measurement));
  measurement.digits = digits;
  measurement.candidate_available = candidate_available;
  measurement.sample_count = XRAY_BENCH_SAMPLES;

  char *left_text[XRAY_MUL_OPERAND_FAMILIES] = {0};
  char *right_text[XRAY_MUL_OPERAND_FAMILIES] = {0};
  XrayScratchBigInt a[XRAY_MUL_OPERAND_FAMILIES];
  XrayScratchBigInt b[XRAY_MUL_OPERAND_FAMILIES];
  XrayScratchBigInt out[XRAY_MUL_OPERAND_FAMILIES];
  mpz_t ga[XRAY_MUL_OPERAND_FAMILIES];
  mpz_t gb[XRAY_MUL_OPERAND_FAMILIES];
  mpz_t gout[XRAY_MUL_OPERAND_FAMILIES];

  unsigned int iterations = perf_iterations("mul", digits);
  int ok = probe != NULL;
  for (size_t family = 0; family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
    xray_bigint_init(&a[family]);
    xray_bigint_init(&b[family]);
    xray_bigint_init(&out[family]);
    mpz_inits(ga[family], gb[family], gout[family], NULL);
    left_text[family] = benchmark_decimal(digits, mul_operand_families[family].left_seed, mul_operand_families[family].left_high_lead);
    right_text[family] = benchmark_decimal(digits, mul_operand_families[family].right_seed, mul_operand_families[family].right_high_lead);
    ok = ok &&
      left_text[family] &&
      right_text[family] &&
      xray_bigint_set_decimal(&a[family], left_text[family]) &&
      xray_bigint_set_decimal(&b[family], right_text[family]) &&
      mpz_set_str(ga[family], left_text[family], 10) == 0 &&
      mpz_set_str(gb[family], right_text[family], 10) == 0;
  }

  unsigned long long candidate_samples[XRAY_BENCH_SAMPLES] = {0};
  unsigned long long gmp_samples[XRAY_BENCH_SAMPLES] = {0};
  int parity = 1;
  for (unsigned int sample = 0; sample < XRAY_BENCH_SAMPLES; ++sample) {
    unsigned long long candidate_started = xray_now_us();
    for (unsigned int index = 0; ok && index < iterations; ++index) {
      for (size_t family = 0; ok && family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
        if (candidate_available) {
          ok = probe(&out[family], &a[family], &b[family], digits, 0, leaf_threshold, depth_limit);
        } else {
          ok = xray_bigint_mul(&out[family], &a[family], &b[family]);
        }
      }
    }
    candidate_samples[sample] = xray_now_us() - candidate_started;

    unsigned long long gmp_started = xray_now_us();
    for (unsigned int index = 0; ok && index < iterations; ++index) {
      for (size_t family = 0; family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
        mpz_mul(gout[family], ga[family], gb[family]);
      }
    }
    gmp_samples[sample] = xray_now_us() - gmp_started;

    for (size_t family = 0; family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
      char *candidate_text = xray_bigint_get_decimal(&out[family]);
      char *gmp_text = mpz_get_str(NULL, 10, gout[family]);
      parity = parity && ok && candidate_text && gmp_text && strcmp(candidate_text, gmp_text) == 0;
      free(candidate_text);
      free(gmp_text);
    }
  }

  measurement.parity = parity;
  measurement.candidate_us = median_samples(candidate_samples, XRAY_BENCH_SAMPLES);
  measurement.gmp_us = median_samples(gmp_samples, XRAY_BENCH_SAMPLES);
  measurement.paired_ratio = median_paired_ratio(candidate_samples, gmp_samples, XRAY_BENCH_SAMPLES);
  measurement.stable_sample_count = paired_ratio_wins(candidate_samples, gmp_samples, XRAY_BENCH_SAMPLES, 1.0);
  measurement.worst_pair_ratio = max_paired_ratio(candidate_samples, gmp_samples, XRAY_BENCH_SAMPLES);

  for (size_t family = 0; family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
    mpz_clears(ga[family], gb[family], gout[family], NULL);
    xray_bigint_clear(&a[family]);
    xray_bigint_clear(&b[family]);
    xray_bigint_clear(&out[family]);
    free(left_text[family]);
    free(right_text[family]);
  }

  return measurement;
}

static void run_mul_policy_safety_case(
  XrayBenchmarkReport *report,
  const char *policy,
  const char *candidate,
  const char *feature_gate,
  const char *gmp_clue,
  size_t min_digits,
  size_t leaf_threshold,
  size_t depth_limit,
  int candidate_available,
  XrayMulPolicyProbeFn probe,
  const size_t *digits,
  size_t digit_count) {
  if (!digits || digit_count == 0) return;
  XrayMulPolicySafetyPoint *points = (XrayMulPolicySafetyPoint *)calloc(digit_count, sizeof(XrayMulPolicySafetyPoint));
  if (!points) return;
  for (size_t index = 0; index < digit_count; ++index) {
    points[index] = measure_forced_mul_policy_candidate(
      digits[index],
      leaf_threshold,
      depth_limit,
      candidate_available,
      probe);
  }
  append_mul_policy_safety_result(
    report,
    policy,
    candidate,
    feature_gate,
    gmp_clue,
    min_digits,
    leaf_threshold,
    depth_limit,
    candidate_available,
    points,
    digit_count);
  free(points);
}

static void run_mul_policy_probe_case(
  XrayBenchmarkReport *report,
  size_t digits,
  const char *policy,
  const char *candidate,
  const char *feature_gate,
  const char *gmp_clue,
  size_t min_digits,
  size_t leaf_threshold,
  size_t depth_limit,
  int candidate_available,
  XrayMulPolicyProbeFn probe) {
  char *left_text[XRAY_MUL_OPERAND_FAMILIES] = {0};
  char *right_text[XRAY_MUL_OPERAND_FAMILIES] = {0};
  XrayScratchBigInt a[XRAY_MUL_OPERAND_FAMILIES];
  XrayScratchBigInt b[XRAY_MUL_OPERAND_FAMILIES];
  XrayScratchBigInt out[XRAY_MUL_OPERAND_FAMILIES];
  mpz_t ga[XRAY_MUL_OPERAND_FAMILIES];
  mpz_t gb[XRAY_MUL_OPERAND_FAMILIES];
  mpz_t gout[XRAY_MUL_OPERAND_FAMILIES];

  unsigned int iterations = perf_iterations("mul", digits);
  int ok = probe != NULL;
  for (size_t family = 0; family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
    xray_bigint_init(&a[family]);
    xray_bigint_init(&b[family]);
    xray_bigint_init(&out[family]);
    mpz_inits(ga[family], gb[family], gout[family], NULL);
    left_text[family] = benchmark_decimal(digits, mul_operand_families[family].left_seed, mul_operand_families[family].left_high_lead);
    right_text[family] = benchmark_decimal(digits, mul_operand_families[family].right_seed, mul_operand_families[family].right_high_lead);
    ok = ok &&
      left_text[family] &&
      right_text[family] &&
      xray_bigint_set_decimal(&a[family], left_text[family]) &&
      xray_bigint_set_decimal(&b[family], right_text[family]) &&
      mpz_set_str(ga[family], left_text[family], 10) == 0 &&
      mpz_set_str(gb[family], right_text[family], 10) == 0;
  }

  unsigned long long candidate_samples[XRAY_BENCH_SAMPLES] = {0};
  unsigned long long gmp_samples[XRAY_BENCH_SAMPLES] = {0};
  int parity = 1;
  for (unsigned int sample = 0; sample < XRAY_BENCH_SAMPLES; ++sample) {
    unsigned long long candidate_started = xray_now_us();
    for (unsigned int index = 0; ok && index < iterations; ++index) {
      for (size_t family = 0; ok && family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
        ok = probe(&out[family], &a[family], &b[family], digits, min_digits, leaf_threshold, depth_limit);
      }
    }
    candidate_samples[sample] = xray_now_us() - candidate_started;

    unsigned long long gmp_started = xray_now_us();
    for (unsigned int index = 0; ok && index < iterations; ++index) {
      for (size_t family = 0; family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
        mpz_mul(gout[family], ga[family], gb[family]);
      }
    }
    gmp_samples[sample] = xray_now_us() - gmp_started;

    for (size_t family = 0; family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
      char *candidate_text = xray_bigint_get_decimal(&out[family]);
      char *gmp_text = mpz_get_str(NULL, 10, gout[family]);
      parity = parity && ok && candidate_text && gmp_text && strcmp(candidate_text, gmp_text) == 0;
      free(candidate_text);
      free(gmp_text);
    }
  }

  append_arithmetic_policy_probe_result(
    report,
    "mul",
    digits,
    XRAY_MUL_OPERAND_FAMILIES,
    policy,
    candidate,
    feature_gate,
    gmp_clue,
    min_digits,
    leaf_threshold,
    depth_limit,
    candidate_available,
    parity,
    median_samples(candidate_samples, XRAY_BENCH_SAMPLES),
    median_samples(gmp_samples, XRAY_BENCH_SAMPLES),
    median_paired_ratio(candidate_samples, gmp_samples, XRAY_BENCH_SAMPLES),
    paired_ratio_wins(candidate_samples, gmp_samples, XRAY_BENCH_SAMPLES, 1.0),
    XRAY_BENCH_SAMPLES,
    max_paired_ratio(candidate_samples, gmp_samples, XRAY_BENCH_SAMPLES));

  for (size_t family = 0; family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
    mpz_clears(ga[family], gb[family], gout[family], NULL);
    xray_bigint_clear(&a[family]);
    xray_bigint_clear(&b[family]);
    xray_bigint_clear(&out[family]);
    free(left_text[family]);
    free(right_text[family]);
  }
}

static void run_mul_toom3_probe_case(XrayBenchmarkReport *report, size_t digits, size_t leaf_threshold) {
  char *left_text[XRAY_MUL_OPERAND_FAMILIES] = {0};
  char *right_text[XRAY_MUL_OPERAND_FAMILIES] = {0};
  XrayScratchBigInt a[XRAY_MUL_OPERAND_FAMILIES];
  XrayScratchBigInt b[XRAY_MUL_OPERAND_FAMILIES];
  XrayScratchBigInt scratch_out[XRAY_MUL_OPERAND_FAMILIES];
  mpz_t ga[XRAY_MUL_OPERAND_FAMILIES];
  mpz_t gb[XRAY_MUL_OPERAND_FAMILIES];
  mpz_t gout[XRAY_MUL_OPERAND_FAMILIES];

  unsigned int iterations = perf_iterations("mul", digits);
  int ok = 1;
  for (size_t family = 0; family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
    xray_bigint_init(&a[family]);
    xray_bigint_init(&b[family]);
    xray_bigint_init(&scratch_out[family]);
    mpz_inits(ga[family], gb[family], gout[family], NULL);
    left_text[family] = benchmark_decimal(digits, mul_operand_families[family].left_seed, mul_operand_families[family].left_high_lead);
    right_text[family] = benchmark_decimal(digits, mul_operand_families[family].right_seed, mul_operand_families[family].right_high_lead);
    ok = ok &&
      left_text[family] &&
      right_text[family] &&
      xray_bigint_set_decimal(&a[family], left_text[family]) &&
      xray_bigint_set_decimal(&b[family], right_text[family]) &&
      mpz_set_str(ga[family], left_text[family], 10) == 0 &&
      mpz_set_str(gb[family], right_text[family], 10) == 0;
  }

  unsigned long long scratch_samples[XRAY_BENCH_SAMPLES] = {0};
  unsigned long long gmp_samples[XRAY_BENCH_SAMPLES] = {0};
  int parity = 1;
  for (unsigned int sample = 0; sample < XRAY_BENCH_SAMPLES; ++sample) {
    unsigned long long scratch_started = xray_now_us();
    for (unsigned int index = 0; ok && index < iterations; ++index) {
      for (size_t family = 0; ok && family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
        ok = xray_bigint_mul_toom3_probe(&scratch_out[family], &a[family], &b[family], leaf_threshold);
      }
    }
    scratch_samples[sample] = xray_now_us() - scratch_started;

    unsigned long long gmp_started = xray_now_us();
    for (unsigned int index = 0; ok && index < iterations; ++index) {
      for (size_t family = 0; family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
        mpz_mul(gout[family], ga[family], gb[family]);
      }
    }
    gmp_samples[sample] = xray_now_us() - gmp_started;

    for (size_t family = 0; family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
      char *scratch_text = xray_bigint_get_decimal(&scratch_out[family]);
      char *gmp_text = mpz_get_str(NULL, 10, gout[family]);
      parity = parity && ok && scratch_text && gmp_text && strcmp(scratch_text, gmp_text) == 0;
      free(scratch_text);
      free(gmp_text);
    }
  }

  append_mul_toom3_probe_result(
    report,
    digits,
    leaf_threshold,
    XRAY_MUL_OPERAND_FAMILIES,
    parity,
    median_samples(scratch_samples, XRAY_BENCH_SAMPLES),
    median_samples(gmp_samples, XRAY_BENCH_SAMPLES),
    median_paired_ratio(scratch_samples, gmp_samples, XRAY_BENCH_SAMPLES),
    paired_ratio_wins(scratch_samples, gmp_samples, XRAY_BENCH_SAMPLES, 0.98),
    XRAY_BENCH_SAMPLES,
    max_paired_ratio(scratch_samples, gmp_samples, XRAY_BENCH_SAMPLES));

  for (size_t family = 0; family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
    mpz_clears(ga[family], gb[family], gout[family], NULL);
    xray_bigint_clear(&a[family]);
    xray_bigint_clear(&b[family]);
    xray_bigint_clear(&scratch_out[family]);
    free(left_text[family]);
    free(right_text[family]);
  }
}

static void run_mul_toom3_vs_scratch_probe_case(XrayBenchmarkReport *report, size_t digits, size_t leaf_threshold) {
  char *left_text[XRAY_MUL_OPERAND_FAMILIES] = {0};
  char *right_text[XRAY_MUL_OPERAND_FAMILIES] = {0};
  XrayScratchBigInt a[XRAY_MUL_OPERAND_FAMILIES];
  XrayScratchBigInt b[XRAY_MUL_OPERAND_FAMILIES];
  XrayScratchBigInt candidate_out[XRAY_MUL_OPERAND_FAMILIES];
  XrayScratchBigInt baseline_out[XRAY_MUL_OPERAND_FAMILIES];

  unsigned int iterations = perf_iterations("mul", digits);
  int ok = 1;
  for (size_t family = 0; family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
    xray_bigint_init(&a[family]);
    xray_bigint_init(&b[family]);
    xray_bigint_init(&candidate_out[family]);
    xray_bigint_init(&baseline_out[family]);
    left_text[family] = benchmark_decimal(digits, mul_operand_families[family].left_seed, mul_operand_families[family].left_high_lead);
    right_text[family] = benchmark_decimal(digits, mul_operand_families[family].right_seed, mul_operand_families[family].right_high_lead);
    ok = ok &&
      left_text[family] &&
      right_text[family] &&
      xray_bigint_set_decimal(&a[family], left_text[family]) &&
      xray_bigint_set_decimal(&b[family], right_text[family]);
  }

  unsigned long long candidate_samples[XRAY_BENCH_SAMPLES] = {0};
  unsigned long long baseline_samples[XRAY_BENCH_SAMPLES] = {0};
  int parity = 1;
  for (unsigned int sample = 0; sample < XRAY_BENCH_SAMPLES; ++sample) {
    if ((sample % 2U) == 0U) {
      unsigned long long candidate_started = xray_now_us();
      for (unsigned int index = 0; ok && index < iterations; ++index) {
        for (size_t family = 0; ok && family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
          ok = xray_bigint_mul_toom3_probe(&candidate_out[family], &a[family], &b[family], leaf_threshold);
        }
      }
      candidate_samples[sample] = xray_now_us() - candidate_started;

      unsigned long long baseline_started = xray_now_us();
      for (unsigned int index = 0; ok && index < iterations; ++index) {
        for (size_t family = 0; ok && family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
          ok = xray_bigint_mul(&baseline_out[family], &a[family], &b[family]);
        }
      }
      baseline_samples[sample] = xray_now_us() - baseline_started;
    } else {
      unsigned long long baseline_started = xray_now_us();
      for (unsigned int index = 0; ok && index < iterations; ++index) {
        for (size_t family = 0; ok && family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
          ok = xray_bigint_mul(&baseline_out[family], &a[family], &b[family]);
        }
      }
      baseline_samples[sample] = xray_now_us() - baseline_started;

      unsigned long long candidate_started = xray_now_us();
      for (unsigned int index = 0; ok && index < iterations; ++index) {
        for (size_t family = 0; ok && family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
          ok = xray_bigint_mul_toom3_probe(&candidate_out[family], &a[family], &b[family], leaf_threshold);
        }
      }
      candidate_samples[sample] = xray_now_us() - candidate_started;
    }

    for (size_t family = 0; family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
      parity = parity && ok && xray_bigint_compare(&candidate_out[family], &baseline_out[family]) == 0;
    }
  }

  append_mul_toom3_vs_scratch_probe_result(
    report,
    digits,
    leaf_threshold,
    XRAY_MUL_OPERAND_FAMILIES,
    parity,
    median_samples(candidate_samples, XRAY_BENCH_SAMPLES),
    median_samples(baseline_samples, XRAY_BENCH_SAMPLES),
    median_paired_ratio(candidate_samples, baseline_samples, XRAY_BENCH_SAMPLES),
    paired_ratio_wins(candidate_samples, baseline_samples, XRAY_BENCH_SAMPLES, 0.98),
    XRAY_BENCH_SAMPLES,
    max_paired_ratio(candidate_samples, baseline_samples, XRAY_BENCH_SAMPLES));

  for (size_t family = 0; family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
    xray_bigint_clear(&a[family]);
    xray_bigint_clear(&b[family]);
    xray_bigint_clear(&candidate_out[family]);
    xray_bigint_clear(&baseline_out[family]);
    free(left_text[family]);
    free(right_text[family]);
  }
}

#if XRAY_HAS_MSVC_BMI2_ADX_INTRINSICS
static void run_mul_toom3_unroll4_vs_scratch_probe_case(XrayBenchmarkReport *report, size_t digits, size_t leaf_threshold) {
  char *left_text[XRAY_MUL_OPERAND_FAMILIES] = {0};
  char *right_text[XRAY_MUL_OPERAND_FAMILIES] = {0};
  XrayScratchBigInt a[XRAY_MUL_OPERAND_FAMILIES];
  XrayScratchBigInt b[XRAY_MUL_OPERAND_FAMILIES];
  XrayScratchBigInt candidate_out[XRAY_MUL_OPERAND_FAMILIES];
  XrayScratchBigInt baseline_out[XRAY_MUL_OPERAND_FAMILIES];

  unsigned int iterations = perf_iterations("mul", digits);
  int ok = 1;
  for (size_t family = 0; family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
    xray_bigint_init(&a[family]);
    xray_bigint_init(&b[family]);
    xray_bigint_init(&candidate_out[family]);
    xray_bigint_init(&baseline_out[family]);
    left_text[family] = benchmark_decimal(digits, mul_operand_families[family].left_seed, mul_operand_families[family].left_high_lead);
    right_text[family] = benchmark_decimal(digits, mul_operand_families[family].right_seed, mul_operand_families[family].right_high_lead);
    ok = ok &&
      left_text[family] &&
      right_text[family] &&
      xray_bigint_set_decimal(&a[family], left_text[family]) &&
      xray_bigint_set_decimal(&b[family], right_text[family]);
  }

  unsigned long long candidate_samples[XRAY_BENCH_SAMPLES] = {0};
  unsigned long long baseline_samples[XRAY_BENCH_SAMPLES] = {0};
  int parity = 1;
  for (unsigned int sample = 0; sample < XRAY_BENCH_SAMPLES; ++sample) {
    if ((sample % 2U) == 0U) {
      unsigned long long candidate_started = xray_now_us();
      for (unsigned int index = 0; ok && index < iterations; ++index) {
        for (size_t family = 0; ok && family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
          ok = xray_bigint_mul_toom3_unroll4_probe(&candidate_out[family], &a[family], &b[family], leaf_threshold);
        }
      }
      candidate_samples[sample] = xray_now_us() - candidate_started;

      unsigned long long baseline_started = xray_now_us();
      for (unsigned int index = 0; ok && index < iterations; ++index) {
        for (size_t family = 0; ok && family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
          ok = xray_bigint_mul(&baseline_out[family], &a[family], &b[family]);
        }
      }
      baseline_samples[sample] = xray_now_us() - baseline_started;
    } else {
      unsigned long long baseline_started = xray_now_us();
      for (unsigned int index = 0; ok && index < iterations; ++index) {
        for (size_t family = 0; ok && family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
          ok = xray_bigint_mul(&baseline_out[family], &a[family], &b[family]);
        }
      }
      baseline_samples[sample] = xray_now_us() - baseline_started;

      unsigned long long candidate_started = xray_now_us();
      for (unsigned int index = 0; ok && index < iterations; ++index) {
        for (size_t family = 0; ok && family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
          ok = xray_bigint_mul_toom3_unroll4_probe(&candidate_out[family], &a[family], &b[family], leaf_threshold);
        }
      }
      candidate_samples[sample] = xray_now_us() - candidate_started;
    }

    for (size_t family = 0; family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
      parity = parity && ok && xray_bigint_compare(&candidate_out[family], &baseline_out[family]) == 0;
    }
  }

  append_mul_toom3_unroll4_vs_scratch_probe_result(
    report,
    digits,
    leaf_threshold,
    XRAY_MUL_OPERAND_FAMILIES,
    parity,
    median_samples(candidate_samples, XRAY_BENCH_SAMPLES),
    median_samples(baseline_samples, XRAY_BENCH_SAMPLES),
    median_paired_ratio(candidate_samples, baseline_samples, XRAY_BENCH_SAMPLES),
    paired_ratio_wins(candidate_samples, baseline_samples, XRAY_BENCH_SAMPLES, 0.98),
    XRAY_BENCH_SAMPLES,
    max_paired_ratio(candidate_samples, baseline_samples, XRAY_BENCH_SAMPLES));

  for (size_t family = 0; family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
    xray_bigint_clear(&a[family]);
    xray_bigint_clear(&b[family]);
    xray_bigint_clear(&candidate_out[family]);
    xray_bigint_clear(&baseline_out[family]);
    free(left_text[family]);
    free(right_text[family]);
  }
}

static void run_mul_toom3_unroll4_vs_gmp_probe_case_samples(
  XrayBenchmarkReport *report,
  size_t digits,
  size_t leaf_threshold,
  size_t sample_count,
  const char *operation_name,
  const char *name_prefix) {
  char *left_text[XRAY_MUL_OPERAND_FAMILIES] = {0};
  char *right_text[XRAY_MUL_OPERAND_FAMILIES] = {0};
  XrayScratchBigInt a[XRAY_MUL_OPERAND_FAMILIES];
  XrayScratchBigInt b[XRAY_MUL_OPERAND_FAMILIES];
  XrayScratchBigInt candidate_out[XRAY_MUL_OPERAND_FAMILIES];
  mpz_t ga[XRAY_MUL_OPERAND_FAMILIES];
  mpz_t gb[XRAY_MUL_OPERAND_FAMILIES];
  mpz_t gout[XRAY_MUL_OPERAND_FAMILIES];

  unsigned int iterations = perf_iterations("mul", digits);
  if (sample_count == 0 || sample_count > XRAY_BENCH_MAX_SAMPLES) sample_count = XRAY_BENCH_SAMPLES;
  int ok = 1;
  for (size_t family = 0; family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
    xray_bigint_init(&a[family]);
    xray_bigint_init(&b[family]);
    xray_bigint_init(&candidate_out[family]);
    mpz_inits(ga[family], gb[family], gout[family], NULL);
    left_text[family] = benchmark_decimal(digits, mul_operand_families[family].left_seed, mul_operand_families[family].left_high_lead);
    right_text[family] = benchmark_decimal(digits, mul_operand_families[family].right_seed, mul_operand_families[family].right_high_lead);
    ok = ok &&
      left_text[family] &&
      right_text[family] &&
      xray_bigint_set_decimal(&a[family], left_text[family]) &&
      xray_bigint_set_decimal(&b[family], right_text[family]) &&
      mpz_set_str(ga[family], left_text[family], 10) == 0 &&
      mpz_set_str(gb[family], right_text[family], 10) == 0;
  }

  unsigned long long candidate_samples[XRAY_BENCH_MAX_SAMPLES] = {0};
  unsigned long long gmp_samples[XRAY_BENCH_MAX_SAMPLES] = {0};
  int parity = 1;
  for (size_t sample = 0; sample < sample_count; ++sample) {
    if ((sample % 2U) == 0U) {
      unsigned long long candidate_started = xray_now_us();
      for (unsigned int index = 0; ok && index < iterations; ++index) {
        for (size_t family = 0; ok && family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
          ok = xray_bigint_mul_toom3_unroll4_probe(&candidate_out[family], &a[family], &b[family], leaf_threshold);
        }
      }
      candidate_samples[sample] = xray_now_us() - candidate_started;

      unsigned long long gmp_started = xray_now_us();
      for (unsigned int index = 0; ok && index < iterations; ++index) {
        for (size_t family = 0; family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
          mpz_mul(gout[family], ga[family], gb[family]);
        }
      }
      gmp_samples[sample] = xray_now_us() - gmp_started;
    } else {
      unsigned long long gmp_started = xray_now_us();
      for (unsigned int index = 0; ok && index < iterations; ++index) {
        for (size_t family = 0; family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
          mpz_mul(gout[family], ga[family], gb[family]);
        }
      }
      gmp_samples[sample] = xray_now_us() - gmp_started;

      unsigned long long candidate_started = xray_now_us();
      for (unsigned int index = 0; ok && index < iterations; ++index) {
        for (size_t family = 0; ok && family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
          ok = xray_bigint_mul_toom3_unroll4_probe(&candidate_out[family], &a[family], &b[family], leaf_threshold);
        }
      }
      candidate_samples[sample] = xray_now_us() - candidate_started;
    }

    for (size_t family = 0; family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
      char *candidate_text = xray_bigint_get_decimal(&candidate_out[family]);
      char *gmp_text = mpz_get_str(NULL, 10, gout[family]);
      parity = parity && ok && candidate_text && gmp_text && strcmp(candidate_text, gmp_text) == 0;
      free(candidate_text);
      free(gmp_text);
    }
  }

  append_mul_toom3_unroll4_vs_gmp_probe_result(
    report,
    operation_name,
    name_prefix,
    digits,
    leaf_threshold,
    XRAY_MUL_OPERAND_FAMILIES,
    parity,
    median_samples(candidate_samples, sample_count),
    median_samples(gmp_samples, sample_count),
    median_paired_ratio(candidate_samples, gmp_samples, sample_count),
    paired_ratio_wins(candidate_samples, gmp_samples, sample_count, 0.98),
    sample_count,
    max_paired_ratio(candidate_samples, gmp_samples, sample_count));

  for (size_t family = 0; family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
    mpz_clears(ga[family], gb[family], gout[family], NULL);
    xray_bigint_clear(&a[family]);
    xray_bigint_clear(&b[family]);
    xray_bigint_clear(&candidate_out[family]);
    free(left_text[family]);
    free(right_text[family]);
  }
}

static void run_mul_toom3_unroll4_vs_gmp_probe_case(XrayBenchmarkReport *report, size_t digits, size_t leaf_threshold) {
  run_mul_toom3_unroll4_vs_gmp_probe_case_samples(
    report,
    digits,
    leaf_threshold,
    XRAY_BENCH_SAMPLES,
    "mul-toom3-unroll4-vs-gmp",
    "kernel mul Toom-3+unroll4 vs GMP");
}

static void run_mul_toom3_unroll4_deep_vs_gmp_probe_case(XrayBenchmarkReport *report, size_t digits, size_t leaf_threshold) {
  run_mul_toom3_unroll4_vs_gmp_probe_case_samples(
    report,
    digits,
    leaf_threshold,
    XRAY_BENCH_DEEP_SAMPLES,
    "mul-toom3-unroll4-deep-vs-gmp",
    "kernel mul Toom-3+unroll4 deep vs GMP");
}

static void run_mul_toom3_unroll4_recursive_vs_gmp_probe_case_samples(
  XrayBenchmarkReport *report,
  size_t digits,
  size_t leaf_threshold,
  size_t depth_limit,
  size_t sample_count,
  const char *operation_name,
  const char *name_prefix) {
  char *left_text[XRAY_MUL_OPERAND_FAMILIES] = {0};
  char *right_text[XRAY_MUL_OPERAND_FAMILIES] = {0};
  XrayScratchBigInt a[XRAY_MUL_OPERAND_FAMILIES];
  XrayScratchBigInt b[XRAY_MUL_OPERAND_FAMILIES];
  XrayScratchBigInt candidate_out[XRAY_MUL_OPERAND_FAMILIES];
  mpz_t ga[XRAY_MUL_OPERAND_FAMILIES];
  mpz_t gb[XRAY_MUL_OPERAND_FAMILIES];
  mpz_t gout[XRAY_MUL_OPERAND_FAMILIES];

  unsigned int iterations = perf_iterations("mul", digits);
  if (sample_count == 0 || sample_count > XRAY_BENCH_MAX_SAMPLES) sample_count = XRAY_BENCH_SAMPLES;
  int ok = 1;
  for (size_t family = 0; family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
    xray_bigint_init(&a[family]);
    xray_bigint_init(&b[family]);
    xray_bigint_init(&candidate_out[family]);
    mpz_inits(ga[family], gb[family], gout[family], NULL);
    left_text[family] = benchmark_decimal(digits, mul_operand_families[family].left_seed, mul_operand_families[family].left_high_lead);
    right_text[family] = benchmark_decimal(digits, mul_operand_families[family].right_seed, mul_operand_families[family].right_high_lead);
    ok = ok &&
      left_text[family] &&
      right_text[family] &&
      xray_bigint_set_decimal(&a[family], left_text[family]) &&
      xray_bigint_set_decimal(&b[family], right_text[family]) &&
      mpz_set_str(ga[family], left_text[family], 10) == 0 &&
      mpz_set_str(gb[family], right_text[family], 10) == 0;
  }

  unsigned long long candidate_samples[XRAY_BENCH_MAX_SAMPLES] = {0};
  unsigned long long gmp_samples[XRAY_BENCH_MAX_SAMPLES] = {0};
  int parity = 1;
  for (size_t sample = 0; sample < sample_count; ++sample) {
    if ((sample % 2U) == 0U) {
      unsigned long long candidate_started = xray_now_us();
      for (unsigned int index = 0; ok && index < iterations; ++index) {
        for (size_t family = 0; ok && family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
          ok = xray_bigint_mul_toom3_unroll4_recursive_probe(&candidate_out[family], &a[family], &b[family], leaf_threshold, depth_limit);
        }
      }
      candidate_samples[sample] = xray_now_us() - candidate_started;

      unsigned long long gmp_started = xray_now_us();
      for (unsigned int index = 0; ok && index < iterations; ++index) {
        for (size_t family = 0; family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
          mpz_mul(gout[family], ga[family], gb[family]);
        }
      }
      gmp_samples[sample] = xray_now_us() - gmp_started;
    } else {
      unsigned long long gmp_started = xray_now_us();
      for (unsigned int index = 0; ok && index < iterations; ++index) {
        for (size_t family = 0; family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
          mpz_mul(gout[family], ga[family], gb[family]);
        }
      }
      gmp_samples[sample] = xray_now_us() - gmp_started;

      unsigned long long candidate_started = xray_now_us();
      for (unsigned int index = 0; ok && index < iterations; ++index) {
        for (size_t family = 0; ok && family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
          ok = xray_bigint_mul_toom3_unroll4_recursive_probe(&candidate_out[family], &a[family], &b[family], leaf_threshold, depth_limit);
        }
      }
      candidate_samples[sample] = xray_now_us() - candidate_started;
    }

    for (size_t family = 0; family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
      char *candidate_text = xray_bigint_get_decimal(&candidate_out[family]);
      char *gmp_text = mpz_get_str(NULL, 10, gout[family]);
      parity = parity && ok && candidate_text && gmp_text && strcmp(candidate_text, gmp_text) == 0;
      free(candidate_text);
      free(gmp_text);
    }
  }

  append_mul_toom3_unroll4_recursive_vs_gmp_probe_result(
    report,
    operation_name,
    name_prefix,
    digits,
    leaf_threshold,
    depth_limit,
    XRAY_MUL_OPERAND_FAMILIES,
    parity,
    median_samples(candidate_samples, sample_count),
    median_samples(gmp_samples, sample_count),
    median_paired_ratio(candidate_samples, gmp_samples, sample_count),
    paired_ratio_wins(candidate_samples, gmp_samples, sample_count, 0.98),
    sample_count,
    max_paired_ratio(candidate_samples, gmp_samples, sample_count));

  for (size_t family = 0; family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
    mpz_clears(ga[family], gb[family], gout[family], NULL);
    xray_bigint_clear(&a[family]);
    xray_bigint_clear(&b[family]);
    xray_bigint_clear(&candidate_out[family]);
    free(left_text[family]);
    free(right_text[family]);
  }
}

static void run_mul_toom3_unroll4_recursive_vs_gmp_probe_case(
  XrayBenchmarkReport *report,
  size_t digits,
  size_t leaf_threshold,
  size_t depth_limit) {
  run_mul_toom3_unroll4_recursive_vs_gmp_probe_case_samples(
    report,
    digits,
    leaf_threshold,
    depth_limit,
    XRAY_BENCH_SAMPLES,
    "mul-toom3-u4-rec-vs-gmp",
    "kernel recursive Toom-3+unroll4 vs GMP");
}

static void run_mul_toom3_unroll4_recursive_deep_vs_gmp_probe_case(
  XrayBenchmarkReport *report,
  size_t digits,
  size_t leaf_threshold,
  size_t depth_limit) {
  run_mul_toom3_unroll4_recursive_vs_gmp_probe_case_samples(
    report,
    digits,
    leaf_threshold,
    depth_limit,
    XRAY_BENCH_DEEP_SAMPLES,
    "mul-toom3-u4-rec-deep-vs-gmp",
    "kernel recursive Toom-3+unroll4 deep vs GMP");
}
#endif

#if XRAY_HAS_MSVC_BMI2_ADX_INTRINSICS
static void run_mul_unroll4_vs_scratch_probe_case(XrayBenchmarkReport *report, size_t digits, size_t leaf_threshold) {
  char *left_text[XRAY_MUL_OPERAND_FAMILIES] = {0};
  char *right_text[XRAY_MUL_OPERAND_FAMILIES] = {0};
  XrayScratchBigInt a[XRAY_MUL_OPERAND_FAMILIES];
  XrayScratchBigInt b[XRAY_MUL_OPERAND_FAMILIES];
  XrayScratchBigInt candidate_out[XRAY_MUL_OPERAND_FAMILIES];
  XrayScratchBigInt baseline_out[XRAY_MUL_OPERAND_FAMILIES];

  unsigned int iterations = perf_iterations("mul", digits);
  int ok = 1;
  for (size_t family = 0; family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
    xray_bigint_init(&a[family]);
    xray_bigint_init(&b[family]);
    xray_bigint_init(&candidate_out[family]);
    xray_bigint_init(&baseline_out[family]);
    left_text[family] = benchmark_decimal(digits, mul_operand_families[family].left_seed, mul_operand_families[family].left_high_lead);
    right_text[family] = benchmark_decimal(digits, mul_operand_families[family].right_seed, mul_operand_families[family].right_high_lead);
    ok = ok &&
      left_text[family] &&
      right_text[family] &&
      xray_bigint_set_decimal(&a[family], left_text[family]) &&
      xray_bigint_set_decimal(&b[family], right_text[family]);
  }

  unsigned long long candidate_samples[XRAY_BENCH_SAMPLES] = {0};
  unsigned long long baseline_samples[XRAY_BENCH_SAMPLES] = {0};
  int parity = 1;
  for (unsigned int sample = 0; sample < XRAY_BENCH_SAMPLES; ++sample) {
    if ((sample % 2U) == 0U) {
      unsigned long long candidate_started = xray_now_us();
      for (unsigned int index = 0; ok && index < iterations; ++index) {
        for (size_t family = 0; ok && family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
          ok = xray_bigint_mul_unroll4_probe(&candidate_out[family], &a[family], &b[family], leaf_threshold);
        }
      }
      candidate_samples[sample] = xray_now_us() - candidate_started;

      unsigned long long baseline_started = xray_now_us();
      for (unsigned int index = 0; ok && index < iterations; ++index) {
        for (size_t family = 0; ok && family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
          ok = xray_bigint_mul_with_threshold(&baseline_out[family], &a[family], &b[family], leaf_threshold);
        }
      }
      baseline_samples[sample] = xray_now_us() - baseline_started;
    } else {
      unsigned long long baseline_started = xray_now_us();
      for (unsigned int index = 0; ok && index < iterations; ++index) {
        for (size_t family = 0; ok && family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
          ok = xray_bigint_mul_with_threshold(&baseline_out[family], &a[family], &b[family], leaf_threshold);
        }
      }
      baseline_samples[sample] = xray_now_us() - baseline_started;

      unsigned long long candidate_started = xray_now_us();
      for (unsigned int index = 0; ok && index < iterations; ++index) {
        for (size_t family = 0; ok && family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
          ok = xray_bigint_mul_unroll4_probe(&candidate_out[family], &a[family], &b[family], leaf_threshold);
        }
      }
      candidate_samples[sample] = xray_now_us() - candidate_started;
    }

    for (size_t family = 0; family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
      parity = parity && ok && xray_bigint_compare(&candidate_out[family], &baseline_out[family]) == 0;
    }
  }

  append_mul_unroll4_vs_scratch_probe_result(
    report,
    digits,
    leaf_threshold,
    XRAY_MUL_OPERAND_FAMILIES,
    parity,
    median_samples(candidate_samples, XRAY_BENCH_SAMPLES),
    median_samples(baseline_samples, XRAY_BENCH_SAMPLES),
    median_paired_ratio(candidate_samples, baseline_samples, XRAY_BENCH_SAMPLES),
    paired_ratio_wins(candidate_samples, baseline_samples, XRAY_BENCH_SAMPLES, 0.98),
    XRAY_BENCH_SAMPLES,
    max_paired_ratio(candidate_samples, baseline_samples, XRAY_BENCH_SAMPLES));

  for (size_t family = 0; family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
    xray_bigint_clear(&a[family]);
    xray_bigint_clear(&b[family]);
    xray_bigint_clear(&candidate_out[family]);
    xray_bigint_clear(&baseline_out[family]);
    free(left_text[family]);
    free(right_text[family]);
  }
}
#endif

#if XRAY_HAS_MSVC_BMI2_ADX_INTRINSICS
static void run_mul_unroll4_vs_gmp_probe_case_samples(
  XrayBenchmarkReport *report,
  size_t digits,
  size_t leaf_threshold,
  size_t sample_count,
  const char *operation_name,
  const char *name_prefix) {
  char *left_text[XRAY_MUL_OPERAND_FAMILIES] = {0};
  char *right_text[XRAY_MUL_OPERAND_FAMILIES] = {0};
  XrayScratchBigInt a[XRAY_MUL_OPERAND_FAMILIES];
  XrayScratchBigInt b[XRAY_MUL_OPERAND_FAMILIES];
  XrayScratchBigInt candidate_out[XRAY_MUL_OPERAND_FAMILIES];
  mpz_t ga[XRAY_MUL_OPERAND_FAMILIES];
  mpz_t gb[XRAY_MUL_OPERAND_FAMILIES];
  mpz_t gout[XRAY_MUL_OPERAND_FAMILIES];

  unsigned int iterations = perf_iterations("mul", digits);
  if (sample_count == 0 || sample_count > XRAY_BENCH_MAX_SAMPLES) sample_count = XRAY_BENCH_SAMPLES;
  int ok = 1;
  for (size_t family = 0; family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
    xray_bigint_init(&a[family]);
    xray_bigint_init(&b[family]);
    xray_bigint_init(&candidate_out[family]);
    mpz_inits(ga[family], gb[family], gout[family], NULL);
    left_text[family] = benchmark_decimal(digits, mul_operand_families[family].left_seed, mul_operand_families[family].left_high_lead);
    right_text[family] = benchmark_decimal(digits, mul_operand_families[family].right_seed, mul_operand_families[family].right_high_lead);
    ok = ok &&
      left_text[family] &&
      right_text[family] &&
      xray_bigint_set_decimal(&a[family], left_text[family]) &&
      xray_bigint_set_decimal(&b[family], right_text[family]) &&
      mpz_set_str(ga[family], left_text[family], 10) == 0 &&
      mpz_set_str(gb[family], right_text[family], 10) == 0;
  }

  unsigned long long candidate_samples[XRAY_BENCH_MAX_SAMPLES] = {0};
  unsigned long long gmp_samples[XRAY_BENCH_MAX_SAMPLES] = {0};
  int parity = 1;
  for (size_t sample = 0; sample < sample_count; ++sample) {
    if ((sample % 2U) == 0U) {
      unsigned long long candidate_started = xray_now_us();
      for (unsigned int index = 0; ok && index < iterations; ++index) {
        for (size_t family = 0; ok && family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
          ok = xray_bigint_mul_unroll4_probe(&candidate_out[family], &a[family], &b[family], leaf_threshold);
        }
      }
      candidate_samples[sample] = xray_now_us() - candidate_started;

      unsigned long long gmp_started = xray_now_us();
      for (unsigned int index = 0; ok && index < iterations; ++index) {
        for (size_t family = 0; family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
          mpz_mul(gout[family], ga[family], gb[family]);
        }
      }
      gmp_samples[sample] = xray_now_us() - gmp_started;
    } else {
      unsigned long long gmp_started = xray_now_us();
      for (unsigned int index = 0; ok && index < iterations; ++index) {
        for (size_t family = 0; family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
          mpz_mul(gout[family], ga[family], gb[family]);
        }
      }
      gmp_samples[sample] = xray_now_us() - gmp_started;

      unsigned long long candidate_started = xray_now_us();
      for (unsigned int index = 0; ok && index < iterations; ++index) {
        for (size_t family = 0; ok && family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
          ok = xray_bigint_mul_unroll4_probe(&candidate_out[family], &a[family], &b[family], leaf_threshold);
        }
      }
      candidate_samples[sample] = xray_now_us() - candidate_started;
    }

    for (size_t family = 0; family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
      char *candidate_text = xray_bigint_get_decimal(&candidate_out[family]);
      char *gmp_text = mpz_get_str(NULL, 10, gout[family]);
      parity = parity && ok && candidate_text && gmp_text && strcmp(candidate_text, gmp_text) == 0;
      free(candidate_text);
      free(gmp_text);
    }
  }

  append_mul_unroll4_vs_gmp_probe_result(
    report,
    operation_name,
    name_prefix,
    digits,
    leaf_threshold,
    XRAY_MUL_OPERAND_FAMILIES,
    parity,
    median_samples(candidate_samples, sample_count),
    median_samples(gmp_samples, sample_count),
    median_paired_ratio(candidate_samples, gmp_samples, sample_count),
    paired_ratio_wins(candidate_samples, gmp_samples, sample_count, 0.98),
    sample_count,
    max_paired_ratio(candidate_samples, gmp_samples, sample_count));

  for (size_t family = 0; family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
    mpz_clears(ga[family], gb[family], gout[family], NULL);
    xray_bigint_clear(&a[family]);
    xray_bigint_clear(&b[family]);
    xray_bigint_clear(&candidate_out[family]);
    free(left_text[family]);
    free(right_text[family]);
  }
}

static void run_mul_unroll4_vs_gmp_probe_case(XrayBenchmarkReport *report, size_t digits, size_t leaf_threshold) {
  run_mul_unroll4_vs_gmp_probe_case_samples(
    report,
    digits,
    leaf_threshold,
    XRAY_BENCH_SAMPLES,
    "mul-unroll4-vs-gmp",
    "kernel mul unroll4 versus GMP");
}

static void run_mul_unroll4_deep_vs_gmp_probe_case(XrayBenchmarkReport *report, size_t digits, size_t leaf_threshold) {
  run_mul_unroll4_vs_gmp_probe_case_samples(
    report,
    digits,
    leaf_threshold,
    XRAY_BENCH_DEEP_SAMPLES,
    "mul-unroll4-deep-vs-gmp",
    "kernel mul unroll4 deep versus GMP");
}
#endif

static void run_scratch_mul_case(XrayBenchmarkReport *report, size_t digits) {
  char *left_text[XRAY_MUL_OPERAND_FAMILIES] = {0};
  char *right_text[XRAY_MUL_OPERAND_FAMILIES] = {0};
  XrayScratchBigInt a[XRAY_MUL_OPERAND_FAMILIES];
  XrayScratchBigInt b[XRAY_MUL_OPERAND_FAMILIES];
  XrayScratchBigInt scratch_out[XRAY_MUL_OPERAND_FAMILIES];
  mpz_t ga[XRAY_MUL_OPERAND_FAMILIES];
  mpz_t gb[XRAY_MUL_OPERAND_FAMILIES];
  mpz_t gout[XRAY_MUL_OPERAND_FAMILIES];

  unsigned int iterations = perf_iterations("mul", digits);
  int ok = 1;
  for (size_t family = 0; family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
    xray_bigint_init(&a[family]);
    xray_bigint_init(&b[family]);
    xray_bigint_init(&scratch_out[family]);
    mpz_inits(ga[family], gb[family], gout[family], NULL);
    left_text[family] = benchmark_decimal(digits, mul_operand_families[family].left_seed, mul_operand_families[family].left_high_lead);
    right_text[family] = benchmark_decimal(digits, mul_operand_families[family].right_seed, mul_operand_families[family].right_high_lead);
    ok = ok &&
      left_text[family] &&
      right_text[family] &&
      xray_bigint_set_decimal(&a[family], left_text[family]) &&
      xray_bigint_set_decimal(&b[family], right_text[family]) &&
      mpz_set_str(ga[family], left_text[family], 10) == 0 &&
      mpz_set_str(gb[family], right_text[family], 10) == 0;
  }

  unsigned long long scratch_samples[XRAY_BENCH_SAMPLES] = {0};
  unsigned long long gmp_samples[XRAY_BENCH_SAMPLES] = {0};
  int parity = 1;
  for (unsigned int sample = 0; sample < XRAY_BENCH_SAMPLES; ++sample) {
    unsigned long long scratch_started = xray_now_us();
    for (unsigned int index = 0; ok && index < iterations; ++index) {
      for (size_t family = 0; ok && family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
        ok = xray_bigint_mul(&scratch_out[family], &a[family], &b[family]);
      }
    }
    scratch_samples[sample] = xray_now_us() - scratch_started;

    unsigned long long gmp_started = xray_now_us();
    for (unsigned int index = 0; ok && index < iterations; ++index) {
      for (size_t family = 0; family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
        mpz_mul(gout[family], ga[family], gb[family]);
      }
    }
    gmp_samples[sample] = xray_now_us() - gmp_started;

    for (size_t family = 0; family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
      char *scratch_text = xray_bigint_get_decimal(&scratch_out[family]);
      char *gmp_text = mpz_get_str(NULL, 10, gout[family]);
      parity = parity && ok && scratch_text && gmp_text && strcmp(scratch_text, gmp_text) == 0;
      free(scratch_text);
      free(gmp_text);
    }
  }

  append_perf_result(
    report,
    "mul",
    digits,
    XRAY_MUL_OPERAND_FAMILIES,
    parity,
    median_samples(scratch_samples, XRAY_BENCH_SAMPLES),
    median_samples(gmp_samples, XRAY_BENCH_SAMPLES),
    median_paired_ratio(scratch_samples, gmp_samples, XRAY_BENCH_SAMPLES),
    paired_ratio_wins(scratch_samples, gmp_samples, XRAY_BENCH_SAMPLES, 1.0),
    XRAY_BENCH_SAMPLES,
    max_paired_ratio(scratch_samples, gmp_samples, XRAY_BENCH_SAMPLES));

  for (size_t family = 0; family < XRAY_MUL_OPERAND_FAMILIES; ++family) {
    mpz_clears(ga[family], gb[family], gout[family], NULL);
    xray_bigint_clear(&a[family]);
    xray_bigint_clear(&b[family]);
    xray_bigint_clear(&scratch_out[family]);
    free(left_text[family]);
    free(right_text[family]);
  }
}

static void run_scratch_square_case(XrayBenchmarkReport *report, size_t digits) {
  char *text = benchmark_decimal(digits, 31, 1);
  if (!text) return;
  unsigned int iterations = perf_iterations("mul", digits);
  XrayScratchBigInt a, scratch_out;
  xray_bigint_init(&a);
  xray_bigint_init(&scratch_out);
  mpz_t ga, gout;
  mpz_inits(ga, gout, NULL);
  int ok = xray_bigint_set_decimal(&a, text) && mpz_set_str(ga, text, 10) == 0;

  unsigned long long scratch_samples[XRAY_BENCH_SAMPLES] = {0};
  unsigned long long gmp_samples[XRAY_BENCH_SAMPLES] = {0};
  int parity = 1;
  for (unsigned int sample = 0; sample < XRAY_BENCH_SAMPLES; ++sample) {
    unsigned long long scratch_started = xray_now_us();
    for (unsigned int index = 0; ok && index < iterations; ++index) {
      ok = xray_bigint_square(&scratch_out, &a);
    }
    scratch_samples[sample] = xray_now_us() - scratch_started;

    unsigned long long gmp_started = xray_now_us();
    for (unsigned int index = 0; ok && index < iterations; ++index) {
      mpz_mul(gout, ga, ga);
    }
    gmp_samples[sample] = xray_now_us() - gmp_started;

    char *scratch_text = xray_bigint_get_decimal(&scratch_out);
    char *gmp_text = mpz_get_str(NULL, 10, gout);
    parity = parity && ok && scratch_text && gmp_text && strcmp(scratch_text, gmp_text) == 0;
    free(scratch_text);
    free(gmp_text);
  }

  append_perf_result(
    report,
    "square",
    digits,
    1,
    parity,
    median_samples(scratch_samples, XRAY_BENCH_SAMPLES),
    median_samples(gmp_samples, XRAY_BENCH_SAMPLES),
    median_paired_ratio(scratch_samples, gmp_samples, XRAY_BENCH_SAMPLES),
    paired_ratio_wins(scratch_samples, gmp_samples, XRAY_BENCH_SAMPLES, 1.0),
    XRAY_BENCH_SAMPLES,
    max_paired_ratio(scratch_samples, gmp_samples, XRAY_BENCH_SAMPLES));

  mpz_clears(ga, gout, NULL);
  xray_bigint_clear(&a);
  xray_bigint_clear(&scratch_out);
  free(text);
}

static void run_square_vs_mul_probe_case(XrayBenchmarkReport *report, size_t digits) {
  char *text = benchmark_decimal(digits, 37, 1);
  if (!text) return;
  unsigned int iterations = perf_iterations("mul", digits);
  XrayScratchBigInt a, square_out, baseline_out;
  xray_bigint_init(&a);
  xray_bigint_init(&square_out);
  xray_bigint_init(&baseline_out);
  int ok = xray_bigint_set_decimal(&a, text);

  unsigned long long square_samples[XRAY_BENCH_SAMPLES] = {0};
  unsigned long long baseline_samples[XRAY_BENCH_SAMPLES] = {0};
  int parity = 1;
  for (unsigned int sample = 0; sample < XRAY_BENCH_SAMPLES; ++sample) {
    if ((sample % 2U) == 0U) {
      unsigned long long square_started = xray_now_us();
      for (unsigned int index = 0; ok && index < iterations; ++index) {
        ok = xray_bigint_square(&square_out, &a);
      }
      square_samples[sample] = xray_now_us() - square_started;

      unsigned long long baseline_started = xray_now_us();
      for (unsigned int index = 0; ok && index < iterations; ++index) {
        ok = xray_bigint_mul_with_threshold(&baseline_out, &a, &a, 64);
      }
      baseline_samples[sample] = xray_now_us() - baseline_started;
    } else {
      unsigned long long baseline_started = xray_now_us();
      for (unsigned int index = 0; ok && index < iterations; ++index) {
        ok = xray_bigint_mul_with_threshold(&baseline_out, &a, &a, 64);
      }
      baseline_samples[sample] = xray_now_us() - baseline_started;

      unsigned long long square_started = xray_now_us();
      for (unsigned int index = 0; ok && index < iterations; ++index) {
        ok = xray_bigint_square(&square_out, &a);
      }
      square_samples[sample] = xray_now_us() - square_started;
    }

    char *square_text = xray_bigint_get_decimal(&square_out);
    char *baseline_text = xray_bigint_get_decimal(&baseline_out);
    parity = parity && ok && square_text && baseline_text && strcmp(square_text, baseline_text) == 0;
    free(square_text);
    free(baseline_text);
  }

  append_square_vs_mul_probe_result(
    report,
    digits,
    parity,
    median_samples(square_samples, XRAY_BENCH_SAMPLES),
    median_samples(baseline_samples, XRAY_BENCH_SAMPLES),
    median_paired_ratio(square_samples, baseline_samples, XRAY_BENCH_SAMPLES),
    paired_ratio_wins(square_samples, baseline_samples, XRAY_BENCH_SAMPLES, 0.98),
    XRAY_BENCH_SAMPLES,
    max_paired_ratio(square_samples, baseline_samples, XRAY_BENCH_SAMPLES));

  xray_bigint_clear(&a);
  xray_bigint_clear(&square_out);
  xray_bigint_clear(&baseline_out);
  free(text);
}

static void run_square_karatsuba_probe_case(
  XrayBenchmarkReport *report,
  size_t digits,
  size_t threshold,
  int baseline_is_gmp) {
  char *text = benchmark_decimal(digits, 41, 1);
  if (!text) return;
  unsigned int iterations = perf_iterations("mul", digits);
  XrayScratchBigInt a, candidate_out, baseline_out;
  xray_bigint_init(&a);
  xray_bigint_init(&candidate_out);
  xray_bigint_init(&baseline_out);
  mpz_t ga, gout;
  mpz_inits(ga, gout, NULL);
  int ok = xray_bigint_set_decimal(&a, text) && mpz_set_str(ga, text, 10) == 0;

  unsigned long long candidate_samples[XRAY_BENCH_SAMPLES] = {0};
  unsigned long long baseline_samples[XRAY_BENCH_SAMPLES] = {0};
  int parity = 1;
  for (unsigned int sample = 0; sample < XRAY_BENCH_SAMPLES; ++sample) {
    if ((sample % 2U) == 0U) {
      unsigned long long candidate_started = xray_now_us();
      for (unsigned int index = 0; ok && index < iterations; ++index) {
        ok = xray_bigint_square_karatsuba_probe(&candidate_out, &a, threshold);
      }
      candidate_samples[sample] = xray_now_us() - candidate_started;

      unsigned long long baseline_started = xray_now_us();
      for (unsigned int index = 0; ok && index < iterations; ++index) {
        if (baseline_is_gmp) mpz_mul(gout, ga, ga);
        else ok = xray_bigint_mul_with_threshold(&baseline_out, &a, &a, 64);
      }
      baseline_samples[sample] = xray_now_us() - baseline_started;
    } else {
      unsigned long long baseline_started = xray_now_us();
      for (unsigned int index = 0; ok && index < iterations; ++index) {
        if (baseline_is_gmp) mpz_mul(gout, ga, ga);
        else ok = xray_bigint_mul_with_threshold(&baseline_out, &a, &a, 64);
      }
      baseline_samples[sample] = xray_now_us() - baseline_started;

      unsigned long long candidate_started = xray_now_us();
      for (unsigned int index = 0; ok && index < iterations; ++index) {
        ok = xray_bigint_square_karatsuba_probe(&candidate_out, &a, threshold);
      }
      candidate_samples[sample] = xray_now_us() - candidate_started;
    }

    char *candidate_text = xray_bigint_get_decimal(&candidate_out);
    char *baseline_text = baseline_is_gmp ? mpz_get_str(NULL, 10, gout) : xray_bigint_get_decimal(&baseline_out);
    parity = parity && ok && candidate_text && baseline_text && strcmp(candidate_text, baseline_text) == 0;
    free(candidate_text);
    free(baseline_text);
  }

  append_square_karatsuba_probe_result(
    report,
    baseline_is_gmp ? "square-karatsuba-vs-gmp" : "square-karatsuba-vs-mul",
    baseline_is_gmp ? "mpz_mul" : "generic-threshold-self-mul",
    digits,
    threshold,
    parity,
    median_samples(candidate_samples, XRAY_BENCH_SAMPLES),
    median_samples(baseline_samples, XRAY_BENCH_SAMPLES),
    median_paired_ratio(candidate_samples, baseline_samples, XRAY_BENCH_SAMPLES),
    paired_ratio_wins(candidate_samples, baseline_samples, XRAY_BENCH_SAMPLES, 0.98),
    XRAY_BENCH_SAMPLES,
    max_paired_ratio(candidate_samples, baseline_samples, XRAY_BENCH_SAMPLES));

  mpz_clears(ga, gout, NULL);
  xray_bigint_clear(&a);
  xray_bigint_clear(&candidate_out);
  xray_bigint_clear(&baseline_out);
  free(text);
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

  unsigned long long scratch_samples[XRAY_BENCH_SAMPLES] = {0};
  unsigned long long gmp_samples[XRAY_BENCH_SAMPLES] = {0};
  int parity = 1;
  for (unsigned int sample = 0; sample < XRAY_BENCH_SAMPLES; ++sample) {
    unsigned long long scratch_started = xray_now_us();
    for (unsigned int index = 0; ok && index < iterations; ++index) {
      ok = xray_bigint_divmod_u32(&quotient, &scratch_remainder, &a, divisor);
    }
    scratch_samples[sample] = xray_now_us() - scratch_started;

    unsigned long long gmp_started = xray_now_us();
    for (unsigned int index = 0; ok && index < iterations; ++index) {
      gmp_remainder = (unsigned long)mpz_tdiv_q_ui(gquot, ga, divisor);
    }
    gmp_samples[sample] = xray_now_us() - gmp_started;

    char *scratch_text = xray_bigint_get_decimal(&quotient);
    char *gmp_text = mpz_get_str(NULL, 10, gquot);
    parity = parity && ok && scratch_text && gmp_text && strcmp(scratch_text, gmp_text) == 0 &&
      scratch_remainder == (uint32_t)gmp_remainder &&
      xray_bigint_mod_u32(&a, divisor) == (uint32_t)gmp_remainder;
    free(scratch_text);
    free(gmp_text);
  }
  unsigned long long scratch_us = median_samples(scratch_samples, XRAY_BENCH_SAMPLES);
  unsigned long long gmp_us = median_samples(gmp_samples, XRAY_BENCH_SAMPLES);
  double paired_ratio = median_paired_ratio(scratch_samples, gmp_samples, XRAY_BENCH_SAMPLES);
  append_perf_result(
    report,
    "divmod-u32",
    digits,
    1,
    parity,
    scratch_us,
    gmp_us,
    paired_ratio,
    paired_ratio_wins(scratch_samples, gmp_samples, XRAY_BENCH_SAMPLES, 1.0),
    XRAY_BENCH_SAMPLES,
    max_paired_ratio(scratch_samples, gmp_samples, XRAY_BENCH_SAMPLES));

  mpz_clears(ga, gquot, NULL);
  xray_bigint_clear(&a);
  xray_bigint_clear(&quotient);
  free(text);
}

static char *benchmark_power_of_ten_decimal(size_t exponent) {
  char *text = (char *)calloc(exponent + 2U, 1);
  if (!text) return NULL;
  text[0] = '1';
  memset(text + 1, '0', exponent);
  text[exponent + 1U] = '\0';
  return text;
}

static void run_divmod_dc_power_probe_case(XrayBenchmarkReport *report, size_t digits, unsigned int seed) {
  size_t power_chunks = digits / (2U * 19U);
  if (power_chunks == 0) power_chunks = 1;
  char *numerator_text = benchmark_decimal(digits, seed, 1);
  char *divisor_text = benchmark_power_of_ten_decimal(power_chunks * 19U);
  if (!numerator_text || !divisor_text) {
    free(numerator_text);
    free(divisor_text);
    return;
  }

  unsigned int iterations = perf_iterations("divmod-bigint", digits);
  XrayScratchBigInt numerator, divisor, quotient, remainder;
  xray_bigint_init(&numerator);
  xray_bigint_init(&divisor);
  xray_bigint_init(&quotient);
  xray_bigint_init(&remainder);
  mpz_t gnum, gdiv, gquot, grem;
  mpz_inits(gnum, gdiv, gquot, grem, NULL);
  int ok = xray_bigint_set_decimal(&numerator, numerator_text) &&
    xray_bigint_set_decimal(&divisor, divisor_text) &&
    mpz_set_str(gnum, numerator_text, 10) == 0 &&
    mpz_set_str(gdiv, divisor_text, 10) == 0;

  unsigned long long candidate_samples[XRAY_BENCH_SAMPLES] = {0};
  unsigned long long baseline_samples[XRAY_BENCH_SAMPLES] = {0};
  int parity = 1;
  for (unsigned int sample = 0; sample < XRAY_BENCH_SAMPLES; ++sample) {
    unsigned long long candidate_started = xray_now_us();
    for (unsigned int index = 0; ok && index < iterations; ++index) {
      ok = xray_bigint_divmod(&quotient, &remainder, &numerator, &divisor);
    }
    candidate_samples[sample] = xray_now_us() - candidate_started;

    unsigned long long baseline_started = xray_now_us();
    for (unsigned int index = 0; ok && index < iterations; ++index) {
      mpz_tdiv_qr(gquot, grem, gnum, gdiv);
    }
    baseline_samples[sample] = xray_now_us() - baseline_started;

    char *quotient_text = xray_bigint_get_decimal(&quotient);
    char *remainder_text = xray_bigint_get_decimal(&remainder);
    char *gquot_text = mpz_get_str(NULL, 10, gquot);
    char *grem_text = mpz_get_str(NULL, 10, grem);
    parity = parity && ok && quotient_text && remainder_text && gquot_text && grem_text &&
      strcmp(quotient_text, gquot_text) == 0 &&
      strcmp(remainder_text, grem_text) == 0;
    free(quotient_text);
    free(remainder_text);
    free(gquot_text);
    free(grem_text);
  }

  append_divmod_dc_power_probe_result(
    report,
    digits,
    power_chunks,
    parity,
    median_samples(candidate_samples, XRAY_BENCH_SAMPLES),
    median_samples(baseline_samples, XRAY_BENCH_SAMPLES),
    median_paired_ratio(candidate_samples, baseline_samples, XRAY_BENCH_SAMPLES),
    paired_ratio_wins(candidate_samples, baseline_samples, XRAY_BENCH_SAMPLES, 0.98),
    XRAY_BENCH_SAMPLES,
    max_paired_ratio(candidate_samples, baseline_samples, XRAY_BENCH_SAMPLES));

  mpz_clears(gnum, gdiv, gquot, grem, NULL);
  xray_bigint_clear(&numerator);
  xray_bigint_clear(&divisor);
  xray_bigint_clear(&quotient);
  xray_bigint_clear(&remainder);
  free(numerator_text);
  free(divisor_text);
}

static void run_divmod_precomputed_probe_case(XrayBenchmarkReport *report, size_t digits, unsigned int seed) {
  size_t power_chunks = digits / (2U * 19U);
  if (power_chunks == 0) power_chunks = 1;
  char *numerator_text = benchmark_decimal(digits, seed, 1);
  char *divisor_text = benchmark_power_of_ten_decimal(power_chunks * 19U);
  if (!numerator_text || !divisor_text) {
    free(numerator_text);
    free(divisor_text);
    return;
  }

  unsigned int iterations = perf_iterations("divmod-bigint", digits);
  XrayScratchBigInt numerator, divisor, candidate_quotient, candidate_remainder, baseline_quotient, baseline_remainder;
  XrayBigIntDivisorContext divisor_context;
  xray_bigint_init(&numerator);
  xray_bigint_init(&divisor);
  xray_bigint_init(&candidate_quotient);
  xray_bigint_init(&candidate_remainder);
  xray_bigint_init(&baseline_quotient);
  xray_bigint_init(&baseline_remainder);
  xray_bigint_divisor_context_init(&divisor_context);
  mpz_t gnum, gdiv, gquot, grem;
  mpz_inits(gnum, gdiv, gquot, grem, NULL);
  int ok = xray_bigint_set_decimal(&numerator, numerator_text) &&
    xray_bigint_set_decimal(&divisor, divisor_text) &&
    xray_bigint_divisor_context_set(&divisor_context, &divisor) &&
    mpz_set_str(gnum, numerator_text, 10) == 0 &&
    mpz_set_str(gdiv, divisor_text, 10) == 0;

  unsigned long long candidate_samples[XRAY_BENCH_SAMPLES] = {0};
  unsigned long long baseline_samples[XRAY_BENCH_SAMPLES] = {0};
  int parity = 1;
  for (unsigned int sample = 0; sample < XRAY_BENCH_SAMPLES; ++sample) {
    unsigned long long candidate_started = xray_now_us();
    for (unsigned int index = 0; ok && index < iterations; ++index) {
      ok = xray_bigint_divmod_precomputed(&candidate_quotient, &candidate_remainder, &numerator, &divisor_context);
    }
    candidate_samples[sample] = xray_now_us() - candidate_started;

    unsigned long long baseline_started = xray_now_us();
    for (unsigned int index = 0; ok && index < iterations; ++index) {
      ok = xray_bigint_divmod(&baseline_quotient, &baseline_remainder, &numerator, &divisor);
    }
    baseline_samples[sample] = xray_now_us() - baseline_started;

    mpz_tdiv_qr(gquot, grem, gnum, gdiv);
    char *candidate_quotient_text = xray_bigint_get_decimal(&candidate_quotient);
    char *candidate_remainder_text = xray_bigint_get_decimal(&candidate_remainder);
    char *baseline_quotient_text = xray_bigint_get_decimal(&baseline_quotient);
    char *baseline_remainder_text = xray_bigint_get_decimal(&baseline_remainder);
    char *gquot_text = mpz_get_str(NULL, 10, gquot);
    char *grem_text = mpz_get_str(NULL, 10, grem);
    parity = parity && ok &&
      candidate_quotient_text &&
      candidate_remainder_text &&
      baseline_quotient_text &&
      baseline_remainder_text &&
      gquot_text &&
      grem_text &&
      strcmp(candidate_quotient_text, baseline_quotient_text) == 0 &&
      strcmp(candidate_remainder_text, baseline_remainder_text) == 0 &&
      strcmp(candidate_quotient_text, gquot_text) == 0 &&
      strcmp(candidate_remainder_text, grem_text) == 0;
    free(candidate_quotient_text);
    free(candidate_remainder_text);
    free(baseline_quotient_text);
    free(baseline_remainder_text);
    free(gquot_text);
    free(grem_text);
  }

  append_divmod_precomputed_probe_result(
    report,
    digits,
    power_chunks,
    parity,
    median_samples(candidate_samples, XRAY_BENCH_SAMPLES),
    median_samples(baseline_samples, XRAY_BENCH_SAMPLES),
    median_paired_ratio(candidate_samples, baseline_samples, XRAY_BENCH_SAMPLES),
    paired_ratio_wins(candidate_samples, baseline_samples, XRAY_BENCH_SAMPLES, 0.98),
    XRAY_BENCH_SAMPLES,
    max_paired_ratio(candidate_samples, baseline_samples, XRAY_BENCH_SAMPLES));

  mpz_clears(gnum, gdiv, gquot, grem, NULL);
  xray_bigint_divisor_context_clear(&divisor_context);
  xray_bigint_clear(&numerator);
  xray_bigint_clear(&divisor);
  xray_bigint_clear(&candidate_quotient);
  xray_bigint_clear(&candidate_remainder);
  xray_bigint_clear(&baseline_quotient);
  xray_bigint_clear(&baseline_remainder);
  free(numerator_text);
  free(divisor_text);
}

static void run_divmod_workspace_probe_case(XrayBenchmarkReport *report, size_t digits, unsigned int seed) {
  size_t power_chunks = digits / (2U * 19U);
  if (power_chunks == 0) power_chunks = 1;
  char *numerator_text = benchmark_decimal(digits, seed, 1);
  char *divisor_text = benchmark_power_of_ten_decimal(power_chunks * 19U);
  if (!numerator_text || !divisor_text) {
    free(numerator_text);
    free(divisor_text);
    return;
  }

  unsigned int iterations = perf_iterations("divmod-bigint", digits);
  XrayScratchBigInt numerator, divisor, candidate_quotient, candidate_remainder, baseline_quotient, baseline_remainder;
  XrayBigIntDivisorContext divisor_context;
  XrayBigIntDivisionWorkspace division_workspace;
  xray_bigint_init(&numerator);
  xray_bigint_init(&divisor);
  xray_bigint_init(&candidate_quotient);
  xray_bigint_init(&candidate_remainder);
  xray_bigint_init(&baseline_quotient);
  xray_bigint_init(&baseline_remainder);
  xray_bigint_divisor_context_init(&divisor_context);
  xray_bigint_division_workspace_init(&division_workspace);
  mpz_t gnum, gdiv, gquot, grem;
  mpz_inits(gnum, gdiv, gquot, grem, NULL);
  int ok = xray_bigint_set_decimal(&numerator, numerator_text) &&
    xray_bigint_set_decimal(&divisor, divisor_text) &&
    xray_bigint_divisor_context_set(&divisor_context, &divisor) &&
    mpz_set_str(gnum, numerator_text, 10) == 0 &&
    mpz_set_str(gdiv, divisor_text, 10) == 0;

  unsigned long long candidate_samples[XRAY_BENCH_SAMPLES] = {0};
  unsigned long long baseline_samples[XRAY_BENCH_SAMPLES] = {0};
  int parity = 1;
  for (unsigned int sample = 0; sample < XRAY_BENCH_SAMPLES; ++sample) {
    unsigned long long candidate_started = xray_now_us();
    for (unsigned int index = 0; ok && index < iterations; ++index) {
      ok = xray_bigint_divmod_precomputed_workspace(
        &candidate_quotient,
        &candidate_remainder,
        &numerator,
        &divisor_context,
        &division_workspace);
    }
    candidate_samples[sample] = xray_now_us() - candidate_started;

    unsigned long long baseline_started = xray_now_us();
    for (unsigned int index = 0; ok && index < iterations; ++index) {
      ok = xray_bigint_divmod_precomputed(&baseline_quotient, &baseline_remainder, &numerator, &divisor_context);
    }
    baseline_samples[sample] = xray_now_us() - baseline_started;

    mpz_tdiv_qr(gquot, grem, gnum, gdiv);
    char *candidate_quotient_text = xray_bigint_get_decimal(&candidate_quotient);
    char *candidate_remainder_text = xray_bigint_get_decimal(&candidate_remainder);
    char *baseline_quotient_text = xray_bigint_get_decimal(&baseline_quotient);
    char *baseline_remainder_text = xray_bigint_get_decimal(&baseline_remainder);
    char *gquot_text = mpz_get_str(NULL, 10, gquot);
    char *grem_text = mpz_get_str(NULL, 10, grem);
    parity = parity && ok &&
      candidate_quotient_text &&
      candidate_remainder_text &&
      baseline_quotient_text &&
      baseline_remainder_text &&
      gquot_text &&
      grem_text &&
      strcmp(candidate_quotient_text, baseline_quotient_text) == 0 &&
      strcmp(candidate_remainder_text, baseline_remainder_text) == 0 &&
      strcmp(candidate_quotient_text, gquot_text) == 0 &&
      strcmp(candidate_remainder_text, grem_text) == 0;
    free(candidate_quotient_text);
    free(candidate_remainder_text);
    free(baseline_quotient_text);
    free(baseline_remainder_text);
    free(gquot_text);
    free(grem_text);
  }

  append_divmod_workspace_probe_result(
    report,
    digits,
    power_chunks,
    parity,
    median_samples(candidate_samples, XRAY_BENCH_SAMPLES),
    median_samples(baseline_samples, XRAY_BENCH_SAMPLES),
    median_paired_ratio(candidate_samples, baseline_samples, XRAY_BENCH_SAMPLES),
    paired_ratio_wins(candidate_samples, baseline_samples, XRAY_BENCH_SAMPLES, 0.98),
    XRAY_BENCH_SAMPLES,
    max_paired_ratio(candidate_samples, baseline_samples, XRAY_BENCH_SAMPLES));

  mpz_clears(gnum, gdiv, gquot, grem, NULL);
  xray_bigint_division_workspace_clear(&division_workspace);
  xray_bigint_divisor_context_clear(&divisor_context);
  xray_bigint_clear(&numerator);
  xray_bigint_clear(&divisor);
  xray_bigint_clear(&candidate_quotient);
  xray_bigint_clear(&candidate_remainder);
  xray_bigint_clear(&baseline_quotient);
  xray_bigint_clear(&baseline_remainder);
  free(numerator_text);
  free(divisor_text);
}

static void run_divmod_preinv_qhat_probe_case(
  XrayBenchmarkReport *report,
  size_t digits,
  unsigned int seed,
  XrayDivmodPreinvQhatSafetyPoint *safety_point) {
  size_t power_chunks = digits / (2U * 19U);
  if (power_chunks == 0) power_chunks = 1;
  char *numerator_text = benchmark_decimal(digits, seed, 1);
  char *divisor_text = benchmark_power_of_ten_decimal(power_chunks * 19U);
  if (!numerator_text || !divisor_text) {
    free(numerator_text);
    free(divisor_text);
    return;
  }

  unsigned int iterations = perf_iterations("divmod-bigint", digits);
  XrayScratchBigInt numerator, divisor, candidate_quotient, candidate_remainder, baseline_quotient, baseline_remainder;
  XrayBigIntDivisorContext divisor_context;
  XrayBigIntDivisionWorkspace candidate_workspace;
  XrayBigIntDivisionWorkspace baseline_workspace;
  xray_bigint_init(&numerator);
  xray_bigint_init(&divisor);
  xray_bigint_init(&candidate_quotient);
  xray_bigint_init(&candidate_remainder);
  xray_bigint_init(&baseline_quotient);
  xray_bigint_init(&baseline_remainder);
  xray_bigint_divisor_context_init(&divisor_context);
  xray_bigint_division_workspace_init(&candidate_workspace);
  xray_bigint_division_workspace_init(&baseline_workspace);
  mpz_t gnum, gdiv, gquot, grem;
  mpz_inits(gnum, gdiv, gquot, grem, NULL);
  int ok = xray_bigint_set_decimal(&numerator, numerator_text) &&
    xray_bigint_set_decimal(&divisor, divisor_text) &&
    xray_bigint_divisor_context_set(&divisor_context, &divisor) &&
    mpz_set_str(gnum, numerator_text, 10) == 0 &&
    mpz_set_str(gdiv, divisor_text, 10) == 0;

  if (ok) {
    ok = xray_bigint_divmod_preinv_qhat_probe(
      &candidate_quotient,
      &candidate_remainder,
      &numerator,
      &divisor_context,
      &candidate_workspace) &&
      xray_bigint_divmod_precomputed_workspace(
        &baseline_quotient,
        &baseline_remainder,
        &numerator,
        &divisor_context,
        &baseline_workspace);
  }

  unsigned long long candidate_samples[XRAY_BENCH_SAMPLES] = {0};
  unsigned long long baseline_samples[XRAY_BENCH_SAMPLES] = {0};
  int parity = 1;
  for (unsigned int sample = 0; sample < XRAY_BENCH_SAMPLES; ++sample) {
    unsigned long long candidate_started = xray_now_us();
    for (unsigned int index = 0; ok && index < iterations; ++index) {
      ok = xray_bigint_divmod_preinv_qhat_probe(
        &candidate_quotient,
        &candidate_remainder,
        &numerator,
        &divisor_context,
        &candidate_workspace);
    }
    candidate_samples[sample] = xray_now_us() - candidate_started;

    unsigned long long baseline_started = xray_now_us();
    for (unsigned int index = 0; ok && index < iterations; ++index) {
      ok = xray_bigint_divmod_precomputed_workspace(
        &baseline_quotient,
        &baseline_remainder,
        &numerator,
        &divisor_context,
        &baseline_workspace);
    }
    baseline_samples[sample] = xray_now_us() - baseline_started;

    mpz_tdiv_qr(gquot, grem, gnum, gdiv);
    char *candidate_quotient_text = xray_bigint_get_decimal(&candidate_quotient);
    char *candidate_remainder_text = xray_bigint_get_decimal(&candidate_remainder);
    char *baseline_quotient_text = xray_bigint_get_decimal(&baseline_quotient);
    char *baseline_remainder_text = xray_bigint_get_decimal(&baseline_remainder);
    char *gquot_text = mpz_get_str(NULL, 10, gquot);
    char *grem_text = mpz_get_str(NULL, 10, grem);
    parity = parity && ok &&
      candidate_quotient_text &&
      candidate_remainder_text &&
      baseline_quotient_text &&
      baseline_remainder_text &&
      gquot_text &&
      grem_text &&
      strcmp(candidate_quotient_text, baseline_quotient_text) == 0 &&
      strcmp(candidate_remainder_text, baseline_remainder_text) == 0 &&
      strcmp(candidate_quotient_text, gquot_text) == 0 &&
      strcmp(candidate_remainder_text, grem_text) == 0;
    free(candidate_quotient_text);
    free(candidate_remainder_text);
    free(baseline_quotient_text);
    free(baseline_remainder_text);
    free(gquot_text);
    free(grem_text);
  }

  unsigned long long candidate_median = median_samples(candidate_samples, XRAY_BENCH_SAMPLES);
  unsigned long long baseline_median = median_samples(baseline_samples, XRAY_BENCH_SAMPLES);
  double paired_ratio = median_paired_ratio(candidate_samples, baseline_samples, XRAY_BENCH_SAMPLES);
  size_t stable_sample_count = paired_ratio_wins(candidate_samples, baseline_samples, XRAY_BENCH_SAMPLES, 0.98);
  double worst_pair_ratio = max_paired_ratio(candidate_samples, baseline_samples, XRAY_BENCH_SAMPLES);
  if (safety_point) {
    safety_point->digits = digits;
    safety_point->power_chunks = power_chunks;
    safety_point->parity = parity;
    safety_point->paired_ratio = paired_ratio;
    safety_point->stable_sample_count = stable_sample_count;
    safety_point->sample_count = XRAY_BENCH_SAMPLES;
    safety_point->worst_pair_ratio = worst_pair_ratio;
  }

  append_divmod_preinv_qhat_probe_result(
    report,
    digits,
    power_chunks,
    parity,
    candidate_median,
    baseline_median,
    paired_ratio,
    stable_sample_count,
    XRAY_BENCH_SAMPLES,
    worst_pair_ratio);

  mpz_clears(gnum, gdiv, gquot, grem, NULL);
  xray_bigint_division_workspace_clear(&candidate_workspace);
  xray_bigint_division_workspace_clear(&baseline_workspace);
  xray_bigint_divisor_context_clear(&divisor_context);
  xray_bigint_clear(&numerator);
  xray_bigint_clear(&divisor);
  xray_bigint_clear(&candidate_quotient);
  xray_bigint_clear(&candidate_remainder);
  xray_bigint_clear(&baseline_quotient);
  xray_bigint_clear(&baseline_remainder);
  free(numerator_text);
  free(divisor_text);
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

  unsigned long long scratch_samples[XRAY_BENCH_SAMPLES] = {0};
  unsigned long long gmp_samples[XRAY_BENCH_SAMPLES] = {0};
  int parity = 1;
  for (unsigned int sample = 0; sample < XRAY_BENCH_SAMPLES; ++sample) {
    unsigned long long scratch_started = xray_now_us();
    for (unsigned int index = 0; ok && index < iterations; ++index) {
      if (strcmp(operation, "mod-u32") == 0) scratch_result = xray_bigint_mod_u32(&a, modulus);
      else if (strcmp(operation, "gcd-u32") == 0) scratch_result = xray_bigint_gcd_u32(&a, gcd_operand);
      else scratch_result = xray_bigint_powmod_u32(&a, exponent, modulus);
    }
    scratch_samples[sample] = xray_now_us() - scratch_started;

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
    gmp_samples[sample] = xray_now_us() - gmp_started;
    parity = parity && ok && scratch_result == (uint32_t)gmp_result;
  }
  unsigned long long scratch_us = median_samples(scratch_samples, XRAY_BENCH_SAMPLES);
  unsigned long long gmp_us = median_samples(gmp_samples, XRAY_BENCH_SAMPLES);
  double paired_ratio = median_paired_ratio(scratch_samples, gmp_samples, XRAY_BENCH_SAMPLES);
  append_perf_result(
    report,
    operation,
    digits,
    1,
    parity,
    scratch_us,
    gmp_us,
    paired_ratio,
    paired_ratio_wins(scratch_samples, gmp_samples, XRAY_BENCH_SAMPLES, 1.0),
    XRAY_BENCH_SAMPLES,
    max_paired_ratio(scratch_samples, gmp_samples, XRAY_BENCH_SAMPLES));

  mpz_clears(ga, gmodulus, gout, NULL);
  xray_bigint_clear(&a);
  free(text);
}

static void run_u32_precompute_probe_case(XrayBenchmarkReport *report, const char *operation, size_t digits) {
  char *text = benchmark_decimal(digits, 29, 1);
  if (!text) return;
  const uint32_t modulus = 1000000007U;
  const uint32_t exponent = 65537U;
  unsigned int iterations = perf_iterations(strstr(operation, "powmod") ? "powmod-u32" : "mod-u32", digits);
  XrayScratchBigInt a;
  xray_bigint_init(&a);
  XrayBigIntU32ModContext context;
  int ok = xray_bigint_set_decimal(&a, text) &&
    xray_bigint_u32_mod_context_init(&context, modulus);
  unsigned long long candidate_samples[XRAY_BENCH_SAMPLES] = {0};
  unsigned long long baseline_samples[XRAY_BENCH_SAMPLES] = {0};
  uint32_t candidate_result = 0;
  uint32_t baseline_result = 0;
  int parity = 1;
  int kind = strcmp(operation, "mod-u32") == 0 ? 0 : (strcmp(operation, "gcd-u32") == 0 ? 1 : 2);

  for (unsigned int sample = 0; sample < XRAY_BENCH_SAMPLES; ++sample) {
    unsigned long long candidate_started = xray_now_us();
    for (unsigned int index = 0; ok && index < iterations; ++index) {
      if (kind == 0) candidate_result = xray_bigint_mod_u32_precomputed(&a, &context);
      else if (kind == 1) candidate_result = xray_bigint_gcd_u32_precomputed(&a, &context);
      else candidate_result = xray_bigint_powmod_u32_precomputed(&a, exponent, &context);
    }
    candidate_samples[sample] = xray_now_us() - candidate_started;

    unsigned long long baseline_started = xray_now_us();
    for (unsigned int index = 0; ok && index < iterations; ++index) {
      if (kind == 0) baseline_result = xray_bigint_mod_u32(&a, modulus);
      else if (kind == 1) baseline_result = xray_bigint_gcd_u32(&a, modulus);
      else baseline_result = xray_bigint_powmod_u32(&a, exponent, modulus);
    }
    baseline_samples[sample] = xray_now_us() - baseline_started;
    parity = parity && ok && candidate_result == baseline_result;
  }

  append_u32_precompute_probe_result(
    report,
    operation,
    digits,
    parity,
    median_samples(candidate_samples, XRAY_BENCH_SAMPLES),
    median_samples(baseline_samples, XRAY_BENCH_SAMPLES),
    median_paired_ratio(candidate_samples, baseline_samples, XRAY_BENCH_SAMPLES),
    paired_ratio_wins(candidate_samples, baseline_samples, XRAY_BENCH_SAMPLES, 0.98),
    XRAY_BENCH_SAMPLES,
    max_paired_ratio(candidate_samples, baseline_samples, XRAY_BENCH_SAMPLES));

  xray_bigint_clear(&a);
  free(text);
}

static void run_scratch_bigint_gates(XrayBenchmarkReport *report) {
  const size_t sizes[] = {40, 150, 1000, 4096, 8192};
  for (size_t index = 0; index < sizeof(sizes) / sizeof(sizes[0]); ++index) {
    run_scratch_parse_case(report, sizes[index]);
    run_scratch_format_case(report, sizes[index]);
    run_scratch_binary_case(report, "add", sizes[index]);
    run_scratch_binary_case(report, "sub", sizes[index]);
    run_scratch_modular_case(report, "mod-u32", sizes[index]);
    run_scratch_modular_case(report, "gcd-u32", sizes[index]);
    run_scratch_modular_case(report, "powmod-u32", sizes[index]);
    run_scratch_divmod_case(report, sizes[index]);
    run_scratch_mul_case(report, sizes[index]);
    run_scratch_square_case(report, sizes[index]);
    run_square_vs_mul_probe_case(report, sizes[index]);
  }
  run_scratch_format_case(report, 768);
  run_scratch_format_case(report, 896);
  run_scratch_mul_case(report, 16384);
  run_scratch_square_case(report, 16384);
  run_square_vs_mul_probe_case(report, 16384);
  const size_t divmod_dc_power_digits[] = {4096, 8192, 16384};
  XrayDivmodPreinvQhatSafetyPoint divmod_preinv_qhat_points[3];
  memset(divmod_preinv_qhat_points, 0, sizeof(divmod_preinv_qhat_points));
  for (size_t digit_index = 0; digit_index < sizeof(divmod_dc_power_digits) / sizeof(divmod_dc_power_digits[0]); ++digit_index) {
    run_divmod_dc_power_probe_case(
      report,
      divmod_dc_power_digits[digit_index],
      (unsigned int)(149U + digit_index));
    run_divmod_precomputed_probe_case(
      report,
      divmod_dc_power_digits[digit_index],
      (unsigned int)(149U + digit_index));
    run_divmod_workspace_probe_case(
      report,
      divmod_dc_power_digits[digit_index],
      (unsigned int)(149U + digit_index));
    run_divmod_preinv_qhat_probe_case(
      report,
      divmod_dc_power_digits[digit_index],
      (unsigned int)(173U + digit_index),
      &divmod_preinv_qhat_points[digit_index]);
  }
  append_divmod_preinv_qhat_safety_result(
    report,
    divmod_preinv_qhat_points,
    sizeof(divmod_preinv_qhat_points) / sizeof(divmod_preinv_qhat_points[0]));
  const size_t square_probe_sizes[] = {1000, 4096, 8192, 16384};
  const size_t square_probe_thresholds[] = {16, 24, 32, 48, 64, 80, 96, 112, 128, 160};
  for (size_t size_index = 0; size_index < sizeof(square_probe_sizes) / sizeof(square_probe_sizes[0]); ++size_index) {
    for (size_t threshold_index = 0; threshold_index < sizeof(square_probe_thresholds) / sizeof(square_probe_thresholds[0]); ++threshold_index) {
      run_square_karatsuba_probe_case(report, square_probe_sizes[size_index], square_probe_thresholds[threshold_index], 0);
      run_square_karatsuba_probe_case(report, square_probe_sizes[size_index], square_probe_thresholds[threshold_index], 1);
    }
    run_square_policy_probe_case(
      report,
      square_probe_sizes[size_index],
      101U,
      "current-default",
      "current-scratch-square",
      "square-policy-current-default",
      "mpn_sqr-product-baseline",
      0,
      square_policy_current_default);
    run_square_policy_probe_case(
      report,
      square_probe_sizes[size_index],
      103U,
      "karatsuba-thr96",
      "karatsuba-square",
      "square-policy-karatsuba-thr96",
      "mpn_sqr-karatsuba-threshold",
      96,
      square_policy_karatsuba_threshold);
  }
}

static void run_kernel_probes(XrayBenchmarkReport *report) {
  run_qhat_estimator_probe_case(report);

  const size_t bit_sizes[] = {2048, 16384};
  for (size_t index = 0; index < sizeof(bit_sizes) / sizeof(bit_sizes[0]); ++index) {
    run_kernel_probe64_case(report, "add-carry", bit_sizes[index]);
    run_kernel_probe64_case(report, "sub-carry", bit_sizes[index]);
#if XRAY_HAS_MSVC_CARRY_INTRINSICS
    run_kernel_probe_intrinsic_case(report, "add-carry", bit_sizes[index]);
    run_kernel_probe_intrinsic_case(report, "sub-carry", bit_sizes[index]);
#endif
#if XRAY_HAS_MSVC_BMI2_ADX_INTRINSICS
    run_kernel_probe_muladd_unroll_case(report, bit_sizes[index], 4U);
    run_kernel_probe_muladd_unroll_case(report, bit_sizes[index], 8U);
    run_kernel_probe_muladd_bmi2_adx_case(report, bit_sizes[index]);
#endif
  }

  const size_t u32_precompute_digits[] = {40, 150, 512, 1000, 2048, 4096, 8192, 16384};
  for (size_t digit_index = 0; digit_index < sizeof(u32_precompute_digits) / sizeof(u32_precompute_digits[0]); ++digit_index) {
    run_u32_precompute_probe_case(report, "mod-u32", u32_precompute_digits[digit_index]);
    run_u32_precompute_probe_case(report, "gcd-u32", u32_precompute_digits[digit_index]);
    run_u32_precompute_probe_case(report, "powmod-u32", u32_precompute_digits[digit_index]);
  }

  const size_t parse_chunk_digits[] = {40, 150, 512, 1000, 2048, 4096, 8192, 16384};
  const unsigned int parse_chunk_sizes[] = {8, 9, 10, 15, 18, 19};
  for (size_t digit_index = 0; digit_index < sizeof(parse_chunk_digits) / sizeof(parse_chunk_digits[0]); ++digit_index) {
    for (size_t chunk_index = 0; chunk_index < sizeof(parse_chunk_sizes) / sizeof(parse_chunk_sizes[0]); ++chunk_index) {
      run_parse_chunk_probe_case(report, parse_chunk_digits[digit_index], parse_chunk_sizes[chunk_index]);
    }
  }

  const size_t format_digits[] = {256, 512, 1000, 2048, 4096, 8192, 16384};
  const size_t format_thresholds[] = {8, 16, 24, 32, 40, 44, 48, 52, 56, 64, 80, 96, 128, 160, 192, 256};
  for (size_t digit_index = 0; digit_index < sizeof(format_digits) / sizeof(format_digits[0]); ++digit_index) {
    for (size_t threshold_index = 0; threshold_index < sizeof(format_thresholds) / sizeof(format_thresholds[0]); ++threshold_index) {
      run_format_threshold_probe_case(report, format_digits[digit_index], format_thresholds[threshold_index]);
    }
  }
  run_format_divider_probe_case(report, 1000);
  run_format_divider_probe_case(report, 4096);
  run_format_divider_probe_case(report, 8192);
  run_format_wide_probe_case(report, 1000);
  run_format_wide_probe_case(report, 4096);
  run_format_wide_probe_case(report, 8192);
  run_format_folded_probe_case(report, 1000);
  run_format_folded_probe_case(report, 4096);
  run_format_folded_probe_case(report, 8192);
  const size_t format_pair_digits[] = {40, 150, 256, 512, 1000, 2048, 4096, 8192, 16384};
  for (size_t digit_index = 0; digit_index < sizeof(format_pair_digits) / sizeof(format_pair_digits[0]); ++digit_index) {
    run_format_pair_writer_probe_case(report, format_pair_digits[digit_index], 0);
    run_format_pair_writer_probe_case(report, format_pair_digits[digit_index], 1);
  }
  const size_t format_strategy_digits[] = {1000, 4096, 8192, 16384};
  for (size_t digit_index = 0; digit_index < sizeof(format_strategy_digits) / sizeof(format_strategy_digits[0]); ++digit_index) {
    size_t digits = format_strategy_digits[digit_index];
    run_format_variant_pair_probe_case(
      report,
      digits,
      29U,
      "format-dc-route",
      "format D&C production route",
      "direct16-vs-ladder8",
      "decimal-dc-direct-writer-leaf16",
      "decimal-dc-pow2-ladder-leaf8",
      "decimal-format-dc-route",
      "mpn_dc_get_str-output-buffer",
      19U,
      format_dc_direct_leaf16_probe,
      format_dc_ladder_leaf8_probe);
    run_format_variant_probe_case(
      report,
      digits,
      31U,
      "format-mixed-writer",
      "format mixed pair writer",
      "mixed-production-chunks",
      "decimal-mixed-pair-writer",
      "current-scratch-format",
      "decimal-format-mixed-writer",
      "mpn_get_str-output-emission",
      9U,
      xray_bigint_get_decimal_mixed_pair_writer_probe);
    run_format_variant_probe_case(
      report,
      digits,
      37U,
      "format-folded-hwdiv",
      "format folded hwdiv",
      "folded-direct128",
      "decimal-folded-hwdiv",
      "current-scratch-format",
      "decimal-format-folded-hwdiv",
      "mpn_get_str-division",
      9U,
      xray_bigint_get_decimal_folded_hwdiv_probe);
    run_format_variant_probe_case(
      report,
      digits,
      41U,
      "format-hwdiv-mixed",
      "format hwdiv mixed",
      "folded-direct128-mixed",
      "decimal-folded-hwdiv-mixed",
      "current-scratch-format",
      "decimal-format-hwdiv-mixed",
      "mpn_get_str-division+output-emission",
      9U,
      xray_bigint_get_decimal_folded_hwdiv_mixed_pair_probe);
    run_format_variant_probe_case(
      report,
      digits,
      43U,
      "format-divide-1e19",
      "format MPIR basecase",
      "divide-copy-by-1e19",
      "decimal-divide-1e19",
      "current-scratch-format",
      "decimal-format-divide-1e19",
      "mpn_sb_get_str-largest-decimal-power",
      19U,
      xray_bigint_get_decimal_divide_1e19_probe);
    run_format_variant_probe_case(
      report,
      digits,
      47U,
      "format-divide-1e19-pairs",
      "format MPIR basecase pair writer",
      "divide-copy-by-1e19-pair-writer",
      "decimal-divide-1e19-pair-writer",
      "current-scratch-format",
      "decimal-format-divide-1e19-pairs",
      "mpn_sb_get_str-largest-decimal-power+digit-emission",
      19U,
      format_divide_1e19_pair_writer_probe);
    run_format_variant_probe_case(
      report,
      digits,
      49U,
      "format-divide-1e19-preinv",
      "format MPIR basecase preinv divider",
      "divide-copy-by-1e19-preinv",
      "decimal-divide-1e19-preinv",
      "current-scratch-format",
      "decimal-format-divide-1e19-preinv",
      "mpn_sb_get_str-preinverted-divrem-1",
      19U,
      format_divide_1e19_preinv_probe);
    run_format_variant_probe_case(
      report,
      digits,
      51U,
      "format-divide-1e19-preinv-pairs",
      "format MPIR basecase preinv pair writer",
      "divide-copy-by-1e19-preinv-pair-writer",
      "decimal-divide-1e19-preinv-pair-writer",
      "current-scratch-format",
      "decimal-format-divide-1e19-preinv-pairs",
      "mpn_sb_get_str-preinverted-divrem-1+digit-emission",
      19U,
      format_divide_1e19_preinv_pair_writer_probe);
  }
  const size_t format_dc_leaf_chunks[] = {8, 16, 32, 64};
  XrayFormatProbeFn format_dc_probes[] = {
    format_dc_leaf8_probe,
    format_dc_leaf16_probe,
    format_dc_leaf32_probe,
    format_dc_leaf64_probe
  };
  XrayFormatProbeFn format_dc_ladder_probes[] = {
    format_dc_ladder_leaf8_probe,
    format_dc_ladder_leaf16_probe,
    format_dc_ladder_leaf32_probe,
    format_dc_ladder_leaf64_probe
  };
  const size_t format_dc_static_leaf_chunks[] = {8, 16};
  XrayFormatProbeFn format_dc_static_ladder_probes[] = {
    format_dc_static_ladder_leaf8_probe,
    format_dc_static_ladder_leaf16_probe
  };
  XrayFormatProbeFn format_dc_direct_probes[] = {
    format_dc_direct_leaf8_probe,
    format_dc_direct_leaf16_probe,
    format_dc_direct_leaf32_probe,
    format_dc_direct_leaf64_probe
  };
  XrayFormatProbeFn format_dc_static_direct_probes[] = {
    format_dc_static_direct_leaf8_probe,
    format_dc_static_direct_leaf16_probe
  };
  const size_t format_dc_division_leaf_chunks[] = {8, 16};
  XrayFormatProbeFn format_dc_workspace_probes[] = {
    format_dc_workspace_leaf8_probe,
    format_dc_workspace_leaf16_probe
  };
  XrayFormatProbeFn format_dc_preinv_qhat_probes[] = {
    format_dc_preinv_qhat_leaf8_probe,
    format_dc_preinv_qhat_leaf16_probe
  };
  for (size_t digit_index = 0; digit_index < sizeof(format_strategy_digits) / sizeof(format_strategy_digits[0]); ++digit_index) {
    size_t digits = format_strategy_digits[digit_index];
    for (size_t leaf_index = 0; leaf_index < sizeof(format_dc_leaf_chunks) / sizeof(format_dc_leaf_chunks[0]); ++leaf_index) {
      char mode[64];
      char label[64];
      snprintf(mode, sizeof(mode), "divide-conquer leafThreshold=%zu", format_dc_leaf_chunks[leaf_index]);
      snprintf(label, sizeof(label), "format D&C leaf %zu", format_dc_leaf_chunks[leaf_index]);
      run_format_variant_probe_case(
        report,
        digits,
        (unsigned int)(53U + leaf_index),
        "format-dc",
        label,
        mode,
        "decimal-dc-powers",
        "current-scratch-format",
        "decimal-format-dc",
        "mpn_dc_get_str-powtab",
        19U,
        format_dc_probes[leaf_index]);
      snprintf(mode, sizeof(mode), "divide-conquer-ladder leafThreshold=%zu", format_dc_leaf_chunks[leaf_index]);
      snprintf(label, sizeof(label), "format D&C ladder leaf %zu", format_dc_leaf_chunks[leaf_index]);
      run_format_variant_probe_case(
        report,
        digits,
        (unsigned int)(67U + leaf_index),
        "format-dc-ladder",
        label,
        mode,
        "decimal-dc-pow2-ladder",
        "current-scratch-format",
        "decimal-format-dc-ladder",
        "mpn_dc_get_str-powtab-squares",
        19U,
        format_dc_ladder_probes[leaf_index]);
      snprintf(mode, sizeof(mode), "divide-conquer-direct leafThreshold=%zu", format_dc_leaf_chunks[leaf_index]);
      snprintf(label, sizeof(label), "format D&C direct leaf %zu", format_dc_leaf_chunks[leaf_index]);
      run_format_variant_probe_case(
        report,
        digits,
        (unsigned int)(79U + leaf_index),
        "format-dc-direct",
        label,
        mode,
        "decimal-dc-direct-writer",
        "current-scratch-format",
        "decimal-format-dc-direct",
        "mpn_dc_get_str-output-buffer",
        19U,
        format_dc_direct_probes[leaf_index]);
    }
    for (size_t leaf_index = 0; leaf_index < sizeof(format_dc_static_leaf_chunks) / sizeof(format_dc_static_leaf_chunks[0]); ++leaf_index) {
      char mode[64];
      char label[80];
      snprintf(mode, sizeof(mode), "dc-static-ladder leafThreshold=%zu", format_dc_static_leaf_chunks[leaf_index]);
      snprintf(label, sizeof(label), "format D&C static ladder leaf %zu", format_dc_static_leaf_chunks[leaf_index]);
      run_format_variant_probe_case(
        report,
        digits,
        (unsigned int)(101U + leaf_index),
        "format-dc-static-ladder",
        label,
        mode,
        "dc-static-pow2",
        "current-scratch-format",
        "format-dc-static-ladder",
        "static-powtab",
        19U,
        format_dc_static_ladder_probes[leaf_index]);
      snprintf(mode, sizeof(mode), "dc-static-direct leafThreshold=%zu", format_dc_static_leaf_chunks[leaf_index]);
      snprintf(label, sizeof(label), "format D&C static direct leaf %zu", format_dc_static_leaf_chunks[leaf_index]);
      run_format_variant_probe_case(
        report,
        digits,
        (unsigned int)(109U + leaf_index),
        "format-dc-static-direct",
        label,
        mode,
        "dc-static-direct",
        "current-scratch-format",
        "format-dc-static-direct",
        "static-powtab+buffer",
        19U,
        format_dc_static_direct_probes[leaf_index]);
    }
    for (size_t leaf_index = 0; leaf_index < sizeof(format_dc_division_leaf_chunks) / sizeof(format_dc_division_leaf_chunks[0]); ++leaf_index) {
      char mode[80];
      char label[80];
      snprintf(
        mode,
        sizeof(mode),
        "dc-direct-workspace leafThreshold=%zu",
        format_dc_division_leaf_chunks[leaf_index]);
      snprintf(
        label,
        sizeof(label),
        "format D&C workspace leaf %zu",
        format_dc_division_leaf_chunks[leaf_index]);
      run_format_variant_pair_probe_case(
        report,
        digits,
        (unsigned int)(127U + leaf_index),
        "format-dc-workspace",
        label,
        mode,
        "decimal-dc-direct-workspace",
        "current-scratch-format",
        "decimal-format-dc-workspace",
        "mpn_dc_get_str-divisor-context",
        19U,
        format_dc_workspace_probes[leaf_index],
        xray_bigint_get_decimal);
      snprintf(
        mode,
        sizeof(mode),
        "dc-direct-preinv-qhat leafThreshold=%zu",
        format_dc_division_leaf_chunks[leaf_index]);
      snprintf(
        label,
        sizeof(label),
        "format D&C preinv qhat leaf %zu",
        format_dc_division_leaf_chunks[leaf_index]);
      run_format_variant_pair_probe_case(
        report,
        digits,
        (unsigned int)(139U + leaf_index),
        "format-dc-preinv-qhat",
        label,
        mode,
        "decimal-dc-direct-preinv-qhat",
        "current-scratch-format",
        "decimal-format-dc-preinv-qhat",
        "mpn_dc_get_str-preinverted-qhat",
        19U,
        format_dc_preinv_qhat_probes[leaf_index],
        xray_bigint_get_decimal);
    }
  }

  for (size_t digit_index = 0; digit_index < sizeof(format_strategy_digits) / sizeof(format_strategy_digits[0]); ++digit_index) {
    size_t digits = format_strategy_digits[digit_index];
    run_format_policy_probe_case(
      report,
      digits,
      83U,
      "current-default",
      "current-scratch-format",
      "decimal-format-policy-current-default",
      "mpz_get_str-product-baseline",
      0,
      0,
      0,
      format_policy_current_default);
    run_format_policy_probe_case(
      report,
      digits,
      89U,
      "direct-ge4096-leaf8",
      "decimal-dc-direct-writer",
      "decimal-format-policy-direct-ge4096-leaf8",
      "mpn_dc_get_str-output-buffer",
      4096,
      0,
      8,
      format_policy_direct);
    run_format_policy_probe_case(
      report,
      digits,
      97U,
      "direct-ge8192-leaf16",
      "decimal-dc-direct-writer",
      "decimal-format-policy-direct-ge8192-leaf16",
      "mpn_dc_get_str-output-buffer",
      8192,
      0,
      16,
      format_policy_direct);
    run_format_policy_probe_case(
      report,
      digits,
      103U,
      "static-ge4096-l16",
      "dc-static-direct",
      "decimal-format-policy-static-direct",
      "static-powtab+buffer",
      4096,
      0,
      16,
      format_policy_static_direct);
    run_format_policy_probe_case(
      report,
      digits,
      107U,
      "static-ge8192-l8",
      "dc-static-direct",
      "decimal-format-policy-static-direct",
      "static-powtab+buffer",
      8192,
      0,
      8,
      format_policy_static_direct);
    run_format_policy_probe_case(
      report,
      digits,
      149U,
      "workspace-ge4096-leaf16",
      "decimal-dc-direct-workspace",
      "decimal-format-policy-workspace",
      "mpn_dc_get_str-divisor-context",
      4096,
      0,
      16,
      format_policy_workspace);
    run_format_policy_probe_case(
      report,
      digits,
      151U,
      "preinv-ge4096-leaf8",
      "decimal-dc-direct-preinv-qhat",
      "decimal-format-policy-preinv-qhat",
      "mpn_dc_get_str-preinverted-qhat",
      4096,
      0,
      8,
      format_policy_preinv_qhat);
    run_format_policy_probe_case(
      report,
      digits,
      157U,
      "preinv-ge8192-leaf16",
      "decimal-dc-direct-preinv-qhat",
      "decimal-format-policy-preinv-qhat",
      "mpn_dc_get_str-preinverted-qhat",
      8192,
      0,
      16,
      format_policy_preinv_qhat);
    run_format_policy_probe_case(
      report,
      digits,
      163U,
      "preinv-ge16384-leaf16",
      "decimal-dc-direct-preinv-qhat",
      "decimal-format-policy-preinv-qhat",
      "mpn_dc_get_str-preinverted-qhat",
      16384,
      0,
      16,
      format_policy_preinv_qhat);
    run_format_policy_probe_case(
      report,
      digits,
      191U,
      "preinv10e19-window768-1000",
      "decimal-divide-1e19-preinv",
      "decimal-format-policy-divide-1e19-preinv",
      "mpn_sb_get_str-preinverted-divrem-1",
      768,
      1000,
      0,
      format_policy_divide_1e19_preinv);
    run_format_policy_probe_case(
      report,
      digits,
      193U,
      "preinv10e19-pairs-window768-1000",
      "decimal-divide-1e19-preinv-pair-writer",
      "decimal-format-policy-divide-1e19-preinv-pairs",
      "mpn_sb_get_str-preinverted-divrem-1+digit-emission",
      768,
      1000,
      0,
      format_policy_divide_1e19_preinv_pairs);
  }

  run_format_policy_safety_case(
    report,
    113U,
    "direct-ge4096-leaf8",
    "decimal-dc-direct-writer",
    3072,
    4096,
    4096,
    0,
    8,
    format_policy_direct);
  run_format_policy_safety_case(
    report,
    127U,
    "direct-ge8192-leaf16",
    "decimal-dc-direct-writer",
    6144,
    8192,
    8192,
    0,
    16,
    format_policy_direct);
  run_format_policy_safety_case(
    report,
    131U,
    "static-ge4096-l16",
    "dc-static-direct",
    3072,
    4096,
    4096,
    0,
    16,
    format_policy_static_direct);
  run_format_policy_safety_case(
    report,
    137U,
    "static-ge8192-l8",
    "dc-static-direct",
    6144,
    8192,
    8192,
    0,
    8,
    format_policy_static_direct);
  run_format_policy_safety_case(
    report,
    167U,
    "workspace-ge4096-leaf16",
    "decimal-dc-direct-workspace",
    3072,
    4096,
    4096,
    0,
    16,
    format_policy_workspace);
  run_format_policy_safety_case(
    report,
    173U,
    "preinv-ge4096-leaf8",
    "decimal-dc-direct-preinv-qhat",
    3072,
    4096,
    4096,
    0,
    8,
    format_policy_preinv_qhat);
  run_format_policy_safety_case(
    report,
    179U,
    "preinv-ge8192-leaf16",
    "decimal-dc-direct-preinv-qhat",
    6144,
    8192,
    8192,
    0,
    16,
    format_policy_preinv_qhat);
  run_format_policy_safety_case(
    report,
    181U,
    "preinv-ge16384-leaf16",
    "decimal-dc-direct-preinv-qhat",
    12288,
    16384,
    16384,
    0,
    16,
    format_policy_preinv_qhat);
  run_format_policy_safety_case(
    report,
    197U,
    "preinv10e19-window768-1000",
    "decimal-divide-1e19-preinv",
    768,
    1000,
    768,
    1000,
    0,
    format_policy_divide_1e19_preinv);
  run_format_policy_safety_case(
    report,
    199U,
    "preinv10e19-pairs-window768-1000",
    "decimal-divide-1e19-preinv-pair-writer",
    768,
    1000,
    768,
    1000,
    0,
    format_policy_divide_1e19_preinv_pairs);

  run_format_policy_window_endpoint_probe_cases(
    report,
    211U,
    "preinv10e19-window768-896",
    "decimal-divide-1e19-preinv",
    "decimal-format-policy-divide-1e19-preinv",
    "mpn_sb_get_str-preinverted-divrem-1",
    768,
    896,
    format_policy_divide_1e19_preinv);
  run_format_policy_window_endpoint_probe_cases(
    report,
    223U,
    "preinv10e19-pairs-window768-896",
    "decimal-divide-1e19-preinv-pair-writer",
    "decimal-format-policy-divide-1e19-preinv-pairs",
    "mpn_sb_get_str-preinverted-divrem-1+digit-emission",
    768,
    896,
    format_policy_divide_1e19_preinv_pairs);
  run_format_policy_window_endpoint_probe_cases(
    report,
    227U,
    "preinv10e19-window768-960",
    "decimal-divide-1e19-preinv",
    "decimal-format-policy-divide-1e19-preinv",
    "mpn_sb_get_str-preinverted-divrem-1",
    768,
    960,
    format_policy_divide_1e19_preinv);
  run_format_policy_window_endpoint_probe_cases(
    report,
    229U,
    "preinv10e19-pairs-window768-960",
    "decimal-divide-1e19-preinv-pair-writer",
    "decimal-format-policy-divide-1e19-preinv-pairs",
    "mpn_sb_get_str-preinverted-divrem-1+digit-emission",
    768,
    960,
    format_policy_divide_1e19_preinv_pairs);
  run_format_policy_window_endpoint_probe_cases(
    report,
    233U,
    "preinv10e19-window896-1000",
    "decimal-divide-1e19-preinv",
    "decimal-format-policy-divide-1e19-preinv",
    "mpn_sb_get_str-preinverted-divrem-1",
    896,
    1000,
    format_policy_divide_1e19_preinv);
  run_format_policy_window_endpoint_probe_cases(
    report,
    239U,
    "preinv10e19-pairs-window896-1000",
    "decimal-divide-1e19-preinv-pair-writer",
    "decimal-format-policy-divide-1e19-preinv-pairs",
    "mpn_sb_get_str-preinverted-divrem-1+digit-emission",
    896,
    1000,
    format_policy_divide_1e19_preinv_pairs);

  run_format_policy_safety_case(
    report,
    241U,
    "preinv10e19-window768-896",
    "decimal-divide-1e19-preinv",
    768,
    896,
    768,
    896,
    0,
    format_policy_divide_1e19_preinv);
  run_format_policy_safety_case(
    report,
    251U,
    "preinv10e19-pairs-window768-896",
    "decimal-divide-1e19-preinv-pair-writer",
    768,
    896,
    768,
    896,
    0,
    format_policy_divide_1e19_preinv_pairs);
  run_format_policy_safety_case(
    report,
    257U,
    "preinv10e19-window768-960",
    "decimal-divide-1e19-preinv",
    768,
    960,
    768,
    960,
    0,
    format_policy_divide_1e19_preinv);
  run_format_policy_safety_case(
    report,
    263U,
    "preinv10e19-pairs-window768-960",
    "decimal-divide-1e19-preinv-pair-writer",
    768,
    960,
    768,
    960,
    0,
    format_policy_divide_1e19_preinv_pairs);
  run_format_policy_safety_case(
    report,
    269U,
    "preinv10e19-window896-1000",
    "decimal-divide-1e19-preinv",
    896,
    1000,
    896,
    1000,
    0,
    format_policy_divide_1e19_preinv);
  run_format_policy_safety_case(
    report,
    271U,
    "preinv10e19-pairs-window896-1000",
    "decimal-divide-1e19-preinv-pair-writer",
    896,
    1000,
    896,
    1000,
    0,
    format_policy_divide_1e19_preinv_pairs);

  run_format_policy_deep_safety_case(
    report,
    273U,
    "deep-preinv10e19-window768-1000",
    "decimal-divide-1e19-preinv",
    768,
    1000,
    768,
    1000,
    0,
    format_policy_divide_1e19_preinv);
  run_format_policy_deep_safety_case(
    report,
    274U,
    "deep-preinv10e19-window768-896",
    "decimal-divide-1e19-preinv",
    768,
    896,
    768,
    896,
    0,
    format_policy_divide_1e19_preinv);
  run_format_policy_deep_safety_case(
    report,
    275U,
    "deep-preinv10e19-window768-960",
    "decimal-divide-1e19-preinv",
    768,
    960,
    768,
    960,
    0,
    format_policy_divide_1e19_preinv);
  run_format_policy_deep_safety_case(
    report,
    276U,
    "deep-preinv10e19-window896-1000",
    "decimal-divide-1e19-preinv",
    896,
    1000,
    896,
    1000,
    0,
    format_policy_divide_1e19_preinv);
  run_format_policy_deep_safety_case(
    report,
    277U,
    "deep-preinv10e19-pairs-window768-896",
    "decimal-divide-1e19-preinv-pair-writer",
    768,
    896,
    768,
    896,
    0,
    format_policy_divide_1e19_preinv_pairs);
  run_format_policy_deep_safety_case(
    report,
    281U,
    "deep-preinv10e19-pairs-window768-960",
    "decimal-divide-1e19-preinv-pair-writer",
    768,
    960,
    768,
    960,
    0,
    format_policy_divide_1e19_preinv_pairs);
  run_format_policy_deep_safety_case(
    report,
    283U,
    "deep-preinv10e19-pairs-window896-1000",
    "decimal-divide-1e19-preinv-pair-writer",
    896,
    1000,
    896,
    1000,
    0,
    format_policy_divide_1e19_preinv_pairs);

  for (size_t digit_index = 0; digit_index < sizeof(format_strategy_digits) / sizeof(format_strategy_digits[0]); ++digit_index) {
    size_t digits = format_strategy_digits[digit_index];
    run_mul_policy_probe_case(
      report,
      digits,
      "current-default",
      "current-scratch-mul",
      "mul-policy-current-default",
      "mpn_mul-product-baseline",
      0,
      0,
      0,
      1,
      mul_policy_current_default);
    run_mul_policy_probe_case(
      report,
      digits,
      "toom3-u4-ge8192-leaf48",
      "one-level-toom3+unroll4-leaf",
      "mul-policy-toom3-u4-ge8192-leaf48",
      "toom33-leaf-schedule",
      8192,
      48,
      1,
      XRAY_HAS_MUL_UNROLL4_POLICY_PROBES,
      mul_policy_toom3_unroll4);
    run_mul_policy_probe_case(
      report,
      digits,
      "toom3-u4-rec-ge16384-leaf64-depth2",
      "recursive-toom3+unroll4",
      "mul-policy-toom3-u4-rec-ge16384-leaf64-depth2",
      "toom33-recursive",
      16384,
      64,
      2,
      XRAY_HAS_MUL_UNROLL4_POLICY_PROBES,
      mul_policy_toom3_unroll4_recursive);
  }
  const size_t mul_policy_toom_leaf48_safety_digits[] = {4096, 8192, 16384};
  run_mul_policy_safety_case(
    report,
    "toom3-u4-ge8192-leaf48",
    "one-level-toom3+unroll4-leaf",
    "mul-policy-toom3-u4-ge8192-leaf48",
    "toom33-leaf-schedule",
    8192,
    48,
    1,
    XRAY_HAS_MUL_UNROLL4_POLICY_PROBES,
    mul_policy_toom3_unroll4,
    mul_policy_toom_leaf48_safety_digits,
    sizeof(mul_policy_toom_leaf48_safety_digits) / sizeof(mul_policy_toom_leaf48_safety_digits[0]));
  const size_t mul_policy_toom_rec_safety_digits[] = {8192, 16384};
  run_mul_policy_safety_case(
    report,
    "toom3-u4-rec-ge16384-leaf64-depth2",
    "recursive-toom3+unroll4",
    "mul-policy-toom3-u4-rec-ge16384-leaf64-depth2",
    "toom33-recursive",
    16384,
    64,
    2,
    XRAY_HAS_MUL_UNROLL4_POLICY_PROBES,
    mul_policy_toom3_unroll4_recursive,
    mul_policy_toom_rec_safety_digits,
    sizeof(mul_policy_toom_rec_safety_digits) / sizeof(mul_policy_toom_rec_safety_digits[0]));

  const size_t digits[] = {512, 1000, 2048, 4096, 8192, 16384};
  const size_t thresholds[] = {16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 256};
  for (size_t digit_index = 0; digit_index < sizeof(digits) / sizeof(digits[0]); ++digit_index) {
    for (size_t threshold_index = 0; threshold_index < sizeof(thresholds) / sizeof(thresholds[0]); ++threshold_index) {
      run_mul_threshold_probe_case(report, digits[digit_index], thresholds[threshold_index]);
    }
  }

  const size_t middle_digits[] = {1000, 4096, 8192, 16384};
  const size_t middle_thresholds[] = {64, 96, 128};
  for (size_t digit_index = 0; digit_index < sizeof(middle_digits) / sizeof(middle_digits[0]); ++digit_index) {
    for (size_t threshold_index = 0; threshold_index < sizeof(middle_thresholds) / sizeof(middle_thresholds[0]); ++threshold_index) {
      run_mul_karatsuba_middle_probe_case(report, middle_digits[digit_index], middle_thresholds[threshold_index]);
    }
  }

  const size_t toom_digits[] = {4096, 8192, 16384};
  const size_t toom_leaf_thresholds[] = {32, 48, 64, 96};
  for (size_t digit_index = 0; digit_index < sizeof(toom_digits) / sizeof(toom_digits[0]); ++digit_index) {
    for (size_t threshold_index = 0; threshold_index < sizeof(toom_leaf_thresholds) / sizeof(toom_leaf_thresholds[0]); ++threshold_index) {
      run_mul_toom3_probe_case(report, toom_digits[digit_index], toom_leaf_thresholds[threshold_index]);
      run_mul_toom3_vs_scratch_probe_case(report, toom_digits[digit_index], toom_leaf_thresholds[threshold_index]);
    }
  }

#if XRAY_HAS_MSVC_BMI2_ADX_INTRINSICS
  const size_t toom_unroll_digits[] = {8192, 16384};
  for (size_t digit_index = 0; digit_index < sizeof(toom_unroll_digits) / sizeof(toom_unroll_digits[0]); ++digit_index) {
    for (size_t threshold_index = 0; threshold_index < sizeof(toom_leaf_thresholds) / sizeof(toom_leaf_thresholds[0]); ++threshold_index) {
      run_mul_toom3_unroll4_vs_scratch_probe_case(report, toom_unroll_digits[digit_index], toom_leaf_thresholds[threshold_index]);
      run_mul_toom3_unroll4_vs_gmp_probe_case(report, toom_unroll_digits[digit_index], toom_leaf_thresholds[threshold_index]);
    }
    const size_t toom_unroll_handoff_leaf_thresholds[] = {24, 40, 56, 80, 128};
    for (size_t threshold_index = 0; threshold_index < sizeof(toom_unroll_handoff_leaf_thresholds) / sizeof(toom_unroll_handoff_leaf_thresholds[0]); ++threshold_index) {
      run_mul_toom3_unroll4_vs_gmp_probe_case(report, toom_unroll_digits[digit_index], toom_unroll_handoff_leaf_thresholds[threshold_index]);
    }
    run_mul_toom3_unroll4_deep_vs_gmp_probe_case(report, toom_unroll_digits[digit_index], 64);
    run_mul_toom3_unroll4_deep_vs_gmp_probe_case(report, toom_unroll_digits[digit_index], 96);
  }
  run_mul_toom3_unroll4_recursive_vs_gmp_probe_case(report, 16384, 64, 2);
  run_mul_toom3_unroll4_recursive_vs_gmp_probe_case(report, 16384, 96, 2);
  run_mul_toom3_unroll4_recursive_deep_vs_gmp_probe_case(report, 16384, 64, 2);
  run_mul_toom3_unroll4_recursive_deep_vs_gmp_probe_case(report, 16384, 96, 2);

  const size_t unroll_digits[] = {40, 150, 1000, 4096, 8192, 16384};
  for (size_t digit_index = 0; digit_index < sizeof(unroll_digits) / sizeof(unroll_digits[0]); ++digit_index) {
    run_mul_unroll4_vs_scratch_probe_case(report, unroll_digits[digit_index], 64);
    run_mul_unroll4_vs_gmp_probe_case(report, unroll_digits[digit_index], 64);
  }
  const size_t unroll_deep_digits[] = {4096, 8192, 16384};
  for (size_t digit_index = 0; digit_index < sizeof(unroll_deep_digits) / sizeof(unroll_deep_digits[0]); ++digit_index) {
    run_mul_unroll4_deep_vs_gmp_probe_case(report, unroll_deep_digits[digit_index], 64);
  }
#endif
}

int xray_benchmark_run_with_callback(
  XrayBenchmarkReport *report,
  XrayBenchmarkResultCallback result_callback,
  void *user_data) {
  if (!report) return 0;
  memset(report, 0, sizeof(*report));
  report->result_callback = result_callback;
  report->result_callback_user_data = user_data;
  unsigned long started = xray_now_ms();
  xray_cpu_features_detect(&report->cpu);

  run_factor_case(report, "toy semiprime 10403", "10403", "solved", 1000);
  run_factor_case(report, "rho semiprime 8051", "8051", "solved", 1000);
  run_factor_case(report, "prime power 3^7", "2187", "solved", 1000);
  run_factor_case(report, "carmichael 561", "561", "solved", 1000);
  run_factor_case(report, "41 digit Fermat semiprime", "10000000000000000111000000000000000041769", "solved", 1000);
  run_factor_case(report, "RSA-260 unresolved guard", "22112825529529666435281085255026230927612089502470015394413748319128822941402001986512729726569746599085900330031400051170742204560859276357953757185954298838958709229238491006703034124620545784566413664540684214361293017694020846391065875914794251435144458199", "unsolved", 100);
  run_cyclo_case(report, "Phi_3(10)", 3, "10", "111");
  run_cyclo_case(report, "Phi_5(2)", 5, "2", "31");
  run_cyclo_case(report, "Phi_8(2)", 8, "2", "17");
  run_kernel_probes(report);
  run_scratch_bigint_gates(report);

  report->elapsed_ms = xray_now_ms() - started;
  report->result_callback = NULL;
  report->result_callback_user_data = NULL;
  return 1;
}

int xray_benchmark_run(XrayBenchmarkReport *report) {
  return xray_benchmark_run_with_callback(report, NULL, NULL);
}

void xray_benchmark_report_clear(XrayBenchmarkReport *report) {
  if (!report) return;
  free(report->results);
  memset(report, 0, sizeof(*report));
}
