#include "xray_workbench.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_MSC_VER) && defined(_M_X64)
#include <intrin.h>
#define XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS 1
#elif defined(__SIZEOF_INT128__)
#define XRAY_BIGINT_HAS_UINT128 1
#else
#error "The 64-bit scratch bigint core requires __uint128 or MSVC x64 128-bit intrinsics."
#endif

#define XRAY_BIGINT_WORD_BITS 64U
#define XRAY_BIGINT_DECIMAL_CHUNK_BASE 1000000000U
#define XRAY_BIGINT_DECIMAL_CHUNK_DIGITS 9U
#define XRAY_BIGINT_DECIMAL_CHUNK_2P64_QUOTIENT UINT64_C(18446744073)
#define XRAY_BIGINT_DECIMAL_CHUNK_2P64_REMAINDER 709551616U
#define XRAY_BIGINT_DECIMAL_WIDE_CHUNK_BASE UINT64_C(10000000000000000000)
#define XRAY_BIGINT_DECIMAL_WIDE_CHUNK_DIGITS 19U
/* UINT64_MAX / 1e9, matching reciprocal_u32 for the decimal chunk divisor. */
#define XRAY_BIGINT_DECIMAL_CHUNK_RECIPROCAL UINT64_C(18446744073)
#define XRAY_BIGINT_DECIMAL_HORNER_MIN_LIMBS 48U
#define XRAY_BIGINT_DECIMAL_PAIR_WRITER_SMALL_MAX_LIMBS 8U
#define XRAY_BIGINT_DECIMAL_PAIR_WRITER_HORNER_MAX_LIMBS 54U
#define XRAY_BIGINT_DECIMAL_DC_MIN_WIDE_CHUNKS 216U
#define XRAY_BIGINT_DECIMAL_DC_LEAF_CHUNKS 8U
#define XRAY_BIGINT_PARSE_CHUNK_DIGITS 19U
#define XRAY_BIGINT_KARATSUBA_THRESHOLD 64U
#define XRAY_BIGINT_UNROLL4_ROUTE_MIN_LIMBS 8U
#define XRAY_BIGINT_UNROLL4_ROUTE_MAX_LIMBS 512U
#define XRAY_BIGINT_SQUARE_SELF_MUL_MAX_LIMBS 8U
#define XRAY_BIGINT_FERMAT_65537 65537U

static const uint64_t parse_decimal_powers[] = {
  UINT64_C(1),
  UINT64_C(10),
  UINT64_C(100),
  UINT64_C(1000),
  UINT64_C(10000),
  UINT64_C(100000),
  UINT64_C(1000000),
  UINT64_C(10000000),
  UINT64_C(100000000),
  UINT64_C(1000000000),
  UINT64_C(10000000000),
  UINT64_C(100000000000),
  UINT64_C(1000000000000),
  UINT64_C(10000000000000),
  UINT64_C(100000000000000),
  UINT64_C(1000000000000000),
  UINT64_C(10000000000000000),
  UINT64_C(100000000000000000),
  UINT64_C(1000000000000000000),
  UINT64_C(10000000000000000000)
};

static const char decimal_digit_pairs[] =
  "00010203040506070809"
  "10111213141516171819"
  "20212223242526272829"
  "30313233343536373839"
  "40414243444546474849"
  "50515253545556575859"
  "60616263646566676869"
  "70717273747576777879"
  "80818283848586878889"
  "90919293949596979899";

static const uint64_t decimal_dc_static_pow2_0[] = {
  UINT64_C(10000000000000000000)
};

static const uint64_t decimal_dc_static_pow2_1[] = {
  UINT64_C(687399551400673280), UINT64_C(5421010862427522170)
};

static const uint64_t decimal_dc_static_pow2_2[] = {
  UINT64_C(0), UINT64_C(8607968719199866880), UINT64_C(532749306367912313), UINT64_C(1593091911132452277)
};

static const uint64_t decimal_dc_static_pow2_3[] = {
  UINT64_C(0), UINT64_C(0), UINT64_C(15252863918154973184), UINT64_C(4477131725245556545),
  UINT64_C(6853971483050138908), UINT64_C(15193086134719162827), UINT64_C(11744654113764246714), UINT64_C(137582102682973977)
};

static const uint64_t decimal_dc_static_pow2_4[] = {
  UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(0),
  UINT64_C(18104751977006104576), UINT64_C(7022382078611961980), UINT64_C(5818713298520399752), UINT64_C(3845577022746030748),
  UINT64_C(12139943004249278607), UINT64_C(5889242339408380438), UINT64_C(14552017514495643209), UINT64_C(4968216578135424189),
  UINT64_C(15773629750718909556), UINT64_C(9342075265934726546), UINT64_C(1149859480163044520), UINT64_C(1026134200324594)
};

static const uint64_t decimal_dc_static_pow2_5[] = {
  UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(0),
  UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(0),
  UINT64_C(0), UINT64_C(14812487063030464512), UINT64_C(15678693317147271784), UINT64_C(13223892582731984624),
  UINT64_C(17465812138978755258), UINT64_C(7664081700110503653), UINT64_C(7867086747378126271), UINT64_C(6694233273289526891),
  UINT64_C(979072618450083183), UINT64_C(3181249480858999896), UINT64_C(1618460960086060797), UINT64_C(9729852571323393537),
  UINT64_C(5512821131327516378), UINT64_C(14040226153079509991), UINT64_C(5675458110915889831), UINT64_C(14808706736072332751),
  UINT64_C(16738973592517505687), UINT64_C(3211101337243187422), UINT64_C(16112068347911025940), UINT64_C(7727566944689383997),
  UINT64_C(16383823567047475423), UINT64_C(9390551701925100160), UINT64_C(10351412666324596833), UINT64_C(57080609611)
};

static const uint64_t decimal_dc_static_pow2_6[] = {
  UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(0),
  UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(0),
  UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(0),
  UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(0),
  UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(319005509880073473),
  UINT64_C(1806551507373502735), UINT64_C(11302742870345376530), UINT64_C(13258527722524180429), UINT64_C(5446412568905165168),
  UINT64_C(12543982081940419832), UINT64_C(15403731642616081653), UINT64_C(4737951089047661062), UINT64_C(3794326284714172484),
  UINT64_C(2300983730994112517), UINT64_C(13011946269911379846), UINT64_C(3105983672841008074), UINT64_C(3271341497136365229),
  UINT64_C(9317620278607948968), UINT64_C(6461467202444947841), UINT64_C(12956296021901158792), UINT64_C(5590221285761126278),
  UINT64_C(5901497884083299244), UINT64_C(15598774421181943027), UINT64_C(12767720238382953525), UINT64_C(985519523759711351),
  UINT64_C(9736924633090785807), UINT64_C(5815317455127115990), UINT64_C(10640367891977486574), UINT64_C(3341215565587611947),
  UINT64_C(3379075655803910730), UINT64_C(7729393315787486022), UINT64_C(10660439658308642124), UINT64_C(2736012397475497809),
  UINT64_C(15080284521096067759), UINT64_C(15230264543647898301), UINT64_C(16946947390220236831), UINT64_C(18357957862637485458),
  UINT64_C(8751981732964013983), UINT64_C(7264578455454564054), UINT64_C(16667145575142581624), UINT64_C(16393813431037489643),
  UINT64_C(14639777417809856269), UINT64_C(17573061233865061950), UINT64_C(17498971390803385829), UINT64_C(16526120241226747429),
  UINT64_C(14725285770136587064), UINT64_C(7748610182331927902), UINT64_C(11569036654566192642), UINT64_C(176)
};

static XrayScratchBigInt decimal_dc_static_ladder_values[] = {
  {(uint64_t *)decimal_dc_static_pow2_0, 1U, 1U},
  {(uint64_t *)decimal_dc_static_pow2_1, 2U, 2U},
  {(uint64_t *)decimal_dc_static_pow2_2, 4U, 4U},
  {(uint64_t *)decimal_dc_static_pow2_3, 8U, 8U},
  {(uint64_t *)decimal_dc_static_pow2_4, 16U, 16U},
  {(uint64_t *)decimal_dc_static_pow2_5, 32U, 32U},
  {(uint64_t *)decimal_dc_static_pow2_6, 64U, 64U},
};

XrayBigIntRouteConfig xray_bigint_route_config(void) {
  XrayBigIntRouteConfig config;
  config.word_bits = XRAY_BIGINT_WORD_BITS;
  config.karatsuba_threshold_limbs = XRAY_BIGINT_KARATSUBA_THRESHOLD;
  config.decimal_horner_min_limbs = XRAY_BIGINT_DECIMAL_HORNER_MIN_LIMBS;
  config.mul_unroll4_route_min_limbs = XRAY_BIGINT_UNROLL4_ROUTE_MIN_LIMBS;
  config.mul_unroll4_route_max_limbs = XRAY_BIGINT_UNROLL4_ROUTE_MAX_LIMBS;
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  config.mul_unroll4_route_enabled = 1;
  config.msvc_uint128_helpers = 1;
#else
  config.mul_unroll4_route_enabled = 0;
  config.msvc_uint128_helpers = 0;
#endif
  return config;
}

void xray_bigint_init(XrayScratchBigInt *value) {
  if (!value) return;
  value->limbs = NULL;
  value->count = 0;
  value->capacity = 0;
}

void xray_bigint_clear(XrayScratchBigInt *value) {
  if (!value) return;
  free(value->limbs);
  value->limbs = NULL;
  value->count = 0;
  value->capacity = 0;
}

static int reserve_limbs(XrayScratchBigInt *value, size_t capacity) {
  if (value->capacity >= capacity) return 1;
  size_t next_capacity = value->capacity ? value->capacity * 2 : 4;
  while (next_capacity < capacity) next_capacity *= 2;
  uint64_t *next = (uint64_t *)realloc(value->limbs, sizeof(uint64_t) * next_capacity);
  if (!next) return 0;
  value->limbs = next;
  value->capacity = next_capacity;
  return 1;
}

static void normalize(XrayScratchBigInt *value) {
  while (value->count > 0 && value->limbs[value->count - 1] == 0) value->count--;
}

static int set_u32(XrayScratchBigInt *value, uint32_t small) {
  if (!reserve_limbs(value, 1)) return 0;
  value->limbs[0] = (uint64_t)small;
  value->count = small ? 1 : 0;
  return 1;
}

static int set_u64(XrayScratchBigInt *value, uint64_t small) {
  if (!reserve_limbs(value, 1)) return 0;
  value->limbs[0] = small;
  value->count = small ? 1 : 0;
  return 1;
}

static int is_ascii_space(unsigned char ch) {
  return ch == ' ' || (ch >= '\t' && ch <= '\r');
}

int xray_bigint_is_zero(const XrayScratchBigInt *value) {
  return !value || value->count == 0;
}

int xray_bigint_copy(XrayScratchBigInt *out, const XrayScratchBigInt *value) {
  if (!out || !value) return 0;
  if (out == value) return 1;
  if (value->count == 0) {
    out->count = 0;
    return 1;
  }
  if (!reserve_limbs(out, value->count ? value->count : 1)) return 0;
  if (value->count) memcpy(out->limbs, value->limbs, sizeof(uint64_t) * value->count);
  out->count = value->count;
  return 1;
}

static uint64_t mul_add_small_word(uint64_t word, uint64_t multiplier, uint64_t carry, uint64_t *out) {
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  unsigned __int64 high = 0;
  unsigned __int64 low = _umul128(word, multiplier, &high);
  unsigned __int64 sum = low + carry;
  if (sum < low) high++;
  *out = (uint64_t)sum;
  return (uint64_t)high;
#else
  __uint128_t product = (__uint128_t)word * (__uint128_t)multiplier + (__uint128_t)carry;
  *out = (uint64_t)product;
  return (uint64_t)(product >> XRAY_BIGINT_WORD_BITS);
#endif
}

static uint64_t mul_add_word(uint64_t existing, uint64_t left, uint64_t right, uint64_t carry, uint64_t *out) {
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  unsigned __int64 high = 0;
  unsigned __int64 low = _umul128(left, right, &high);
  unsigned __int64 sum = 0;
  unsigned char carry_out = _addcarry_u64(0, low, existing, &sum);
  high += carry_out;
  carry_out = _addcarry_u64(0, sum, carry, &sum);
  high += carry_out;
  *out = (uint64_t)sum;
  return (uint64_t)high;
#else
  __uint128_t product = (__uint128_t)left * (__uint128_t)right + (__uint128_t)existing + (__uint128_t)carry;
  *out = (uint64_t)product;
  return (uint64_t)(product >> XRAY_BIGINT_WORD_BITS);
#endif
}

#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
static uint64_t mul_add_word_unroll4_row(uint64_t *target, const uint64_t *right, uint64_t left, size_t count) {
  unsigned __int64 carry = 0;
  size_t index = 0;
#define XRAY_MULADD_UNROLL4_STEP(offset) do { \
    unsigned __int64 high = 0; \
    unsigned __int64 low = _umul128(left, right[index + (offset)], &high); \
    unsigned __int64 sum = 0; \
    unsigned char carry_out = _addcarry_u64(0, low, target[index + (offset)], &sum); \
    high += carry_out; \
    carry_out = _addcarry_u64(0, sum, carry, &sum); \
    high += carry_out; \
    target[index + (offset)] = (uint64_t)sum; \
    carry = high; \
  } while (0)
  for (; index + 4U <= count; index += 4U) {
    XRAY_MULADD_UNROLL4_STEP(0U);
    XRAY_MULADD_UNROLL4_STEP(1U);
    XRAY_MULADD_UNROLL4_STEP(2U);
    XRAY_MULADD_UNROLL4_STEP(3U);
  }
  for (; index < count; ++index) {
    XRAY_MULADD_UNROLL4_STEP(0U);
  }
#undef XRAY_MULADD_UNROLL4_STEP
  return (uint64_t)carry;
}
#endif

static uint64_t mul_high_u64(uint64_t left, uint64_t right) {
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  unsigned __int64 high = 0;
  (void)_umul128(left, right, &high);
  return (uint64_t)high;
#else
  return (uint64_t)(((__uint128_t)left * (__uint128_t)right) >> 64);
#endif
}

static unsigned int clz_u64(uint64_t value) {
  if (value == 0) return 64U;
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  unsigned long index = 0;
  _BitScanReverse64(&index, value);
  return 63U - (unsigned int)index;
#elif defined(__GNUC__) || defined(__clang__)
  return (unsigned int)__builtin_clzll(value);
#else
  unsigned int count = 0;
  uint64_t bit = UINT64_C(1) << 63U;
  while ((value & bit) == 0) {
    count++;
    bit >>= 1U;
  }
  return count;
#endif
}

static unsigned char add_with_carry_u64(uint64_t left, uint64_t right, unsigned char carry, uint64_t *out) {
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  unsigned __int64 word = 0;
  unsigned char next = _addcarry_u64(carry, left, right, &word);
  *out = (uint64_t)word;
  return next;
#else
  uint64_t sum = left + right;
  uint64_t carry_from_sum = sum < left;
  uint64_t with_carry = sum + (uint64_t)carry;
  *out = with_carry;
  return (unsigned char)(carry_from_sum || (with_carry < sum));
#endif
}

static unsigned char sub_with_borrow_u64(uint64_t left, uint64_t right, unsigned char borrow, uint64_t *out) {
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  unsigned __int64 word = 0;
  unsigned char next = _subborrow_u64(borrow, left, right, &word);
  *out = (uint64_t)word;
  return next;
#else
  uint64_t subtrahend = right + (uint64_t)borrow;
  uint64_t borrow_from_add = subtrahend < right;
  *out = left - subtrahend;
  return (unsigned char)(borrow_from_add || (left < subtrahend));
#endif
}

static uint64_t reciprocal_u32(uint32_t divisor) {
  if (divisor == 1U) return 0;
  uint64_t reciprocal = UINT64_MAX / divisor;
  if (UINT64_MAX % divisor == (uint64_t)divisor - 1ULL) reciprocal++;
  return reciprocal;
}

