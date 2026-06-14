#include "xray_workbench.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define XRAY_BIGINT_BASE 1000000000U

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

static int mul_small_inplace(XrayScratchBigInt *value, uint32_t multiplier) {
  if (value->count == 0 || multiplier == 1) return 1;
  if (multiplier == 0) {
    value->count = 0;
    return 1;
  }
  if (!reserve_limbs(value, value->count + 1)) return 0;
  uint64_t carry = 0;
  for (size_t index = 0; index < value->count; ++index) {
    uint64_t product = (uint64_t)value->limbs[index] * multiplier + carry;
    value->limbs[index] = (uint32_t)(product % XRAY_BIGINT_BASE);
    carry = product / XRAY_BIGINT_BASE;
  }
  if (carry) value->limbs[value->count++] = (uint32_t)carry;
  return 1;
}

static int add_small_inplace(XrayScratchBigInt *value, uint32_t addend) {
  if (!reserve_limbs(value, value->count + 1)) return 0;
  uint64_t carry = addend;
  size_t index = 0;
  while (carry) {
    if (index == value->count) value->limbs[value->count++] = 0;
    uint64_t sum = (uint64_t)value->limbs[index] + carry;
    value->limbs[index] = (uint32_t)(sum % XRAY_BIGINT_BASE);
    carry = sum / XRAY_BIGINT_BASE;
    index++;
  }
  return 1;
}

int xray_bigint_set_decimal(XrayScratchBigInt *value, const char *decimal) {
  if (!value || !decimal) return 0;
  value->count = 0;
  int saw_digit = 0;
  for (const unsigned char *p = (const unsigned char *)decimal; *p; ++p) {
    if (*p == ',' || *p == '_' || isspace(*p)) continue;
    if (!isdigit(*p)) return 0;
    saw_digit = 1;
    if (!mul_small_inplace(value, 10)) return 0;
    if (!add_small_inplace(value, (uint32_t)(*p - '0'))) return 0;
  }
  if (!saw_digit) return 0;
  normalize(value);
  return 1;
}

char *xray_bigint_get_decimal(const XrayScratchBigInt *value) {
  if (!value || value->count == 0) {
    char *zero = (char *)calloc(2, 1);
    if (zero) zero[0] = '0';
    return zero;
  }
  size_t capacity = value->count * 10 + 1;
  char *text = (char *)calloc(capacity, 1);
  if (!text) return NULL;
  int written = snprintf(text, capacity, "%u", value->limbs[value->count - 1]);
  size_t used = written > 0 ? (size_t)written : 0;
  for (size_t index = value->count - 1; index-- > 0;) {
    written = snprintf(text + used, capacity - used, "%09u", value->limbs[index]);
    used += written > 0 ? (size_t)written : 0;
  }
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
  size_t max_count = left->count > right->count ? left->count : right->count;
  if (!reserve_limbs(out, max_count + 1)) return 0;
  uint64_t carry = 0;
  for (size_t index = 0; index < max_count; ++index) {
    uint64_t sum = carry;
    if (index < left->count) sum += left->limbs[index];
    if (index < right->count) sum += right->limbs[index];
    out->limbs[index] = (uint32_t)(sum % XRAY_BIGINT_BASE);
    carry = sum / XRAY_BIGINT_BASE;
  }
  out->count = max_count;
  if (carry) out->limbs[out->count++] = (uint32_t)carry;
  normalize(out);
  return 1;
}

int xray_bigint_mul(XrayScratchBigInt *out, const XrayScratchBigInt *left, const XrayScratchBigInt *right) {
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
      out->limbs[i + j] = (uint32_t)(current % XRAY_BIGINT_BASE);
      carry = current / XRAY_BIGINT_BASE;
    }
    size_t pos = i + right->count;
    while (carry) {
      uint64_t current = out->limbs[pos] + carry;
      out->limbs[pos] = (uint32_t)(current % XRAY_BIGINT_BASE);
      carry = current / XRAY_BIGINT_BASE;
      pos++;
      if (pos >= out->count && carry) {
        if (!reserve_limbs(out, out->count + 1)) return 0;
        out->limbs[out->count++] = 0;
      }
    }
  }
  normalize(out);
  return 1;
}
