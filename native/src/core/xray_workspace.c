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

static void emit_run_event(const XrayRunConfig *config, const char *stage, const char *status, const char *detail) {
  if (config && config->event_callback) {
    config->event_callback(stage ? stage : "", status ? status : "", detail ? detail : "", config->event_user_data);
  }
}

static void emit_benchmark_row_event(const XrayBenchmarkResult *result, size_t result_index, void *user_data) {
  const XrayRunConfig *config = (const XrayRunConfig *)user_data;
  if (!config || !config->event_callback || !result) return;
  const char *status = "passed";
  if (!result->passed) status = "failed";
  else if (result->replacement_ready) status = "replacement-ready";
  else if (strcmp(result->category, "scratch-vs-gmp") == 0 && result->adoption[0]) status = result->adoption;
  else if (result->status[0]) status = result->status;

  char detail[384];
  snprintf(detail, sizeof(detail),
    "row=%zu category=%s operation=%s digits=%zu ratio=%.3f stable=%zu/%zu ready=%s scratchUs=%llu backendUs=%llu",
    result_index,
    result->category[0] ? result->category : "benchmark",
    result->operation[0] ? result->operation : result->name,
    result->digits,
    result->speed_ratio,
    result->stable_sample_count,
    result->sample_count,
    result->replacement_ready ? "true" : "false",
    result->scratch_us,
    result->gmp_us);
  emit_run_event(config, "benchmark-row", status, detail);
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
    "{\"stage\":\"cpu\",\"status\":\"profiled\",\"detail\":\"logical=%u avx=%s avx2=%s avx512f=%s bmi2=%s adx=%s\"}\n"
    "{\"stage\":\"factor\",\"status\":\"%s\",\"detail\":\"factors=%zu unresolved=%zu productVerified=%s\"}\n"
    "{\"stage\":\"cyclotomic\",\"status\":\"complete\",\"detail\":\"scanned=%zu exact=%zu\"}\n"
    "{\"stage\":\"benchmark\",\"status\":\"%s\",\"detail\":\"passed=%zu/%zu scratch=%zu replacementReady=%zu oracleOnly=%zu blocked=%zu lanePromotionReady=%zu laneOracleOnly=%zu laneSafetyRejected=%zu\"}\n"
    "{\"stage\":\"gnfs\",\"status\":\"%s\",\"detail\":\"stages=%zu\"}\n",
    report->expression.ok ? "complete" : "invalid",
    report->expression.ok ? "exact integer expression evaluated" : (report->expression.error ? report->expression.error : "invalid"),
    report->cpu.logical_cpus,
    report->cpu.avx ? "true" : "false",
    report->cpu.avx2 ? "true" : "false",
    report->cpu.avx512f ? "true" : "false",
    report->cpu.bmi2 ? "true" : "false",
    report->cpu.adx ? "true" : "false",
    report->factor.status[0] ? report->factor.status : "skipped",
    report->factor.factor_count,
    report->factor.unresolved_count,
    report->factor.product_verified ? "true" : "false",
    report->cyclotomic.scanned,
    report->cyclotomic.exact_matches,
    report->benchmark.result_count ? "complete" : "skipped",
    report->benchmark.passed_count,
    report->benchmark.result_count,
    report->benchmark.scratch_count,
    report->benchmark.replacement_ready_count,
    report->benchmark.oracle_only_count,
    report->benchmark.blocked_count,
    report->benchmark.lanes.promotion_ready_count,
    report->benchmark.lanes.oracle_only_count,
    report->benchmark.lanes.safety_rejected_count,
    report->gnfs.status[0] ? report->gnfs.status : "skipped",
    report->gnfs.stage_count);
  return events;
}

