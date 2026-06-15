#include "xray_workbench.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct JsonBuffer {
  char *data;
  size_t length;
  size_t capacity;
} JsonBuffer;

static int jb_reserve(JsonBuffer *buffer, size_t extra) {
  size_t needed = buffer->length + extra + 1;
  if (needed <= buffer->capacity) return 1;
  size_t next_capacity = buffer->capacity ? buffer->capacity * 2 : 1024;
  while (next_capacity < needed) next_capacity *= 2;
  char *next = (char *)realloc(buffer->data, next_capacity);
  if (!next) return 0;
  buffer->data = next;
  buffer->capacity = next_capacity;
  return 1;
}

static int jb_append(JsonBuffer *buffer, const char *text) {
  size_t length = strlen(text);
  if (!jb_reserve(buffer, length)) return 0;
  memcpy(buffer->data + buffer->length, text, length);
  buffer->length += length;
  buffer->data[buffer->length] = '\0';
  return 1;
}

static int jb_printf(JsonBuffer *buffer, const char *format, ...) {
  va_list args;
  va_start(args, format);
  va_list copy;
  va_copy(copy, args);
  int needed = vsnprintf(NULL, 0, format, copy);
  va_end(copy);
  if (needed < 0) {
    va_end(args);
    return 0;
  }
  if (!jb_reserve(buffer, (size_t)needed)) {
    va_end(args);
    return 0;
  }
  vsnprintf(buffer->data + buffer->length, buffer->capacity - buffer->length, format, args);
  buffer->length += (size_t)needed;
  va_end(args);
  return 1;
}

static void jb_string(JsonBuffer *buffer, const char *text) {
  jb_append(buffer, "\"");
  if (text) {
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
      switch (*p) {
        case '\\': jb_append(buffer, "\\\\"); break;
        case '"': jb_append(buffer, "\\\""); break;
        case '\n': jb_append(buffer, "\\n"); break;
        case '\r': jb_append(buffer, "\\r"); break;
        case '\t': jb_append(buffer, "\\t"); break;
        default:
          if (*p < 0x20) jb_printf(buffer, "\\u%04x", *p);
          else {
            char ch[2] = {(char)*p, 0};
            jb_append(buffer, ch);
          }
      }
    }
  }
  jb_append(buffer, "\"");
}

static char *jb_take(JsonBuffer *buffer) {
  if (!buffer->data) return NULL;
  char *data = buffer->data;
  buffer->data = NULL;
  buffer->length = 0;
  buffer->capacity = 0;
  return data;
}

