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
#define XRAY_BIGINT_PARSE_CHUNK_BASE UINT64_C(10000000000000000000)
#define XRAY_BIGINT_PARSE_CHUNK_DIGITS 19U
#define XRAY_BIGINT_KARATSUBA_THRESHOLD 64U
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
  if (!xray_bigint_copy(&copy, value)) return NULL;

  uint32_t *chunks = NULL;
  size_t chunk_count = 0;
  size_t chunk_capacity = 0;
  while (copy.count > 0) {
    if (chunk_count == chunk_capacity) {
      size_t next_capacity = chunk_capacity ? chunk_capacity * 2 : 8;
      uint32_t *next = (uint32_t *)realloc(chunks, sizeof(uint32_t) * next_capacity);
      if (!next) {
        free(chunks);
        xray_bigint_clear(&copy);
        return NULL;
      }
      chunks = next;
      chunk_capacity = next_capacity;
    }
    uint32_t remainder = 0;
    if (!xray_bigint_divmod_u32(&copy, &remainder, &copy, XRAY_BIGINT_DECIMAL_CHUNK_BASE)) {
      free(chunks);
      xray_bigint_clear(&copy);
      return NULL;
    }
    chunks[chunk_count++] = remainder;
  }
  xray_bigint_clear(&copy);

  size_t capacity = chunk_count * XRAY_BIGINT_DECIMAL_CHUNK_DIGITS + 1;
  char *text = (char *)calloc(capacity, 1);
  if (!text) {
    free(chunks);
    return NULL;
  }
  int written = snprintf(text, capacity, "%u", chunks[chunk_count - 1]);
  size_t used = written > 0 ? (size_t)written : 0;
  for (size_t index = chunk_count - 1; index-- > 0;) {
    written = snprintf(text + used, capacity - used, "%09u", chunks[index]);
    used += written > 0 ? (size_t)written : 0;
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
  for (; index < left->count; ++index) {
    borrow = sub_with_borrow_u64(left->limbs[index], 0, borrow, &out->limbs[index]);
  }
  out->count = left->count;
  normalize(out);
  return borrow == 0;
}

static int mul_schoolbook(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right) {
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
    for (size_t j = 0; j < inner->count; ++j) {
      carry = mul_add_word(out->limbs[i + j], outer->limbs[i], inner->limbs[j], carry, &out->limbs[i + j]);
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

static int mul_dispatch_threshold(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right, size_t threshold);

static int mul_karatsuba_threshold(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right, size_t threshold) {
  size_t left_count = left ? left->count : 0;
  size_t right_count = right ? right->count : 0;
  size_t max_count = left_count > right_count ? left_count : right_count;
  size_t min_count = left_count < right_count ? left_count : right_count;
  size_t active_threshold = threshold >= 2U ? threshold : XRAY_BIGINT_KARATSUBA_THRESHOLD;
  if (max_count < active_threshold || min_count * 2U < max_count) {
    return mul_schoolbook(out, left, right);
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
    mul_dispatch_threshold(&z0, &a0, &b0, active_threshold) &&
    mul_dispatch_threshold(&z2, &a1, &b1, active_threshold) &&
    abs_diff_bigint(&sum_a, &a1, &a0, &a_order) &&
    abs_diff_bigint(&sum_b, &b1, &b0, &b_order) &&
    mul_dispatch_threshold(&z1, &sum_a, &sum_b, active_threshold) &&
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

static int mul_dispatch_threshold(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right, size_t threshold) {
  return mul_karatsuba_threshold(out, left, right, threshold);
}

static int mul_dispatch(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right) {
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