int xray_workbench_run(const char *raw_input, const XrayRunConfig *config_input, XrayWorkbenchReport *report) {
  if (!report) return 0;
  memset(report, 0, sizeof(*report));
  XrayRunConfig config = config_input ? *config_input : xray_run_default_config();
  config.factor.cancel_flag = config.cancel_flag;
  emit_run_event(&config, "expression", "running", "Evaluating exact integer expression");
  xray_cpu_features_detect(&report->cpu);
  emit_run_event(&config, "cpu", "profiled", "CPU feature gates captured for this run");

  mpz_t value;
  mpz_init(value);
  int expression_ok = xray_evaluate_expression(raw_input, value, &report->expression);
  emit_run_event(&config, "expression", expression_ok ? "complete" : "invalid",
    expression_ok ? "Expression normalized to an exact integer" :
      (report->expression.error ? report->expression.error : "Expression did not evaluate to an integer"));
  const char *identity = expression_ok ? report->expression.normalized : raw_input;
  report->run_dir = make_run_dir(config.workspace_root, identity);
  emit_run_event(&config, "workspace", report->run_dir ? "created" : "skipped",
    report->run_dir ? report->run_dir : "Could not create run directory");
  if (report->run_dir) {
    report->input_path = path_for(report->run_dir, "input.txt");
    report->normalized_path = path_for(report->run_dir, "normalized.txt");
    report->config_path = path_for(report->run_dir, "config.txt");
    report->cpu_features_path = path_for(report->run_dir, "cpu_features.txt");
    write_text_file(report->input_path, raw_input ? raw_input : "");
    write_text_file(report->normalized_path, expression_ok ? report->expression.normalized : (report->expression.error ? report->expression.error : "invalid"));
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
    write_text_file(report->config_path, config_text);
    char *cpu_summary = xray_cpu_features_summary(&report->cpu);
    write_text_file(report->cpu_features_path, cpu_summary);
    free(cpu_summary);
  }

  if (expression_ok && config.enable_factor) {
    emit_run_event(&config, "factor", "running", "Solving with bounded local factor methods");
    xray_factor_solve(report->expression.normalized, &config.factor, &report->factor);
    char factor_detail[160];
    snprintf(factor_detail, sizeof(factor_detail), "factors=%zu unresolved=%zu productVerified=%s",
      report->factor.factor_count,
      report->factor.unresolved_count,
      report->factor.product_verified ? "true" : "false");
    emit_run_event(&config, "factor", report->factor.status[0] ? report->factor.status : "complete", factor_detail);
  } else {
    emit_run_event(&config, "factor", config.enable_factor ? "skipped" : "disabled",
      expression_ok ? "Factor stage disabled by config" : "Factor stage skipped because expression is invalid");
  }
  if (expression_ok && config.enable_cyclotomic) {
    emit_run_event(&config, "cyclotomic", "running", "Scanning bounded cyclotomic candidates");
    xray_cyclotomic_scan(report->expression.normalized, &config.cyclotomic, &report->cyclotomic);
    char cyclo_detail[160];
    snprintf(cyclo_detail, sizeof(cyclo_detail), "scanned=%zu exact=%zu timedOut=%s",
      report->cyclotomic.scanned,
      report->cyclotomic.exact_matches,
      report->cyclotomic.timed_out ? "true" : "false");
    emit_run_event(&config, "cyclotomic", "complete", cyclo_detail);
  } else {
    emit_run_event(&config, "cyclotomic", config.enable_cyclotomic ? "skipped" : "disabled",
      expression_ok ? "Cyclotomic stage disabled by config" : "Cyclotomic stage skipped because expression is invalid");
  }
  if (config.enable_benchmark) {
    emit_run_event(&config, "benchmark", "running", "Measuring scratch bigint primitives against GMP/MPIR");
    xray_benchmark_run_with_callback(&report->benchmark, emit_benchmark_row_event, &config);
    char benchmark_detail[192];
    snprintf(benchmark_detail, sizeof(benchmark_detail), "passed=%zu/%zu scratch=%zu replacementReady=%zu oracleOnly=%zu blocked=%zu lanePromotionReady=%zu laneOracleOnly=%zu laneSafetyRejected=%zu",
      report->benchmark.passed_count,
      report->benchmark.result_count,
      report->benchmark.scratch_count,
      report->benchmark.replacement_ready_count,
      report->benchmark.oracle_only_count,
      report->benchmark.blocked_count,
      report->benchmark.lanes.promotion_ready_count,
      report->benchmark.lanes.oracle_only_count,
      report->benchmark.lanes.safety_rejected_count);
    emit_run_event(&config, "benchmark", "complete", benchmark_detail);
  } else {
    emit_run_event(&config, "benchmark", "disabled", "Benchmark ladder disabled by config");
  }
  if (expression_ok && config.enable_gnfs_stage_proof) {
    emit_run_event(&config, "gnfs", "running", "Writing inspectable GNFS stage-proof scaffold artifacts");
    xray_gnfs_stage_proof(report->expression.normalized, report->run_dir, &report->gnfs);
    char gnfs_detail[96];
    snprintf(gnfs_detail, sizeof(gnfs_detail), "status=%s stages=%zu",
      report->gnfs.status[0] ? report->gnfs.status : "unknown",
      report->gnfs.stage_count);
    emit_run_event(&config, "gnfs", report->gnfs.status[0] ? report->gnfs.status : "complete", gnfs_detail);
  } else {
    emit_run_event(&config, "gnfs", config.enable_gnfs_stage_proof ? "skipped" : "disabled",
      expression_ok ? "GNFS stage proof disabled by config" : "GNFS stage proof skipped because expression is invalid");
  }

  emit_run_event(&config, "assemble", "running", "Serializing reports and audit events");
  report->events_jsonl = events_jsonl(report);
  if (report->run_dir) {
    report->events_jsonl_path = path_for(report->run_dir, "events.jsonl");
    report->report_json_path = path_for(report->run_dir, "report.json");
    if (config.enable_benchmark) {
      report->benchmark_json_path = path_for(report->run_dir, "benchmark.json");
      report->benchmark_tsv_path = path_for(report->run_dir, "benchmark.tsv");
      report->benchmark_frontier_path = path_for(report->run_dir, "benchmark_frontier.txt");
    }
  }
  report->json = xray_workbench_full_report_json(report);
  if (report->run_dir) {
    write_text_file(report->events_jsonl_path, report->events_jsonl);
    write_text_file(report->report_json_path, report->json);
    if (config.enable_benchmark) {
      char *benchmark_json = xray_benchmark_report_json(&report->benchmark);
      char *benchmark_tsv = xray_benchmark_report_tsv(&report->benchmark);
      char *benchmark_frontier = xray_benchmark_frontier_text(&report->benchmark);
      write_text_file(report->benchmark_json_path, benchmark_json);
      write_text_file(report->benchmark_tsv_path, benchmark_tsv);
      write_text_file(report->benchmark_frontier_path, benchmark_frontier);
      free(benchmark_json);
      free(benchmark_tsv);
      free(benchmark_frontier);
    }
  }
  emit_run_event(&config, "assemble", "complete", "Report JSON, events, and artifacts written");
  mpz_clear(value);
  emit_run_event(&config, "run", expression_ok ? "complete" : "invalid",
    expression_ok ? "Workbench run complete" : "Workbench run finished without a valid integer");
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
  free(report->input_path);
  free(report->normalized_path);
  free(report->config_path);
  free(report->cpu_features_path);
  free(report->report_json_path);
  free(report->events_jsonl_path);
  free(report->benchmark_json_path);
  free(report->benchmark_tsv_path);
  free(report->benchmark_frontier_path);
  free(report->json);
  free(report->events_jsonl);
  free(report->source_notes);
  memset(report, 0, sizeof(*report));
}