static void append_factor_json(JsonBuffer *buffer, const XrayFactorReport *report) {
  jb_append(buffer, "\"factorReport\":{");
  jb_append(buffer, "\"input\":"); jb_string(buffer, report->input);
  jb_printf(buffer, ",\"status\":\"%s\",\"digits\":%zu,\"bitLength\":%zu", report->status, report->digits, report->bit_length);
  jb_printf(buffer, ",\"productVerified\":%s,\"accountingVerified\":%s,\"timedOut\":%s,\"cancelled\":%s,\"elapsedMs\":%lu",
    report->product_verified ? "true" : "false",
    report->accounting_verified ? "true" : "false",
    report->timed_out ? "true" : "false",
    report->cancelled ? "true" : "false",
    report->elapsed_ms);
  jb_printf(buffer, ",\"config\":{\"trialLimit\":%lu,\"fermatIterations\":%lu,\"rhoIterations\":%lu,\"pm1Bound\":%lu,\"brentIterations\":%lu,\"maxPasses\":%lu,\"timeBudgetMs\":%lu}",
    report->config.trial_limit,
    report->config.fermat_iterations,
    report->config.rho_iterations,
    report->config.pm1_bound,
    report->config.brent_iterations,
    report->config.max_passes,
    report->config.time_budget_ms);
  jb_append(buffer, ",\"factors\":[");
  for (size_t index = 0; index < report->factor_count; ++index) {
    if (index) jb_append(buffer, ",");
    jb_append(buffer, "{");
    jb_append(buffer, "\"value\":"); jb_string(buffer, report->factors[index].value);
    jb_printf(buffer, ",\"exponent\":%u,\"probablePrime\":%s,\"methods\":",
      report->factors[index].exponent,
      report->factors[index].probable_prime ? "true" : "false");
    jb_string(buffer, report->factors[index].methods);
    jb_append(buffer, "}");
  }
  jb_append(buffer, "],\"unresolved\":[");
  for (size_t index = 0; index < report->unresolved_count; ++index) {
    if (index) jb_append(buffer, ",");
    jb_append(buffer, "{");
    jb_append(buffer, "\"value\":"); jb_string(buffer, report->unresolved[index].value);
    jb_printf(buffer, ",\"digits\":%zu,\"probablePrime\":%s,\"deferred\":%s,\"note\":",
      report->unresolved[index].digits,
      report->unresolved[index].probable_prime ? "true" : "false",
      report->unresolved[index].deferred ? "true" : "false");
    jb_string(buffer, report->unresolved[index].note);
    jb_append(buffer, "}");
  }
  jb_append(buffer, "],\"steps\":[");
  for (size_t index = 0; index < report->step_count; ++index) {
    if (index) jb_append(buffer, ",");
    jb_append(buffer, "{");
    jb_append(buffer, "\"method\":"); jb_string(buffer, report->steps[index].method);
    jb_append(buffer, ",\"status\":"); jb_string(buffer, report->steps[index].status);
    jb_append(buffer, ",\"targetPreview\":"); jb_string(buffer, report->steps[index].target_preview);
    jb_append(buffer, ",\"detail\":"); jb_string(buffer, report->steps[index].detail);
    jb_printf(buffer, ",\"elapsedMs\":%lu}", report->steps[index].elapsed_ms);
  }
  jb_append(buffer, "]}");
}

static void append_cyclotomic_json(JsonBuffer *buffer, const XrayCyclotomicReport *report) {
  jb_append(buffer, "\"cyclotomicReport\":{");
  jb_append(buffer, "\"input\":"); jb_string(buffer, report->input);
  jb_printf(buffer, ",\"config\":{\"nMin\":%u,\"nMax\":%u,\"baseWindow\":%u,\"reportLimit\":%u,\"timeBudgetMs\":%lu}",
    report->config.n_min,
    report->config.n_max,
    report->config.base_window,
    report->config.report_limit,
    report->config.time_budget_ms);
  jb_printf(buffer, ",\"scanned\":%zu,\"exactMatches\":%zu,\"timedOut\":%s,\"elapsedMs\":%lu,\"candidates\":[",
    report->scanned,
    report->exact_matches,
    report->timed_out ? "true" : "false",
    report->elapsed_ms);
  for (size_t index = 0; index < report->candidate_count; ++index) {
    const XrayCyclotomicCandidate *candidate = &report->candidates[index];
    if (index) jb_append(buffer, ",");
    jb_printf(buffer, "{\"n\":%u,\"phi\":%u,\"base\":", candidate->n, candidate->phi);
    jb_string(buffer, candidate->base);
    jb_printf(buffer, ",\"score\":%.6f,\"exactMatch\":%s,\"verdict\":",
      candidate->score,
      candidate->exact_match ? "true" : "false");
    jb_string(buffer, candidate->verdict);
    jb_append(buffer, ",\"differencePreview\":");
    jb_string(buffer, candidate->difference_preview);
    jb_append(buffer, "}");
  }
  jb_append(buffer, "]}");
}