static uint32_t divmod_half_u32(uint32_t high, uint32_t low, uint32_t divisor, uint64_t reciprocal, uint32_t *remainder) {
  if (divisor == 1U) {
    if (remainder) *remainder = 0;
    return low;
  }
  uint64_t numerator = ((uint64_t)high << 32U) | low;
  uint64_t quotient = mul_high_u64(numerator, reciprocal);
  uint64_t rem = numerator - quotient * divisor;
  while (rem >= divisor) {
    rem -= divisor;
    quotient++;
  }
  if (remainder) *remainder = (uint32_t)rem;
  return (uint32_t)quotient;
}

static uint64_t divmod_word_u32(uint32_t high, uint64_t low, uint32_t divisor, uint64_t reciprocal, int use_high_half, uint32_t *remainder) {
  uint32_t quotient_high = 0;
  uint32_t rem = high;
  if (use_high_half) {
    quotient_high = divmod_half_u32(high, (uint32_t)(low >> 32U), divisor, reciprocal, &rem);
  }
  uint32_t quotient_low = divmod_half_u32(rem, (uint32_t)low, divisor, reciprocal, &rem);
  if (remainder) *remainder = rem;
  return ((uint64_t)quotient_high << 32U) | quotient_low;
}

static uint64_t divmod_word_u32_direct(uint32_t high, uint64_t low, uint32_t divisor, uint32_t *remainder) {
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  unsigned __int64 rem = 0;
  unsigned __int64 quotient = _udiv128(
    (unsigned __int64)high,
    (unsigned __int64)low,
    (unsigned __int64)divisor,
    &rem);
  if (remainder) *remainder = (uint32_t)rem;
  return (uint64_t)quotient;
#else
  __uint128_t numerator = ((__uint128_t)high << XRAY_BIGINT_WORD_BITS) | (__uint128_t)low;
  if (remainder) *remainder = (uint32_t)(numerator % divisor);
  return (uint64_t)(numerator / divisor);
#endif
}

static uint64_t divmod_word_u64_direct(uint64_t high, uint64_t low, uint64_t divisor, uint64_t *remainder) {
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  unsigned __int64 rem = 0;
  unsigned __int64 quotient = _udiv128(
    (unsigned __int64)high,
    (unsigned __int64)low,
    (unsigned __int64)divisor,
    &rem);
  if (remainder) *remainder = (uint64_t)rem;
  return (uint64_t)quotient;
#else
  __uint128_t numerator = ((__uint128_t)high << XRAY_BIGINT_WORD_BITS) | (__uint128_t)low;
  if (remainder) *remainder = (uint64_t)(numerator % divisor);
  return (uint64_t)(numerator / divisor);
#endif
}

static uint32_t divmod_decimal_chunk_inplace(XrayScratchBigInt *value) {
  uint32_t remainder = 0;
  for (size_t remaining = value->count; remaining > 0; --remaining) {
    size_t index = remaining - 1;
    int use_high_half = index + 1 != value->count || (value->limbs[index] >> 32U) != 0;
    value->limbs[index] = divmod_word_u32(
      remainder,
      value->limbs[index],
      XRAY_BIGINT_DECIMAL_CHUNK_BASE,
      XRAY_BIGINT_DECIMAL_CHUNK_RECIPROCAL,
      use_high_half,
      &remainder);
  }
  normalize(value);
  return remainder;
}

static int reserve_decimal_chunks(uint32_t **chunks, size_t *capacity, size_t needed) {
  if (*capacity >= needed) return 1;
  if (needed > SIZE_MAX / sizeof(uint32_t)) return 0;
  size_t next_capacity = *capacity ? *capacity * 2U : 8U;
  while (next_capacity < needed) {
    if (next_capacity > SIZE_MAX / 2U) return 0;
    next_capacity *= 2U;
  }
  if (next_capacity > SIZE_MAX / sizeof(uint32_t)) return 0;
  uint32_t *next = (uint32_t *)realloc(*chunks, sizeof(uint32_t) * next_capacity);
  if (!next) return 0;
  *chunks = next;
  *capacity = next_capacity;
  return 1;
}

static size_t estimate_decimal_chunk_capacity(const XrayScratchBigInt *value) {
  if (!value || value->count == 0) return 1U;
  if (value->count > (SIZE_MAX - 8U) / 20U) return SIZE_MAX;
  return (value->count * 20U + 8U) / XRAY_BIGINT_DECIMAL_CHUNK_DIGITS;
}

static int append_decimal_chunk(uint32_t **chunks, size_t *count, size_t *capacity, uint32_t chunk) {
  if (!reserve_decimal_chunks(chunks, capacity, *count + 1U)) return 0;
  (*chunks)[(*count)++] = chunk;
  return 1;
}

static int reserve_decimal_wide_chunks(uint64_t **chunks, size_t *capacity, size_t needed) {
  if (*capacity >= needed) return 1;
  if (needed > SIZE_MAX / sizeof(uint64_t)) return 0;
  size_t next_capacity = *capacity ? *capacity * 2U : 8U;
  while (next_capacity < needed) {
    if (next_capacity > SIZE_MAX / 2U) return 0;
    next_capacity *= 2U;
  }
  if (next_capacity > SIZE_MAX / sizeof(uint64_t)) return 0;
  uint64_t *next = (uint64_t *)realloc(*chunks, sizeof(uint64_t) * next_capacity);
  if (!next) return 0;
  *chunks = next;
  *capacity = next_capacity;
  return 1;
}

static size_t estimate_decimal_wide_chunk_capacity(const XrayScratchBigInt *value) {
  if (!value || value->count == 0) return 1U;
  if (value->count > (SIZE_MAX - 8U) / 20U) return SIZE_MAX;
  return (value->count * 20U + 8U) / XRAY_BIGINT_DECIMAL_WIDE_CHUNK_DIGITS;
}

static int append_decimal_wide_chunk(uint64_t **chunks, size_t *count, size_t *capacity, uint64_t chunk) {
  if (!reserve_decimal_wide_chunks(chunks, capacity, *count + 1U)) return 0;
  (*chunks)[(*count)++] = chunk;
  return 1;
}

static int mul_add_small_inplace(XrayScratchBigInt *value, uint64_t multiplier, uint64_t addend);
static size_t write_u64_decimal(char *out, uint64_t value);
static void write_u64_decimal_padded19(char *out, uint64_t value);

static int decimal_chunks_from_limbs_horner(uint32_t **chunks_out, size_t *chunk_count_out, const XrayScratchBigInt *value) {
  uint32_t *chunks = NULL;
  size_t chunk_count = 0;
  size_t chunk_capacity = 0;
  if (!reserve_decimal_chunks(&chunks, &chunk_capacity, estimate_decimal_chunk_capacity(value))) return 0;
  for (size_t remaining = value->count; remaining > 0; --remaining) {
    uint64_t carry = value->limbs[remaining - 1U];
    for (size_t index = 0; index < chunk_count; ++index) {
      uint32_t remainder = 0;
      carry = divmod_word_u32(
        chunks[index],
        carry,
        XRAY_BIGINT_DECIMAL_CHUNK_BASE,
        XRAY_BIGINT_DECIMAL_CHUNK_RECIPROCAL,
        1,
        &remainder);
      chunks[index] = remainder;
    }
    while (carry) {
      uint32_t remainder = 0;
      carry = divmod_word_u32(
        0,
        carry,
        XRAY_BIGINT_DECIMAL_CHUNK_BASE,
        XRAY_BIGINT_DECIMAL_CHUNK_RECIPROCAL,
        (carry >> 32U) != 0,
        &remainder);
      if (!append_decimal_chunk(&chunks, &chunk_count, &chunk_capacity, remainder)) {
        free(chunks);
        return 0;
      }
    }
  }
  while (chunk_count > 0 && chunks[chunk_count - 1U] == 0) chunk_count--;
  *chunks_out = chunks;
  *chunk_count_out = chunk_count;
  return 1;
}

static uint64_t divmod_u64_decimal_chunk(uint64_t value, uint32_t *remainder) {
  return divmod_word_u32(
    0,
    value,
    XRAY_BIGINT_DECIMAL_CHUNK_BASE,
    XRAY_BIGINT_DECIMAL_CHUNK_RECIPROCAL,
    (value >> 32U) != 0,
    remainder);
}

static uint64_t divmod_u64_decimal_chunk_direct(uint64_t value, uint32_t *remainder) {
  return divmod_word_u32_direct(0, value, XRAY_BIGINT_DECIMAL_CHUNK_BASE, remainder);
}

static uint64_t divmod_folded_decimal_chunk(uint64_t low, int overflow, uint32_t *remainder) {
  if (!overflow) return divmod_u64_decimal_chunk(low, remainder);
  uint64_t adjusted = low + (uint64_t)XRAY_BIGINT_DECIMAL_CHUNK_2P64_REMAINDER;
  return XRAY_BIGINT_DECIMAL_CHUNK_2P64_QUOTIENT + divmod_u64_decimal_chunk(adjusted, remainder);
}

static uint64_t divmod_folded_decimal_chunk_direct(uint64_t low, int overflow, uint32_t *remainder) {
  if (!overflow) return divmod_u64_decimal_chunk_direct(low, remainder);
  uint64_t adjusted = low + (uint64_t)XRAY_BIGINT_DECIMAL_CHUNK_2P64_REMAINDER;
  return XRAY_BIGINT_DECIMAL_CHUNK_2P64_QUOTIENT + divmod_u64_decimal_chunk_direct(adjusted, remainder);
}

static int decimal_chunks_from_limbs_horner_folded_mode(
  uint32_t **chunks_out,
  size_t *chunk_count_out,
  const XrayScratchBigInt *value,
  int use_direct_divider) {
  uint32_t *chunks = NULL;
  size_t chunk_count = 0;
  size_t chunk_capacity = 0;
  if (!reserve_decimal_chunks(&chunks, &chunk_capacity, estimate_decimal_chunk_capacity(value))) return 0;
  for (size_t remaining = value->count; remaining > 0; --remaining) {
    uint64_t carry = value->limbs[remaining - 1U];
    for (size_t index = 0; index < chunk_count; ++index) {
      uint32_t chunk = chunks[index];
      uint64_t scaled = (uint64_t)chunk * (uint64_t)XRAY_BIGINT_DECIMAL_CHUNK_2P64_REMAINDER;
      uint64_t low = scaled + carry;
      uint32_t remainder = 0;
      uint64_t carry_delta = use_direct_divider ?
        divmod_folded_decimal_chunk_direct(low, low < scaled, &remainder) :
        divmod_folded_decimal_chunk(low, low < scaled, &remainder);
      chunks[index] = remainder;
      carry = (uint64_t)chunk * XRAY_BIGINT_DECIMAL_CHUNK_2P64_QUOTIENT + carry_delta;
    }
    while (carry) {
      uint32_t remainder = 0;
      carry = use_direct_divider ?
        divmod_u64_decimal_chunk_direct(carry, &remainder) :
        divmod_u64_decimal_chunk(carry, &remainder);
      if (!append_decimal_chunk(&chunks, &chunk_count, &chunk_capacity, remainder)) {
        free(chunks);
        return 0;
      }
    }
  }
  while (chunk_count > 0 && chunks[chunk_count - 1U] == 0) chunk_count--;
  *chunks_out = chunks;
  *chunk_count_out = chunk_count;
  return 1;
}

static int decimal_chunks_from_limbs_horner_folded(uint32_t **chunks_out, size_t *chunk_count_out, const XrayScratchBigInt *value) {
  return decimal_chunks_from_limbs_horner_folded_mode(chunks_out, chunk_count_out, value, 0);
}

static int decimal_chunks_from_limbs_horner_folded_direct(uint32_t **chunks_out, size_t *chunk_count_out, const XrayScratchBigInt *value) {
  return decimal_chunks_from_limbs_horner_folded_mode(chunks_out, chunk_count_out, value, 1);
}

static int decimal_wide_chunks_from_limbs_horner(uint64_t **chunks_out, size_t *chunk_count_out, const XrayScratchBigInt *value) {
  uint64_t *chunks = NULL;
  size_t chunk_count = 0;
  size_t chunk_capacity = 0;
  if (!reserve_decimal_wide_chunks(&chunks, &chunk_capacity, estimate_decimal_wide_chunk_capacity(value))) return 0;
  for (size_t remaining = value->count; remaining > 0; --remaining) {
    uint64_t carry = value->limbs[remaining - 1U];
    for (size_t index = 0; index < chunk_count; ++index) {
      uint64_t remainder = 0;
      carry = divmod_word_u64_direct(
        chunks[index],
        carry,
        XRAY_BIGINT_DECIMAL_WIDE_CHUNK_BASE,
        &remainder);
      chunks[index] = remainder;
    }
    while (carry) {
      uint64_t remainder = 0;
      carry = divmod_word_u64_direct(
        0,
        carry,
        XRAY_BIGINT_DECIMAL_WIDE_CHUNK_BASE,
        &remainder);
      if (!append_decimal_wide_chunk(&chunks, &chunk_count, &chunk_capacity, remainder)) {
        free(chunks);
        return 0;
      }
    }
  }
  while (chunk_count > 0 && chunks[chunk_count - 1U] == 0) chunk_count--;
  *chunks_out = chunks;
  *chunk_count_out = chunk_count;
  return 1;
}

static uint64_t divmod_decimal_wide_chunk_inplace(XrayScratchBigInt *value) {
  uint64_t remainder = 0;
  for (size_t remaining = value->count; remaining > 0; --remaining) {
    size_t index = remaining - 1;
    value->limbs[index] = divmod_word_u64_direct(
      remainder,
      value->limbs[index],
      XRAY_BIGINT_DECIMAL_WIDE_CHUNK_BASE,
      &remainder);
  }
  normalize(value);
  return remainder;
}

static int decimal_wide_chunks_from_limbs_divide(uint64_t **chunks_out, size_t *chunk_count_out, const XrayScratchBigInt *value) {
  XrayScratchBigInt copy;
  xray_bigint_init(&copy);
  uint64_t *chunks = NULL;
  size_t chunk_count = 0;
  size_t chunk_capacity = 0;
  int ok = xray_bigint_copy(&copy, value) &&
    reserve_decimal_wide_chunks(&chunks, &chunk_capacity, estimate_decimal_wide_chunk_capacity(value));
  while (ok && copy.count > 0) {
    ok = append_decimal_wide_chunk(
      &chunks,
      &chunk_count,
      &chunk_capacity,
      divmod_decimal_wide_chunk_inplace(&copy));
  }
  xray_bigint_clear(&copy);
  if (!ok) {
    free(chunks);
    return 0;
  }
  while (chunk_count > 0 && chunks[chunk_count - 1U] == 0) chunk_count--;
  *chunks_out = chunks;
  *chunk_count_out = chunk_count;
  return 1;
}

static int shift_left_bits_copy(XrayScratchBigInt *out, const XrayScratchBigInt *value, unsigned int shift) {
  if (!out || !value || shift >= XRAY_BIGINT_WORD_BITS) return 0;
  if (shift == 0) return xray_bigint_copy(out, value);
  if (value->count == 0) return set_u32(out, 0);
  if (!reserve_limbs(out, value->count + 1U)) return 0;
  uint64_t carry = 0;
  for (size_t index = 0; index < value->count; ++index) {
    uint64_t word = value->limbs[index];
    out->limbs[index] = (word << shift) | carry;
    carry = word >> (XRAY_BIGINT_WORD_BITS - shift);
  }
  out->count = value->count;
  if (carry) out->limbs[out->count++] = carry;
  normalize(out);
  return 1;
}

static int shift_right_bits_copy(XrayScratchBigInt *out, const XrayScratchBigInt *value, unsigned int shift) {
  if (!out || !value || shift >= XRAY_BIGINT_WORD_BITS) return 0;
  if (shift == 0) return xray_bigint_copy(out, value);
  if (value->count == 0) return set_u32(out, 0);
  if (!reserve_limbs(out, value->count)) return 0;
  uint64_t carry = 0;
  uint64_t low_mask = (UINT64_C(1) << shift) - 1U;
  for (size_t remaining = value->count; remaining > 0; --remaining) {
    size_t index = remaining - 1U;
    uint64_t word = value->limbs[index];
    out->limbs[index] = (word >> shift) | (carry << (XRAY_BIGINT_WORD_BITS - shift));
    carry = word & low_mask;
  }
  out->count = value->count;
  normalize(out);
  return 1;
}

