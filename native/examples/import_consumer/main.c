#include "number_xray.h"

#include <stdio.h>
#include <string.h>

int main(void) {
  XrayScratchBigInt value;
  XrayScratchBigInt one;
  XrayScratchBigInt sum;
  xray_bigint_init(&value);
  xray_bigint_init(&one);
  xray_bigint_init(&sum);
  XrayBigIntRouteConfig route = xray_bigint_route_config();
  XrayBigIntU32ModContext mod_context;

  int ok = strcmp(NUMBER_XRAY_VERSION, XRAY_VERSION) == 0 &&
    strcmp(xray_version(), NUMBER_XRAY_VERSION) == 0 &&
    xray_abi_version() == NUMBER_XRAY_ABI_VERSION &&
    xray_bignum_backend_name() &&
    xray_bignum_backend_name()[0] &&
    xray_bignum_backend_version() &&
    xray_bignum_backend_version()[0] &&
    xray_bignum_backend_library() &&
    xray_bignum_backend_library()[0] &&
    route.word_bits == 64u &&
    route.karatsuba_threshold_limbs > 0 &&
    route.decimal_horner_min_limbs > 0 &&
    route.mul_unroll4_route_min_limbs <= route.mul_unroll4_route_max_limbs &&
    (!route.mul_unroll4_route_enabled || route.msvc_uint128_helpers) &&
    xray_bigint_set_decimal(&value, "10,000_000 000,000_000 000") &&
    xray_bigint_set_decimal(&one, "1") &&
    xray_bigint_add(&sum, &value, &one) &&
    xray_bigint_u32_mod_context_init(&mod_context, 1000000007U) &&
    xray_bigint_mod_u32_precomputed(&value, &mod_context) == xray_bigint_mod_u32(&value, 1000000007U) &&
    xray_bigint_gcd_u32_precomputed(&value, &mod_context) == xray_bigint_gcd_u32(&value, 1000000007U) &&
    xray_bigint_powmod_u32_precomputed(&value, 65537U, &mod_context) == xray_bigint_powmod_u32(&value, 65537U, 1000000007U);

  char *text = ok ? xray_bigint_get_decimal(&sum) : NULL;
  ok = ok && text && strcmp(text, "10000000000000000001") == 0;
  char *ffi_sum = ok ? xray_bigint_add_decimal("10,000_000 000,000_000 000", "1") : NULL;
  ok = ok && ffi_sum && strcmp(ffi_sum, "10000000000000000001") == 0;
  char *factor_json = ok ? xray_factor_solve_json("10_403") : NULL;
  ok = ok &&
    factor_json &&
    strstr(factor_json, "\"status\":\"solved\"") &&
    strstr(factor_json, "\"productVerified\":true");

  if (ok) {
    printf("NumberXRay::core import ok: %s\n", ffi_sum);
  } else {
    fprintf(stderr, "NumberXRay::core import smoke failed\n");
  }

  xray_free(text);
  xray_free(ffi_sum);
  xray_free(factor_json);
  xray_bigint_clear(&value);
  xray_bigint_clear(&one);
  xray_bigint_clear(&sum);
  return ok ? 0 : 1;
}
