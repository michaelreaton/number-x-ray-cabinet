#include "xray_workbench.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef XRAY_GMP_BACKEND_NAME
#define XRAY_GMP_BACKEND_NAME "GMP/MPIR"
#endif

#ifndef XRAY_GMP_BACKEND_LIBRARY
#define XRAY_GMP_BACKEND_LIBRARY "unknown"
#endif

#ifndef XRAY_CMAKE_BUILD_CONFIG
#define XRAY_CMAKE_BUILD_CONFIG "unknown"
#endif

#ifndef XRAY_CMAKE_GENERATOR
#define XRAY_CMAKE_GENERATOR "unknown"
#endif

#ifndef XRAY_CMAKE_GENERATOR_PLATFORM
#define XRAY_CMAKE_GENERATOR_PLATFORM ""
#endif

#ifndef XRAY_CMAKE_GENERATOR_TOOLSET
#define XRAY_CMAKE_GENERATOR_TOOLSET ""
#endif

#ifndef XRAY_CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE
#define XRAY_CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE 0
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

static void build_copy(char *out, size_t out_size, const char *value) {
  if (!out || out_size == 0) return;
  snprintf(out, out_size, "%s", value && value[0] ? value : "unknown");
}

void xray_build_info_detect(XrayBuildInfo *info) {
  if (!info) return;
  memset(info, 0, sizeof(*info));

#if defined(_MSC_VER)
  build_copy(info->compiler, sizeof(info->compiler), "MSVC");
#if defined(_MSC_FULL_VER)
  snprintf(info->compiler_version, sizeof(info->compiler_version), "%d full=%d", _MSC_VER, _MSC_FULL_VER);
#else
  snprintf(info->compiler_version, sizeof(info->compiler_version), "%d", _MSC_VER);
#endif
  info->msvc = 1;
#elif defined(__clang__)
  build_copy(info->compiler, sizeof(info->compiler), "Clang");
  build_copy(info->compiler_version, sizeof(info->compiler_version), __clang_version__);
  info->clang = 1;
#elif defined(__GNUC__)
  build_copy(info->compiler, sizeof(info->compiler), "GCC");
  build_copy(info->compiler_version, sizeof(info->compiler_version), __VERSION__);
  info->gcc = 1;
#else
  build_copy(info->compiler, sizeof(info->compiler), "unknown");
  build_copy(info->compiler_version, sizeof(info->compiler_version), "unknown");
#endif

  build_copy(info->build_config, sizeof(info->build_config), XRAY_CMAKE_BUILD_CONFIG);
  build_copy(info->cmake_generator, sizeof(info->cmake_generator), XRAY_CMAKE_GENERATOR);
  build_copy(info->cmake_generator_platform, sizeof(info->cmake_generator_platform), XRAY_CMAKE_GENERATOR_PLATFORM);
  build_copy(info->cmake_generator_toolset, sizeof(info->cmake_generator_toolset), XRAY_CMAKE_GENERATOR_TOOLSET);
  info->interprocedural_optimization = XRAY_CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ? 1 : 0;

#if defined(_DEBUG)
  info->debug_build = 1;
#endif
#if defined(NDEBUG)
  info->ndebug_build = 1;
#endif
#if defined(__AVX__)
  info->compile_target_avx = 1;
#endif
#if defined(__AVX2__)
  info->compile_target_avx2 = 1;
#endif
#if defined(__AVX512F__)
  info->compile_target_avx512f = 1;
#endif
}

char *xray_build_info_summary(const XrayBuildInfo *info) {
  XrayBuildInfo detected;
  const XrayBuildInfo *build = info;
  if (!build) {
    xray_build_info_detect(&detected);
    build = &detected;
  }
  char *summary = (char *)calloc(512, 1);
  if (!summary) return NULL;
  snprintf(summary, 512,
    "Build: compiler=%s %s | config=%s | generator=%s | platform=%s | toolset=%s | ipo=%s | ndebug=%s | target=%s%s%s",
    build->compiler,
    build->compiler_version,
    build->build_config,
    build->cmake_generator,
    build->cmake_generator_platform,
    build->cmake_generator_toolset,
    build->interprocedural_optimization ? "on" : "off",
    build->ndebug_build ? "yes" : "no",
    build->compile_target_avx ? "AVX" : "scalar",
    build->compile_target_avx2 ? "+AVX2" : "",
    build->compile_target_avx512f ? "+AVX512F" : "");
  return summary;
}