static int divmod_bigint_u64_probe(
  XrayScratchBigInt *quotient,
  XrayScratchBigInt *remainder_out,
  const XrayScratchBigInt *value,
  uint64_t divisor) {
  if (!quotient || !remainder_out || !value || divisor == 0) return 0;
  if (value->count == 0) {
    quotient->count = 0;
    remainder_out->count = 0;
    return 1;
  }
  if (!reserve_limbs(quotient, value->count)) return 0;
  uint64_t remainder = 0;
  for (size_t remaining = value->count; remaining > 0; --remaining) {
    size_t index = remaining - 1U;
    quotient->limbs[index] = divmod_word_u64_direct(remainder, value->limbs[index], divisor, &remainder);
  }
  quotient->count = value->count;
  normalize(quotient);
  return set_u64(remainder_out, remainder);
}

static int product_gt_two_limb(uint64_t left, uint64_t right, uint64_t high, uint64_t low) {
  uint64_t product_low = 0;
  uint64_t product_high = mul_add_small_word(left, right, 0, &product_low);
  return product_high > high || (product_high == high && product_low > low);
}

static int divmod_bigint_normalized_probe(
  XrayScratchBigInt *quotient,
  XrayScratchBigInt *remainder,
  const XrayScratchBigInt *numerator,
  const XrayScratchBigInt *normalized_divisor,
  unsigned int shift) {
  size_t n = normalized_divisor->count;
  size_t m = numerator->count - n;
  XrayScratchBigInt normalized_numerator;
  XrayScratchBigInt remainder_slice;
  xray_bigint_init(&normalized_numerator);
  xray_bigint_init(&remainder_slice);

  int ok = shift_left_bits_copy(&normalized_numerator, numerator, shift) &&
    reserve_limbs(&normalized_numerator, n + m + 1U) &&
    reserve_limbs(quotient, m + 1U);
  if (ok) {
    while (normalized_numerator.count < n + m + 1U) {
      normalized_numerator.limbs[normalized_numerator.count++] = 0;
    }
    memset(quotient->limbs, 0, sizeof(uint64_t) * (m + 1U));
    quotient->count = m + 1U;

    for (size_t jj = m + 1U; jj > 0; --jj) {
      size_t j = jj - 1U;
      uint64_t qhat = 0;
      uint64_t rhat = 0;
      uint64_t ujn = normalized_numerator.limbs[j + n];
      uint64_t ujn1 = normalized_numerator.limbs[j + n - 1U];
      uint64_t vn1 = normalized_divisor->limbs[n - 1U];
      int rhat_overflow = 0;
      if (ujn == vn1) {
        qhat = UINT64_MAX;
        rhat = ujn1 + vn1;
        rhat_overflow = rhat < ujn1;
      } else {
        qhat = divmod_word_u64_direct(ujn, ujn1, vn1, &rhat);
      }

      if (n > 1U) {
        uint64_t vn2 = normalized_divisor->limbs[n - 2U];
        uint64_t ujn2 = normalized_numerator.limbs[j + n - 2U];
        while (!rhat_overflow && product_gt_two_limb(qhat, vn2, rhat, ujn2)) {
          qhat--;
          uint64_t old_rhat = rhat;
          rhat += vn1;
          rhat_overflow = rhat < old_rhat;
        }
      }

      uint64_t carry = 0;
      unsigned char borrow = 0;
      for (size_t index = 0; index < n; ++index) {
        uint64_t product_low = 0;
        carry = mul_add_small_word(normalized_divisor->limbs[index], qhat, carry, &product_low);
        borrow = sub_with_borrow_u64(
          normalized_numerator.limbs[j + index],
          product_low,
          borrow,
          &normalized_numerator.limbs[j + index]);
      }
      borrow = sub_with_borrow_u64(
        normalized_numerator.limbs[j + n],
        carry,
        borrow,
        &normalized_numerator.limbs[j + n]);

      if (borrow) {
        qhat--;
        unsigned char carry_back = 0;
        for (size_t index = 0; index < n; ++index) {
          carry_back = add_with_carry_u64(
            normalized_numerator.limbs[j + index],
            normalized_divisor->limbs[index],
            carry_back,
            &normalized_numerator.limbs[j + index]);
        }
        add_with_carry_u64(
          normalized_numerator.limbs[j + n],
          0,
          carry_back,
          &normalized_numerator.limbs[j + n]);
      }
      quotient->limbs[j] = qhat;
    }

    normalize(quotient);
    ok = reserve_limbs(&remainder_slice, n) != 0;
    if (ok) {
      memcpy(remainder_slice.limbs, normalized_numerator.limbs, sizeof(uint64_t) * n);
      remainder_slice.count = n;
      normalize(&remainder_slice);
      ok = shift_right_bits_copy(remainder, &remainder_slice, shift);
    }
  }

  xray_bigint_clear(&normalized_numerator);
  xray_bigint_clear(&remainder_slice);
  return ok;
}

static int divmod_bigint_probe(
  XrayScratchBigInt *quotient,
  XrayScratchBigInt *remainder,
  const XrayScratchBigInt *numerator,
  const XrayScratchBigInt *divisor) {
  if (!quotient || !remainder || !numerator || !divisor || divisor->count == 0) return 0;
  int ordering = xray_bigint_compare(numerator, divisor);
  if (ordering < 0) {
    quotient->count = 0;
    return xray_bigint_copy(remainder, numerator);
  }
  if (ordering == 0) {
    remainder->count = 0;
    return set_u32(quotient, 1U);
  }
  if (divisor->count == 1U) {
    return divmod_bigint_u64_probe(quotient, remainder, numerator, divisor->limbs[0]);
  }

  size_t n = divisor->count;
  unsigned int shift = clz_u64(divisor->limbs[n - 1U]);
  XrayScratchBigInt normalized_divisor;
  xray_bigint_init(&normalized_divisor);

  int ok = shift_left_bits_copy(&normalized_divisor, divisor, shift) &&
    divmod_bigint_normalized_probe(quotient, remainder, numerator, &normalized_divisor, shift);

  xray_bigint_clear(&normalized_divisor);
  return ok;
}

void xray_bigint_divisor_context_init(XrayBigIntDivisorContext *context) {
  if (!context) return;
  xray_bigint_init(&context->divisor);
  xray_bigint_init(&context->normalized_divisor);
  context->normalization_shift = 0;
  context->valid = 0;
}

void xray_bigint_divisor_context_clear(XrayBigIntDivisorContext *context) {
  if (!context) return;
  xray_bigint_clear(&context->divisor);
  xray_bigint_clear(&context->normalized_divisor);
  context->normalization_shift = 0;
  context->valid = 0;
}

int xray_bigint_divisor_context_set(XrayBigIntDivisorContext *context, const XrayScratchBigInt *divisor) {
  if (!context) return 0;
  context->valid = 0;
  context->normalization_shift = 0;
  if (!divisor || divisor->count == 0) {
    context->divisor.count = 0;
    context->normalized_divisor.count = 0;
    return 0;
  }
  unsigned int shift = clz_u64(divisor->limbs[divisor->count - 1U]);
  int ok = xray_bigint_copy(&context->divisor, divisor) &&
    shift_left_bits_copy(&context->normalized_divisor, divisor, shift);
  if (!ok) {
    context->divisor.count = 0;
    context->normalized_divisor.count = 0;
    return 0;
  }
  context->normalization_shift = shift;
  context->valid = 1;
  return 1;
}

static int divmod_bigint_precomputed_probe(
  XrayScratchBigInt *quotient,
  XrayScratchBigInt *remainder,
  const XrayScratchBigInt *numerator,
  const XrayBigIntDivisorContext *context) {
  if (!quotient || !remainder || !numerator || !context || !context->valid || context->divisor.count == 0) return 0;
  int ordering = xray_bigint_compare(numerator, &context->divisor);
  if (ordering < 0) {
    quotient->count = 0;
    return xray_bigint_copy(remainder, numerator);
  }
  if (ordering == 0) {
    remainder->count = 0;
    return set_u32(quotient, 1U);
  }
  if (context->divisor.count == 1U) {
    return divmod_bigint_u64_probe(quotient, remainder, numerator, context->divisor.limbs[0]);
  }
  return divmod_bigint_normalized_probe(
    quotient,
    remainder,
    numerator,
    &context->normalized_divisor,
    context->normalization_shift);
}

int xray_bigint_divmod(
  XrayScratchBigInt *quotient,
  XrayScratchBigInt *remainder,
  const XrayScratchBigInt *numerator,
  const XrayScratchBigInt *divisor) {
  if (!quotient || !remainder || quotient == remainder || !numerator || !divisor || divisor->count == 0) return 0;
  if (quotient == numerator || quotient == divisor || remainder == numerator || remainder == divisor) {
    XrayScratchBigInt quotient_temp;
    XrayScratchBigInt remainder_temp;
    xray_bigint_init(&quotient_temp);
    xray_bigint_init(&remainder_temp);
    int ok = divmod_bigint_probe(&quotient_temp, &remainder_temp, numerator, divisor);
    if (ok) ok = xray_bigint_copy(quotient, &quotient_temp) && xray_bigint_copy(remainder, &remainder_temp);
    xray_bigint_clear(&quotient_temp);
    xray_bigint_clear(&remainder_temp);
    return ok;
  }
  return divmod_bigint_probe(quotient, remainder, numerator, divisor);
}

int xray_bigint_divmod_precomputed(
  XrayScratchBigInt *quotient,
  XrayScratchBigInt *remainder,
  const XrayScratchBigInt *numerator,
  const XrayBigIntDivisorContext *context) {
  if (!quotient || !remainder || quotient == remainder || !numerator || !context || !context->valid || context->divisor.count == 0) return 0;
  if (quotient == numerator ||
      remainder == numerator ||
      quotient == &context->divisor ||
      quotient == &context->normalized_divisor ||
      remainder == &context->divisor ||
      remainder == &context->normalized_divisor) {
    XrayScratchBigInt quotient_temp;
    XrayScratchBigInt remainder_temp;
    xray_bigint_init(&quotient_temp);
    xray_bigint_init(&remainder_temp);
    int ok = divmod_bigint_precomputed_probe(&quotient_temp, &remainder_temp, numerator, context);
    if (ok) ok = xray_bigint_copy(quotient, &quotient_temp) && xray_bigint_copy(remainder, &remainder_temp);
    xray_bigint_clear(&quotient_temp);
    xray_bigint_clear(&remainder_temp);
    return ok;
  }
  return divmod_bigint_precomputed_probe(quotient, remainder, numerator, context);
}

typedef struct {
  size_t chunks;
  XrayScratchBigInt value;
} XrayDecimalDcPower;

typedef struct {
  XrayDecimalDcPower *items;
  size_t count;
  size_t capacity;
  XrayScratchBigInt *ladder;
  size_t ladder_count;
  size_t ladder_capacity;
  int use_ladder;
  int use_static_ladder;
} XrayDecimalDcPowerCache;

static const XrayScratchBigInt *decimal_dc_static_ladder_get(size_t bit_index) {
  size_t count = sizeof(decimal_dc_static_ladder_values) / sizeof(decimal_dc_static_ladder_values[0]);
  return bit_index < count ? &decimal_dc_static_ladder_values[bit_index] : NULL;
}

static void decimal_dc_power_cache_init(XrayDecimalDcPowerCache *cache, int use_ladder, int use_static_ladder) {
  cache->items = NULL;
  cache->count = 0;
  cache->capacity = 0;
  cache->ladder = NULL;
  cache->ladder_count = 0;
  cache->ladder_capacity = 0;
  cache->use_ladder = use_ladder;
  cache->use_static_ladder = use_static_ladder;
}

static void decimal_dc_power_cache_clear(XrayDecimalDcPowerCache *cache) {
  if (!cache) return;
  for (size_t index = 0; index < cache->count; ++index) {
    xray_bigint_clear(&cache->items[index].value);
  }
  for (size_t index = 0; index < cache->ladder_count; ++index) {
    xray_bigint_clear(&cache->ladder[index]);
  }
  free(cache->items);
  free(cache->ladder);
  cache->items = NULL;
  cache->count = 0;
  cache->capacity = 0;
  cache->ladder = NULL;
  cache->ladder_count = 0;
  cache->ladder_capacity = 0;
}

static int decimal_dc_power_cache_reserve(XrayDecimalDcPowerCache *cache, size_t needed) {
  if (cache->capacity >= needed) return 1;
  size_t next_capacity = cache->capacity ? cache->capacity * 2U : 8U;
  while (next_capacity < needed) {
    if (next_capacity > SIZE_MAX / 2U) return 0;
    next_capacity *= 2U;
  }
  if (next_capacity > SIZE_MAX / sizeof(XrayDecimalDcPower)) return 0;
  XrayDecimalDcPower *next = (XrayDecimalDcPower *)realloc(cache->items, sizeof(XrayDecimalDcPower) * next_capacity);
  if (!next) return 0;
  cache->items = next;
  cache->capacity = next_capacity;
  return 1;
}

static int decimal_dc_power_ladder_reserve(XrayDecimalDcPowerCache *cache, size_t needed) {
  if (cache->ladder_capacity >= needed) return 1;
  size_t old_capacity = cache->ladder_capacity;
  size_t next_capacity = cache->ladder_capacity ? cache->ladder_capacity * 2U : 8U;
  while (next_capacity < needed) {
    if (next_capacity > SIZE_MAX / 2U) return 0;
    next_capacity *= 2U;
  }
  if (next_capacity > SIZE_MAX / sizeof(XrayScratchBigInt)) return 0;
  XrayScratchBigInt *next = (XrayScratchBigInt *)realloc(cache->ladder, sizeof(XrayScratchBigInt) * next_capacity);
  if (!next) return 0;
  cache->ladder = next;
  for (size_t index = old_capacity; index < next_capacity; ++index) {
    xray_bigint_init(&cache->ladder[index]);
  }
  cache->ladder_capacity = next_capacity;
  return 1;
}

static const XrayScratchBigInt *decimal_dc_power_ladder_get(XrayDecimalDcPowerCache *cache, size_t bit_index) {
  if (!cache) return NULL;
  if (cache->use_static_ladder) {
    const XrayScratchBigInt *static_power = decimal_dc_static_ladder_get(bit_index);
    if (static_power) return static_power;
  }
  if (!decimal_dc_power_ladder_reserve(cache, bit_index + 1U)) return NULL;
  while (cache->ladder_count <= bit_index) {
    size_t target = cache->ladder_count;
    int ok = 0;
    const XrayScratchBigInt *static_power = cache->use_static_ladder ? decimal_dc_static_ladder_get(target) : NULL;
    if (static_power) {
      ok = xray_bigint_copy(&cache->ladder[target], static_power);
    } else if (target == 0) {
      ok = set_u64(&cache->ladder[target], XRAY_BIGINT_DECIMAL_WIDE_CHUNK_BASE);
    } else {
      ok = xray_bigint_mul(&cache->ladder[target], &cache->ladder[target - 1U], &cache->ladder[target - 1U]);
    }
    if (!ok) return NULL;
    cache->ladder_count++;
  }
  return &cache->ladder[bit_index];
}

static int decimal_dc_power_cache_store(
  XrayDecimalDcPowerCache *cache,
  size_t chunks,
  const XrayScratchBigInt *power) {
  if (!cache || !power) return 0;
  if (!decimal_dc_power_cache_reserve(cache, cache->count + 1U)) return 0;
  size_t target = cache->count++;
  cache->items[target].chunks = chunks;
  xray_bigint_init(&cache->items[target].value);
  int ok = xray_bigint_copy(&cache->items[target].value, power);
  if (!ok) {
    xray_bigint_clear(&cache->items[target].value);
    cache->count--;
  }
  return ok;
}

static int decimal_dc_power_from_ladder(
  XrayDecimalDcPowerCache *cache,
  XrayScratchBigInt *out,
  size_t chunks) {
  if (!cache || !out) return 0;
  int ok = set_u32(out, 1U);
  size_t remaining = chunks;
  size_t bit_index = 0;
  while (ok && remaining) {
    if (remaining & 1U) {
      const XrayScratchBigInt *factor = decimal_dc_power_ladder_get(cache, bit_index);
      if (!factor) return 0;
      XrayScratchBigInt product;
      xray_bigint_init(&product);
      ok = xray_bigint_mul(&product, out, factor) && xray_bigint_copy(out, &product);
      xray_bigint_clear(&product);
    }
    remaining >>= 1U;
    bit_index++;
  }
  return ok;
}