static void append_cpu_json(JsonBuffer *buffer, const char *key, const XrayCpuFeatures *cpu_input) {
  XrayCpuFeatures empty;
  memset(&empty, 0, sizeof(empty));
  const XrayCpuFeatures *cpu = cpu_input ? cpu_input : &empty;
  jb_append(buffer, "\"");
  jb_append(buffer, key ? key : "cpu");
  jb_append(buffer, "\":{");
  jb_append(buffer, "\"architecture\":"); jb_string(buffer, cpu->architecture);
  jb_append(buffer, ",\"vendor\":"); jb_string(buffer, cpu->vendor);
  jb_append(buffer, ",\"brand\":"); jb_string(buffer, cpu->brand);
  jb_printf(buffer,
    ",\"logicalCpus\":%u,\"cpuidSupported\":%s,\"xsave\":%s,\"osxsave\":%s,\"xcr0\":\"0x%llx\",\"avxOsEnabled\":%s,\"avx512OsEnabled\":%s",
    cpu->logical_cpus,
    cpu->cpuid_supported ? "true" : "false",
    cpu->xsave ? "true" : "false",
    cpu->osxsave ? "true" : "false",
    (unsigned long long)cpu->xcr0,
    cpu->avx_os_enabled ? "true" : "false",
    cpu->avx512_os_enabled ? "true" : "false");
  jb_printf(buffer,
    ",\"features\":{\"sse2\":%s,\"sse3\":%s,\"ssse3\":%s,\"sse41\":%s,\"sse42\":%s,\"pclmulqdq\":%s,\"popcnt\":%s,\"aes\":%s,\"fma\":%s,\"avx\":%s,\"avx2\":%s,\"avx512f\":%s,\"avx512dq\":%s,\"avx512ifma\":%s,\"avx512bw\":%s,\"avx512vl\":%s,\"vaes\":%s,\"vpclmulqdq\":%s,\"bmi1\":%s,\"bmi2\":%s,\"adx\":%s}}",
    cpu->sse2 ? "true" : "false",
    cpu->sse3 ? "true" : "false",
    cpu->ssse3 ? "true" : "false",
    cpu->sse41 ? "true" : "false",
    cpu->sse42 ? "true" : "false",
    cpu->pclmulqdq ? "true" : "false",
    cpu->popcnt ? "true" : "false",
    cpu->aes ? "true" : "false",
    cpu->fma ? "true" : "false",
    cpu->avx ? "true" : "false",
    cpu->avx2 ? "true" : "false",
    cpu->avx512f ? "true" : "false",
    cpu->avx512dq ? "true" : "false",
    cpu->avx512ifma ? "true" : "false",
    cpu->avx512bw ? "true" : "false",
    cpu->avx512vl ? "true" : "false",
    cpu->vaes ? "true" : "false",
    cpu->vpclmulqdq ? "true" : "false",
    cpu->bmi1 ? "true" : "false",
    cpu->bmi2 ? "true" : "false",
    cpu->adx ? "true" : "false");
}

static void append_bigint_route_config_json(JsonBuffer *buffer) {
  XrayBigIntRouteConfig route = xray_bigint_route_config();
  jb_printf(buffer,
    "\"scratchRouteConfig\":{\"wordBits\":%u,\"karatsubaThresholdLimbs\":%zu,\"decimalHornerMinLimbs\":%zu,\"mulUnroll4RouteMinLimbs\":%zu,\"mulUnroll4RouteMaxLimbs\":%zu,\"mulUnroll4RouteEnabled\":%s,\"msvcUint128Helpers\":%s}",
    route.word_bits,
    route.karatsuba_threshold_limbs,
    route.decimal_horner_min_limbs,
    route.mul_unroll4_route_min_limbs,
    route.mul_unroll4_route_max_limbs,
    route.mul_unroll4_route_enabled ? "true" : "false",
    route.msvc_uint128_helpers ? "true" : "false");
}

