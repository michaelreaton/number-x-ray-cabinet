#include "xray_workbench.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define XRAY_BIGINT_WORD_BITS 32U
#define XRAY_BIGINT_DECIMAL_CHUNK_BASE 1000000000U
#define XRAY_BIGINT_DECIMAL_CHUNK_DIGITS 9U

static const uint32_t decimal_powers[] = {
  1U,
  10U,
  100U,
  1000U,
  10000U,
  100000U,
  1000000U,
  10000000U,
  100000000U,
  1000000000U
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
  uint32_t *next = (uint32_t *)realloc(value->limbs, sizeof(uint32_t) * next_capacity);
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
  value->limbs[0] = small;
  value->count = small ? 1 : 0;
  return 1;
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
  if (value->count) memcpy(out->limbs, value->limbs, sizeof(uint32_t) * value->count);
  out->count = value->count;
  return 1;
}

static int mul_add_small_inplace(XrayScratchBigInt *value, uint32_t multiplier, uint32_t addend) {
  if (value->count == 0 || multiplier == 0) return set_u32(value, addend);
  if (!reserve_limbs(value, value->count + 1)) return 0;
  uint64_t carry = addend;
  for (size_t index = 0; index < value->count; ++index) {
    uint64_t product = (uint64_t)value->limbs[index] * multiplier + carry;
    value->limbs[index] = (uint32_t)product;
    carry = product >> XRAY_BIGINT_WORD_BITS;
  }
  if (carry) value->limbs[value->count++] = (uint32_t)carry;
  return 1;
}

int xray_bigint_set_decimal(XrayScratchBigInt *value, const char *decimal) {
  if (!value || !decimal) return 0;
  value->count = 0;
  size_t digit_count = 0;
  uint32_t chunk = 0;
  unsigned int chunk_digits = 0;
  for (const unsigned char *p = (const unsigned char *)decimal; *p; ++p) {
    if (*p == ',' || *p == '_' || isspace(*p)) continue;
    if (!isdigit(*p)) return 0;
    digit_count++;
    chunk = chunk * 10U + (uint32_t)(*p - '0');
    chunk_digits++;
    if (chunk_digits == XRAY_BIGINT_DECIMAL_CHUNK_DIGITS) {
      if (!mul_add_small_inplace(value, XRAY_BIGINT_DECIMAL_CHUNK_BASE, chunk)) return 0;
      chunk = 0;
      chunk_digits = 0;
    }
  }
  if (!digit_count) return 0;
  if (chunk_digits) {
    if (!mul_add_small_inplace(value, decimal_powers[chunk_digits], chunk)) return 0;
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
    uint64_t sum = (uint64_t)left->limbs[index] + right->limbs[index] + carry;
    out->limbs[index] = (uint32_t)sum;
    carry = sum >> XRAY_BIGINT_WORD_BITS;
  }
  for (; index < longer->count; ++index) {
    uint64_t sum = (uint64_t)longer->limbs[index] + carry;
    out->limbs[index] = (uint32_t)sum;
    carry = sum >> XRAY_BIGINT_WORD_BITS;
  }
  out->count = longer->count;
  if (carry) out->limbs[out->count++] = (uint32_t)carry;
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
    uint64_t rhs = (uint64_t)right->limbs[index] + borrow;
    out->limbs[index] = (uint32_t)(lhs - rhs);
    borrow = lhs < rhs;
  }
  for (; index < left->count; ++index) {
    uint64_t lhs = left->limbs[index];
    out->limbs[index] = (uint32_t)(lhs - borrow);
    borrow = lhs < borrow;
  }
  out->count = left->count;
  normalize(out);
  return borrow == 0;
}

static int mul_schoolbook(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right) {
  if (!out || !left || !right) return 0;
  if (left->count == 0 || right->count == 0) return set_u32(out, 0);
  size_t needed = left->count + right->count;
  if (!reserve_limbs(out, needed)) return 0;
  memset(out->limbs, 0, sizeof(uint32_t) * needed);
  out->count = needed;
  for (size_t i = 0; i < left->count; ++i) {
    uint64_t carry = 0;
    for (size_t j = 0; j < right->count; ++j) {
      uint64_t current = out->limbs[i + j] + (uint64_t)left->limbs[i] * right->limbs[j] + carry;
      out->limbs[i + j] = (uint32_t)current;
      carry = current >> XRAY_BIGINT_WORD_BITS;
    }
    size_t pos = i + right->count;
    while (carry) {
      uint64_t current = out->limbs[pos] + carry;
      out->limbs[pos] = (uint32_t)current;
      carry = current >> XRAY_BIGINT_WORD_BITS;
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
  uint64_t remainder = 0;
  for (size_t index = value->count; index-- > 0;) {
    remainder = ((remainder << XRAY_BIGINT_WORD_BITS) | value->limbs[index]) % modulus;
  }
  return (uint32_t)remainder;
}

int xray_bigint_divmod_u32(XrayScratchBigInt *quotient, uint32_t *remainder, const XrayScratchBigInt *value, uint32_t divisor) {
  if (!quotient || !value || divisor == 0) return 0;
  if (!reserve_limbs(quotient, value->count ? value->count : 1)) return 0;
  uint64_t rem = 0;
  for (size_t index = value->count; index-- > 0;) {
    uint64_t current = (rem << XRAY_BIGINT_WORD_BITS) | value->limbs[index];
    quotient->limbs[index] = (uint32_t)(current / divisor);
    rem = current % divisor;
  }
  quotient->count = value->count;
  normalize(quotient);
  if (remainder) *remainder = (uint32_t)rem;
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