static const XrayScratchBigInt *decimal_dc_power_cache_get(XrayDecimalDcPowerCache *cache, size_t chunks) {
  if (!cache) return NULL;
  for (size_t index = 0; index < cache->count; ++index) {
    if (cache->items[index].chunks == chunks) return &cache->items[index].value;
  }

  size_t source_index = SIZE_MAX;
  size_t source_chunks = 0;
  for (size_t index = 0; index < cache->count; ++index) {
    if (cache->items[index].chunks <= chunks && cache->items[index].chunks >= source_chunks) {
      source_index = index;
      source_chunks = cache->items[index].chunks;
    }
  }

  XrayScratchBigInt power;
  xray_bigint_init(&power);
  int ok = 0;
  if (cache->use_ladder) {
    ok = decimal_dc_power_from_ladder(cache, &power, chunks);
  } else {
    ok = source_index == SIZE_MAX ? set_u32(&power, 1U) : xray_bigint_copy(&power, &cache->items[source_index].value);
    for (size_t index = source_chunks; ok && index < chunks; ++index) {
      ok = mul_add_small_inplace(&power, XRAY_BIGINT_DECIMAL_WIDE_CHUNK_BASE, 0);
    }
  }

  if (ok) ok = decimal_dc_power_cache_store(cache, chunks, &power);
  xray_bigint_clear(&power);
  if (!ok) return NULL;
  return &cache->items[cache->count - 1U].value;
}

static size_t estimate_decimal_wide_chunks_from_bits(const XrayScratchBigInt *value) {
  if (!value || value->count == 0) return 1U;
  unsigned int top_bits = XRAY_BIGINT_WORD_BITS - clz_u64(value->limbs[value->count - 1U]);
  size_t bits = (value->count - 1U) * XRAY_BIGINT_WORD_BITS + top_bits;
  if (bits > (SIZE_MAX - 4096U) / 1233U) return estimate_decimal_wide_chunk_capacity(value);
  size_t digits = (bits * 1233U) / 4096U + 1U;
  return digits / XRAY_BIGINT_DECIMAL_WIDE_CHUNK_DIGITS + 1U;
}

static char *format_decimal_dc_internal(
  const XrayScratchBigInt *value,
  XrayDecimalDcPowerCache *cache,
  size_t leaf_chunks,
  unsigned int depth) {
  if (!value || value->count == 0) {
    char *zero = (char *)calloc(2, 1);
    if (zero) zero[0] = '0';
    return zero;
  }
  if (leaf_chunks == 0) leaf_chunks = 32U;
  size_t estimated_chunks = estimate_decimal_wide_chunks_from_bits(value);
  if (estimated_chunks <= leaf_chunks || depth >= 64U) {
    return xray_bigint_get_decimal_divide_1e19_probe(value);
  }

  size_t split_chunks = estimated_chunks / 2U;
  const XrayScratchBigInt *power = NULL;
  while (split_chunks > 0) {
    power = decimal_dc_power_cache_get(cache, split_chunks);
    if (!power) return NULL;
    if (xray_bigint_compare(value, power) >= 0) break;
    split_chunks--;
  }
  if (split_chunks == 0 || !power) return xray_bigint_get_decimal_divide_1e19_probe(value);

  XrayScratchBigInt quotient;
  XrayScratchBigInt remainder;
  xray_bigint_init(&quotient);
  xray_bigint_init(&remainder);
  char *left = NULL;
  char *right = NULL;
  char *combined = NULL;
  int ok = divmod_bigint_probe(&quotient, &remainder, value, power);
  if (ok && quotient.count == 0) {
    xray_bigint_clear(&quotient);
    xray_bigint_clear(&remainder);
    return xray_bigint_get_decimal_divide_1e19_probe(value);
  }
  if (ok) {
    left = format_decimal_dc_internal(&quotient, cache, leaf_chunks, depth + 1U);
    right = format_decimal_dc_internal(&remainder, cache, leaf_chunks, depth + 1U);
    ok = left != NULL && right != NULL;
  }
  if (ok) {
    size_t left_len = strlen(left);
    size_t right_len = strlen(right);
    size_t right_width = split_chunks * XRAY_BIGINT_DECIMAL_WIDE_CHUNK_DIGITS;
    if (right_len > right_width) ok = 0;
    if (ok && left_len > SIZE_MAX - right_width - 1U) ok = 0;
    if (ok) {
      combined = (char *)calloc(left_len + right_width + 1U, 1);
      ok = combined != NULL;
    }
    if (ok) {
      memcpy(combined, left, left_len);
      memset(combined + left_len, '0', right_width - right_len);
      memcpy(combined + left_len + right_width - right_len, right, right_len);
    }
  }
  free(left);
  free(right);
  xray_bigint_clear(&quotient);
  xray_bigint_clear(&remainder);
  return ok ? combined : NULL;
}

static int write_decimal_wide_chunks_tail(
  char *buffer,
  size_t end,
  size_t min_width,
  const uint64_t *chunks,
  size_t chunk_count,
  size_t *start_out) {
  if (!buffer || !chunks || !start_out) return 0;
  if (chunk_count == 0) {
    if (min_width > end) return 0;
    if (min_width > 0) {
      size_t start = end - min_width;
      memset(buffer + start, '0', min_width);
      *start_out = start;
      return 1;
    }
    if (end == 0) return 0;
    buffer[end - 1U] = '0';
    *start_out = end - 1U;
    return 1;
  }

  char top_digits[20];
  size_t top_len = write_u64_decimal(top_digits, chunks[chunk_count - 1U]);
  if (chunk_count - 1U > (SIZE_MAX - top_len) / XRAY_BIGINT_DECIMAL_WIDE_CHUNK_DIGITS) return 0;
  size_t actual_width = top_len + (chunk_count - 1U) * XRAY_BIGINT_DECIMAL_WIDE_CHUNK_DIGITS;
  size_t width = min_width > actual_width ? min_width : actual_width;
  if (width > end) return 0;
  size_t start = end - width;
  size_t actual_start = end - actual_width;
  if (actual_start > start) memset(buffer + start, '0', actual_start - start);
  size_t used = actual_start;
  memcpy(buffer + used, top_digits, top_len);
  used += top_len;
  for (size_t index = chunk_count - 1U; index-- > 0;) {
    write_u64_decimal_padded19(buffer + used, chunks[index]);
    used += XRAY_BIGINT_DECIMAL_WIDE_CHUNK_DIGITS;
  }
  *start_out = start;
  return 1;
}

static int format_decimal_dc_write_leaf(
  const XrayScratchBigInt *value,
  char *buffer,
  size_t end,
  size_t min_width,
  size_t *start_out) {
  uint64_t *chunks = NULL;
  size_t chunk_count = 0;
  int ok = decimal_wide_chunks_from_limbs_divide(&chunks, &chunk_count, value);
  if (ok && chunk_count == 0) {
    uint64_t *zero_chunk = (uint64_t *)realloc(chunks, sizeof(uint64_t));
    if (!zero_chunk) {
      free(chunks);
      return 0;
    }
    chunks = zero_chunk;
    chunks[0] = 0;
    chunk_count = 1;
  }
  if (ok) ok = write_decimal_wide_chunks_tail(buffer, end, min_width, chunks, chunk_count, start_out);
  free(chunks);
  return ok;
}

static int format_decimal_dc_write_internal(
  const XrayScratchBigInt *value,
  XrayDecimalDcPowerCache *cache,
  size_t leaf_chunks,
  unsigned int depth,
  char *buffer,
  size_t end,
  size_t min_width,
  size_t *start_out) {
  if (!value || value->count == 0) {
    static const uint64_t zero_chunks[1] = {0};
    return write_decimal_wide_chunks_tail(buffer, end, min_width, zero_chunks, 1U, start_out);
  }
  if (leaf_chunks == 0) leaf_chunks = 32U;
  size_t estimated_chunks = estimate_decimal_wide_chunks_from_bits(value);
  if (estimated_chunks <= leaf_chunks || depth >= 64U) {
    return format_decimal_dc_write_leaf(value, buffer, end, min_width, start_out);
  }

  size_t split_chunks = estimated_chunks / 2U;
  const XrayScratchBigInt *power = NULL;
  while (split_chunks > 0) {
    power = decimal_dc_power_cache_get(cache, split_chunks);
    if (!power) return 0;
    if (xray_bigint_compare(value, power) >= 0) break;
    split_chunks--;
  }
  if (split_chunks == 0 || !power) {
    return format_decimal_dc_write_leaf(value, buffer, end, min_width, start_out);
  }

  XrayScratchBigInt quotient;
  XrayScratchBigInt remainder;
  xray_bigint_init(&quotient);
  xray_bigint_init(&remainder);
  int ok = divmod_bigint_probe(&quotient, &remainder, value, power);
  if (ok && quotient.count == 0) {
    ok = format_decimal_dc_write_leaf(value, buffer, end, min_width, start_out);
    xray_bigint_clear(&quotient);
    xray_bigint_clear(&remainder);
    return ok;
  }

  if (split_chunks > SIZE_MAX / XRAY_BIGINT_DECIMAL_WIDE_CHUNK_DIGITS) ok = 0;
  size_t right_width = ok ? split_chunks * XRAY_BIGINT_DECIMAL_WIDE_CHUNK_DIGITS : 0;
  if (ok && right_width > end) ok = 0;
  size_t right_start = 0;
  size_t left_start = 0;
  if (ok) {
    ok = format_decimal_dc_write_internal(
      &remainder,
      cache,
      leaf_chunks,
      depth + 1U,
      buffer,
      end,
      right_width,
      &right_start);
  }
  if (ok && right_start != end - right_width) ok = 0;
  if (ok) {
    ok = format_decimal_dc_write_internal(
      &quotient,
      cache,
      leaf_chunks,
      depth + 1U,
      buffer,
      right_start,
      0,
      &left_start);
  }
  if (ok) {
    size_t natural_width = end - left_start;
    if (min_width > natural_width) {
      if (min_width > end) {
        ok = 0;
      } else {
        size_t padded_start = end - min_width;
        memset(buffer + padded_start, '0', left_start - padded_start);
        left_start = padded_start;
      }
    }
  }
  if (ok) *start_out = left_start;
  xray_bigint_clear(&quotient);
  xray_bigint_clear(&remainder);
  return ok;
}

static int decimal_chunks_from_limbs_horner_direct(uint32_t **chunks_out, size_t *chunk_count_out, const XrayScratchBigInt *value) {
  uint32_t *chunks = NULL;
  size_t chunk_count = 0;
  size_t chunk_capacity = 0;
  if (!reserve_decimal_chunks(&chunks, &chunk_capacity, estimate_decimal_chunk_capacity(value))) return 0;
  for (size_t remaining = value->count; remaining > 0; --remaining) {
    uint64_t carry = value->limbs[remaining - 1U];
    for (size_t index = 0; index < chunk_count; ++index) {
      uint32_t remainder = 0;
      carry = divmod_word_u32_direct(chunks[index], carry, XRAY_BIGINT_DECIMAL_CHUNK_BASE, &remainder);
      chunks[index] = remainder;
    }
    while (carry) {
      uint32_t remainder = 0;
      carry = divmod_word_u32_direct(0, carry, XRAY_BIGINT_DECIMAL_CHUNK_BASE, &remainder);
      if (!append_decimal_chunk(&chunks, &chunk_count, &chunk_capacity, remainder)) {
        free(chunks);
        return 0;
      }
    }
  }
  while (chunk_count > 0 && chunks[chunk_count - 1U] == 0) chunk_count--;
  *chunks_out = chunks;
  *chunk_count_out = chunk_count;
  return 1;
}

static uint32_t reduce_65537_signed(int64_t value) {
  while (value < 0) value += XRAY_BIGINT_FERMAT_65537;
  while (value >= XRAY_BIGINT_FERMAT_65537) value -= XRAY_BIGINT_FERMAT_65537;
  return (uint32_t)value;
}

static uint32_t mod_65537_folded(const XrayScratchBigInt *value) {
  uint32_t remainder = 0;
  for (size_t index = 0; index < value->count; ++index) {
    uint64_t word = value->limbs[index];
    int64_t folded = (int64_t)(word & 0xffffU) -
      (int64_t)((word >> 16U) & 0xffffU) +
      (int64_t)((word >> 32U) & 0xffffU) -
      (int64_t)((word >> 48U) & 0xffffU);
    remainder = reduce_65537_signed((int64_t)remainder + folded);
  }
  return remainder;
}

static int mul_add_small_inplace(XrayScratchBigInt *value, uint64_t multiplier, uint64_t addend) {
  if (value->count == 0 || multiplier == 0) return set_u64(value, addend);
  if (!reserve_limbs(value, value->count + 1)) return 0;
  uint64_t carry = addend;
  for (size_t index = 0; index < value->count; ++index) {
    carry = mul_add_small_word(value->limbs[index], multiplier, carry, &value->limbs[index]);
  }
  if (carry) value->limbs[value->count++] = carry;
  return 1;
}

static size_t write_u32_decimal(char *out, uint32_t value) {
  char digits[10];
  size_t count = 0;
  do {
    digits[count++] = (char)('0' + (value % 10U));
    value /= 10U;
  } while (value);
  for (size_t index = 0; index < count; ++index) {
    out[index] = digits[count - index - 1];
  }
  return count;
}

static size_t write_u64_decimal(char *out, uint64_t value) {
  char digits[20];
  size_t count = 0;
  do {
    digits[count++] = (char)('0' + (value % 10U));
    value /= 10U;
  } while (value);
  for (size_t index = 0; index < count; ++index) {
    out[index] = digits[count - index - 1];
  }
  return count;
}

static void copy_decimal_digit_pair(char *out, uint32_t value) {
  const char *pair = decimal_digit_pairs + ((size_t)value * 2U);
  out[0] = pair[0];
  out[1] = pair[1];
}

static size_t write_u32_decimal_pairs(char *out, uint32_t value) {
  char digits[10];
  size_t offset = sizeof(digits);
  do {
    uint32_t quotient = value / 100U;
    uint32_t pair = value - quotient * 100U;
    offset -= 2U;
    copy_decimal_digit_pair(digits + offset, pair);
    value = quotient;
  } while (value);
  if (digits[offset] == '0') offset++;
  size_t count = sizeof(digits) - offset;
  memcpy(out, digits + offset, count);
  return count;
}

static void write_u32_decimal_padded9(char *out, uint32_t value) {
  for (size_t index = XRAY_BIGINT_DECIMAL_CHUNK_DIGITS; index-- > 0;) {
    out[index] = (char)('0' + (value % 10U));
    value /= 10U;
  }
}

static void write_u32_decimal_padded9_pairs(char *out, uint32_t value) {
  uint32_t lead = value / 100000000U;
  value -= lead * 100000000U;
  out[0] = (char)('0' + lead);
  uint32_t pair = value / 1000000U;
  value -= pair * 1000000U;
  copy_decimal_digit_pair(out + 1U, pair);
  pair = value / 10000U;
  value -= pair * 10000U;
  copy_decimal_digit_pair(out + 3U, pair);
  pair = value / 100U;
  value -= pair * 100U;
  copy_decimal_digit_pair(out + 5U, pair);
  copy_decimal_digit_pair(out + 7U, value);
}

static void write_u32_decimal_padded4_pairs(char *out, uint32_t value) {
  uint32_t pair = value / 100U;
  copy_decimal_digit_pair(out, pair);
  copy_decimal_digit_pair(out + 2U, value - pair * 100U);
}

static void write_u32_decimal_padded9_mixed_pairs(char *out, uint32_t value) {
  uint32_t lead = value / 100000000U;
  uint32_t low8 = value - lead * 100000000U;
  uint32_t high4 = low8 / 10000U;
  uint32_t low4 = low8 - high4 * 10000U;
  out[0] = (char)('0' + lead);
  write_u32_decimal_padded4_pairs(out + 1U, high4);
  write_u32_decimal_padded4_pairs(out + 5U, low4);
}

