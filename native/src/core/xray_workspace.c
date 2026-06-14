#include "xray_workbench.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <direct.h>
#define XRAY_MKDIR(path) _mkdir(path)
#else
#include <sys/stat.h>
#define XRAY_MKDIR(path) mkdir(path, 0775)
#endif

static char *xray_strdup_workspace(const char *text) {
  size_t length = text ? strlen(text) : 0;
  char *copy = (char *)calloc(length + 1, 1);
  if (!copy) return NULL;
  if (length) memcpy(copy, text, length);
  return copy;
}

static void ensure_dir(const char *path) {
  if (!path || !*path) return;
  if (XRAY_MKDIR(path) != 0 && errno != EEXIST) {
    /* The caller records missing artifacts through failed file writes. */
  }
}

static unsigned long long fnv1a(const char *text) {
  unsigned long long hash = 1469598103934665603ULL;
  for (const unsigned char *p = (const unsigned char *)(text ? text : ""); *p; ++p) {
    hash ^= (unsigned long long)*p;
    hash *= 1099511628211ULL;
  }
  return hash;
}

static char *make_run_dir(const char *root, const char *normalized_or_raw) {
  const char *base = (root && *root) ? root : "runs";
  ensure_dir(base);
  time_t now = time(NULL);
  struct tm tm_value;
#ifdef _WIN32
  localtime_s(&tm_value, &now);
#else
  localtime_r(&now, &tm_value);
#endif
  char stamp[32];
  strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &tm_value);
  unsigned long long hash = fnv1a(normalized_or_raw);
  char *dir = (char *)calloc(384, 1);
  if (!dir) return NULL;
  snprintf(dir, 384, "%s/%s-%08llx", base, stamp, hash & 0xffffffffULL);
  ensure_dir(dir);
  return dir;
}

static int write_text_file(const char *path, const char *text) {
  FILE *file = NULL;
#ifdef _WIN32
  if (fopen_s(&file, path, "wb") != 0) file = NULL;
#else
  file = fopen(path, "wb");
#endif
  if (!file) return 0;
  if (text) fputs(text, file);
  fclose(file);
  return 1;
}

static char *path_for(const char *dir, const char *name) {
  char *path = (char *)calloc(512, 1);
  if (!path) return NULL;
  snprintf(path, 512, "%s/%s", dir ? dir : ".", name ? name : "");
  return path;
}

XrayRunConfig xray_run_default_config(void) {
  XrayRunConfig config;
  memset(&config, 0, sizeof(config));
  config.factor = xray_factor_default_config();
  config.cyclotomic = xray_cyclotomic_default_config();
  config.enable_factor = 1;
  config.enable_cyclotomic = 1;
  config.enable_benchmark = 1;
  config.enable_gnfs_stage_proof = 1;
  config.threads = 12;
  config.memory_mb = 16384;
  snprintf(config.scan_depth, sizeof(config.scan_depth), "deep");
  snprintf(config.proof_strategy, sizeof(config.proof_strategy), "deterministic");
  snprintf(config.primality_mode, sizeof(config.primality_mode), "probable-prime");
  snprintf(config.workspace_root, sizeof(config.workspace_root), "runs");
  return config;
}

static char *events_jsonl(const XrayWorkbenchReport *report) {
  char *events = (char *)calloc(4096, 1);
  if (!events) return NULL;
  snprintf(events, 4096,
    "{\"stage\":\"expression\",\"status\":\"%s\",\"detail\":\"%s\"}\n"
    "{\"stage\":\"factor\",\"status\":\"%s\",\"detail\":\"factors=%zu unresolved=%zu productVerified=%s\"}\n"
    "{\"stage\":\"cyclotomic\",\"status\":\"complete\",\"detail\":\"scanned=%zu exact=%zu\"}\n"
    "{\"stage\":\"gnfs\",\"status\":\"%s\",\"detail\":\"stages=%zu\"}\n",
    report->expression.ok ? "complete" : "invalid",
    report->expression.ok ? "exact integer expression evaluated" : (report->expression.error ? report->expression.error : "invalid"),
    report->factor.status[0] ? report->factor.status : "skipped",
    report->factor.factor_count,
    report->factor.unresolved_count,
    report->factor.product_verified ? "true" : "false",
    report->cyclotomic.scanned,
    report->cyclotomic.exact_matches,
    report->gnfs.status[0] ? report->gnfs.status : "skipped",
    report->gnfs.stage_count);
  return events;
}

