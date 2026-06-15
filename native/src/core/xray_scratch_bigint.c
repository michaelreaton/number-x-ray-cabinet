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
  uint64_t carry = 0;
  size_t index = 0;
  for (; index < shorter->count; ++index) {
    uint64_t sum = left->limbs[index] + right->limbs[index];
    uint64_t carry_from_sum = sum < left->limbs[index];
    uint64_t with_carry = sum + carry;
    out->limbs[index] = with_carry;
    carry = carry_from_sum || (with_carry < sum);
  }
  for (; index < longer->count; ++index) {
    uint64_t sum = longer->limbs[index] + carry;
    out->limbs[index] = sum;
    carry = sum < longer->limbs[index];
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
  uint64_t borrow = 0;
  size_t index = 0;
  for (; index < right->count; ++index) {
    uint64_t lhs = left->limbs[index];
    uint64_t rhs = right->limbs[index] + borrow;
    uint64_t borrow_from_add = rhs < right->limbs[index];
    out->limbs[index] = lhs - rhs;
    borrow = borrow_from_add || (lhs < rhs);
  }
  for (; index < left->count; ++index) {
    uint64_t lhs = left->limbs[index];
    out->limbs[index] = lhs - borrow;
    borrow = lhs < borrow;
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
    carry = mul_add_word(0, outer->limbs[0], inner->limbs[j], carry, &out->limbs[j]);
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

int xray_bigint_mul(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right) {
  if (!out || !left || !right) return 0;
  if (out == left || out == right) {
    XrayScratchBigInt temp;
    xray_bigint_init(&temp);
    int ok = mul_schoolbook(&temp, left, right);
    if (ok) ok = xray_bigint_copy(out, &temp);
    xray_bigint_clear(&temp);
    return ok;
  }
  return mul_schoolbook(out, left, right);
}

uint32_t xray_bigint_mod_u32(const XrayScratchBigInt *value, uint32_t modulus) {
  if (!value || modulus == 0) return 0;
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
  while (b) {
    uint32_t next = a % b;
    a = b;
    b = next;
  }
  return a;
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