static void write_u64_decimal_padded19(char *out, uint64_t value) {
  for (size_t index = XRAY_BIGINT_DECIMAL_WIDE_CHUNK_DIGITS; index-- > 0;) {
    out[index] = (char)('0' + (value % 10U));
    value /= 10U;
  }
}

static char *format_decimal_chunks_u32_mode(const uint32_t *chunks, size_t chunk_count, int writer_mode) {
  if (chunk_count == 0) {
    char *zero = (char *)calloc(2, 1);
    if (zero) zero[0] = '0';
    return zero;
  }
  if (chunk_count > (SIZE_MAX - 1U) / XRAY_BIGINT_DECIMAL_CHUNK_DIGITS) return NULL;
  size_t capacity = chunk_count * XRAY_BIGINT_DECIMAL_CHUNK_DIGITS + 1U;
  char *text = (char *)calloc(capacity, 1);
  if (!text) return NULL;
  size_t used = writer_mode ?
    write_u32_decimal_pairs(text, chunks[chunk_count - 1]) :
    write_u32_decimal(text, chunks[chunk_count - 1]);
  for (size_t index = chunk_count - 1; index-- > 0;) {
    if (writer_mode == 2) {
      write_u32_decimal_padded9_mixed_pairs(text + used, chunks[index]);
    } else if (writer_mode == 1) {
      write_u32_decimal_padded9_pairs(text + used, chunks[index]);
    } else {
      write_u32_decimal_padded9(text + used, chunks[index]);
    }
    used += XRAY_BIGINT_DECIMAL_CHUNK_DIGITS;
  }
  return text;
}

static char *format_decimal_chunks_u32(const uint32_t *chunks, size_t chunk_count, int use_pair_writer) {
  return format_decimal_chunks_u32_mode(chunks, chunk_count, use_pair_writer ? 1 : 0);
}

static char *format_decimal_chunks_u64(const uint64_t *chunks, size_t chunk_count) {
  if (chunk_count == 0) {
    char *zero = (char *)calloc(2, 1);
    if (zero) zero[0] = '0';
    return zero;
  }
  if (chunk_count > (SIZE_MAX - 1U) / XRAY_BIGINT_DECIMAL_WIDE_CHUNK_DIGITS) return NULL;
  size_t capacity = chunk_count * XRAY_BIGINT_DECIMAL_WIDE_CHUNK_DIGITS + 1U;
  char *text = (char *)calloc(capacity, 1);
  if (!text) return NULL;
  size_t used = write_u64_decimal(text, chunks[chunk_count - 1]);
  for (size_t index = chunk_count - 1; index-- > 0;) {
    write_u64_decimal_padded19(text + used, chunks[index]);
    used += XRAY_BIGINT_DECIMAL_WIDE_CHUNK_DIGITS;
  }
  return text;
}

static int set_decimal_with_chunk_digits(XrayScratchBigInt *value, const char *decimal, unsigned int chunk_size) {
  if (!value || !decimal) return 0;
  if (chunk_size == 0 || chunk_size >= sizeof(parse_decimal_powers) / sizeof(parse_decimal_powers[0])) return 0;
  value->count = 0;
  size_t digit_count = 0;
  uint64_t chunk = 0;
  unsigned int chunk_digits = 0;
  for (const unsigned char *p = (const unsigned char *)decimal; *p; ++p) {
    unsigned char ch = *p;
    if (ch < '0' || ch > '9') {
      if (ch == ',' || ch == '_' || is_ascii_space(ch)) continue;
      return 0;
    }
    digit_count++;
    chunk = chunk * 10U + (uint64_t)(ch - '0');
    chunk_digits++;
    if (chunk_digits == chunk_size) {
      if (!mul_add_small_inplace(value, parse_decimal_powers[chunk_size], chunk)) return 0;
      chunk = 0;
      chunk_digits = 0;
    }
  }
  if (!digit_count) return 0;
  if (chunk_digits) {
    if (!mul_add_small_inplace(value, parse_decimal_powers[chunk_digits], chunk)) return 0;
  }
  return 1;
}

int xray_bigint_set_decimal(XrayScratchBigInt *value, const char *decimal) {
  return set_decimal_with_chunk_digits(value, decimal, XRAY_BIGINT_PARSE_CHUNK_DIGITS);
}

int xray_bigint_set_decimal_chunk_probe(XrayScratchBigInt *value, const char *decimal, unsigned int chunk_digits) {
  return set_decimal_with_chunk_digits(value, decimal, chunk_digits);
}

static char *get_decimal_with_options_writer(const XrayScratchBigInt *value, size_t horner_min_limbs, int use_direct_divider, int use_pair_writer) {
  if (!value || value->count == 0) {
    char *zero = (char *)calloc(2, 1);
    if (zero) zero[0] = '0';
    return zero;
  }

  XrayScratchBigInt copy;
  xray_bigint_init(&copy);

  uint32_t *chunks = NULL;
  size_t chunk_count = 0;
  size_t chunk_capacity = 0;
  if (value->count >= horner_min_limbs) {
    int ok = use_direct_divider ?
      decimal_chunks_from_limbs_horner_direct(&chunks, &chunk_count, value) :
      decimal_chunks_from_limbs_horner(&chunks, &chunk_count, value);
    if (!ok) {
      xray_bigint_clear(&copy);
      return NULL;
    }
  } else {
    if (!xray_bigint_copy(&copy, value)) {
      xray_bigint_clear(&copy);
      return NULL;
    }
    if (!reserve_decimal_chunks(&chunks, &chunk_capacity, estimate_decimal_chunk_capacity(value))) {
      xray_bigint_clear(&copy);
      return NULL;
    }
    while (copy.count > 0) {
      if (!append_decimal_chunk(&chunks, &chunk_count, &chunk_capacity, divmod_decimal_chunk_inplace(&copy))) {
        free(chunks);
        xray_bigint_clear(&copy);
        return NULL;
      }
    }
  }
  xray_bigint_clear(&copy);

  if (chunk_count == 0 && !append_decimal_chunk(&chunks, &chunk_count, &chunk_capacity, 0)) {
    free(chunks);
    return NULL;
  }

  char *text = format_decimal_chunks_u32(chunks, chunk_count, use_pair_writer);
  free(chunks);
  return text;
}

static char *get_decimal_with_options(const XrayScratchBigInt *value, size_t horner_min_limbs, int use_direct_divider) {
  return get_decimal_with_options_writer(value, horner_min_limbs, use_direct_divider, 0);
}

static int use_decimal_pair_writer_route(const XrayScratchBigInt *value) {
  size_t limbs = value ? value->count : 0;
  return limbs <= XRAY_BIGINT_DECIMAL_PAIR_WRITER_SMALL_MAX_LIMBS ||
    (limbs >= XRAY_BIGINT_DECIMAL_HORNER_MIN_LIMBS &&
     limbs <= XRAY_BIGINT_DECIMAL_PAIR_WRITER_HORNER_MAX_LIMBS);
}

char *xray_bigint_get_decimal_folded_probe(const XrayScratchBigInt *value) {
  if (!value || value->count == 0) {
    char *zero = (char *)calloc(2, 1);
    if (zero) zero[0] = '0';
    return zero;
  }

  uint32_t *chunks = NULL;
  size_t chunk_count = 0;
  size_t chunk_capacity = 0;
  if (!decimal_chunks_from_limbs_horner_folded(&chunks, &chunk_count, value)) return NULL;
  if (chunk_count == 0 && !append_decimal_chunk(&chunks, &chunk_count, &chunk_capacity, 0)) {
    free(chunks);
    return NULL;
  }

  char *text = format_decimal_chunks_u32(chunks, chunk_count, 0);
  free(chunks);
  return text;
}

char *xray_bigint_get_decimal_pair_writer_probe(const XrayScratchBigInt *value) {
  return get_decimal_with_options_writer(value, XRAY_BIGINT_DECIMAL_HORNER_MIN_LIMBS, 0, 1);
}

char *xray_bigint_get_decimal_folded_pair_writer_probe(const XrayScratchBigInt *value) {
  if (!value || value->count == 0) {
    char *zero = (char *)calloc(2, 1);
    if (zero) zero[0] = '0';
    return zero;
  }

  uint32_t *chunks = NULL;
  size_t chunk_count = 0;
  size_t chunk_capacity = 0;
  if (!decimal_chunks_from_limbs_horner_folded(&chunks, &chunk_count, value)) return NULL;
  if (chunk_count == 0 && !append_decimal_chunk(&chunks, &chunk_count, &chunk_capacity, 0)) {
    free(chunks);
    return NULL;
  }

  char *text = format_decimal_chunks_u32(chunks, chunk_count, 1);
  free(chunks);
  return text;
}

char *xray_bigint_get_decimal_mixed_pair_writer_probe(const XrayScratchBigInt *value) {
  if (!value || value->count == 0) {
    char *zero = (char *)calloc(2, 1);
    if (zero) zero[0] = '0';
    return zero;
  }

  uint32_t *chunks = NULL;
  size_t chunk_count = 0;
  size_t chunk_capacity = 0;
  if (!decimal_chunks_from_limbs_horner(&chunks, &chunk_count, value)) return NULL;
  if (chunk_count == 0 && !append_decimal_chunk(&chunks, &chunk_count, &chunk_capacity, 0)) {
    free(chunks);
    return NULL;
  }

  char *text = format_decimal_chunks_u32_mode(chunks, chunk_count, 2);
  free(chunks);
  return text;
}

char *xray_bigint_get_decimal_folded_hwdiv_probe(const XrayScratchBigInt *value) {
  if (!value || value->count == 0) {
    char *zero = (char *)calloc(2, 1);
    if (zero) zero[0] = '0';
    return zero;
  }

  uint32_t *chunks = NULL;
  size_t chunk_count = 0;
  size_t chunk_capacity = 0;
  if (!decimal_chunks_from_limbs_horner_folded_direct(&chunks, &chunk_count, value)) return NULL;
  if (chunk_count == 0 && !append_decimal_chunk(&chunks, &chunk_count, &chunk_capacity, 0)) {
    free(chunks);
    return NULL;
  }

  char *text = format_decimal_chunks_u32(chunks, chunk_count, 0);
  free(chunks);
  return text;
}

char *xray_bigint_get_decimal_folded_hwdiv_mixed_pair_probe(const XrayScratchBigInt *value) {
  if (!value || value->count == 0) {
    char *zero = (char *)calloc(2, 1);
    if (zero) zero[0] = '0';
    return zero;
  }

  uint32_t *chunks = NULL;
  size_t chunk_count = 0;
  size_t chunk_capacity = 0;
  if (!decimal_chunks_from_limbs_horner_folded_direct(&chunks, &chunk_count, value)) return NULL;
  if (chunk_count == 0 && !append_decimal_chunk(&chunks, &chunk_count, &chunk_capacity, 0)) {
    free(chunks);
    return NULL;
  }

  char *text = format_decimal_chunks_u32_mode(chunks, chunk_count, 2);
  free(chunks);
  return text;
}

char *xray_bigint_get_decimal_divide_1e19_probe(const XrayScratchBigInt *value) {
  if (!value || value->count == 0) {
    char *zero = (char *)calloc(2, 1);
    if (zero) zero[0] = '0';
    return zero;
  }

  uint64_t *chunks = NULL;
  size_t chunk_count = 0;
  if (!decimal_wide_chunks_from_limbs_divide(&chunks, &chunk_count, value)) return NULL;
  if (chunk_count == 0) {
    uint64_t *zero_chunk = (uint64_t *)realloc(chunks, sizeof(uint64_t));
    if (!zero_chunk) {
      free(chunks);
      return NULL;
    }
    chunks = zero_chunk;
    chunks[0] = 0;
    chunk_count = 1;
  }

  char *text = format_decimal_chunks_u64(chunks, chunk_count);
  free(chunks);
  return text;
}

char *xray_bigint_get_decimal_dc_probe(const XrayScratchBigInt *value, size_t leaf_chunks) {
  if (!value || value->count == 0) {
    char *zero = (char *)calloc(2, 1);
    if (zero) zero[0] = '0';
    return zero;
  }
  XrayDecimalDcPowerCache cache;
  decimal_dc_power_cache_init(&cache, 0, 0);
  char *text = format_decimal_dc_internal(value, &cache, leaf_chunks ? leaf_chunks : 32U, 0);
  decimal_dc_power_cache_clear(&cache);
  return text;
}

char *xray_bigint_get_decimal_dc_ladder_probe(const XrayScratchBigInt *value, size_t leaf_chunks) {
  if (!value || value->count == 0) {
    char *zero = (char *)calloc(2, 1);
    if (zero) zero[0] = '0';
    return zero;
  }
  XrayDecimalDcPowerCache cache;
  decimal_dc_power_cache_init(&cache, 1, 0);
  char *text = format_decimal_dc_internal(value, &cache, leaf_chunks ? leaf_chunks : 32U, 0);
  decimal_dc_power_cache_clear(&cache);
  return text;
}

char *xray_bigint_get_decimal_dc_static_ladder_probe(const XrayScratchBigInt *value, size_t leaf_chunks) {
  if (!value || value->count == 0) {
    char *zero = (char *)calloc(2, 1);
    if (zero) zero[0] = '0';
    return zero;
  }
  XrayDecimalDcPowerCache cache;
  decimal_dc_power_cache_init(&cache, 1, 1);
  char *text = format_decimal_dc_internal(value, &cache, leaf_chunks ? leaf_chunks : 32U, 0);
  decimal_dc_power_cache_clear(&cache);
  return text;
}

char *xray_bigint_get_decimal_dc_direct_probe(const XrayScratchBigInt *value, size_t leaf_chunks) {
  if (!value || value->count == 0) {
    char *zero = (char *)calloc(2, 1);
    if (zero) zero[0] = '0';
    return zero;
  }
  size_t chunk_capacity = estimate_decimal_wide_chunk_capacity(value);
  if (chunk_capacity > (SIZE_MAX - 3U) / XRAY_BIGINT_DECIMAL_WIDE_CHUNK_DIGITS) return NULL;
  size_t text_capacity = (chunk_capacity + 2U) * XRAY_BIGINT_DECIMAL_WIDE_CHUNK_DIGITS + 1U;
  char *text = (char *)calloc(text_capacity, 1);
  if (!text) return NULL;

  XrayDecimalDcPowerCache cache;
  decimal_dc_power_cache_init(&cache, 1, 0);
  size_t start = 0;
  int ok = format_decimal_dc_write_internal(
    value,
    &cache,
    leaf_chunks ? leaf_chunks : 32U,
    0,
    text,
    text_capacity - 1U,
    0,
    &start);
  decimal_dc_power_cache_clear(&cache);
  if (!ok) {
    free(text);
    return NULL;
  }
  size_t used = text_capacity - 1U - start;
  memmove(text, text + start, used);
  text[used] = '\0';
  return text;
}

char *xray_bigint_get_decimal_dc_static_direct_probe(const XrayScratchBigInt *value, size_t leaf_chunks) {
  if (!value || value->count == 0) {
    char *zero = (char *)calloc(2, 1);
    if (zero) zero[0] = '0';
    return zero;
  }
  size_t chunk_capacity = estimate_decimal_wide_chunk_capacity(value);
  if (chunk_capacity > (SIZE_MAX - 3U) / XRAY_BIGINT_DECIMAL_WIDE_CHUNK_DIGITS) return NULL;
  size_t text_capacity = (chunk_capacity + 2U) * XRAY_BIGINT_DECIMAL_WIDE_CHUNK_DIGITS + 1U;
  char *text = (char *)calloc(text_capacity, 1);
  if (!text) return NULL;

  XrayDecimalDcPowerCache cache;
  decimal_dc_power_cache_init(&cache, 1, 1);
  size_t start = 0;
  int ok = format_decimal_dc_write_internal(
    value,
    &cache,
    leaf_chunks ? leaf_chunks : 32U,
    0,
    text,
    text_capacity - 1U,
    0,
    &start);
  decimal_dc_power_cache_clear(&cache);
  if (!ok) {
    free(text);
    return NULL;
  }
  size_t used = text_capacity - 1U - start;
  memmove(text, text + start, used);
  text[used] = '\0';
  return text;
}