int xray_workbench_run(const char *raw_input, const XrayRunConfig *config_input, XrayWorkbenchReport *report) {
  if (!report) return 0;
  memset(report, 0, sizeof(*report));
  XrayRunConfig config = config_input ? *config_input : xray_run_default_config();
  config.factor.cancel_flag = config.cancel_flag;

  mpz_t value;
  mpz_init(value);
  int expression_ok = xray_evaluate_expression(raw_input, value, &report->expression);
  const char *identity = expression_ok ? report->expression.normalized : raw_input;
  report->run_dir = make_run_dir(config.workspace_root, identity);
  if (report->run_dir) {
    char *input_path = path_for(report->run_dir, "input.txt");
    char *normalized_path = path_for(report->run_dir, "normalized.txt");
    char *config_path = path_for(report->run_dir, "config.txt");
    write_text_file(input_path, raw_input ? raw_input : "");
    write_text_file(normalized_path, expression_ok ? report->expression.normalized : (report->expression.error ? report->expression.error : "invalid"));
    char config_text[512];
    snprintf(config_text, sizeof(config_text),
      "scanDepth=%s\nproofStrategy=%s\nprimalityMode=%s\nthreads=%u\nmemoryMb=%lu\ntrialLimit=%lu\nfermatIterations=%lu\nrhoIterations=%lu\npm1Bound=%lu\nbrentIterations=%lu\nnMin=%u\nnMax=%u\nbaseWindow=%u\ngnfsStageProof=%d\n",
      config.scan_depth,
      config.proof_strategy,
      config.primality_mode,
      config.threads,
      config.memory_mb,
      config.factor.trial_limit,
      config.factor.fermat_iterations,
      config.factor.rho_iterations,
      config.factor.pm1_bound,
      config.factor.brent_iterations,
      config.cyclotomic.n_min,
      config.cyclotomic.n_max,
      config.cyclotomic.base_window,
      config.enable_gnfs_stage_proof);
    write_text_file(config_path, config_text);
    free(input_path);
    free(normalized_path);
    free(config_path);
  }

  if (expression_ok && config.enable_factor) {
    xray_factor_solve(report->expression.normalized, &config.factor, &report->factor);
  }
  if (expression_ok && config.enable_cyclotomic) {
    xray_cyclotomic_scan(report->expression.normalized, &config.cyclotomic, &report->cyclotomic);
  }
  if (config.enable_benchmark) {
    xray_benchmark_run(&report->benchmark);
  }
  if (expression_ok && config.enable_gnfs_stage_proof) {
    xray_gnfs_stage_proof(report->expression.normalized, report->run_dir, &report->gnfs);
  }

  report->events_jsonl = events_jsonl(report);
  report->json = xray_workbench_full_report_json(report);
  if (report->run_dir) {
    char *events_path = path_for(report->run_dir, "events.jsonl");
    char *report_path = path_for(report->run_dir, "report.json");
    write_text_file(events_path, report->events_jsonl);
    write_text_file(report_path, report->json);
    free(events_path);
    free(report_path);
  }
  mpz_clear(value);
  return expression_ok;
}

void xray_workbench_report_clear(XrayWorkbenchReport *report) {
  if (!report) return;
  xray_expression_result_clear(&report->expression);
  xray_factor_report_clear(&report->factor);
  xray_cyclotomic_report_clear(&report->cyclotomic);
  xray_benchmark_report_clear(&report->benchmark);
  xray_gnfs_report_clear(&report->gnfs);
  free(report->run_dir);
  free(report->json);
  free(report->events_jsonl);
  free(report->source_notes);
  memset(report, 0, sizeof(*report));
}
