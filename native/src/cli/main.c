#include "xray_workbench.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *rsa260(void) {
  return "22112825529529666435281085255026230927612089502470015394413748319128822941402001986512729726569746599085900330031400051170742204560859276357953757185954298838958709229238491006703034124620545784566413664540684214361293017694020846391065875914794251435144458199";
}

static void usage(const char *argv0) {
  fprintf(stderr, "Usage: %s [--bench|--bench-frontier] [--rsa260] [exact-integer-expression]\n", argv0);
  fprintf(stderr, "       %s --bench-progress artifact.tsv\n", argv0);
  fprintf(stderr, "       %s --bench-progress-tsv artifact.tsv\n", argv0);
  fprintf(stderr, "       %s --bench-compare left.tsv right.tsv\n", argv0);
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
  int run_bench = 0;
  int print_frontier = 0;

  for (int index = 1; index < argc; ++index) {
    if (strcmp(argv[index], "--help") == 0 || strcmp(argv[index], "-h") == 0) {
      usage(argv[0]);
      return 0;
    }
    else if (strcmp(argv[index], "--bench") == 0) run_bench = 1;
    else if (strcmp(argv[index], "--bench-progress") == 0) {
      if (index + 1 >= argc) {
        usage(argv[0]);
        return 2;
      }
      char *tsv = read_all_text(argv[index + 1]);
      if (!tsv) {
        fprintf(stderr, "Could not read benchmark TSV input.\n");
        return 3;
      }
      char *digest = xray_benchmark_progress_tsv_text(tsv);
      if (digest) {
        puts(digest);
        xray_free(digest);
      }
      free(tsv);
      return 0;
    }
    else if (strcmp(argv[index], "--bench-progress-tsv") == 0) {
      if (index + 1 >= argc) {
        usage(argv[0]);
        return 2;
      }
      char *tsv = read_all_text(argv[index + 1]);
      if (!tsv) {
        fprintf(stderr, "Could not read benchmark TSV input.\n");
        return 3;
      }
      char *progress_tsv = xray_benchmark_progress_classification_tsv(tsv);
      if (progress_tsv) {
        puts(progress_tsv);
        xray_free(progress_tsv);
      }
      free(tsv);
      return 0;
    }
    else if (strcmp(argv[index], "--bench-compare") == 0) {
      if (index + 2 >= argc) {
        usage(argv[0]);
        return 2;
      }
      char *left = read_all_text(argv[index + 1]);
      char *right = read_all_text(argv[index + 2]);
      if (!left || !right) {
        fprintf(stderr, "Could not read benchmark TSV inputs.\n");
        free(left);
        free(right);
        return 3;
      }
      char *review = xray_benchmark_compare_tsv_text(left, right);
      if (review) {
        puts(review);
        xray_free(review);
      }
      free(left);
      free(right);
      return 0;
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

  XrayRunConfig config = xray_run_default_config();
  if (strcmp(input, rsa260()) == 0) {
    config.factor.time_budget_ms = 2500;
    config.factor.max_passes = 64;
    config.cyclotomic.time_budget_ms = 1500;
  }
  config.enable_benchmark = run_bench;

  XrayWorkbenchReport report;
  xray_workbench_run(input, &config, &report);
  if (print_frontier) {
    char *frontier = xray_benchmark_frontier_text(&report.benchmark);
    if (frontier) {
      puts(frontier);
      free(frontier);
    }
  } else if (report.json) {
    puts(report.json);
  }

  xray_workbench_report_clear(&report);
  return 0;
}
