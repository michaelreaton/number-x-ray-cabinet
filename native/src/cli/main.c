#include "xray_workbench.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *rsa260(void) {
  return "22112825529529666435281085255026230927612089502470015394413748319128822941402001986512729726569746599085900330031400051170742204560859276357953757185954298838958709229238491006703034124620545784566413664540684214361293017694020846391065875914794251435144458199";
}

static void usage(const char *argv0) {
  fprintf(stderr, "Usage: %s [--bench] [--rsa260] [integer]\n", argv0);
}

int main(int argc, char **argv) {
  const char *input = "10403";
  int run_bench = 0;

  for (int index = 1; index < argc; ++index) {
    if (strcmp(argv[index], "--bench") == 0) run_bench = 1;
    else if (strcmp(argv[index], "--rsa260") == 0) input = rsa260();
    else if (argv[index][0] == '-') {
      usage(argv[0]);
      return 2;
    } else {
      input = argv[index];
    }
  }

  XrayFactorConfig factor_config = xray_factor_default_config();
  XrayCyclotomicConfig cyclo_config = xray_cyclotomic_default_config();
  if (strcmp(input, rsa260()) == 0) {
    factor_config.time_budget_ms = 2500;
    factor_config.max_passes = 64;
    cyclo_config.time_budget_ms = 1500;
  }

  XrayFactorReport factor;
  XrayCyclotomicReport cyclotomic;
  XrayBenchmarkReport benchmark;
  XrayBenchmarkReport *benchmark_ptr = NULL;
  memset(&benchmark, 0, sizeof(benchmark));

  xray_factor_solve(input, &factor_config, &factor);
  xray_cyclotomic_scan(input, &cyclo_config, &cyclotomic);
  if (run_bench) {
    xray_benchmark_run(&benchmark);
    benchmark_ptr = &benchmark;
  }

  char *json = xray_workbench_report_json(&factor, &cyclotomic, benchmark_ptr);
  if (json) {
    puts(json);
    free(json);
  }

  xray_factor_report_clear(&factor);
  xray_cyclotomic_report_clear(&cyclotomic);
  xray_benchmark_report_clear(&benchmark);
  return 0;
}