char *xray_bigint_get_decimal_wide_probe(const XrayScratchBigInt *value) {
  if (!value || value->count == 0) {
    char *zero = (char *)calloc(2, 1);
    if (zero) zero[0] = '0';
    return zero;
  }

  uint64_t *chunks = NULL;
  size_t chunk_count = 0;
  if (!decimal_wide_chunks_from_limbs_horner(&chunks, &chunk_count, value)) return NULL;
  if (chunk_count == 0) {
    uint64_t *zero_chunk = (uint64_t *)realloc(chunks, sizeof(uint64_t));
    if (!zero_chunk) {
      free(chunks);
      return NULL;
    }
    chunks = zero_chunk;
    chunks[0] = 0;
    chunk_count = 1;
  }
  char *text = format_decimal_chunks_u64(chunks, chunk_count);
  free(chunks);
  return text;
}

char *xray_bigint_get_decimal(const XrayScratchBigInt *value) {
  if (value && value->count > 0 &&
      estimate_decimal_wide_chunks_from_bits(value) >= XRAY_BIGINT_DECIMAL_DC_MIN_WIDE_CHUNKS) {
    char *dc_text = xray_bigint_get_decimal_dc_ladder_probe(value, XRAY_BIGINT_DECIMAL_DC_LEAF_CHUNKS);
    if (dc_text) return dc_text;
  }
  return get_decimal_with_options_writer(
    value,
    XRAY_BIGINT_DECIMAL_HORNER_MIN_LIMBS,
    0,
    use_decimal_pair_writer_route(value));
}

char *xray_bigint_get_decimal_horner_threshold_probe(const XrayScratchBigInt *value, size_t horner_min_limbs) {
  return get_decimal_with_options(value, horner_min_limbs ? horner_min_limbs : 1U, 0);
}

char *xray_bigint_get_decimal_divider_probe(const XrayScratchBigInt *value, int use_direct_divider) {
  return get_decimal_with_options(value, XRAY_BIGINT_DECIMAL_HORNER_MIN_LIMBS, use_direct_divider);
}

int xray_bigint_compare(const XrayScratchBigInt *left, const XrayScratchBigInt *right) {
  size_t left_count = left ? left->count : 0;
  size_t right_count = right ? right->count : 0;
  if (left_count < right_count) return -1;
  if (left_count > right_count) return 1;
  for (size_t index = left_count; index-- > 0;) {
    if (left->limbs[index] < right->limbs[index]) return -1;
    if (left->limbs[index] > right->limbs[index]) return 1;
  }
  return 0;
}

int xray_bigint_add(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right) {
  if (!out || !left || !right) return 0;
  if (left->count == 0) return xray_bigint_copy(out, right);
  if (right->count == 0) return xray_bigint_copy(out, left);
  const XrayScratchBigInt *longer = left;
  const XrayScratchBigInt *shorter = right;
  if (right->count > left->count) {
    longer = right;
    shorter = left;
  }
  if (!reserve_limbs(out, longer->count + 1)) return 0;
  unsigned char carry = 0;
  size_t index = 0;
  for (; index < shorter->count; ++index) {
    carry = add_with_carry_u64(left->limbs[index], right->limbs[index], carry, &out->limbs[index]);
  }
  for (; index < longer->count; ++index) {
    carry = add_with_carry_u64(longer->limbs[index], 0, carry, &out->limbs[index]);
  }
  out->count = longer->count;
  if (carry) out->limbs[out->count++] = carry;
  return 1;
}

int xray_bigint_sub(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right) {
  if (!out || !left || !right) return 0;
  int ordering = xray_bigint_compare(left, right);
  if (ordering < 0) return 0;
  if (ordering == 0) return set_u32(out, 0);
  if (right->count == 0) return xray_bigint_copy(out, left);
  if (!reserve_limbs(out, left->count ? left->count : 1)) return 0;
  unsigned char borrow = 0;
  size_t index = 0;
  for (; index < right->count; ++index) {
    borrow = sub_with_borrow_u64(left->limbs[index], right->limbs[index], borrow, &out->limbs[index]);
  }
  if (!borrow && index < left->count) {
    if (out->limbs != left->limbs) {
      memcpy(out->limbs + index, left->limbs + index, sizeof(uint64_t) * (left->count - index));
    }
    out->count = left->count;
    normalize(out);
    return 1;
  }
  for (; index < left->count; ++index) {
    borrow = sub_with_borrow_u64(left->limbs[index], 0, borrow, &out->limbs[index]);
  }
  out->count = left->count;
  normalize(out);
  return borrow == 0;
}

static void add_two_limb_at(XrayScratchBigInt *out, size_t position, uint64_t low, uint64_t high) {
  unsigned char carry = add_with_carry_u64(out->limbs[position], low, 0, &out->limbs[position]);
  position++;
  uint64_t word = high + (uint64_t)carry;
  uint64_t extra = word < high ? 1U : 0U;
  while (word || extra) {
    carry = add_with_carry_u64(out->limbs[position], word, 0, &out->limbs[position]);
    word = extra + (uint64_t)carry;
    extra = 0;
    position++;
  }
}

static void square_add_diagonal_word(XrayScratchBigInt *out, size_t position, uint64_t word) {
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  unsigned __int64 high = 0;
  unsigned __int64 low = _umul128(word, word, &high);
  add_two_limb_at(out, position, (uint64_t)low, (uint64_t)high);
#else
  __uint128_t product = (__uint128_t)word * (__uint128_t)word;
  add_two_limb_at(out, position, (uint64_t)product, (uint64_t)(product >> XRAY_BIGINT_WORD_BITS));
#endif
}

static void square_add_doubled_cross_row(XrayScratchBigInt *out, const uint64_t *limbs, size_t count, size_t row) {
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  uint64_t carry_low = 0;
  uint64_t carry_high = 0;
  for (size_t column = row + 1U; column < count; ++column) {
    unsigned __int64 high = 0;
    unsigned __int64 low = _umul128(limbs[row], limbs[column], &high);
    unsigned __int64 doubled_low = low << 1U;
    uint64_t carry_from_double = (uint64_t)(low >> 63U);
    unsigned __int64 sum = 0;
    unsigned char carry1 = _addcarry_u64(0, doubled_low, out->limbs[row + column], &sum);
    unsigned char carry2 = _addcarry_u64(0, sum, carry_low, &sum);
    out->limbs[row + column] = (uint64_t)sum;

    uint64_t small = carry_from_double + (uint64_t)carry1 + (uint64_t)carry2 + carry_high;
    uint64_t next_low = ((uint64_t)high) << 1U;
    uint64_t next_high = ((uint64_t)high) >> 63U;
    uint64_t next_sum = next_low + small;
    if (next_sum < next_low) next_high++;
    carry_low = next_sum;
    carry_high = next_high;
  }
  if (carry_low || carry_high) add_two_limb_at(out, row + count, carry_low, carry_high);
#else
  __uint128_t carry = 0;
  for (size_t column = row + 1U; column < count; ++column) {
    __uint128_t product = (__uint128_t)limbs[row] * (__uint128_t)limbs[column];
    uint64_t low = (uint64_t)product;
    uint64_t high = (uint64_t)(product >> XRAY_BIGINT_WORD_BITS);
    __uint128_t sum = ((__uint128_t)low << 1U) +
      (__uint128_t)out->limbs[row + column] +
      (uint64_t)carry;
    out->limbs[row + column] = (uint64_t)sum;
    carry = ((__uint128_t)high << 1U) + (sum >> XRAY_BIGINT_WORD_BITS) + (carry >> XRAY_BIGINT_WORD_BITS);
  }
  if (carry) add_two_limb_at(out, row + count, (uint64_t)carry, (uint64_t)(carry >> XRAY_BIGINT_WORD_BITS));
#endif
}

static int square_schoolbook(XrayScratchBigInt *out, const XrayScratchBigInt *value) {
  if (!out || !value) return 0;
  if (value->count == 0) return set_u32(out, 0);
  size_t needed = value->count * 2U;
  if (!reserve_limbs(out, needed + 2U)) return 0;
  memset(out->limbs, 0, sizeof(uint64_t) * (needed + 2U));
  out->count = needed + 2U;

  for (size_t row = 0; row < value->count; ++row) {
    square_add_doubled_cross_row(out, value->limbs, value->count, row);
  }
  for (size_t index = 0; index < value->count; ++index) {
    square_add_diagonal_word(out, index * 2U, value->limbs[index]);
  }
  normalize(out);
  return 1;
}

static int slice_bigint(XrayScratchBigInt *out, const XrayScratchBigInt *value, size_t offset, size_t count);
static int add_shifted_inplace(XrayScratchBigInt *out, const XrayScratchBigInt *addend, size_t shift);
static int mul_schoolbook_mode(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right, int use_unroll4);
static int square_dispatch_threshold(XrayScratchBigInt *out, const XrayScratchBigInt *value, size_t threshold);

static int square_karatsuba_threshold(XrayScratchBigInt *out, const XrayScratchBigInt *value, size_t threshold) {
  if (!out || !value) return 0;
  if (value->count == 0) return set_u32(out, 0);
  if (value->count <= XRAY_BIGINT_SQUARE_SELF_MUL_MAX_LIMBS) {
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
    int use_unroll4 = value->count >= XRAY_BIGINT_UNROLL4_ROUTE_MIN_LIMBS;
#else
    int use_unroll4 = 0;
#endif
    return mul_schoolbook_mode(out, value, value, use_unroll4);
  }
  size_t active_threshold = threshold >= 2U ? threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
  if (value->count < active_threshold) return square_schoolbook(out, value);

  size_t split = value->count / 2U;
  XrayScratchBigInt a0, a1, z0, z1, z2, sum;
  xray_bigint_init(&a0);
  xray_bigint_init(&a1);
  xray_bigint_init(&z0);
  xray_bigint_init(&z1);
  xray_bigint_init(&z2);
  xray_bigint_init(&sum);

  int ok = slice_bigint(&a0, value, 0, split) &&
    slice_bigint(&a1, value, split, value->count - split) &&
    square_dispatch_threshold(&z0, &a0, active_threshold) &&
    square_dispatch_threshold(&z2, &a1, active_threshold) &&
    xray_bigint_add(&sum, &a0, &a1) &&
    square_dispatch_threshold(&z1, &sum, active_threshold) &&
    xray_bigint_sub(&z1, &z1, &z0) &&
    xray_bigint_sub(&z1, &z1, &z2);

  if (ok) {
    out->count = 0;
    ok = reserve_limbs(out, value->count * 2U + 2U) &&
      add_shifted_inplace(out, &z0, 0) &&
      add_shifted_inplace(out, &z1, split) &&
      add_shifted_inplace(out, &z2, split * 2U);
    if (ok) normalize(out);
  }

  xray_bigint_clear(&a0);
  xray_bigint_clear(&a1);
  xray_bigint_clear(&z0);
  xray_bigint_clear(&z1);
  xray_bigint_clear(&z2);
  xray_bigint_clear(&sum);
  return ok;
}

static int square_dispatch_threshold(XrayScratchBigInt *out, const XrayScratchBigInt *value, size_t threshold) {
  return square_karatsuba_threshold(out, value, threshold);
}

static int mul_schoolbook_mode(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right, int use_unroll4) {
  if (!out || !left || !right) return 0;
  if (left->count == 0 || right->count == 0) return set_u32(out, 0);
  const XrayScratchBigInt *outer = left;
  const XrayScratchBigInt *inner = right;
  if (outer->count > inner->count) {
    outer = right;
    inner = left;
  }
  size_t needed = outer->count + inner->count;
  if (!reserve_limbs(out, needed)) return 0;
  out->count = needed;

  uint64_t carry = 0;
  for (size_t j = 0; j < inner->count; ++j) {
    carry = mul_add_small_word(inner->limbs[j], outer->limbs[0], carry, &out->limbs[j]);
  }
  out->limbs[inner->count] = carry;
  if (needed > inner->count + 1) {
    memset(out->limbs + inner->count + 1, 0, sizeof(uint64_t) * (needed - inner->count - 1));
  }

  for (size_t i = 1; i < outer->count; ++i) {
    carry = 0;
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
    if (use_unroll4) {
      carry = mul_add_word_unroll4_row(out->limbs + i, inner->limbs, outer->limbs[i], inner->count);
    } else
#else
    (void)use_unroll4;
#endif
    {
    for (size_t j = 0; j < inner->count; ++j) {
      carry = mul_add_word(out->limbs[i + j], outer->limbs[i], inner->limbs[j], carry, &out->limbs[i + j]);
    }
    }
    size_t pos = i + inner->count;
    while (carry) {
      uint64_t current = out->limbs[pos] + carry;
      out->limbs[pos] = current;
      carry = current < carry;
      pos++;
    }
  }
  normalize(out);
  return 1;
}

static int mul_schoolbook(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right) {
  return mul_schoolbook_mode(out, left, right, 0);
}

static int slice_bigint(XrayScratchBigInt *out, const XrayScratchBigInt *value, size_t offset, size_t count) {
  if (!out || !value) return 0;
  out->count = 0;
  if (offset >= value->count || count == 0) return 1;
  size_t available = value->count - offset;
  size_t actual = count < available ? count : available;
  if (!reserve_limbs(out, actual)) return 0;
  memcpy(out->limbs, value->limbs + offset, sizeof(uint64_t) * actual);
  out->count = actual;
  normalize(out);
  return 1;
}

static int add_shifted_inplace(XrayScratchBigInt *out, const XrayScratchBigInt *addend, size_t shift) {
  if (!out || !addend) return 0;
  if (addend->count == 0) return 1;
  size_t needed = shift + addend->count + 1;
  if (!reserve_limbs(out, needed)) return 0;
  if (out->count < shift) {
    memset(out->limbs + out->count, 0, sizeof(uint64_t) * (shift - out->count));
    out->count = shift;
  }
  if (out->count < shift + addend->count) {
    memset(out->limbs + out->count, 0, sizeof(uint64_t) * (shift + addend->count - out->count));
    out->count = shift + addend->count;
  }
  unsigned char carry = 0;
  size_t index = 0;
  for (; index < addend->count; ++index) {
    carry = add_with_carry_u64(out->limbs[shift + index], addend->limbs[index], carry, &out->limbs[shift + index]);
  }
  size_t position = shift + index;
  while (carry) {
    if (position == out->count) {
      out->limbs[out->count++] = 0;
    }
    carry = add_with_carry_u64(out->limbs[position], 0, carry, &out->limbs[position]);
    position++;
  }
  normalize(out);
  return 1;
}

static int abs_diff_bigint(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *left,
  const XrayScratchBigInt *right,
  int *ordering) {
  int compare = xray_bigint_compare(left, right);
  if (ordering) *ordering = compare;
  return compare >= 0 ? xray_bigint_sub(out, left, right) : xray_bigint_sub(out, right, left);
}

static void clear_many_bigints(
  XrayScratchBigInt *a0,
  XrayScratchBigInt *a1,
  XrayScratchBigInt *b0,
  XrayScratchBigInt *b1,
  XrayScratchBigInt *z0,
  XrayScratchBigInt *z1,
  XrayScratchBigInt *z2,
  XrayScratchBigInt *sum_a,
  XrayScratchBigInt *sum_b) {
  xray_bigint_clear(a0);
  xray_bigint_clear(a1);
  xray_bigint_clear(b0);
  xray_bigint_clear(b1);
  xray_bigint_clear(z0);
  xray_bigint_clear(z1);
  xray_bigint_clear(z2);
  xray_bigint_clear(sum_a);
  xray_bigint_clear(sum_b);
}

static int mul_dispatch_threshold_mode(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right, size_t threshold, int use_unroll4);
static int mul_dispatch_threshold(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right, size_t threshold);
static int mul_dispatch_threshold_sum_mode(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right, size_t threshold, int use_unroll4);
static int mul_toom3_probe_internal(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right, size_t leaf_threshold, int use_unroll4, size_t depth_limit);

