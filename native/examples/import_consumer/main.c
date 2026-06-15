#include "number_xray.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
  XrayScratchBigInt value;
  XrayScratchBigInt one;
  XrayScratchBigInt sum;
  xray_bigint_init(&value);
  xray_bigint_init(&one);
  xray_bigint_init(&sum);

  int ok = strcmp(NUMBER_XRAY_VERSION, XRAY_VERSION) == 0 &&
    xray_bigint_set_decimal(&value, "10,000_000 000,000_000 000") &&
    xray_bigint_set_decimal(&one, "1") &&
    xray_bigint_add(&sum, &value, &one);

  char *text = ok ? xray_bigint_get_decimal(&sum) : NULL;
  ok = ok && text && strcmp(text, "10000000000000000001") == 0;

  if (ok) {
    printf("NumberXRay::core import ok: %s\n", text);
  } else {
    fprintf(stderr, "NumberXRay::core import smoke failed\n");
  }

  free(text);
  xray_bigint_clear(&value);
  xray_bigint_clear(&one);
  xray_bigint_clear(&sum);
  return ok ? 0 : 1;
}
