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
  jb_printf(buffer, ",\"config\":{\"trialLimit\":%lu,\"fermatIterations\":%lu,\"rhoIterations\":%lu,\"maxPasses\":%lu,\"timeBudgetMs\":%lu}",
    report->config.trial_limit,
    report->config.fermat_iterations,
    report->config.rho_iterations,
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

static void append_benchmark_json(JsonBuffer *buffer, const XrayBenchmarkReport *report) {
  jb_append(buffer, "\"benchmarkReport\":{");
  jb_printf(buffer, "\"passed\":%zu,\"total\":%zu,\"elapsedMs\":%lu,\"results\":[",
    report->passed_count,
    report->result_count,
    report->elapsed_ms);
  for (size_t index = 0; index < report->result_count; ++index) {
    if (index) jb_append(buffer, ",");
    jb_append(buffer, "{\"name\":"); jb_string(buffer, report->results[index].name);
    jb_append(buffer, ",\"status\":"); jb_string(buffer, report->results[index].status);
    jb_printf(buffer, ",\"passed\":%s,\"elapsedMs\":%lu,\"detail\":",
      report->results[index].passed ? "true" : "false",
      report->results[index].elapsed_ms);
    jb_string(buffer, report->results[index].detail);
    jb_append(buffer, "}");
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

char *xray_cyclotomic_report_json(const XrayCyclotomicReport *report) {
  JsonBuffer buffer = {0};
  jb_append(&buffer, "{");
  append_cyclotomic_json(&buffer, report);
  jb_append(&buffer, "}");
  return jb_take(&buffer);
}

char *xray_benchmark_report_json(const XrayBenchmarkReport *report) {
  JsonBuffer buffer = {0};
  jb_append(&buffer, "{");
  append_benchmark_json(&buffer, report);
  jb_append(&buffer, "}");
  return jb_take(&buffer);
}

char *xray_workbench_report_json(const XrayFactorReport *factor, const XrayCyclotomicReport *cyclotomic, const XrayBenchmarkReport *benchmark) {
  JsonBuffer buffer = {0};
  jb_append(&buffer, "{");
  jb_append(&buffer, "\"app\":\"Number X-Ray Workbench\",");
  jb_append(&buffer, "\"version\":"); jb_string(&buffer, XRAY_VERSION);
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
