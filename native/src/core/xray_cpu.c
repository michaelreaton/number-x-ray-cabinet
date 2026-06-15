#include "xray_workbench.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable:5105)
#endif
#include <windows.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
#else
#include <unistd.h>
#endif

#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
#include <intrin.h>
#define XRAY_CPU_X86 1
#elif defined(__i386__) || defined(__x86_64__)
#include <cpuid.h>
#define XRAY_CPU_X86 1
#else
#define XRAY_CPU_X86 0
#endif

static void set_architecture(XrayCpuFeatures *features) {
#if defined(_M_X64) || defined(__x86_64__)
  snprintf(features->architecture, sizeof(features->architecture), "x86_64");
#elif defined(_M_IX86) || defined(__i386__)
  snprintf(features->architecture, sizeof(features->architecture), "x86");
#elif defined(_M_ARM64) || defined(__aarch64__)
  snprintf(features->architecture, sizeof(features->architecture), "arm64");
#elif defined(_M_ARM) || defined(__arm__)
  snprintf(features->architecture, sizeof(features->architecture), "arm");
#else
  snprintf(features->architecture, sizeof(features->architecture), "unknown");
#endif
}

static unsigned int logical_cpu_count(void) {
#if defined(_WIN32)
  SYSTEM_INFO info;
  GetNativeSystemInfo(&info);
  return info.dwNumberOfProcessors ? (unsigned int)info.dwNumberOfProcessors : 1U;
#elif defined(_SC_NPROCESSORS_ONLN)
  long count = sysconf(_SC_NPROCESSORS_ONLN);
  return count > 0 ? (unsigned int)count : 1U;
#else
  return 1U;
#endif
}

#if XRAY_CPU_X86
static int cpuid_leaf(unsigned int leaf, unsigned int subleaf, unsigned int regs[4]) {
#if defined(_MSC_VER)
  int values[4] = {0, 0, 0, 0};
  __cpuidex(values, (int)leaf, (int)subleaf);
  regs[0] = (unsigned int)values[0];
  regs[1] = (unsigned int)values[1];
  regs[2] = (unsigned int)values[2];
  regs[3] = (unsigned int)values[3];
  return 1;
#else
  return __get_cpuid_count(leaf, subleaf, &regs[0], &regs[1], &regs[2], &regs[3]);
#endif
}

static unsigned long long read_xcr0(void) {
#if defined(_MSC_VER)
  return (unsigned long long)_xgetbv(0);
#else
  unsigned int eax = 0;
  unsigned int edx = 0;
  __asm__ volatile(".byte 0x0f, 0x01, 0xd0" : "=a"(eax), "=d"(edx) : "c"(0));
  return ((unsigned long long)edx << 32) | (unsigned long long)eax;
#endif
}
#endif