static void append_benchmark_json(JsonBuffer *buffer, const XrayBenchmarkReport *report) {
  jb_append(buffer, "\"benchmarkReport\":{");
  append_cpu_json(buffer, "cpu", report ? &report->cpu : NULL);
  jb_append(buffer, ",\"baselineBackend\":");
  jb_string(buffer, xray_bignum_backend_name());
  jb_append(buffer, ",\"baselineBackendVersion\":");
  jb_string(buffer, xray_bignum_backend_version());
  jb_append(buffer, ",\"baselineBackendLibrary\":");
  jb_string(buffer, xray_bignum_backend_library());
  jb_append(buffer, ",");
  append_bigint_route_config_json(buffer);
  jb_printf(buffer,
    ",\"passed\":%zu,\"total\":%zu,\"scratchRows\":%zu,\"replacementReadyRows\":%zu,\"oracleOnlyRows\":%zu,\"blockedRows\":%zu,\"elapsedMs\":%lu,\"results\":[",
    report->passed_count,
    report->result_count,
    report->scratch_count,
    report->replacement_ready_count,
    report->oracle_only_count,
    report->blocked_count,
    report->elapsed_ms);
  for (size_t index = 0; index < report->result_count; ++index) {
    if (index) jb_append(buffer, ",");
    jb_append(buffer, "{\"name\":"); jb_string(buffer, report->results[index].name);
    jb_append(buffer, ",\"category\":"); jb_string(buffer, report->results[index].category);
    jb_append(buffer, ",\"operation\":"); jb_string(buffer, report->results[index].operation);
    jb_append(buffer, ",\"status\":"); jb_string(buffer, report->results[index].status);
    jb_append(buffer, ",\"adoption\":"); jb_string(buffer, report->results[index].adoption);
    jb_printf(buffer,
      ",\"digits\":%zu,\"passed\":%s,\"parityVerified\":%s,\"replacementReady\":%s,\"scratchUs\":%llu,\"gmpUs\":%llu,\"speedRatio\":%.6f,\"maxAllowedSpeedRatio\":%.6f,\"worstPairRatio\":%.6f,\"stableSampleCount\":%zu,\"sampleCount\":%zu,\"elapsedMs\":%lu,\"detail\":",
      report->results[index].digits,
      report->results[index].passed ? "true" : "false",
      report->results[index].parity_verified ? "true" : "false",
      report->results[index].replacement_ready ? "true" : "false",
      report->results[index].scratch_us,
      report->results[index].gmp_us,
      report->results[index].speed_ratio,
      report->results[index].max_allowed_speed_ratio,
      report->results[index].worst_pair_ratio,
      report->results[index].stable_sample_count,
      report->results[index].sample_count,
      report->results[index].elapsed_ms);
    jb_string(buffer, report->results[index].detail);
    jb_append(buffer, "}");
  }
  jb_append(buffer, "]}");
}

static void append_tsv_field(JsonBuffer *buffer, const char *text) {
  if (!text) return;
  for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
    char ch[2] = {(char)*p, 0};
    if (*p == '\t' || *p == '\r' || *p == '\n') ch[0] = ' ';
    jb_append(buffer, ch);
  }
}

static void append_expression_json(JsonBuffer *buffer, const XrayExpressionResult *expression) {
  jb_append(buffer, "\"expression\":{");
  jb_append(buffer, "\"raw\":"); jb_string(buffer, expression ? expression->raw : NULL);
  jb_append(buffer, ",\"normalized\":"); jb_string(buffer, expression ? expression->normalized : NULL);
  jb_append(buffer, ",\"error\":"); jb_string(buffer, expression ? expression->error : NULL);
  jb_printf(buffer, ",\"ok\":%s,\"digits\":%zu,\"bitLength\":%zu}",
    expression && expression->ok ? "true" : "false",
    expression ? expression->digits : 0,
    expression ? expression->bit_length : 0);
}

static void append_gnfs_json(JsonBuffer *buffer, const XrayGnfsReport *report) {
  jb_append(buffer, "\"gnfsReport\":{");
  jb_append(buffer, "\"input\":"); jb_string(buffer, report ? report->input : NULL);
  jb_append(buffer, ",\"runDir\":"); jb_string(buffer, report ? report->run_dir : NULL);
  jb_append(buffer, ",\"status\":"); jb_string(buffer, report ? report->status : "skipped");
  jb_printf(buffer, ",\"elapsedMs\":%lu,\"stages\":[", report ? report->elapsed_ms : 0);
  if (report) {
    for (size_t index = 0; index < report->stage_count; ++index) {
      if (index) jb_append(buffer, ",");
      jb_append(buffer, "{");
      jb_append(buffer, "\"name\":"); jb_string(buffer, report->stages[index].name);
      jb_append(buffer, ",\"status\":"); jb_string(buffer, report->stages[index].status);
      jb_append(buffer, ",\"artifact\":"); jb_string(buffer, report->stages[index].artifact);
      jb_append(buffer, ",\"detail\":"); jb_string(buffer, report->stages[index].detail);
      jb_printf(buffer, ",\"elapsedMs\":%lu}", report->stages[index].elapsed_ms);
    }
  }
  jb_append(buffer, "]}");
}

