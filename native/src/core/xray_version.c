#include "xray_workbench.h"

#ifndef XRAY_GMP_BACKEND_NAME
#define XRAY_GMP_BACKEND_NAME "GMP/MPIR"
#endif

#ifndef XRAY_GMP_BACKEND_LIBRARY
#define XRAY_GMP_BACKEND_LIBRARY "unknown"
#endif

#define XRAY_STRINGIFY_IMPL(value) #value
#define XRAY_STRINGIFY(value) XRAY_STRINGIFY_IMPL(value)

const char *xray_version(void) {
  return XRAY_VERSION;
}

unsigned int xray_abi_version(void) {
  return XRAY_ABI_VERSION;
}

const char *xray_bignum_backend_name(void) {
  return XRAY_GMP_BACKEND_NAME;
}

const char *xray_bignum_backend_version(void) {
#if defined(_MSC_MPIR_VERSION)
  return _MSC_MPIR_VERSION;
#elif defined(__MPIR_VERSION)
  return XRAY_STRINGIFY(__MPIR_VERSION) "." XRAY_STRINGIFY(__MPIR_VERSION_MINOR) "." XRAY_STRINGIFY(__MPIR_VERSION_PATCHLEVEL);
#elif defined(GMP_VERSION)
  return GMP_VERSION;
#else
  return "unknown";
#endif
}

const char *xray_bignum_backend_library(void) {
  return XRAY_GMP_BACKEND_LIBRARY;
}