void xray_cpu_features_detect(XrayCpuFeatures *features) {
  if (!features) return;
  memset(features, 0, sizeof(*features));
  set_architecture(features);
  features->logical_cpus = logical_cpu_count();

#if XRAY_CPU_X86
  unsigned int regs[4] = {0, 0, 0, 0};
  if (!cpuid_leaf(0, 0, regs)) return;
  unsigned int max_leaf = regs[0];
  features->cpuid_supported = 1;
  memcpy(features->vendor + 0, &regs[1], 4);
  memcpy(features->vendor + 4, &regs[3], 4);
  memcpy(features->vendor + 8, &regs[2], 4);
  features->vendor[12] = '\0';

  if (max_leaf >= 1 && cpuid_leaf(1, 0, regs)) {
    unsigned int ecx = regs[2];
    unsigned int edx = regs[3];
    features->sse2 = (edx & (1U << 26)) != 0;
    features->sse3 = (ecx & (1U << 0)) != 0;
    features->pclmulqdq = (ecx & (1U << 1)) != 0;
    features->ssse3 = (ecx & (1U << 9)) != 0;
    features->fma = (ecx & (1U << 12)) != 0;
    features->sse41 = (ecx & (1U << 19)) != 0;
    features->sse42 = (ecx & (1U << 20)) != 0;
    features->popcnt = (ecx & (1U << 23)) != 0;
    features->aes = (ecx & (1U << 25)) != 0;
    features->xsave = (ecx & (1U << 26)) != 0;
    features->osxsave = (ecx & (1U << 27)) != 0;
    if (features->osxsave) {
      features->xcr0 = read_xcr0();
      features->avx_os_enabled = (features->xcr0 & 0x6ULL) == 0x6ULL;
      features->avx512_os_enabled = (features->xcr0 & 0xe6ULL) == 0xe6ULL;
    }
    features->avx = ((ecx & (1U << 28)) != 0) && features->avx_os_enabled;
  }

  if (max_leaf >= 7 && cpuid_leaf(7, 0, regs)) {
    unsigned int ebx = regs[1];
    unsigned int ecx = regs[2];
    features->bmi1 = (ebx & (1U << 3)) != 0;
    features->avx2 = ((ebx & (1U << 5)) != 0) && features->avx_os_enabled;
    features->bmi2 = (ebx & (1U << 8)) != 0;
    features->avx512f = ((ebx & (1U << 16)) != 0) && features->avx512_os_enabled;
    features->avx512dq = ((ebx & (1U << 17)) != 0) && features->avx512_os_enabled;
    features->adx = (ebx & (1U << 19)) != 0;
    features->avx512ifma = ((ebx & (1U << 21)) != 0) && features->avx512_os_enabled;
    features->avx512bw = ((ebx & (1U << 30)) != 0) && features->avx512_os_enabled;
    features->avx512vl = ((ebx & (1U << 31)) != 0) && features->avx512_os_enabled;
    features->vaes = ((ecx & (1U << 9)) != 0) && features->avx_os_enabled;
    features->vpclmulqdq = ((ecx & (1U << 10)) != 0) && features->avx_os_enabled;
  }

  if (cpuid_leaf(0x80000000U, 0, regs)) {
    unsigned int max_extended = regs[0];
    if (max_extended >= 0x80000004U) {
      char brand[49];
      memset(brand, 0, sizeof(brand));
      for (unsigned int leaf = 0; leaf < 3; ++leaf) {
        if (cpuid_leaf(0x80000002U + leaf, 0, regs)) {
          memcpy(brand + leaf * 16U + 0U, &regs[0], 4);
          memcpy(brand + leaf * 16U + 4U, &regs[1], 4);
          memcpy(brand + leaf * 16U + 8U, &regs[2], 4);
          memcpy(brand + leaf * 16U + 12U, &regs[3], 4);
        }
      }
      char *start = brand;
      while (*start == ' ') start++;
      snprintf(features->brand, sizeof(features->brand), "%s", start);
    }
  }
#endif

  if (!features->vendor[0]) snprintf(features->vendor, sizeof(features->vendor), "unknown");
  if (!features->brand[0]) snprintf(features->brand, sizeof(features->brand), "%s processor", features->architecture);
}

static void append_feature(char *buffer, size_t size, const char *feature, int enabled) {
  if (!enabled || !buffer || !feature) return;
  size_t used = strlen(buffer);
  if (used + strlen(feature) + 2 >= size) return;
  if (used) snprintf(buffer + used, size - used, " %s", feature);
  else snprintf(buffer + used, size - used, "%s", feature);
}

char *xray_cpu_features_summary(const XrayCpuFeatures *features) {
  if (!features) return NULL;
  char flags[256] = {0};
  append_feature(flags, sizeof(flags), "SSE2", features->sse2);
  append_feature(flags, sizeof(flags), "SSE4.2", features->sse42);
  append_feature(flags, sizeof(flags), "POPCNT", features->popcnt);
  append_feature(flags, sizeof(flags), "AES", features->aes);
  append_feature(flags, sizeof(flags), "FMA", features->fma);
  append_feature(flags, sizeof(flags), "AVX", features->avx);
  append_feature(flags, sizeof(flags), "AVX2", features->avx2);
  append_feature(flags, sizeof(flags), "AVX512F", features->avx512f);
  append_feature(flags, sizeof(flags), "AVX512BW", features->avx512bw);
  append_feature(flags, sizeof(flags), "AVX512VL", features->avx512vl);
  append_feature(flags, sizeof(flags), "BMI1", features->bmi1);
  append_feature(flags, sizeof(flags), "BMI2", features->bmi2);
  append_feature(flags, sizeof(flags), "ADX", features->adx);
  if (!flags[0]) snprintf(flags, sizeof(flags), "portable-scalar");

  char *summary = (char *)calloc(640, 1);
  if (!summary) return NULL;
  snprintf(summary, 640,
    "CPU: %s | vendor=%s | arch=%s | logical=%u | xcr0=0x%llx | flags=%s",
    features->brand,
    features->vendor,
    features->architecture,
    features->logical_cpus,
    (unsigned long long)features->xcr0,
    flags);
  return summary;
}