char *xray_factor_report_json(const XrayFactorReport *report) {
  JsonBuffer buffer = {0};
  jb_append(&buffer, "{");
  append_factor_json(&buffer, report);
  jb_append(&buffer, "}");
  return jb_take(&buffer);
}

char *xray_factor_solve_json(const char *raw_input) {
  XrayFactorReport report;
  xray_factor_solve(raw_input, NULL, &report);
  char *json = xray_factor_report_json(&report);
  xray_factor_report_clear(&report);
  return json;
}

char *xray_cyclotomic_report_json(const XrayCyclotomicReport *report) {
  JsonBuffer buffer = {0};
  jb_append(&buffer, "{");
  append_cyclotomic_json(&buffer, report);
  jb_append(&buffer, "}");
  return jb_take(&buffer);
}

char *xray_cyclotomic_scan_json(const char *raw_input) {
  XrayCyclotomicReport report;
  xray_cyclotomic_scan(raw_input, NULL, &report);
  char *json = xray_cyclotomic_report_json(&report);
  xray_cyclotomic_report_clear(&report);
  return json;
}

char *xray_benchmark_report_json(const XrayBenchmarkReport *report) {
  JsonBuffer buffer = {0};
  jb_append(&buffer, "{");
  append_benchmark_json(&buffer, report);
  jb_append(&buffer, "}");
  return jb_take(&buffer);
}

char *xray_benchmark_report_tsv(const XrayBenchmarkReport *report) {
  JsonBuffer buffer = {0};
  jb_append(&buffer,
    "category\tname\toperation\tdigits\tstatus\tpassed\tparityVerified\treplacementReady\tadoption\tscratchUs\tgmpUs\tspeedRatio\tmaxAllowedSpeedRatio\tworstPairRatio\tstableSampleCount\tsampleCount\telapsedMs\tdetail\n");
  if (!report) return jb_take(&buffer);
  for (size_t index = 0; index < report->result_count; ++index) {
    const XrayBenchmarkResult *row = &report->results[index];
    append_tsv_field(&buffer, row->category);
    jb_append(&buffer, "\t");
    append_tsv_field(&buffer, row->name);
    jb_append(&buffer, "\t");
    append_tsv_field(&buffer, row->operation);
    jb_printf(&buffer, "\t%zu\t", row->digits);
    append_tsv_field(&buffer, row->status);
    jb_printf(&buffer, "\t%s\t%s\t%s\t",
      row->passed ? "true" : "false",
      row->parity_verified ? "true" : "false",
      row->replacement_ready ? "true" : "false");
    append_tsv_field(&buffer, row->adoption);
    jb_printf(&buffer, "\t%llu\t%llu\t%.6f\t%.6f\t%.6f\t%zu\t%zu\t%lu\t",
      row->scratch_us,
      row->gmp_us,
      row->speed_ratio,
      row->max_allowed_speed_ratio,
      row->worst_pair_ratio,
      row->stable_sample_count,
      row->sample_count,
      row->elapsed_ms);
    append_tsv_field(&buffer, row->detail);
    jb_append(&buffer, "\n");
  }
  return jb_take(&buffer);
}

static void benchmark_row_label(const XrayBenchmarkResult *row, char *out, size_t out_size) {
  if (!out || out_size == 0) return;
  if (!row) {
    snprintf(out, out_size, "unknown");
    return;
  }
  if (row->operation[0] && strcmp(row->category, "kernel-probe") != 0) {
    snprintf(out, out_size, "%s", row->operation);
    return;
  }
  snprintf(out, out_size, "%s", row->name[0] ? row->name : row->operation);
}

static int benchmark_detail_value(const XrayBenchmarkResult *row, const char *key, char *out, size_t out_size) {
  if (!row || !key || !out || out_size == 0) return 0;
  out[0] = '\0';
  size_t key_len = strlen(key);
  const char *cursor = row->detail;
  while ((cursor = strstr(cursor, key)) != NULL) {
    if ((cursor == row->detail || cursor[-1] == ' ') && cursor[key_len] == '=') {
      const char *value = cursor + key_len + 1U;
      size_t length = 0;
      while (value[length] && value[length] != ' ') length++;
      if (length >= out_size) length = out_size - 1U;
      memcpy(out, value, length);
      out[length] = '\0';
      return 1;
    }
    cursor += key_len;
  }
  return 0;
}

