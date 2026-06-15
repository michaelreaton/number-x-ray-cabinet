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
/* UINT64_MAX / 1e9, matching reciprocal_u32 for the decimal chunk divisor. */
#define XRAY_BIGINT_DECIMAL_CHUNK_RECIPROCAL UINT64_C(18446744073)
#define XRAY_BIGINT_DECIMAL_HORNER_MIN_LIMBS 64U
#define XRAY_BIGINT_PARSE_CHUNK_BASE UINT64_C(10000000000000000000)
#define XRAY_BIGINT_PARSE_CHUNK_DIGITS 19U
#define XRAY_BIGINT_KARATSUBA_THRESHOLD 64U
#define XRAY_BIGINT_UNROLL4_ROUTE_MIN_LIMBS 8U
#define XRAY_BIGINT_UNROLL4_ROUTE_MAX_LIMBS 512U
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
  size_t next_capacity = *capacity ? *capacity * 2U : 8U;
  while (next_capacity < needed) next_capacity *= 2U;
  uint32_t *next = (uint32_t *)realloc(*chunks, sizeof(uint32_t) * next_capacity);
  if (!next) return 0;
  *chunks = next;
  *capacity = next_capacity;
  return 1;
}

static int append_decimal_chunk(uint32_t **chunks, size_t *count, size_t *capacity, uint32_t chunk) {
  if (!reserve_decimal_chunks(chunks, capacity, *count + 1U)) return 0;
  (*chunks)[(*count)++] = chunk;
  return 1;
}

static int decimal_chunks_from_limbs_horner(uint32_t **chunks_out, size_t *chunk_count_out, const XrayScratchBigInt *value) {
  uint32_t *chunks = NULL;
  size_t chunk_count = 0;
  size_t chunk_capacity = 0;
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

static void write_u32_decimal_padded9(char *out, uint32_t value) {
  for (size_t index = XRAY_BIGINT_DECIMAL_CHUNK_DIGITS; index-- > 0;) {
    out[index] = (char)('0' + (value % 10U));
    value /= 10U;
  }
}

int xray_bigint_set_decimal(XrayScratchBigInt *value, const char *decimal) {
  if (!value || !decimal) return 0;
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
    if (chunk_digits == XRAY_BIGINT_PARSE_CHUNK_DIGITS) {
      if (!mul_add_small_inplace(value, XRAY_BIGINT_PARSE_CHUNK_BASE, chunk)) return 0;
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

char *xray_bigint_get_decimal(const XrayScratchBigInt *value) {
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
  if (value->count >= XRAY_BIGINT_DECIMAL_HORNER_MIN_LIMBS) {
    if (!decimal_chunks_from_limbs_horner(&chunks, &chunk_count, value)) {
      xray_bigint_clear(&copy);
      return NULL;
    }
  } else {
    if (!xray_bigint_copy(&copy, value)) {
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

  size_t capacity = chunk_count * XRAY_BIGINT_DECIMAL_CHUNK_DIGITS + 1;
  char *text = (char *)calloc(capacity, 1);
  if (!text) {
    free(chunks);
    return NULL;
  }
  size_t used = write_u32_decimal(text, chunks[chunk_count - 1]);
  for (size_t index = chunk_count - 1; index-- > 0;) {
    write_u32_decimal_padded9(text + used, chunks[index]);
    used += XRAY_BIGINT_DECIMAL_CHUNK_DIGITS;
  }
  free(chunks);
  return text;
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
static int square_dispatch_threshold(XrayScratchBigInt *out, const XrayScratchBigInt *value, size_t threshold);

static int square_karatsuba_threshold(XrayScratchBigInt *out, const XrayScratchBigInt *value, size_t threshold) {
  if (!out || !value) return 0;
  if (value->count == 0) return set_u32(out, 0);
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

uint32_t xray_bigint_mod_u32(const XrayScratchBigInt *value, uint32_t modulus) {
  if (!value || modulus == 0) return 0;
  if (modulus == XRAY_BIGINT_FERMAT_65537) return mod_65537_folded(value);
  uint32_t remainder = 0;
  uint64_t reciprocal = reciprocal_u32(modulus);
  for (size_t remaining = value->count; remaining > 0; --remaining) {
    size_t index = remaining - 1;
    int use_high_half = index + 1 != value->count || (value->limbs[index] >> 32U) != 0;
    divmod_word_u32(remainder, value->limbs[index], modulus, reciprocal, use_high_half, &remainder);
  }
  return remainder;
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