static int mul_karatsuba_threshold_mode(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right, size_t threshold, int use_unroll4) {
  size_t left_count = left ? left->count : 0;
  size_t right_count = right ? right->count : 0;
  size_t max_count = left_count > right_count ? left_count : right_count;
  size_t min_count = left_count < right_count ? left_count : right_count;
  size_t active_threshold = threshold >= 2U ? threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
  if (max_count < active_threshold || min_count * 2U < max_count) {
    return mul_schoolbook_mode(out, left, right, use_unroll4);
  }

  size_t split = max_count / 2U;
  XrayScratchBigInt a0, a1, b0, b1, z0, z1, z2, sum_a, sum_b;
  xray_bigint_init(&a0);
  xray_bigint_init(&a1);
  xray_bigint_init(&b0);
  xray_bigint_init(&b1);
  xray_bigint_init(&z0);
  xray_bigint_init(&z1);
  xray_bigint_init(&z2);
  xray_bigint_init(&sum_a);
  xray_bigint_init(&sum_b);

  int a_order = 0;
  int b_order = 0;
  int ok = slice_bigint(&a0, left, 0, split) &&
    slice_bigint(&a1, left, split, left_count > split ? left_count - split : 0) &&
    slice_bigint(&b0, right, 0, split) &&
    slice_bigint(&b1, right, split, right_count > split ? right_count - split : 0) &&
    mul_dispatch_threshold_mode(&z0, &a0, &b0, active_threshold, use_unroll4) &&
    mul_dispatch_threshold_mode(&z2, &a1, &b1, active_threshold, use_unroll4) &&
    abs_diff_bigint(&sum_a, &a1, &a0, &a_order) &&
    abs_diff_bigint(&sum_b, &b1, &b0, &b_order) &&
    mul_dispatch_threshold_mode(&z1, &sum_a, &sum_b, active_threshold, use_unroll4) &&
    xray_bigint_add(&sum_a, &z0, &z2) &&
    (((a_order >= 0) == (b_order >= 0)) ? xray_bigint_sub(&z1, &sum_a, &z1) : xray_bigint_add(&z1, &sum_a, &z1));

  if (ok) {
    out->count = 0;
    ok = reserve_limbs(out, left_count + right_count) &&
      add_shifted_inplace(out, &z0, 0) &&
      add_shifted_inplace(out, &z1, split) &&
      add_shifted_inplace(out, &z2, split * 2U);
    if (ok) normalize(out);
  }

  clear_many_bigints(&a0, &a1, &b0, &b1, &z0, &z1, &z2, &sum_a, &sum_b);
  return ok;
}

static int mul_karatsuba_threshold(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right, size_t threshold) {
  return mul_karatsuba_threshold_mode(out, left, right, threshold, 0);
}

static int mul_dispatch_threshold_mode(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right, size_t threshold, int use_unroll4) {
  return mul_karatsuba_threshold_mode(out, left, right, threshold, use_unroll4);
}

static int mul_dispatch_threshold(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right, size_t threshold) {
  return mul_karatsuba_threshold(out, left, right, threshold);
}

static int mul_karatsuba_sum_threshold_mode(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right, size_t threshold, int use_unroll4) {
  size_t left_count = left ? left->count : 0;
  size_t right_count = right ? right->count : 0;
  size_t max_count = left_count > right_count ? left_count : right_count;
  size_t min_count = left_count < right_count ? left_count : right_count;
  size_t active_threshold = threshold >= 2U ? threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
  if (max_count < active_threshold || min_count * 2U < max_count) {
    return mul_schoolbook_mode(out, left, right, use_unroll4);
  }

  size_t split = max_count / 2U;
  XrayScratchBigInt a0, a1, b0, b1, z0, z1, z2, sum_a, sum_b;
  xray_bigint_init(&a0);
  xray_bigint_init(&a1);
  xray_bigint_init(&b0);
  xray_bigint_init(&b1);
  xray_bigint_init(&z0);
  xray_bigint_init(&z1);
  xray_bigint_init(&z2);
  xray_bigint_init(&sum_a);
  xray_bigint_init(&sum_b);

  int ok = slice_bigint(&a0, left, 0, split) &&
    slice_bigint(&a1, left, split, left_count > split ? left_count - split : 0) &&
    slice_bigint(&b0, right, 0, split) &&
    slice_bigint(&b1, right, split, right_count > split ? right_count - split : 0) &&
    mul_dispatch_threshold_sum_mode(&z0, &a0, &b0, active_threshold, use_unroll4) &&
    mul_dispatch_threshold_sum_mode(&z2, &a1, &b1, active_threshold, use_unroll4) &&
    xray_bigint_add(&sum_a, &a0, &a1) &&
    xray_bigint_add(&sum_b, &b0, &b1) &&
    mul_dispatch_threshold_sum_mode(&z1, &sum_a, &sum_b, active_threshold, use_unroll4) &&
    xray_bigint_sub(&z1, &z1, &z0) &&
    xray_bigint_sub(&z1, &z1, &z2);

  if (ok) {
    out->count = 0;
    ok = reserve_limbs(out, left_count + right_count + 2U) &&
      add_shifted_inplace(out, &z0, 0) &&
      add_shifted_inplace(out, &z1, split) &&
      add_shifted_inplace(out, &z2, split * 2U);
    if (ok) normalize(out);
  }

  clear_many_bigints(&a0, &a1, &b0, &b1, &z0, &z1, &z2, &sum_a, &sum_b);
  return ok;
}

static int mul_dispatch_threshold_sum_mode(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right, size_t threshold, int use_unroll4) {
  return mul_karatsuba_sum_threshold_mode(out, left, right, threshold, use_unroll4);
}

typedef struct XraySignedScratchBigInt {
  int sign;
  XrayScratchBigInt mag;
} XraySignedScratchBigInt;

static void signed_init(XraySignedScratchBigInt *value) {
  if (!value) return;
  value->sign = 0;
  xray_bigint_init(&value->mag);
}

static void signed_clear(XraySignedScratchBigInt *value) {
  if (!value) return;
  xray_bigint_clear(&value->mag);
  value->sign = 0;
}

static void signed_normalize(XraySignedScratchBigInt *value) {
  if (!value) return;
  normalize(&value->mag);
  if (value->mag.count == 0) value->sign = 0;
}

static int signed_set_unsigned(XraySignedScratchBigInt *out, const XrayScratchBigInt *value) {
  if (!out || !value) return 0;
  if (!xray_bigint_copy(&out->mag, value)) return 0;
  out->sign = out->mag.count ? 1 : 0;
  return 1;
}

static int signed_copy(XraySignedScratchBigInt *out, const XraySignedScratchBigInt *value) {
  if (!out || !value) return 0;
  if (!xray_bigint_copy(&out->mag, &value->mag)) return 0;
  out->sign = value->sign;
  signed_normalize(out);
  return 1;
}

static int signed_add(XraySignedScratchBigInt *out, const XraySignedScratchBigInt *left, const XraySignedScratchBigInt *right) {
  if (!out || !left || !right) return 0;
  XraySignedScratchBigInt temp;
  signed_init(&temp);
  int ok = 1;
  if (left->sign == 0) ok = signed_copy(&temp, right);
  else if (right->sign == 0) ok = signed_copy(&temp, left);
  else if (left->sign == right->sign) {
    ok = xray_bigint_add(&temp.mag, &left->mag, &right->mag);
    temp.sign = ok && temp.mag.count ? left->sign : 0;
  } else {
    int compare = xray_bigint_compare(&left->mag, &right->mag);
    if (compare == 0) ok = set_u32(&temp.mag, 0);
    else if (compare > 0) {
      ok = xray_bigint_sub(&temp.mag, &left->mag, &right->mag);
      temp.sign = ok && temp.mag.count ? left->sign : 0;
    } else {
      ok = xray_bigint_sub(&temp.mag, &right->mag, &left->mag);
      temp.sign = ok && temp.mag.count ? right->sign : 0;
    }
  }
  if (ok) ok = signed_copy(out, &temp);
  signed_clear(&temp);
  return ok;
}

static int signed_sub(XraySignedScratchBigInt *out, const XraySignedScratchBigInt *left, const XraySignedScratchBigInt *right) {
  if (!out || !left || !right) return 0;
  XraySignedScratchBigInt neg_right;
  signed_init(&neg_right);
  int ok = signed_copy(&neg_right, right);
  if (ok) {
    neg_right.sign = -neg_right.sign;
    ok = signed_add(out, left, &neg_right);
  }
  signed_clear(&neg_right);
  return ok;
}

static int signed_sub_inplace(XraySignedScratchBigInt *value, const XraySignedScratchBigInt *subtrahend) {
  return signed_sub(value, value, subtrahend);
}

static int signed_divexact_u32(XraySignedScratchBigInt *value, uint32_t divisor) {
  if (!value || divisor == 0) return 0;
  if (value->sign == 0) return 1;
  XrayScratchBigInt quotient;
  xray_bigint_init(&quotient);
  uint32_t remainder = 0;
  int ok = xray_bigint_divmod_u32(&quotient, &remainder, &value->mag, divisor) && remainder == 0;
  if (ok) {
    ok = xray_bigint_copy(&value->mag, &quotient);
    signed_normalize(value);
  }
  xray_bigint_clear(&quotient);
  return ok;
}

static int signed_mul_u32_inplace(XraySignedScratchBigInt *value, uint64_t multiplier) {
  if (!value) return 0;
  if (value->sign == 0 || multiplier == 1) return 1;
  if (multiplier == 0) {
    value->sign = 0;
    value->mag.count = 0;
    return 1;
  }
  int ok = mul_add_small_inplace(&value->mag, multiplier, 0);
  signed_normalize(value);
  return ok;
}

static int signed_mul_unsigned_threshold_mode(
  XraySignedScratchBigInt *out,
  const XraySignedScratchBigInt *left,
  const XraySignedScratchBigInt *right,
  size_t threshold,
  int use_unroll4) {
  if (!out || !left || !right) return 0;
  if (left->sign == 0 || right->sign == 0) {
    out->sign = 0;
    return set_u32(&out->mag, 0);
  }
  int ok = mul_dispatch_threshold_mode(&out->mag, &left->mag, &right->mag, threshold, use_unroll4);
  out->sign = ok && out->mag.count ? left->sign * right->sign : 0;
  return ok;
}

static int signed_mul_toom3_recursive_mode(
  XraySignedScratchBigInt *out,
  const XraySignedScratchBigInt *left,
  const XraySignedScratchBigInt *right,
  size_t threshold,
  int use_unroll4,
  size_t depth_limit) {
  if (!out || !left || !right) return 0;
  if (left->sign == 0 || right->sign == 0) {
    out->sign = 0;
    return set_u32(&out->mag, 0);
  }
  int ok = mul_toom3_probe_internal(&out->mag, &left->mag, &right->mag, threshold, use_unroll4, depth_limit);
  out->sign = ok && out->mag.count ? left->sign * right->sign : 0;
  return ok;
}

static int add_scaled_unsigned(XrayScratchBigInt *out, const XrayScratchBigInt *value, uint64_t scale) {
  if (!out || !value) return 0;
  if (value->count == 0 || scale == 0) return 1;
  size_t needed = out->count > value->count ? out->count : value->count;
  if (!reserve_limbs(out, needed + 1U)) return 0;
  if (out->count < value->count) {
    memset(out->limbs + out->count, 0, sizeof(uint64_t) * (value->count - out->count));
    out->count = value->count;
  }

  if (scale == 1) {
    unsigned char carry = 0;
    for (size_t index = 0; index < value->count; ++index) {
      carry = add_with_carry_u64(out->limbs[index], value->limbs[index], carry, &out->limbs[index]);
    }
    size_t position = value->count;
    while (carry) {
      if (position == out->count) {
        out->limbs[out->count++] = 1;
        carry = 0;
      } else {
        carry = add_with_carry_u64(out->limbs[position], 0, carry, &out->limbs[position]);
        position++;
      }
    }
  } else {
    uint64_t carry = 0;
    for (size_t index = 0; index < value->count; ++index) {
      carry = mul_add_word(out->limbs[index], value->limbs[index], scale, carry, &out->limbs[index]);
    }
    size_t position = value->count;
    while (carry) {
      if (position == out->count) {
        out->limbs[out->count++] = carry;
        carry = 0;
      } else {
        uint64_t current = out->limbs[position] + carry;
        out->limbs[position] = current;
        carry = current < carry;
        position++;
      }
    }
  }
  normalize(out);
  return 1;
}

static int eval_toom3_positive(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *part0,
  const XrayScratchBigInt *part1,
  const XrayScratchBigInt *part2,
  uint64_t weight1,
  uint64_t weight2) {
  return set_u32(out, 0) &&
    add_scaled_unsigned(out, part0, 1) &&
    add_scaled_unsigned(out, part1, weight1) &&
    add_scaled_unsigned(out, part2, weight2);
}

static int eval_toom3_minus_one(
  XraySignedScratchBigInt *out,
  const XrayScratchBigInt *part0,
  const XrayScratchBigInt *part1,
  const XrayScratchBigInt *part2) {
  XrayScratchBigInt positive;
  xray_bigint_init(&positive);
  int ok = eval_toom3_positive(&positive, part0, part1, part2, 0, 1);
  if (ok) {
    int compare = xray_bigint_compare(&positive, part1);
    if (compare == 0) {
      ok = set_u32(&out->mag, 0);
      out->sign = 0;
    } else if (compare > 0) {
      ok = xray_bigint_sub(&out->mag, &positive, part1);
      out->sign = ok && out->mag.count ? 1 : 0;
    } else {
      ok = xray_bigint_sub(&out->mag, part1, &positive);
      out->sign = ok && out->mag.count ? -1 : 0;
    }
  }
  xray_bigint_clear(&positive);
  return ok;
}

static int signed_from_positive_eval(
  XraySignedScratchBigInt *out,
  const XrayScratchBigInt *part0,
  const XrayScratchBigInt *part1,
  const XrayScratchBigInt *part2,
  uint64_t weight1,
  uint64_t weight2) {
  XrayScratchBigInt eval;
  xray_bigint_init(&eval);
  int ok = eval_toom3_positive(&eval, part0, part1, part2, weight1, weight2) && signed_set_unsigned(out, &eval);
  xray_bigint_clear(&eval);
  return ok;
}