static void append_label_token(char *label, size_t label_size, const char *prefix, const char *value) {
  if (!label || !prefix || !value || !value[0] || label_size == 0) return;
  size_t used = strlen(label);
  if (used >= label_size - 1U) return;
  snprintf(label + used, label_size - used, " %s%s", prefix, value);
}

static void benchmark_frontier_label(const XrayBenchmarkResult *row, char *out, size_t out_size) {
  benchmark_row_label(row, out, out_size);
  if (!row || strcmp(row->category, "kernel-probe") != 0) return;

  if (row->operation[0]) snprintf(out, out_size, "%s", row->operation);

  char value[48];
  if (benchmark_detail_value(row, "threshold", value, sizeof(value))) {
    append_label_token(out, out_size, "thr=", value);
  }
  if (benchmark_detail_value(row, "leafThreshold", value, sizeof(value))) {
    append_label_token(out, out_size, "leaf=", value);
  }
  if (benchmark_detail_value(row, "depthLimit", value, sizeof(value))) {
    append_label_token(out, out_size, "depth=", value);
  }
  if (benchmark_detail_value(row, "mode", value, sizeof(value))) {
    append_label_token(out, out_size, "mode=", value);
  }
  if (benchmark_detail_value(row, "baseline", value, sizeof(value))) {
    append_label_token(out, out_size, "base=", value);
  }
}

