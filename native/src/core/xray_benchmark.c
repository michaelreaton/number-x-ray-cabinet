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

static void append_result(XrayBenchmarkReport *report, const XrayBenchmarkResult *result) {
  XrayBenchmarkResult *next = (XrayBenchmarkResult *)realloc(report->results, sizeof(XrayBenchmarkResult) * (report->result_count + 1));
  if (!next) return;
  report->results = next;
  report->results[report->result_count++] = *result;
  if (result->passed) report->passed_count++;
  if (strcmp(result->category, "scratch-vs-gmp") == 0) {
    report->scratch_count++;
    if (strcmp(result->adoption, "allowed") == 0) report->replacement_ready_count++;
    else if (strcmp(result->adoption, "oracle-only") == 0) report->oracle_only_count++;
    else report->blocked_count++;
  }
}

const char *xray_scratch_adoption_for_result(const XrayBenchmarkResult *result) {
  if (!result || !result->parity_verified) return "blocked-output-mismatch";
  double limit = result->max_allowed_speed_ratio > 0.0 ? result->max_allowed_speed_ratio : 1.0;
  if (result->speed_ratio <= limit) {
    size_t required = result->sample_count < XRAY_SCRATCH_REQUIRED_STABLE_SAMPLES ?
      result->sample_count :
      XRAY_SCRATCH_REQUIRED_STABLE_SAMPLES;
    if (required == 0 || result->stable_sample_count >= required) return "allowed";
  }
  return "oracle-only";
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
    result.speed_ratio <= result.max_allowed_speed_ratio &&
    result.stable_sample_count >= required_stable;
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
  snprintf(result.name, sizeof(result.name), "kernel square versus self-mul %zu digits", digits);
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
    result.speed_ratio <= result.max_allowed_speed_ratio &&
    result.stable_sample_count >= required_stable;
  snprintf(result.adoption, sizeof(result.adoption), "%s",
    !parity ? "blocked-output-mismatch" : (result.replacement_ready ? "promote-candidate" : "observe-only"));
  snprintf(result.status, sizeof(result.status), "%s",
    !parity ? "mismatch" : (result.replacement_ready ? "candidate-faster" : (result.speed_ratio < 1.0 ? "candidate-no-margin" : "baseline-faster")));
  result.passed = parity;
  result.elapsed_ms = (unsigned long)((result.scratch_us + result.gmp_us + 999ULL) / 1000ULL);
  snprintf(result.detail, sizeof(result.detail),
    "op=square-vs-mul digits=%zu routeCandidate=unrouted operandFamilies=1 samples=%zu stablePairs=%zu/%zu squareUs=%llu currentMulUs=%llu ratio=%.3f worstPairRatio=%.3f ratioMethod=paired-median max=%.2f candidate=specialized-square baseline=current-scratch-self-mul featureGate=square-basecase-probe gmpClue=sqr_basecase adoption=%s",
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
    result.speed_ratio <= result.max_allowed_speed_ratio &&
    result.stable_sample_count >= required_stable;
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
    result.speed_ratio <= result.max_allowed_speed_ratio &&
    result.stable_sample_count >= required_stable;
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
    result.speed_ratio <= result.max_allowed_speed_ratio &&
    result.stable_sample_count >= required_stable;
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
    result.speed_ratio <= result.max_allowed_speed_ratio &&
    result.stable_sample_count >= required_stable;
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
    result.speed_ratio <= result.max_allowed_speed_ratio &&
    result.stable_sample_count >= required_stable;
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
    result.speed_ratio <= result.max_allowed_speed_ratio &&
    result.stable_sample_count >= required_stable;
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
    result.speed_ratio <= result.max_allowed_speed_ratio &&
    result.stable_sample_count >= required_stable;
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
    result.speed_ratio <= result.max_allowed_speed_ratio &&
    result.stable_sample_count >= required_stable;
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
    result.speed_ratio <= result.max_allowed_speed_ratio &&
    result.stable_sample_count >= required_stable;
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
    result.speed_ratio <= result.max_allowed_speed_ratio &&
    result.stable_sample_count >= required_stable;
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
    baseline_is_gmp ? "mpz_mul" : "current-scratch-self-mul",
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
  run_scratch_mul_case(report, 16384);
  run_scratch_square_case(report, 16384);
  run_square_vs_mul_probe_case(report, 16384);
  const size_t square_probe_sizes[] = {1000, 4096, 8192, 16384};
  for (size_t index = 0; index < sizeof(square_probe_sizes) / sizeof(square_probe_sizes[0]); ++index) {
    run_square_karatsuba_probe_case(report, square_probe_sizes[index], 64, 0);
    run_square_karatsuba_probe_case(report, square_probe_sizes[index], 64, 1);
  }
  const size_t square_scout_sizes[] = {4096, 8192, 16384};
  const size_t square_scout_thresholds[] = {32, 96, 128};
  for (size_t size_index = 0; size_index < sizeof(square_scout_sizes) / sizeof(square_scout_sizes[0]); ++size_index) {
    for (size_t threshold_index = 0; threshold_index < sizeof(square_scout_thresholds) / sizeof(square_scout_thresholds[0]); ++threshold_index) {
      run_square_karatsuba_probe_case(report, square_scout_sizes[size_index], square_scout_thresholds[threshold_index], 0);
      run_square_karatsuba_probe_case(report, square_scout_sizes[size_index], square_scout_thresholds[threshold_index], 1);
    }
  }
}

static void run_kernel_probes(XrayBenchmarkReport *report) {
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

  const size_t format_thresholds[] = {48, 64};
  for (size_t threshold_index = 0; threshold_index < sizeof(format_thresholds) / sizeof(format_thresholds[0]); ++threshold_index) {
    run_format_threshold_probe_case(report, 1000, format_thresholds[threshold_index]);
  }

  const size_t digits[] = {1000, 4096, 8192, 16384};
  const size_t thresholds[] = {32, 48, 64, 96, 128};
  for (size_t digit_index = 0; digit_index < sizeof(digits) / sizeof(digits[0]); ++digit_index) {
    for (size_t threshold_index = 0; threshold_index < sizeof(thresholds) / sizeof(thresholds[0]); ++threshold_index) {
      run_mul_threshold_probe_case(report, digits[digit_index], thresholds[threshold_index]);
    }
  }

  const size_t toom_digits[] = {4096, 8192, 16384};
  const size_t toom_leaf_thresholds[] = {32, 64};
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
    const size_t toom_unroll_handoff_leaf_thresholds[] = {24, 48, 96};
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

int xray_benchmark_run(XrayBenchmarkReport *report) {
  if (!report) return 0;
  memset(report, 0, sizeof(*report));
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
  return 1;
}

void xray_benchmark_report_clear(XrayBenchmarkReport *report) {
  if (!report) return;
  free(report->results);
  memset(report, 0, sizeof(*report));
}
