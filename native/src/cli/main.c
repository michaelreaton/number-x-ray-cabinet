#include "xray_workbench.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *rsa260(void) {
  return "22112825529529666435281085255026230927612089502470015394413748319128822941402001986512729726569746599085900330031400051170742204560859276357953757185954298838958709229238491006703034124620545784566413664540684214361293017694020846391065875914794251435144458199";
}

static void usage(const char *argv0) {
  fprintf(stderr, "Usage: %s [--bench|--bench-frontier|--bench-tsv] [--rsa260] [exact-integer-expression]\n", argv0);
  fprintf(stderr, "       %s --bench-focus FOCUS [--bench-frontier|--bench-tsv]\n", argv0);
  fprintf(stderr, "       %s [--bench-min-digits N] [--bench-max-digits N] [--bench-filter TEXT] --bench-progress artifact.tsv\n", argv0);
  fprintf(stderr, "       %s [--bench-min-digits N] [--bench-max-digits N] [--bench-filter TEXT] --bench-progress-tsv artifact.tsv\n", argv0);
  fprintf(stderr, "       %s [--bench-min-digits N] [--bench-max-digits N] [--bench-filter TEXT] --bench-compare left.tsv right.tsv\n", argv0);
  fprintf(stderr, "Focus examples: mul-large, mul-toom5-smoke, mul-toom-div-transition,\n");
  fprintf(stderr, "                mul-toom-div, mul-toom4-top, mul-backend-gap,\n");
  fprintf(stderr, "                mul-full-audit-pocket, mul-combo-lower,\n");
  fprintf(stderr, "                mul-combo-transition, mul-combo-upper,\n");
  fprintf(stderr, "                mul-combo-reuse, mul-combo-handoff-boundary,\n");
  fprintf(stderr, "                mul-sparse, mul-novelty\n");
}

static int parse_size_arg(const char *text, size_t *out) {
  if (!text || !out || !text[0]) return 0;
  errno = 0;
  char *end = NULL;
  unsigned long long parsed = strtoull(text, &end, 10);
  if (errno || end == text || (end && *end)) return 0;
  *out = (size_t)parsed;
  return 1;
}

static char *read_all_text(const char *path) {
  FILE *file = NULL;
#ifdef _WIN32
  if (fopen_s(&file, path, "rb") != 0) file = NULL;
#else
  file = fopen(path, "rb");
#endif
  if (!file) return NULL;
  if (fseek(file, 0, SEEK_END) != 0) {
    fclose(file);
    return NULL;
  }
  long length = ftell(file);
  if (length < 0 || fseek(file, 0, SEEK_SET) != 0) {
    fclose(file);
    return NULL;
  }
  char *text = (char *)calloc((size_t)length + 1U, 1);
  if (!text) {
    fclose(file);
    return NULL;
  }
  if (length && fread(text, 1, (size_t)length, file) != (size_t)length) {
    free(text);
    fclose(file);
    return NULL;
  }
  fclose(file);
  return text;
}