char *xray_benchmark_frontier_text(const XrayBenchmarkReport *report) {
  if (!report || !report->result_count) {
    char *empty = (char *)calloc(256, 1);
    if (empty) {
      snprintf(empty, 256,
        "BENCHMARK FRONTIER\n"
        "Run Proof with benchmarks enabled to populate scratch-vs-GMP/MPIR timings, CPU feature gates, and frontier rows.\n");
    }
    return empty;
  }

  JsonBuffer buffer = {0};
  const XrayBenchmarkResult *near_wins[5] = {0};
  const XrayBenchmarkResult *top_gaps[5] = {0};
  size_t near_count = 0;

  for (size_t index = 0; index < report->result_count; ++index) {
    const XrayBenchmarkResult *row = &report->results[index];
    if (strcmp(row->category, "scratch-vs-gmp") != 0 || row->replacement_ready) continue;
    if (row->parity_verified && row->speed_ratio > 0.0 && row->speed_ratio <= 1.10) {
      for (size_t slot = 0; slot < sizeof(near_wins) / sizeof(near_wins[0]); ++slot) {
        if (!near_wins[slot] || row->speed_ratio < near_wins[slot]->speed_ratio) {
          for (size_t move = sizeof(near_wins) / sizeof(near_wins[0]) - 1; move > slot; --move) {
            near_wins[move] = near_wins[move - 1];
          }
          near_wins[slot] = row;
          if (near_count < sizeof(near_wins) / sizeof(near_wins[0])) near_count++;
          break;
        }
      }
    }
    for (size_t slot = 0; slot < sizeof(top_gaps) / sizeof(top_gaps[0]); ++slot) {
      if (!top_gaps[slot] || row->speed_ratio > top_gaps[slot]->speed_ratio) {
        for (size_t move = sizeof(top_gaps) / sizeof(top_gaps[0]) - 1; move > slot; --move) {
          top_gaps[move] = top_gaps[move - 1];
        }
        top_gaps[slot] = row;
        break;
      }
    }
  }

  char *cpu_summary = xray_cpu_features_summary(&report->cpu);
  jb_append(&buffer, "BENCHMARK FRONTIER\n");
  jb_printf(&buffer, "%s\n", cpu_summary ? cpu_summary : "CPU: unavailable");
  jb_printf(&buffer, "Baseline backend: %s %s (%s)\n",
    xray_bignum_backend_name(),
    xray_bignum_backend_version(),
    xray_bignum_backend_library());
  XrayBigIntRouteConfig route = xray_bigint_route_config();
  jb_printf(&buffer,
    "Bigint route: word=%ub | karatsuba>=%zu limbs | format-horner>=%zu limbs | mul-unroll4=%s %zu..%zu limbs | msvc128=%s\n",
    route.word_bits,
    route.karatsuba_threshold_limbs,
    route.decimal_horner_min_limbs,
    route.mul_unroll4_route_enabled ? "on" : "off",
    route.mul_unroll4_route_min_limbs,
    route.mul_unroll4_route_max_limbs,
    route.msvc_uint128_helpers ? "yes" : "no");
  free(cpu_summary);
  jb_printf(&buffer,
    "Passed: %zu/%zu   Scratch rows: %zu   Replacement-ready: %zu   Oracle-only: %zu   Blocked: %zu   Elapsed: %lums\n\n",
    report->passed_count,
    report->result_count,
    report->scratch_count,
    report->replacement_ready_count,
    report->oracle_only_count,
    report->blocked_count,
    report->elapsed_ms);

  jb_append(&buffer,
    "FRONTIER SUMMARY\n"
    "Ready rows are locally safe replacements. Oracle-only rows are evidence, not proof-routing permission.\n");
  if (near_count) {
    jb_append(&buffer, "Near wins needing stability:\n");
    for (size_t index = 0; index < near_count; ++index) {
      const XrayBenchmarkResult *row = near_wins[index];
      char label[80];
      benchmark_frontier_label(row, label, sizeof(label));
      jb_printf(&buffer,
        "  %-24s %5zu digits   ratio %.3f   stable %zu/%zu   %s\n",
        label,
        row->digits,
        row->speed_ratio,
        row->stable_sample_count,
        row->sample_count,
        row->adoption);
    }
  } else {
    jb_append(&buffer, "Near wins needing stability: none in this run\n");
  }

  jb_append(&buffer, "Largest scratch gaps:\n");
  for (size_t index = 0; index < sizeof(top_gaps) / sizeof(top_gaps[0]) && top_gaps[index]; ++index) {
    const XrayBenchmarkResult *row = top_gaps[index];
    char label[80];
    benchmark_frontier_label(row, label, sizeof(label));
    jb_printf(&buffer,
      "  %-24s %5zu digits   ratio %.3f   stable %zu/%zu   %s\n",
      label,
      row->digits,
      row->speed_ratio,
      row->stable_sample_count,
      row->sample_count,
      row->adoption);
  }

  jb_append(&buffer,
    "\nSCRATCH VS ");
  jb_append(&buffer, xray_bignum_backend_name());
  jb_append(&buffer,
    "\nOperation                  Digits   Adoption       Ready    ScratchUs   BackendUs   Ratio   Stable\n"
    "------------------------   ------   ------------   -----   ----------   --------   -----   ------\n");
  for (size_t index = 0; index < report->result_count; ++index) {
    const XrayBenchmarkResult *row = &report->results[index];
    if (strcmp(row->category, "scratch-vs-gmp") != 0) continue;
    char label[80];
    benchmark_frontier_label(row, label, sizeof(label));
    jb_printf(&buffer,
      "%-24s   %6zu   %-12s   %-5s   %10llu   %8llu   %5.2f   %3zu/%-3zu\n",
      label,
      row->digits,
      row->adoption,
      row->replacement_ready ? "yes" : "no",
      row->scratch_us,
      row->gmp_us,
      row->speed_ratio,
      row->stable_sample_count,
      row->sample_count);
  }

  jb_append(&buffer,
    "\nKERNEL PROBES\n"
    "Operation                                  Digits   Status              Adoption               Ratio   Stable\n"
    "----------------------------------------   ------   ----------------   --------------------   -----   ------\n");
  for (size_t index = 0; index < report->result_count; ++index) {
    const XrayBenchmarkResult *row = &report->results[index];
    if (strcmp(row->category, "kernel-probe") != 0) continue;
    char label[80];
    benchmark_frontier_label(row, label, sizeof(label));
    jb_printf(&buffer,
      "%-40s   %6zu   %-16s   %-20s   %5.2f   %3zu/%-3zu\n",
      label,
      row->digits,
      row->status,
      row->adoption,
      row->speed_ratio,
      row->stable_sample_count,
      row->sample_count);
  }

  jb_append(&buffer,
    "\nRule: replacements require exact parity, a same-run paired-median speed win inside the configured gate, and enough paired-sample wins. Ordinary five-sample rows require 4 stable wins; deep nine-sample rows require 8.\n");
  return jb_take(&buffer);
}