static int mul_toom3_probe_internal(
  XrayScratchBigInt *out,
  const XrayScratchBigInt *left,
  const XrayScratchBigInt *right,
  size_t leaf_threshold,
  int use_unroll4,
  size_t depth_limit) {
  if (!out || !left || !right) return 0;
  if (left->count == 0 || right->count == 0) return set_u32(out, 0);
  size_t max_count = left->count > right->count ? left->count : right->count;
  size_t min_count = left->count < right->count ? left->count : right->count;
  size_t active_threshold = leaf_threshold >= 2U ? leaf_threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
  if (depth_limit == 0 || max_count < active_threshold * 3U || min_count * 3U < max_count * 2U) {
    return mul_dispatch_threshold_mode(out, left, right, active_threshold, use_unroll4);
  }

  size_t split = (max_count + 2U) / 3U;
  XrayScratchBigInt a0, a1, a2, b0, b1, b2;
  XraySignedScratchBigInt x0, x1, xm1, x2, xinf, y0, y1, ym1, y2, yinf;
  XraySignedScratchBigInt v0, v1, vm1, v2, vinf, twice_vinf;
  xray_bigint_init(&a0);
  xray_bigint_init(&a1);
  xray_bigint_init(&a2);
  xray_bigint_init(&b0);
  xray_bigint_init(&b1);
  xray_bigint_init(&b2);
  signed_init(&x0);
  signed_init(&x1);
  signed_init(&xm1);
  signed_init(&x2);
  signed_init(&xinf);
  signed_init(&y0);
  signed_init(&y1);
  signed_init(&ym1);
  signed_init(&y2);
  signed_init(&yinf);
  signed_init(&v0);
  signed_init(&v1);
  signed_init(&vm1);
  signed_init(&v2);
  signed_init(&vinf);
  signed_init(&twice_vinf);

  int ok = slice_bigint(&a0, left, 0, split) &&
    slice_bigint(&a1, left, split, split) &&
    slice_bigint(&a2, left, split * 2U, left->count > split * 2U ? left->count - split * 2U : 0) &&
    slice_bigint(&b0, right, 0, split) &&
    slice_bigint(&b1, right, split, split) &&
    slice_bigint(&b2, right, split * 2U, right->count > split * 2U ? right->count - split * 2U : 0) &&
    signed_set_unsigned(&x0, &a0) &&
    signed_from_positive_eval(&x1, &a0, &a1, &a2, 1, 1) &&
    eval_toom3_minus_one(&xm1, &a0, &a1, &a2) &&
    signed_from_positive_eval(&x2, &a0, &a1, &a2, 2, 4) &&
    signed_set_unsigned(&xinf, &a2) &&
    signed_set_unsigned(&y0, &b0) &&
    signed_from_positive_eval(&y1, &b0, &b1, &b2, 1, 1) &&
    eval_toom3_minus_one(&ym1, &b0, &b1, &b2) &&
    signed_from_positive_eval(&y2, &b0, &b1, &b2, 2, 4) &&
    signed_set_unsigned(&yinf, &b2) &&
    signed_mul_toom3_recursive_mode(&v0, &x0, &y0, active_threshold, use_unroll4, depth_limit - 1U) &&
    signed_mul_toom3_recursive_mode(&v1, &x1, &y1, active_threshold, use_unroll4, depth_limit - 1U) &&
    signed_mul_toom3_recursive_mode(&vm1, &xm1, &ym1, active_threshold, use_unroll4, depth_limit - 1U) &&
    signed_mul_toom3_recursive_mode(&v2, &x2, &y2, active_threshold, use_unroll4, depth_limit - 1U) &&
    signed_mul_toom3_recursive_mode(&vinf, &xinf, &yinf, active_threshold, use_unroll4, depth_limit - 1U);

  if (ok) {
    ok = signed_sub_inplace(&v2, &vm1) &&
      signed_divexact_u32(&v2, 3) &&
      signed_sub(&vm1, &v1, &vm1) &&
      signed_divexact_u32(&vm1, 2) &&
      signed_sub_inplace(&v1, &v0) &&
      signed_sub_inplace(&v2, &v1) &&
      signed_divexact_u32(&v2, 2) &&
      signed_sub_inplace(&v1, &vm1) &&
      signed_sub_inplace(&v1, &vinf) &&
      signed_copy(&twice_vinf, &vinf) &&
      signed_mul_u32_inplace(&twice_vinf, 2) &&
      signed_sub_inplace(&v2, &twice_vinf) &&
      signed_sub_inplace(&vm1, &v2);
  }

  if (ok) {
    ok = v0.sign >= 0 && vm1.sign >= 0 && v1.sign >= 0 && v2.sign >= 0 && vinf.sign >= 0;
  }
  if (ok) {
    out->count = 0;
    ok = reserve_limbs(out, left->count + right->count + 4U) &&
      add_shifted_inplace(out, &v0.mag, 0) &&
      add_shifted_inplace(out, &vm1.mag, split) &&
      add_shifted_inplace(out, &v1.mag, split * 2U) &&
      add_shifted_inplace(out, &v2.mag, split * 3U) &&
      add_shifted_inplace(out, &vinf.mag, split * 4U);
    if (ok) normalize(out);
  }

  xray_bigint_clear(&a0);
  xray_bigint_clear(&a1);
  xray_bigint_clear(&a2);
  xray_bigint_clear(&b0);
  xray_bigint_clear(&b1);
  xray_bigint_clear(&b2);
  signed_clear(&x0);
  signed_clear(&x1);
  signed_clear(&xm1);
  signed_clear(&x2);
  signed_clear(&xinf);
  signed_clear(&y0);
  signed_clear(&y1);
  signed_clear(&ym1);
  signed_clear(&y2);
  signed_clear(&yinf);
  signed_clear(&v0);
  signed_clear(&v1);
  signed_clear(&vm1);
  signed_clear(&v2);
  signed_clear(&vinf);
  signed_clear(&twice_vinf);
  return ok;
}

static int mul_dispatch(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right) {
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  size_t left_count = left ? left->count : 0;
  size_t right_count = right ? right->count : 0;
  size_t max_count = left_count > right_count ? left_count : right_count;
  size_t min_count = left_count < right_count ? left_count : right_count;
  if (min_count >= XRAY_BIGINT_UNROLL4_ROUTE_MIN_LIMBS &&
      max_count <= XRAY_BIGINT_UNROLL4_ROUTE_MAX_LIMBS &&
      min_count * 3U >= max_count * 2U) {
    return mul_dispatch_threshold_mode(out, left, right, XRAY_BIGINT_KARATSUBA_THRESHOLD, 1);
  }
#endif
  return mul_dispatch_threshold(out, left, right, XRAY_BIGINT_KARATSUBA_THRESHOLD);
}

int xray_bigint_mul(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right) {
  if (!out || !left || !right) return 0;
  if (out == left || out == right) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = mul_dispatch(&temp, left, right);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return mul_dispatch(out, left, right);
}

int xray_bigint_square(XrayScratchBigInt *out, const XrayScratchBigInt *value) {
  if (!out || !value) return 0;
  if (out == value) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = square_dispatch_threshold(&temp, value, XRAY_BIGINT_KARATSUBA_THRESHOLD);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return square_dispatch_threshold(out, value, XRAY_BIGINT_KARATSUBA_THRESHOLD);
}

typedef int (*XrayBigintBinaryOp)(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right);

static char *decimal_binary_result(const char *left_decimal, const char *right_decimal, XrayBigintBinaryOp op) {
  if (!op) return NULL;
  XrayScratchBigInt left;
  XrayScratchBigInt right;
  XrayScratchBigInt out;
  xray_bigint_init(&left);
  xray_bigint_init(&right);
  xray_bigint_init(&out);

  char *result = NULL;
  if (xray_bigint_set_decimal(&left, left_decimal) &&
      xray_bigint_set_decimal(&right, right_decimal) &&
      op(&out, &left, &right)) {
    result = xray_bigint_get_decimal(&out);
  }

  xray_bigint_clear(&left);
  xray_bigint_clear(&right);
  xray_bigint_clear(&out);
  return result;
}

char *xray_bigint_add_decimal(const char *left_decimal, const char *right_decimal) {
  return decimal_binary_result(left_decimal, right_decimal, xray_bigint_add);
}

char *xray_bigint_sub_decimal(const char *left_decimal, const char *right_decimal) {
  return decimal_binary_result(left_decimal, right_decimal, xray_bigint_sub);
}

char *xray_bigint_mul_decimal(const char *left_decimal, const char *right_decimal) {
  return decimal_binary_result(left_decimal, right_decimal, xray_bigint_mul);
}

char *xray_bigint_square_decimal(const char *decimal) {
  XrayScratchBigInt value;
  XrayScratchBigInt out;
  xray_bigint_init(&value);
  xray_bigint_init(&out);

  char *result = NULL;
  if (xray_bigint_set_decimal(&value, decimal) && xray_bigint_square(&out, &value)) {
    result = xray_bigint_get_decimal(&out);
  }

  xray_bigint_clear(&value);
  xray_bigint_clear(&out);
  return result;
}

int xray_bigint_compare_decimal(const char *left_decimal, const char *right_decimal, int *comparison) {
  if (!comparison) return 0;
  XrayScratchBigInt left;
  XrayScratchBigInt right;
  xray_bigint_init(&left);
  xray_bigint_init(&right);

  int ok = 0;
  if (xray_bigint_set_decimal(&left, left_decimal) &&
      xray_bigint_set_decimal(&right, right_decimal)) {
    *comparison = xray_bigint_compare(&left, &right);
    ok = 1;
  }

  xray_bigint_clear(&left);
  xray_bigint_clear(&right);
  return ok;
}

int xray_bigint_u32_mod_context_init(XrayBigIntU32ModContext *context, uint32_t modulus) {
  if (!context || modulus == 0) return 0;
  context->modulus = modulus;
  context->reciprocal = reciprocal_u32(modulus);
  context->use_fermat_65537 = modulus == XRAY_BIGINT_FERMAT_65537;
  return 1;
}

int xray_bigint_square_karatsuba_probe(XrayScratchBigInt *out, const XrayScratchBigInt *value, size_t threshold) {
  if (!out || !value) return 0;
  size_t active_threshold = threshold >= 2U ? threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
  if (out == value) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = square_dispatch_threshold(&temp, value, active_threshold);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return square_dispatch_threshold(out, value, active_threshold);
}

int xray_bigint_mul_with_threshold(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right, size_t threshold) {
  if (!out || !left || !right) return 0;
  size_t active_threshold = threshold >= 2U ? threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
  if (out == left || out == right) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = mul_dispatch_threshold(&temp, left, right, active_threshold);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return mul_dispatch_threshold(out, left, right, active_threshold);
}

int xray_bigint_mul_karatsuba_sum_probe(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right, size_t threshold) {
  if (!out || !left || !right) return 0;
  size_t active_threshold = threshold >= 2U ? threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
  if (out == left || out == right) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = mul_dispatch_threshold_sum_mode(&temp, left, right, active_threshold, 0);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return mul_dispatch_threshold_sum_mode(out, left, right, active_threshold, 0);
}

int xray_bigint_mul_toom3_probe(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right, size_t leaf_threshold) {
  if (!out || !left || !right) return 0;
  size_t active_threshold = leaf_threshold >= 2U ? leaf_threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
  if (out == left || out == right) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = mul_toom3_probe_internal(&temp, left, right, active_threshold, 0, 1);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return mul_toom3_probe_internal(out, left, right, active_threshold, 0, 1);
}

int xray_bigint_mul_toom3_unroll4_probe(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right, size_t leaf_threshold) {
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  if (!out || !left || !right) return 0;
  size_t active_threshold = leaf_threshold >= 2U ? leaf_threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
  if (out == left || out == right) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = mul_toom3_probe_internal(&temp, left, right, active_threshold, 1, 1);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return mul_toom3_probe_internal(out, left, right, active_threshold, 1, 1);
#else
  (void)out;
  (void)left;
  (void)right;
  (void)leaf_threshold;
  return 0;
#endif
}

int xray_bigint_mul_toom3_unroll4_recursive_probe(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right, size_t leaf_threshold, size_t depth_limit) {
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  if (!out || !left || !right) return 0;
  size_t active_threshold = leaf_threshold >= 2U ? leaf_threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
  size_t active_depth = depth_limit >= 1U ? depth_limit : 1U;
  if (out == left || out == right) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = mul_toom3_probe_internal(&temp, left, right, active_threshold, 1, active_depth);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return mul_toom3_probe_internal(out, left, right, active_threshold, 1, active_depth);
#else
  (void)out;
  (void)left;
  (void)right;
  (void)leaf_threshold;
  (void)depth_limit;
  return 0;
#endif
}

int xray_bigint_mul_unroll4_probe(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right, size_t leaf_threshold) {
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  if (!out || !left || !right) return 0;
  size_t active_threshold = leaf_threshold >= 2U ? leaf_threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
  if (out == left || out == right) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = mul_dispatch_threshold_mode(&temp, left, right, active_threshold, 1);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return mul_dispatch_threshold_mode(out, left, right, active_threshold, 1);
#else
  (void)out;
  (void)left;
  (void)right;
  (void)leaf_threshold;
  return 0;
#endif
}

static uint32_t mod_u32_with_reciprocal(const XrayScratchBigInt *value, uint32_t modulus, uint64_t reciprocal) {
  uint32_t remainder = 0;
  for (size_t remaining = value->count; remaining > 0; --remaining) {
    size_t index = remaining - 1;
    int use_high_half = index + 1 != value->count || (value->limbs[index] >> 32U) != 0;
    divmod_word_u32(remainder, value->limbs[index], modulus, reciprocal, use_high_half, &remainder);
  }
  return remainder;
}

uint32_t xray_bigint_mod_u32(const XrayScratchBigInt *value, uint32_t modulus) {
  if (!value || modulus == 0) return 0;
  if (modulus == XRAY_BIGINT_FERMAT_65537) return mod_65537_folded(value);
  return mod_u32_with_reciprocal(value, modulus, reciprocal_u32(modulus));
}

uint32_t xray_bigint_mod_u32_precomputed(const XrayScratchBigInt *value, const XrayBigIntU32ModContext *context) {
  if (!value || !context || context->modulus == 0) return 0;
  if (context->use_fermat_65537) return mod_65537_folded(value);
  return mod_u32_with_reciprocal(value, context->modulus, context->reciprocal);
}

int xray_bigint_divmod_u32(XrayScratchBigInt *quotient, uint32_t *remainder, const XrayScratchBigInt *value, uint32_t divisor) {
  if (!quotient || !value || divisor == 0) return 0;
  if (!reserve_limbs(quotient, value->count ? value->count : 1)) return 0;
  uint32_t rem = 0;
  uint64_t reciprocal = reciprocal_u32(divisor);
  for (size_t remaining = value->count; remaining > 0; --remaining) {
    size_t index = remaining - 1;
    int use_high_half = index + 1 != value->count || (value->limbs[index] >> 32U) != 0;
    quotient->limbs[index] = divmod_word_u32(rem, value->limbs[index], divisor, reciprocal, use_high_half, &rem);
  }
  quotient->count = value->count;
  normalize(quotient);
  if (remainder) *remainder = rem;
  return 1;
}

static uint32_t gcd_u32(uint32_t a, uint32_t b) {
  if (a == 0) return b;
  if (b == 0) return a;
#if XRAY_BIGINT_HAS_MSVC_UINT128_HELPERS
  unsigned long shift = 0;
  unsigned long a_shift = 0;
  unsigned long b_shift = 0;
  _BitScanForward(&shift, a | b);
  _BitScanForward(&a_shift, a);
  a >>= a_shift;
  do {
    _BitScanForward(&b_shift, b);
    b >>= b_shift;
    if (a > b) {
      uint32_t swap = a;
      a = b;
      b = swap;
    }
    b -= a;
  } while (b);
  return a << shift;
#elif defined(__GNUC__) || defined(__clang__)
  unsigned int shift = (unsigned int)__builtin_ctz(a | b);
  a >>= (unsigned int)__builtin_ctz(a);
  do {
    b >>= (unsigned int)__builtin_ctz(b);
    if (a > b) {
      uint32_t swap = a;
      a = b;
      b = swap;
    }
    b -= a;
  } while (b);
  return a << shift;
#else
  unsigned int shift = 0;
  while (((a | b) & 1U) == 0) {
    a >>= 1U;
    b >>= 1U;
    shift++;
  }
  while ((a & 1U) == 0) a >>= 1U;
  do {
    while ((b & 1U) == 0) b >>= 1U;
    if (a > b) {
      uint32_t swap = a;
      a = b;
      b = swap;
    }
    b -= a;
  } while (b);
  return a << shift;
#endif
}

uint32_t xray_bigint_gcd_u32(const XrayScratchBigInt *value, uint32_t other) {
  if (other == 0) return 0;
  return gcd_u32(xray_bigint_mod_u32(value, other), other);
}

uint32_t xray_bigint_gcd_u32_precomputed(const XrayScratchBigInt *value, const XrayBigIntU32ModContext *context) {
  if (!context || context->modulus == 0) return 0;
  return gcd_u32(xray_bigint_mod_u32_precomputed(value, context), context->modulus);
}

uint32_t xray_bigint_powmod_u32(const XrayScratchBigInt *base, uint32_t exponent, uint32_t modulus) {
  if (!base || modulus == 0) return 0;
  uint64_t result = 1 % modulus;
  uint64_t factor = xray_bigint_mod_u32(base, modulus);
  uint32_t power = exponent;
  while (power) {
    if (power & 1U) result = (result * factor) % modulus;
    factor = (factor * factor) % modulus;
    power >>= 1;
  }
  return (uint32_t)result;
}

uint32_t xray_bigint_powmod_u32_precomputed(const XrayScratchBigInt *base, uint32_t exponent, const XrayBigIntU32ModContext *context) {
  if (!base || !context || context->modulus == 0) return 0;
  uint32_t modulus = context->modulus;
  uint64_t result = 1 % modulus;
  uint64_t factor = xray_bigint_mod_u32_precomputed(base, context);
  uint32_t power = exponent;
  while (power) {
    if (power & 1U) result = (result * factor) % modulus;
    factor = (factor * factor) % modulus;
    power >>= 1;
  }
  return (uint32_t)result;
}