int main(int argc, char **argv) {
  const char *input = "10403";
  const char *progress_path = NULL;
  const char *compare_left_path = NULL;
  const char *compare_right_path = NULL;
  enum {
    XRAY_CLI_RUN,
    XRAY_CLI_PROGRESS,
    XRAY_CLI_PROGRESS_TSV,
    XRAY_CLI_COMPARE
  } mode = XRAY_CLI_RUN;
  int run_bench = 0;
  int print_frontier = 0;
  int print_tsv = 0;
  size_t bench_min_digits = 0;
  size_t bench_max_digits = 0;
  const char *bench_filter = NULL;
  const char *bench_focus = NULL;

  for (int index = 1; index < argc; ++index) {
    if (strcmp(argv[index], "--help") == 0 || strcmp(argv[index], "-h") == 0) {
      usage(argv[0]);
      return 0;
    }
    else if (strcmp(argv[index], "--bench") == 0) run_bench = 1;
    else if (strcmp(argv[index], "--bench-tsv") == 0) {
      run_bench = 1;
      print_tsv = 1;
    }
    else if (strcmp(argv[index], "--bench-focus") == 0 || strcmp(argv[index], "--focus-bench") == 0) {
      if (index + 1 >= argc) {
        usage(argv[0]);
        return 2;
      }
      run_bench = 1;
      bench_focus = argv[++index];
    }
    else if (strcmp(argv[index], "--bench-min-digits") == 0 || strcmp(argv[index], "--min-digits") == 0) {
      if (index + 1 >= argc || !parse_size_arg(argv[index + 1], &bench_min_digits)) {
        usage(argv[0]);
        return 2;
      }
      index++;
    }
    else if (strcmp(argv[index], "--bench-max-digits") == 0 || strcmp(argv[index], "--max-digits") == 0) {
      if (index + 1 >= argc || !parse_size_arg(argv[index + 1], &bench_max_digits)) {
        usage(argv[0]);
        return 2;
      }
      index++;
    }
    else if (strcmp(argv[index], "--bench-filter") == 0 || strcmp(argv[index], "--filter") == 0) {
      if (index + 1 >= argc) {
        usage(argv[0]);
        return 2;
      }
      bench_filter = argv[++index];
    }
    else if (strcmp(argv[index], "--bench-progress") == 0) {
      if (index + 1 >= argc) {
        usage(argv[0]);
        return 2;
      }
      mode = XRAY_CLI_PROGRESS;
      progress_path = argv[++index];
    }
    else if (strcmp(argv[index], "--bench-progress-tsv") == 0) {
      if (index + 1 >= argc) {
        usage(argv[0]);
        return 2;
      }
      mode = XRAY_CLI_PROGRESS_TSV;
      progress_path = argv[++index];
    }
    else if (strcmp(argv[index], "--bench-compare") == 0) {
      if (index + 2 >= argc) {
        usage(argv[0]);
        return 2;
      }
      mode = XRAY_CLI_COMPARE;
      compare_left_path = argv[++index];
      compare_right_path = argv[++index];
    }
    else if (strcmp(argv[index], "--bench-frontier") == 0) {
      run_bench = 1;
      print_frontier = 1;
    }
    else if (strcmp(argv[index], "--rsa260") == 0) input = rsa260();
    else if (argv[index][0] == '-') {
      usage(argv[0]);
      return 2;
    } else {
      input = argv[index];
    }
  }

  if (bench_max_digits && bench_min_digits && bench_min_digits > bench_max_digits) {
    fprintf(stderr, "Benchmark digit window is invalid: min is greater than max.\n");
    return 2;
  }

  if (mode == XRAY_CLI_PROGRESS || mode == XRAY_CLI_PROGRESS_TSV) {
    char *tsv = read_all_text(progress_path);
    if (!tsv) {
      fprintf(stderr, "Could not read benchmark TSV input.\n");
      return 3;
    }
    char *digit_filtered = xray_benchmark_filter_tsv_digits(tsv, bench_min_digits, bench_max_digits);
    const char *input_tsv = digit_filtered ? digit_filtered : tsv;
    char *text_filtered = NULL;
    if (bench_filter && bench_filter[0]) {
      text_filtered = xray_benchmark_filter_tsv_text(input_tsv, bench_filter);
      if (text_filtered) input_tsv = text_filtered;
    }
    if (bench_min_digits || bench_max_digits) {
      printf("Focused digit window: min=%zu max=%zu\n", bench_min_digits, bench_max_digits);
    }
    if (bench_filter && bench_filter[0]) {
      printf("Focused row filter: %s\n", bench_filter);
    }
    char *output = mode == XRAY_CLI_PROGRESS ?
      xray_benchmark_progress_tsv_text(input_tsv) :
      xray_benchmark_progress_classification_tsv(input_tsv);
    if (output) {
      puts(output);
      xray_free(output);
    }
    xray_free(text_filtered);
    xray_free(digit_filtered);
    free(tsv);
    return 0;
  }

  if (mode == XRAY_CLI_COMPARE) {
    char *left = read_all_text(compare_left_path);
    char *right = read_all_text(compare_right_path);
    if (!left || !right) {
      fprintf(stderr, "Could not read benchmark TSV inputs.\n");
      free(left);
      free(right);
      return 3;
    }
    char *digit_filtered_left = xray_benchmark_filter_tsv_digits(left, bench_min_digits, bench_max_digits);
    char *digit_filtered_right = xray_benchmark_filter_tsv_digits(right, bench_min_digits, bench_max_digits);
    const char *left_tsv = digit_filtered_left ? digit_filtered_left : left;
    const char *right_tsv = digit_filtered_right ? digit_filtered_right : right;
    char *text_filtered_left = NULL;
    char *text_filtered_right = NULL;
    if (bench_filter && bench_filter[0]) {
      text_filtered_left = xray_benchmark_filter_tsv_text(left_tsv, bench_filter);
      text_filtered_right = xray_benchmark_filter_tsv_text(right_tsv, bench_filter);
      if (text_filtered_left) left_tsv = text_filtered_left;
      if (text_filtered_right) right_tsv = text_filtered_right;
    }
    if (bench_min_digits || bench_max_digits) {
      printf("Focused digit window: min=%zu max=%zu\n", bench_min_digits, bench_max_digits);
    }
    if (bench_filter && bench_filter[0]) {
      printf("Focused row filter: %s\n", bench_filter);
    }
    char *review = xray_benchmark_compare_tsv_text(left_tsv, right_tsv);
    if (review) {
      puts(review);
      xray_free(review);
    }
    xray_free(text_filtered_left);
    xray_free(text_filtered_right);
    xray_free(digit_filtered_left);
    xray_free(digit_filtered_right);
    free(left);
    free(right);
    return 0;
  }

  XrayRunConfig config = xray_run_default_config();
  if (strcmp(input, rsa260()) == 0) {
    config.factor.time_budget_ms = 2500;
    config.factor.max_passes = 64;
    config.cyclotomic.time_budget_ms = 1500;
  }
  config.enable_benchmark = run_bench;

  if (bench_focus && bench_focus[0]) {
    XrayBenchmarkReport benchmark;
    if (!xray_benchmark_run_focus(&benchmark, bench_focus)) {
      fprintf(stderr, "Focused benchmark run failed.\n");
      return 4;
    }
    if (print_frontier) {
      char *frontier = xray_benchmark_frontier_text(&benchmark);
      if (frontier) {
        puts(frontier);
        free(frontier);
      }
    } else if (print_tsv) {
      char *tsv = xray_benchmark_report_tsv(&benchmark);
      if (tsv) {
        puts(tsv);
        xray_free(tsv);
      }
    } else {
      char *json = xray_benchmark_report_json(&benchmark);
      if (json) {
        puts(json);
        xray_free(json);
      }
    }
    xray_benchmark_report_clear(&benchmark);
    return 0;
  }

  XrayWorkbenchReport report;
  xray_workbench_run(input, &config, &report);
  if (print_frontier) {
    char *frontier = xray_benchmark_frontier_text(&report.benchmark);
    if (frontier) {
      puts(frontier);
      free(frontier);
    }
  } else if (print_tsv) {
    char *tsv = xray_benchmark_report_tsv(&report.benchmark);
    if (tsv) {
      puts(tsv);
      xray_free(tsv);
    }
  } else if (report.json) {
    puts(report.json);
  }

  xray_workbench_report_clear(&report);
  return 0;
}