char *xray_workbench_full_report_json(const XrayWorkbenchReport *report) {
  JsonBuffer buffer = {0};
  jb_append(&buffer, "{");
  jb_append(&buffer, "\"app\":\"Number X-Ray Workbench\",");
  jb_append(&buffer, "\"version\":"); jb_string(&buffer, XRAY_VERSION);
  jb_append(&buffer, ",\"runDir\":"); jb_string(&buffer, report ? report->run_dir : NULL);
  jb_append(&buffer, ",");
  append_cpu_json(&buffer, "cpu", report ? &report->cpu : NULL);
  jb_append(&buffer, ",\"sourceNotes\":[");
  jb_string(&buffer, "Built from Payam's MY GFN2 page (https://amathz.com/my_gfn.html): Number X-Ray runs cyclotomic construction backward and labels evidence unless exact verification passes.");
  jb_append(&buffer, ",");
  jb_string(&buffer, "GNFS stage-proof artifacts are scaffolding for an in-house implementation; they are not external CADO-NFS execution.");
  jb_append(&buffer, "],");
  append_expression_json(&buffer, report ? &report->expression : NULL);
  jb_append(&buffer, ",");
  if (report && report->factor.input) append_factor_json(&buffer, &report->factor);
  else jb_append(&buffer, "\"factorReport\":null");
  jb_append(&buffer, ",");
  if (report && report->cyclotomic.input) append_cyclotomic_json(&buffer, &report->cyclotomic);
  else jb_append(&buffer, "\"cyclotomicReport\":null");
  jb_append(&buffer, ",");
  if (report && report->benchmark.result_count) append_benchmark_json(&buffer, &report->benchmark);
  else jb_append(&buffer, "\"benchmarkReport\":null");
  jb_append(&buffer, ",");
  append_gnfs_json(&buffer, report ? &report->gnfs : NULL);
  jb_append(&buffer, "}");
  return jb_take(&buffer);
}

char *xray_workbench_run_json(const char *raw_input) {
  XrayWorkbenchReport report;
  XrayRunConfig config = xray_run_default_config();
  config.enable_benchmark = 0;
  xray_workbench_run(raw_input, &config, &report);
  char *json = xray_workbench_full_report_json(&report);
  xray_workbench_report_clear(&report);
  return json;
}

char *xray_workbench_report_json(const XrayFactorReport *factor, const XrayCyclotomicReport *cyclotomic, const XrayBenchmarkReport *benchmark) {
  JsonBuffer buffer = {0};
  jb_append(&buffer, "{");
  jb_append(&buffer, "\"app\":\"Number X-Ray Workbench\",");
  jb_append(&buffer, "\"version\":"); jb_string(&buffer, XRAY_VERSION);
  jb_append(&buffer, ",");
  XrayCpuFeatures cpu;
  xray_cpu_features_detect(&cpu);
  append_cpu_json(&buffer, "cpu", &cpu);
  jb_append(&buffer, ",\"sourceNotes\":[");
  jb_string(&buffer, "Built from Payam's MY GFN2 page (https://amathz.com/my_gfn.html): Payam builds from Fermat-style and generalized Mersenne chains to principal cyclotomic parts, summarized as Phi(n)(2^p^m); Number X-Ray runs the idea backward and labels evidence unless exact verification passes.");
  jb_append(&buffer, ",");
  jb_string(&buffer, "RSA-260 remains unsolved locally unless exact factors are found and product verification passes.");
  jb_append(&buffer, "],");
  if (factor) append_factor_json(&buffer, factor);
  else jb_append(&buffer, "\"factorReport\":null");
  jb_append(&buffer, ",");
  if (cyclotomic) append_cyclotomic_json(&buffer, cyclotomic);
  else jb_append(&buffer, "\"cyclotomicReport\":null");
  jb_append(&buffer, ",");
  if (benchmark) append_benchmark_json(&buffer, benchmark);
  else jb_append(&buffer, "\"benchmarkReport\":null");
  jb_append(&buffer, "}");
  return jb_take(&buffer);
}
