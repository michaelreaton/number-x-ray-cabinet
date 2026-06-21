#include "xray_workbench.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct CompareBuffer {
  char *data;
  size_t length;
  size_t capacity;
} CompareBuffer;

typedef struct CompareRow {
  char *category;
  char *key;
  char *name;
  char *operation;
  char *display;
  char *variant;
  char *status;
  char *adoption;
  char *detail;
  char *build_config;
  char *ipo;
  char *compiler;
  char *compiler_version;
  size_t digits;
  size_t stable_sample_count;
  size_t sample_count;
  double speed_ratio;
  double worst_pair_ratio;
  int parity_verified;
  int replacement_ready;
} CompareRow;

typedef struct CompareSet {
  CompareRow *rows;
  size_t count;
  char fingerprint[256];
  char error[160];
} CompareSet;

typedef struct ComparePair {
  const CompareRow *left;
  const CompareRow *right;
  double score;
} ComparePair;

typedef struct ProgressClass {
  const char *primary_lane;
  int route_candidate;
  int route_completed;
  int route_open;
  int product_gated;
  int setup_context;
  int warmup_review;
  int lower_bound;
  int run_failed;
  int safety_rejected;
  int baseline;
  int control;
  int noisy_control;
  int promotion_ready;
} ProgressClass;

static void append_progress_tsv_field(CompareBuffer *buffer, const char *text);

static int cb_reserve(CompareBuffer *buffer, size_t extra) {
  size_t needed = buffer->length + extra + 1U;
  if (needed <= buffer->capacity) return 1;
  size_t next_capacity = buffer->capacity ? buffer->capacity * 2U : 1024U;
  while (next_capacity < needed) next_capacity *= 2U;
  char *next = (char *)realloc(buffer->data, next_capacity);
  if (!next) return 0;
  buffer->data = next;
  buffer->capacity = next_capacity;
  return 1;
}

static int cb_append(CompareBuffer *buffer, const char *text) {
  if (!text) text = "";
  size_t length = strlen(text);
  if (!cb_reserve(buffer, length)) return 0;
  memcpy(buffer->data + buffer->length, text, length);
  buffer->length += length;
  buffer->data[buffer->length] = '\0';
  return 1;
}

static int cb_printf(CompareBuffer *buffer, const char *format, ...) {
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
  if (!cb_reserve(buffer, (size_t)needed)) {
    va_end(args);
    return 0;
  }
  vsnprintf(buffer->data + buffer->length, buffer->capacity - buffer->length, format, args);
  buffer->length += (size_t)needed;
  va_end(args);
  return 1;
}

static char *cb_take(CompareBuffer *buffer) {
  if (!buffer->data) cb_append(buffer, "");
  char *data = buffer->data;
  buffer->data = NULL;
  buffer->length = 0;
  buffer->capacity = 0;
  return data;
}

static char *compare_strdup_range(const char *text, size_t length) {
  char *copy = (char *)calloc(length + 1U, 1);
  if (!copy) return NULL;
  if (length) memcpy(copy, text, length);
  return copy;
}

static char *compare_strdup(const char *text) {
  return compare_strdup_range(text ? text : "", text ? strlen(text) : 0U);
}

static int compare_streq(const char *left, const char *right) {
  return strcmp(left ? left : "", right ? right : "") == 0;
}

static int compare_contains(const char *text, const char *token) {
  return text && token && strstr(text, token) != NULL;
}

static int compare_starts_with(const char *text, const char *prefix) {
  if (!text || !prefix) return 0;
  size_t prefix_length = strlen(prefix);
  return strncmp(text, prefix, prefix_length) == 0;
}

static int parse_bool_field(const char *text) {
  return compare_streq(text, "true") || compare_streq(text, "1") || compare_streq(text, "yes");
}

static size_t parse_size_field(const char *text) {
  return (size_t)strtoull(text ? text : "0", NULL, 10);
}

static double parse_double_field(const char *text) {
  return strtod(text ? text : "0", NULL);
}

static size_t split_tsv_line(char *line, char **fields, size_t max_fields) {
  size_t count = 0;
  char *cursor = line;
  while (cursor && count < max_fields) {
    fields[count++] = cursor;
    char *tab = strchr(cursor, '\t');
    if (!tab) break;
    *tab = '\0';
    cursor = tab + 1;
  }
  return count;
}

static int column_index(char **fields, size_t field_count, const char *name) {
  for (size_t index = 0; index < field_count; ++index) {
    if (compare_streq(fields[index], name)) return (int)index;
  }
  return -1;
}

static const char *field_at(char **fields, size_t field_count, int column) {
  if (column < 0 || (size_t)column >= field_count) return "";
  return fields[column] ? fields[column] : "";
}

static int detail_value(const char *detail, const char *key, char *out, size_t out_size) {
  if (!detail || !key || !out || out_size == 0) return 0;
  out[0] = '\0';
  size_t key_length = strlen(key);
  const char *cursor = detail;
  while ((cursor = strstr(cursor, key)) != NULL) {
    if ((cursor == detail || cursor[-1] == ' ') && cursor[key_length] == '=') {
      const char *value = cursor + key_length + 1U;
      size_t length = 0;
      while (value[length] && value[length] != ' ') length++;
      if (length >= out_size) length = out_size - 1U;
      memcpy(out, value, length);
      out[length] = '\0';
      return 1;
    }
    cursor += key_length;
  }
  return 0;
}

static void append_variant_token(char *out, size_t out_size, const char *name, const char *value) {
  if (!out || out_size == 0 || !name || !value || !value[0]) return;
  size_t used = strlen(out);
  if (used >= out_size - 1U) return;
  snprintf(out + used, out_size - used, "%s%s=%s", used ? " " : "", name, value);
}

static char *make_row_variant(const char *detail) {
  char variant[256] = {0};
  char value[96];
  const char *keys[] = {
    "policy",
    "threshold",
    "leafThreshold",
    "depthLimit",
    "chunkDigits",
    "powerChunks",
    "mode",
    "baseline",
    "featureGate",
    "candidate"
  };
  for (size_t index = 0; index < sizeof(keys) / sizeof(keys[0]); ++index) {
    if (detail_value(detail, keys[index], value, sizeof(value))) {
      append_variant_token(variant, sizeof(variant), keys[index], value);
    }
  }
  return compare_strdup(variant);
}

static char *make_display_label(const char *operation, const char *variant) {
  const char *op = operation && operation[0] ? operation : "unknown";
  if (!variant || !variant[0]) return compare_strdup(op);
  int needed = snprintf(NULL, 0, "%s %s", op, variant);
  if (needed < 0) return NULL;
  char *display = (char *)calloc((size_t)needed + 1U, 1);
  if (!display) return NULL;
  snprintf(display, (size_t)needed + 1U, "%s %s", op, variant);
  return display;
}

static char *make_row_key(const char *operation, const char *name, size_t digits, const char *variant) {
  const char *identity = variant && variant[0] ? variant : (name ? name : "");
  int needed = snprintf(NULL, 0, "%s|%zu|%s", operation ? operation : "", digits, identity);
  if (needed < 0) return NULL;
  char *key = (char *)calloc((size_t)needed + 1U, 1);
  if (!key) return NULL;
  snprintf(key, (size_t)needed + 1U, "%s|%zu|%s", operation ? operation : "", digits, identity);
  return key;
}

static void compare_row_clear(CompareRow *row) {
  if (!row) return;
  free(row->category);
  free(row->key);
  free(row->name);
  free(row->operation);
  free(row->display);
  free(row->variant);
  free(row->status);
  free(row->adoption);
  free(row->detail);
  free(row->build_config);
  free(row->ipo);
  free(row->compiler);
  free(row->compiler_version);
  memset(row, 0, sizeof(*row));
}

static void compare_set_clear(CompareSet *set) {
  if (!set) return;
  for (size_t index = 0; index < set->count; ++index) compare_row_clear(&set->rows[index]);
  free(set->rows);
  memset(set, 0, sizeof(*set));
}

static int append_compare_row(CompareSet *set, const CompareRow *row) {
  CompareRow *next = (CompareRow *)realloc(set->rows, sizeof(CompareRow) * (set->count + 1U));
  if (!next) return 0;
  set->rows = next;
  set->rows[set->count++] = *row;
  return 1;
}

static char *next_line(char **cursor) {
  if (!cursor || !*cursor || !**cursor) return NULL;
  char *line = *cursor;
  char *end = line;
  while (*end && *end != '\n' && *end != '\r') end++;
  if (*end) {
    *end++ = '\0';
    if (*end == '\n' || *end == '\r') end++;
  }
  *cursor = end;
  return line;
}

static int parse_compare_set(const char *tsv, CompareSet *set, const char *label) {
  memset(set, 0, sizeof(*set));
  if (!tsv || !tsv[0]) {
    snprintf(set->error, sizeof(set->error), "%s TSV is empty", label ? label : "input");
    return 0;
  }

  char *text = compare_strdup(tsv);
  if (!text) {
    snprintf(set->error, sizeof(set->error), "%s TSV allocation failed", label ? label : "input");
    return 0;
  }

  char *cursor = text;
  char *header = next_line(&cursor);
  char *header_fields[64] = {0};
  size_t header_count = split_tsv_line(header, header_fields, sizeof(header_fields) / sizeof(header_fields[0]));
  int col_category = column_index(header_fields, header_count, "category");
  int col_name = column_index(header_fields, header_count, "name");
  int col_operation = column_index(header_fields, header_count, "operation");
  int col_digits = column_index(header_fields, header_count, "digits");
  int col_status = column_index(header_fields, header_count, "status");
  int col_parity = column_index(header_fields, header_count, "parityVerified");
  int col_ready = column_index(header_fields, header_count, "replacementReady");
  int col_adoption = column_index(header_fields, header_count, "adoption");
  int col_ratio = column_index(header_fields, header_count, "speedRatio");
  int col_worst = column_index(header_fields, header_count, "worstPairRatio");
  int col_stable = column_index(header_fields, header_count, "stableSampleCount");
  int col_samples = column_index(header_fields, header_count, "sampleCount");
  int col_detail = column_index(header_fields, header_count, "detail");
  int col_build = column_index(header_fields, header_count, "buildConfig");
  int col_ipo = column_index(header_fields, header_count, "ipo");
  int col_compiler = column_index(header_fields, header_count, "compiler");
  int col_compiler_version = column_index(header_fields, header_count, "compilerVersion");

  if (col_name < 0 || col_operation < 0 || col_digits < 0 || col_ratio < 0) {
    snprintf(set->error, sizeof(set->error), "%s TSV is missing required benchmark columns", label ? label : "input");
    free(text);
    return 0;
  }

  for (char *line = next_line(&cursor); line; line = next_line(&cursor)) {
    if (!line[0]) continue;
    char *fields[64] = {0};
    size_t field_count = split_tsv_line(line, fields, sizeof(fields) / sizeof(fields[0]));
    CompareRow row;
    memset(&row, 0, sizeof(row));
    row.category = compare_strdup(field_at(fields, field_count, col_category));
    row.name = compare_strdup(field_at(fields, field_count, col_name));
    row.operation = compare_strdup(field_at(fields, field_count, col_operation));
    row.detail = compare_strdup(field_at(fields, field_count, col_detail));
    row.variant = make_row_variant(row.detail);
    row.display = make_display_label(row.operation, row.variant);
    row.status = compare_strdup(field_at(fields, field_count, col_status));
    row.adoption = compare_strdup(field_at(fields, field_count, col_adoption));
    row.build_config = compare_strdup(field_at(fields, field_count, col_build));
    row.ipo = compare_strdup(field_at(fields, field_count, col_ipo));
    row.compiler = compare_strdup(field_at(fields, field_count, col_compiler));
    row.compiler_version = compare_strdup(field_at(fields, field_count, col_compiler_version));
    row.digits = parse_size_field(field_at(fields, field_count, col_digits));
    row.stable_sample_count = parse_size_field(field_at(fields, field_count, col_stable));
    row.sample_count = parse_size_field(field_at(fields, field_count, col_samples));
    row.speed_ratio = parse_double_field(field_at(fields, field_count, col_ratio));
    row.worst_pair_ratio = parse_double_field(field_at(fields, field_count, col_worst));
    row.parity_verified = parse_bool_field(field_at(fields, field_count, col_parity));
    row.replacement_ready = parse_bool_field(field_at(fields, field_count, col_ready));
    row.key = make_row_key(row.operation, row.name, row.digits, row.variant);
    if (!row.category || !row.name || !row.operation || !row.display || !row.variant || !row.status || !row.adoption ||
        !row.detail || !row.build_config || !row.ipo || !row.compiler || !row.compiler_version || !row.key ||
        !append_compare_row(set, &row)) {
      compare_row_clear(&row);
      compare_set_clear(set);
      snprintf(set->error, sizeof(set->error), "%s TSV row allocation failed", label ? label : "input");
      free(text);
      return 0;
    }
  }

  if (set->count) {
    const CompareRow *first = &set->rows[0];
    snprintf(set->fingerprint, sizeof(set->fingerprint),
      "buildConfig=%s ipo=%s compiler=%s %s",
      first->build_config[0] ? first->build_config : "unknown",
      first->ipo[0] ? first->ipo : "unknown",
      first->compiler[0] ? first->compiler : "unknown",
      first->compiler_version[0] ? first->compiler_version : "");
  } else {
    snprintf(set->fingerprint, sizeof(set->fingerprint), "no rows");
  }

  free(text);
  return 1;
}

static const CompareRow *find_row(const CompareSet *set, const char *key) {
  if (!set || !key) return NULL;
  for (size_t index = 0; index < set->count; ++index) {
    if (compare_streq(set->rows[index].key, key)) return &set->rows[index];
  }
  return NULL;
}

static int median_win_worst_pair_rejected(const CompareRow *row) {
  return row && row->parity_verified && !row->replacement_ready &&
    row->speed_ratio > 0.0 && row->speed_ratio < 1.0 && row->worst_pair_ratio > 1.0;
}

static const char *row_label(const CompareRow *row) {
  if (!row) return "unknown";
  return row->display && row->display[0] ? row->display : (row->name ? row->name : "unknown");
}

static void insert_pair(ComparePair *pairs, size_t *count, size_t capacity, ComparePair pair, int descending) {
  if (!pairs || !count || capacity == 0) return;
  size_t slot = *count;
  if (*count < capacity) {
    (*count)++;
  } else {
    size_t tail = capacity - 1U;
    int better_than_tail = descending ? pair.score > pairs[tail].score : pair.score < pairs[tail].score;
    if (!better_than_tail) return;
    slot = tail;
  }
  while (slot > 0) {
    int move = descending ? pair.score > pairs[slot - 1U].score : pair.score < pairs[slot - 1U].score;
    if (!move) break;
    pairs[slot] = pairs[slot - 1U];
    slot--;
  }
  pairs[slot] = pair;
}

static void append_pair_line(CompareBuffer *buffer, const ComparePair *pair) {
  const CompareRow *left = pair->left;
  const CompareRow *right = pair->right;
  cb_printf(buffer,
    "  %-24s %6zu digits   L %.3f/w%.3f %zu/%zu %-15s   R %.3f/w%.3f %zu/%zu %-15s\n",
    row_label(left),
    left ? left->digits : (right ? right->digits : 0U),
    left ? left->speed_ratio : 0.0,
    left ? left->worst_pair_ratio : 0.0,
    left ? left->stable_sample_count : 0U,
    left ? left->sample_count : 0U,
    left && left->adoption ? left->adoption : "",
    right ? right->speed_ratio : 0.0,
    right ? right->worst_pair_ratio : 0.0,
    right ? right->stable_sample_count : 0U,
    right ? right->sample_count : 0U,
    right && right->adoption ? right->adoption : "");
}

static int compare_row_is_promotion_ready(const CompareRow *row) {
  if (!row) return 0;
  return row->replacement_ready ||
    compare_streq(row->adoption, "allowed") ||
    compare_streq(row->adoption, "promotion-ready") ||
    compare_contains(row->adoption, "promote") ||
    compare_streq(row->status, "replacement-ready") ||
    compare_streq(row->status, "policy-ready") ||
    compare_contains(row->status, "promote");
}

static int compare_row_is_oracle_only(const CompareRow *row) {
  if (!row) return 0;
  return compare_streq(row->adoption, "oracle-only") ||
    compare_streq(row->status, "oracle-only");
}

static int compare_row_is_safety_rejected(const CompareRow *row) {
  if (!row) return 0;
  return compare_contains(row->status, "safety-rejected") ||
    compare_contains(row->status, "safety-blocked") ||
    compare_contains(row->status, "regression") ||
    compare_contains(row->status, "neighbor") ||
    compare_contains(row->status, "blocked") ||
    compare_contains(row->status, "mismatch") ||
    compare_contains(row->adoption, "safety-rejected") ||
    compare_contains(row->adoption, "safety-blocked") ||
    compare_contains(row->adoption, "regression") ||
    compare_contains(row->adoption, "neighbor") ||
    compare_contains(row->adoption, "blocked") ||
    compare_contains(row->adoption, "mismatch");
}

static int compare_row_is_lower_bound(const CompareRow *row) {
  if (!row) return 0;
  return compare_contains(row->status, "timeout") ||
    compare_contains(row->status, "lower-bound") ||
    compare_contains(row->status, "no-complete-run") ||
    compare_contains(row->status, "incomplete") ||
    compare_contains(row->adoption, "timeout") ||
    compare_contains(row->adoption, "lower-bound") ||
    compare_contains(row->adoption, "no-complete-run") ||
    compare_contains(row->adoption, "incomplete") ||
    compare_contains(row->detail, "timeout lower-bound") ||
    compare_contains(row->detail, "Status=timeout") ||
    compare_contains(row->detail, "CompletedRuns=0");
}

static int compare_row_is_run_failed(const CompareRow *row) {
  if (!row) return 0;
  return compare_contains(row->status, "run failed") ||
    compare_contains(row->status, "run-failed") ||
    compare_contains(row->status, "run_failed") ||
    compare_contains(row->adoption, "run failed") ||
    compare_contains(row->adoption, "run-failed") ||
    compare_contains(row->adoption, "run_failed") ||
    compare_contains(row->detail, "Status=run failed") ||
    compare_contains(row->detail, "Status=run-failed") ||
    compare_contains(row->detail, "Status=run_failed") ||
    compare_contains(row->detail, "RunFailed=true") ||
    compare_contains(row->detail, "RunFailed=1");
}

static int compare_row_is_warmup_review(const CompareRow *row) {
  if (!row) return 0;
  return compare_contains(row->status, "warmup-review") ||
    compare_contains(row->status, "review-warmup") ||
    compare_contains(row->adoption, "warmup-review") ||
    compare_contains(row->adoption, "review-warmup") ||
    compare_contains(row->detail, "WarmupPolicy=review-warmup") ||
    compare_contains(row->detail, "warmupPolicy=review-warmup") ||
    compare_contains(row->detail, "setupPolicy=review-warmup");
}

static int compare_row_has_setup_context(const CompareRow *row) {
  if (!row) return 0;
  return compare_contains(row->detail, "setupPolicy=reported-not-scored") ||
    compare_contains(row->detail, "WarmupPolicy=not-counted") ||
    compare_contains(row->detail, "warmupPolicy=not-counted") ||
    compare_contains(row->detail, "setupSeconds=") ||
    compare_contains(row->detail, "SetupSeconds=") ||
    compare_contains(row->detail, "setupUs=") ||
    compare_contains(row->detail, "setupMs=") ||
    compare_contains(row->detail, "setupSamples=") ||
    compare_contains(row->detail, "setupIterations=") ||
    compare_contains(row->detail, "warmup_s=") ||
    compare_contains(row->detail, "WarmupSecondsMedian=") ||
    compare_contains(row->detail, "EstimatedWarmupSeconds=") ||
    compare_contains(row->detail, "HelperWarmupSeconds=");
}

static int compare_row_detail_seconds(
  const CompareRow *row,
  const char *key,
  double scale,
  double *seconds
) {
  if (!row || !key || !seconds) return 0;
  char value[96];
  if (!detail_value(row->detail, key, value, sizeof(value))) return 0;
  char *end = NULL;
  double parsed = strtod(value, &end);
  if (end == value || parsed < 0.0) return 0;
  *seconds = parsed * scale;
  return 1;
}

static double compare_row_setup_seconds(const CompareRow *row) {
  double seconds = 0.0;
  if (!row) return seconds;
  if (compare_row_detail_seconds(row, "setupSeconds", 1.0, &seconds) ||
      compare_row_detail_seconds(row, "SetupSeconds", 1.0, &seconds)) {
    return seconds;
  }
  const struct {
    const char *key;
    double scale;
  } measured_keys[] = {
    {"setupUs", 0.000001},
    {"setupMs", 0.001},
    {"warmup_s", 1.0},
    {"WarmupSecondsMedian", 1.0},
    {"HelperWarmupSeconds", 1.0}
  };
  double best = 0.0;
  for (size_t index = 0; index < sizeof(measured_keys) / sizeof(measured_keys[0]); ++index) {
    double candidate = 0.0;
    if (compare_row_detail_seconds(row, measured_keys[index].key, measured_keys[index].scale, &candidate) &&
        candidate > best) {
      best = candidate;
    }
  }
  return best;
}

static int compare_row_detail_size(const CompareRow *row, const char *key, size_t *value) {
  if (!row || !key || !value) return 0;
  char text[96];
  if (!detail_value(row->detail, key, text, sizeof(text))) return 0;
  char *end = NULL;
  unsigned long long parsed = strtoull(text, &end, 10);
  if (end == text) return 0;
  *value = (size_t)parsed;
  return 1;
}

static void compare_row_run_counts(
  const CompareRow *row,
  size_t *attempted_runs,
  size_t *completed_runs
) {
  if (attempted_runs) *attempted_runs = 0;
  if (completed_runs) *completed_runs = 0;
  if (!row) return;
  size_t value = 0;
  if (attempted_runs && compare_row_detail_size(row, "Runs", &value)) {
    *attempted_runs = value;
  }
  if (completed_runs && compare_row_detail_size(row, "CompletedRuns", &value)) {
    *completed_runs = value;
  }
}

static const char *compare_row_digit_band(size_t digits) {
  if (digits <= 150U) return "small";
  if (digits <= 1000U) return "medium";
  if (digits <= 8192U) return "large";
  if (digits <= 16384U) return "xlarge";
  return "frontier";
}

static const char *compare_row_workload_shape(const CompareRow *row) {
  if (!row) return "unknown";
  if (compare_contains(row->detail, "sparse-zero") ||
      compare_contains(row->detail, "sparse-pair") ||
      compare_contains(row->detail, "sparse-square") ||
      compare_contains(row->detail, "sparse-product")) {
    return compare_contains(row->operation, "square") ? "sparse-square" : "sparse-multiply";
  }
  if (compare_contains(row->operation, "format")) return "decimal-format";
  if (compare_contains(row->operation, "parse")) return "decimal-parse";
  if (compare_contains(row->operation, "frontier") ||
      compare_contains(row->detail, "frontier") ||
      compare_contains(row->detail, "mfast")) {
    return "frontier-scout";
  }
  if (compare_contains(row->operation, "square")) return "dense-square";
  if (compare_contains(row->operation, "mul")) return "dense-multiply";
  if (compare_contains(row->operation, "divmod") ||
      compare_contains(row->operation, "division") ||
      compare_contains(row->operation, "qhat") ||
      compare_contains(row->detail, "division-qhat")) {
    return "division";
  }
  if (compare_contains(row->operation, "powmod") ||
      compare_contains(row->operation, "mod-u32") ||
      compare_contains(row->operation, "gcd-u32")) {
    return "single-limb-modular";
  }
  return "general";
}

static void append_detail_value_or_empty(CompareBuffer *buffer, const CompareRow *row, const char *key) {
  char value[160] = {0};
  if (row && key) detail_value(row->detail, key, value, sizeof(value));
  append_progress_tsv_field(buffer, value);
}

static int compare_row_is_noisy_control(const CompareRow *row) {
  if (!row) return 0;
  return compare_contains(row->status, "noisy-control") ||
    compare_contains(row->adoption, "noisy-control") ||
    compare_contains(row->detail, "controlSafety=noisy-control");
}

static int compare_row_is_control(const CompareRow *row) {
  if (!row) return 0;
  return compare_contains(row->detail, "duplicateControl=") ||
    compare_contains(row->detail, "IsControlSubject=true") ||
    compare_contains(row->detail, "controlSafety=") ||
    compare_contains(row->status, "duplicate-control") ||
    compare_contains(row->adoption, "duplicate-control");
}

static int compare_row_is_baseline_subject(const CompareRow *row) {
  if (!row) return 0;
  char value[128];
  if (detail_value(row->detail, "policy", value, sizeof(value)) &&
      compare_streq(value, "current-default")) {
    return 1;
  }
  if (detail_value(row->detail, "candidate", value, sizeof(value)) &&
      (compare_streq(value, "default") ||
       compare_streq(value, "release") ||
       compare_streq(value, "baseline") ||
       compare_streq(value, "gmp") ||
       compare_starts_with(value, "current-scratch-"))) {
    return 1;
  }
  return compare_contains(row->name, "current-default");
}

static int compare_row_is_product_gated(const CompareRow *row) {
  if (!row) return 0;
  char value[128];
  if (detail_value(row->detail, "noAutoRoute", value, sizeof(value)) &&
      (compare_streq(value, "1") || compare_streq(value, "true") || compare_streq(value, "yes"))) {
    return 1;
  }
  if (detail_value(row->detail, "thresholdSafety", value, sizeof(value)) &&
      (compare_streq(value, "requires-forced-neighbor") ||
       compare_streq(value, "forced-neighbor"))) {
    return 1;
  }
  if (detail_value(row->detail, "deepConfirmation", value, sizeof(value)) &&
      compare_streq(value, "required")) {
    return 1;
  }
  return compare_contains(row->detail, "forcedCandidate=yes");
}

static const char *compare_row_blocker_reason(const CompareRow *row) {
  if (!row) return "";
  if (compare_row_is_run_failed(row)) return "run-failed";
  if (compare_row_is_lower_bound(row)) return "lower-bound";
  if (compare_row_is_warmup_review(row)) return "warmup-review";
  if (compare_row_is_noisy_control(row)) return "noisy-control";
  if (compare_row_is_control(row)) return "control-row";
  if (compare_row_is_baseline_subject(row)) return "baseline-row";
  if (compare_contains(row->status, "mismatch") ||
      compare_contains(row->adoption, "mismatch")) {
    return "output-mismatch";
  }
  if (compare_contains(row->status, "neighbor") ||
      compare_contains(row->adoption, "neighbor")) {
    return "neighbor-regression";
  }
  if (compare_contains(row->status, "worst-pair") ||
      compare_contains(row->adoption, "worst-pair")) {
    return "worst-pair-regression";
  }
  if (compare_contains(row->status, "blocked") ||
      compare_contains(row->adoption, "blocked")) {
    return "safety-blocked";
  }
  if (compare_row_is_safety_rejected(row)) return "safety-rejected";

  char value[128];
  if (detail_value(row->detail, "thresholdSafety", value, sizeof(value)) &&
      value[0] &&
      !compare_streq(value, "passed")) {
    return compare_streq(value, "forced-neighbor") ||
      compare_streq(value, "requires-forced-neighbor") ?
      "forced-neighbor-required" : "threshold-safety-required";
  }
  if (detail_value(row->detail, "deepConfirmation", value, sizeof(value)) &&
      compare_streq(value, "required")) {
    return "deep-confirmation-required";
  }
  if (detail_value(row->detail, "noAutoRoute", value, sizeof(value)) &&
      (compare_streq(value, "1") || compare_streq(value, "true") || compare_streq(value, "yes"))) {
    return "no-auto-route";
  }
  if (compare_contains(row->detail, "forcedCandidate=yes")) return "forced-neighbor-required";
  if (row->worst_pair_ratio > 1.0) return "worst-pair-regression";
  if (!compare_row_is_promotion_ready(row)) {
    if (row->sample_count > 0 && row->stable_sample_count < row->sample_count) return "stability-shortfall";
    return "not-promotion-ready";
  }
  return "";
}

static int compare_row_has_progress_signal(const CompareRow *row) {
  if (!row) return 0;
  if (compare_row_is_lower_bound(row) ||
      compare_row_is_run_failed(row) ||
      compare_row_is_warmup_review(row)) {
    return 1;
  }
  return row->speed_ratio > 0.0 &&
    (compare_row_is_lower_bound(row) ||
     compare_row_is_warmup_review(row) ||
     row->parity_verified ||
     compare_row_is_promotion_ready(row) ||
     compare_row_is_oracle_only(row) ||
     compare_row_is_safety_rejected(row) ||
     compare_row_is_noisy_control(row));
}

static void insert_progress_row(
  ComparePair *rows,
  size_t *count,
  size_t capacity,
  const CompareRow *row,
  double score,
  int descending
) {
  ComparePair pair;
  pair.left = row;
  pair.right = NULL;
  pair.score = score;
  insert_pair(rows, count, capacity, pair, descending);
}

static double progress_open_score(const CompareRow *row) {
  if (!row) return 0.0;
  double score = row->speed_ratio;
  if (row->worst_pair_ratio > score) score = row->worst_pair_ratio;
  if (compare_row_is_noisy_control(row) && score < 1.0) score = 1.0;
  return score;
}

static ProgressClass progress_classify_row(const CompareRow *row) {
  ProgressClass progress;
  memset(&progress, 0, sizeof(progress));
  progress.primary_lane = "ignored";
  if (!row || !compare_row_has_progress_signal(row)) return progress;

  progress.run_failed = compare_row_is_run_failed(row);
  progress.lower_bound = !progress.run_failed && compare_row_is_lower_bound(row);
  progress.warmup_review = compare_row_is_warmup_review(row);
  progress.setup_context = compare_row_has_setup_context(row);
  progress.control = compare_row_is_control(row);
  progress.baseline = compare_row_is_baseline_subject(row);
  progress.noisy_control = compare_row_is_noisy_control(row);
  progress.safety_rejected = compare_row_is_safety_rejected(row);
  progress.promotion_ready = compare_row_is_promotion_ready(row);
  progress.product_gated = compare_row_is_product_gated(row);

  if (progress.run_failed) {
    progress.primary_lane = "run-failed";
    return progress;
  }
  if (progress.lower_bound) {
    progress.primary_lane = "lower-bound";
    return progress;
  }
  if (progress.warmup_review) {
    progress.primary_lane = "warmup-review";
    return progress;
  }

  if (!progress.control && !progress.baseline) {
    progress.route_candidate = 1;
  }
  if (progress.promotion_ready && !progress.control && !progress.baseline &&
      !progress.noisy_control && !progress.safety_rejected) {
    if (!progress.product_gated) {
      progress.route_completed = 1;
      progress.primary_lane = "completed";
    } else {
      progress.route_open = 1;
      progress.primary_lane = "product-gated";
    }
  } else if (!progress.control && !progress.baseline) {
    progress.route_open = 1;
    if (progress.safety_rejected) progress.primary_lane = "safety-rejected";
    else if (progress.product_gated) progress.primary_lane = "product-gated";
    else progress.primary_lane = "open";
  } else if (progress.control) {
    progress.primary_lane = "control";
  } else if (progress.baseline) {
    progress.primary_lane = "baseline";
  }

  return progress;
}

static void append_progress_line(CompareBuffer *buffer, const CompareRow *row, const char *lane) {
  if (!buffer || !row) return;
  double factor = row->speed_ratio;
  const char *motion = "slower";
  if (row->speed_ratio > 0.0 && row->speed_ratio < 1.0) {
    factor = 1.0 / row->speed_ratio;
    motion = "faster median";
  }
  cb_printf(buffer,
    "  %-36s %6zu digits   %.2fx %s   ratio %.3f   worst %.3f   stable %zu/%zu   %s   %s/%s\n",
    row_label(row),
    row->digits,
    factor,
    motion,
    row->speed_ratio,
    row->worst_pair_ratio,
    row->stable_sample_count,
    row->sample_count,
    lane ? lane : "open",
    row->status ? row->status : "",
    row->adoption ? row->adoption : "");
}

static void append_progress_blocker_line(CompareBuffer *buffer, const CompareRow *row) {
  if (!buffer || !row) return;
  double factor = row->speed_ratio > 0.0 ? 1.0 / row->speed_ratio : 0.0;
  cb_printf(buffer,
    "  %-36s %6zu digits   %.2fx faster median   ratio %.3f   worst %.3f   stable %zu/%zu   blocked=%s   %s/%s\n",
    row_label(row),
    row->digits,
    factor,
    row->speed_ratio,
    row->worst_pair_ratio,
    row->stable_sample_count,
    row->sample_count,
    compare_row_blocker_reason(row),
    row->status ? row->status : "",
    row->adoption ? row->adoption : "");
}

static void append_progress_tsv_field(CompareBuffer *buffer, const char *text) {
  if (!buffer || !text) return;
  for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
    char ch[2] = {(char)*p, 0};
    if (*p == '\t' || *p == '\r' || *p == '\n') ch[0] = ' ';
    cb_append(buffer, ch);
  }
}

static const char *progress_bool_text(int value) {
  return value ? "true" : "false";
}

char *xray_benchmark_filter_tsv_digits(const char *tsv, size_t min_digits, size_t max_digits) {
  CompareBuffer buffer = {0};
  if (!tsv) return cb_take(&buffer);
  if (!min_digits && !max_digits) return compare_strdup(tsv);

  char *text = compare_strdup(tsv);
  if (!text) return NULL;

  char *cursor = text;
  char *header = next_line(&cursor);
  if (!header) {
    free(text);
    return cb_take(&buffer);
  }

  char *header_copy = compare_strdup(header);
  if (!header_copy) {
    free(text);
    return NULL;
  }
  char *header_fields[64] = {0};
  size_t header_count = split_tsv_line(header_copy, header_fields, sizeof(header_fields) / sizeof(header_fields[0]));
  int col_digits = column_index(header_fields, header_count, "digits");
  free(header_copy);
  if (col_digits < 0) {
    free(text);
    return compare_strdup(tsv);
  }

  cb_append(&buffer, header);
  cb_append(&buffer, "\n");
  for (char *line = next_line(&cursor); line; line = next_line(&cursor)) {
    if (!line[0]) continue;
    char *line_copy = compare_strdup(line);
    if (!line_copy) {
      free(text);
      free(buffer.data);
      return NULL;
    }
    char *fields[64] = {0};
    size_t field_count = split_tsv_line(line_copy, fields, sizeof(fields) / sizeof(fields[0]));
    size_t digits = parse_size_field(field_at(fields, field_count, col_digits));
    free(line_copy);
    if ((min_digits && digits < min_digits) || (max_digits && digits > max_digits)) continue;
    cb_append(&buffer, line);
    cb_append(&buffer, "\n");
  }

  free(text);
  return cb_take(&buffer);
}

char *xray_benchmark_filter_tsv_text(const char *tsv, const char *needle) {
  CompareBuffer buffer = {0};
  if (!tsv) return cb_take(&buffer);
  if (!needle || !needle[0]) return compare_strdup(tsv);

  char *text = compare_strdup(tsv);
  if (!text) return NULL;

  char *cursor = text;
  char *header = next_line(&cursor);
  if (!header) {
    free(text);
    return cb_take(&buffer);
  }

  cb_append(&buffer, header);
  cb_append(&buffer, "\n");
  for (char *line = next_line(&cursor); line; line = next_line(&cursor)) {
    if (!line[0]) continue;
    if (!strstr(line, needle)) continue;
    cb_append(&buffer, line);
    cb_append(&buffer, "\n");
  }

  free(text);
  return cb_take(&buffer);
}

char *xray_benchmark_progress_classification_tsv(const char *tsv) {
  CompareSet set;
  CompareBuffer buffer = {0};
  int ok = parse_compare_set(tsv, &set, "benchmark");

  cb_append(&buffer,
    "category\tname\toperation\tdigits\tdisplay\tprimaryLane\trouteCandidate\trouteCompleted\trouteOpen\tproductGated\thasSetupContext\tsetupSeconds\twarmupReview\tlowerBound\trunFailed\tattemptedRuns\tcompletedRuns\tsafetyRejected\tbaseline\tcontrol\tnoisyControl\tpromotionReady\tstatus\tadoption\tspeedRatio\tworstPairRatio\tstableSampleCount\tsampleCount\tdetail\tbuildConfig\tipo\tcompiler\tcompilerVersion\tdigitBand\tworkloadShape\tpolicy\tcandidate\tactiveCandidate\tbaseline\tfeatureGate\tgmpClue\tcontrolSafety\tthresholdSafety\thashGate\tblockerReason\n");
  if (!ok) {
    cb_append(&buffer, "error\t");
    append_progress_tsv_field(&buffer, set.error);
    cb_append(&buffer, "\t\t0\terror\tinvalid\tfalse\tfalse\tfalse\tfalse\tfalse\t0.000000\tfalse\tfalse\tfalse\t0\t0\tfalse\tfalse\tfalse\tfalse\tfalse\tparse-error\t\t0.000000\t0.000000\t0\t0\t");
    append_progress_tsv_field(&buffer, set.error);
    for (size_t field = 0; field < 15U; ++field) cb_append(&buffer, "\t");
    cb_append(&buffer, "parse-error");
    cb_append(&buffer, "\n");
    return cb_take(&buffer);
  }

  for (size_t index = 0; index < set.count; ++index) {
    const CompareRow *row = &set.rows[index];
    if (!compare_row_has_progress_signal(row)) continue;
    ProgressClass progress = progress_classify_row(row);
    size_t attempted_runs = 0;
    size_t completed_runs = 0;
    compare_row_run_counts(row, &attempted_runs, &completed_runs);
    append_progress_tsv_field(&buffer, row->category);
    cb_append(&buffer, "\t");
    append_progress_tsv_field(&buffer, row->name);
    cb_append(&buffer, "\t");
    append_progress_tsv_field(&buffer, row->operation);
    cb_printf(&buffer, "\t%zu\t", row->digits);
    append_progress_tsv_field(&buffer, row_label(row));
    cb_append(&buffer, "\t");
    append_progress_tsv_field(&buffer, progress.primary_lane);
    cb_printf(&buffer,
      "\t%s\t%s\t%s\t%s\t%s",
      progress_bool_text(progress.route_candidate),
      progress_bool_text(progress.route_completed),
      progress_bool_text(progress.route_open),
      progress_bool_text(progress.product_gated),
      progress_bool_text(progress.setup_context));
    cb_printf(&buffer,
      "\t%.6f\t%s\t%s\t%s\t%zu\t%zu\t%s\t%s\t%s\t%s\t%s\t",
      compare_row_setup_seconds(row),
      progress_bool_text(progress.warmup_review),
      progress_bool_text(progress.lower_bound),
      progress_bool_text(progress.run_failed),
      attempted_runs,
      completed_runs,
      progress_bool_text(progress.safety_rejected),
      progress_bool_text(progress.baseline),
      progress_bool_text(progress.control),
      progress_bool_text(progress.noisy_control),
      progress_bool_text(progress.promotion_ready));
    append_progress_tsv_field(&buffer, row->status);
    cb_append(&buffer, "\t");
    append_progress_tsv_field(&buffer, row->adoption);
    cb_printf(&buffer, "\t%.6f\t%.6f\t%zu\t%zu\t",
      row->speed_ratio,
      row->worst_pair_ratio,
      row->stable_sample_count,
      row->sample_count);
    append_progress_tsv_field(&buffer, row->detail);
    cb_append(&buffer, "\t");
    append_progress_tsv_field(&buffer, row->build_config);
    cb_append(&buffer, "\t");
    append_progress_tsv_field(&buffer, row->ipo);
    cb_append(&buffer, "\t");
    append_progress_tsv_field(&buffer, row->compiler);
    cb_append(&buffer, "\t");
    append_progress_tsv_field(&buffer, row->compiler_version);
    cb_append(&buffer, "\t");
    append_progress_tsv_field(&buffer, compare_row_digit_band(row->digits));
    cb_append(&buffer, "\t");
    append_progress_tsv_field(&buffer, compare_row_workload_shape(row));
    cb_append(&buffer, "\t");
    append_detail_value_or_empty(&buffer, row, "policy");
    cb_append(&buffer, "\t");
    append_detail_value_or_empty(&buffer, row, "candidate");
    cb_append(&buffer, "\t");
    append_detail_value_or_empty(&buffer, row, "activeCandidate");
    cb_append(&buffer, "\t");
    append_detail_value_or_empty(&buffer, row, "baseline");
    cb_append(&buffer, "\t");
    append_detail_value_or_empty(&buffer, row, "featureGate");
    cb_append(&buffer, "\t");
    append_detail_value_or_empty(&buffer, row, "gmpClue");
    cb_append(&buffer, "\t");
    append_detail_value_or_empty(&buffer, row, "controlSafety");
    cb_append(&buffer, "\t");
    append_detail_value_or_empty(&buffer, row, "thresholdSafety");
    cb_append(&buffer, "\t");
    append_detail_value_or_empty(&buffer, row, "hashGate");
    cb_append(&buffer, "\t");
    append_progress_tsv_field(&buffer, compare_row_blocker_reason(row));
    cb_append(&buffer, "\n");
  }

  compare_set_clear(&set);
  return cb_take(&buffer);
}

char *xray_benchmark_progress_tsv_text(const char *tsv) {
  CompareSet set;
  CompareBuffer buffer = {0};
  int ok = parse_compare_set(tsv, &set, "benchmark");

  cb_append(&buffer, "BENCHMARK PROGRESS DIGEST\n");
  if (!ok) {
    cb_printf(&buffer, "Error: %s\n", set.error);
    return cb_take(&buffer);
  }

  ComparePair completed[12] = {0};
  ComparePair open_rows[12] = {0};
  ComparePair baselines[8] = {0};
  ComparePair controls[8] = {0};
  ComparePair rejected[8] = {0};
  ComparePair blockers[12] = {0};
  ComparePair product_gated[8] = {0};
  ComparePair lower_bound[8] = {0};
  ComparePair run_failed[8] = {0};
  ComparePair warmup_review[8] = {0};
  ComparePair setup_context[8] = {0};
  ComparePair large_mul_campaign[216] = {0};
  size_t completed_count = 0;
  size_t open_count = 0;
  size_t baseline_count = 0;
  size_t control_count = 0;
  size_t rejected_count = 0;
  size_t blocker_count = 0;
  size_t product_gated_count = 0;
  size_t lower_bound_count = 0;
  size_t run_failed_count = 0;
  size_t warmup_review_count = 0;
  size_t setup_context_count = 0;
  size_t large_mul_campaign_count = 0;
  size_t route_candidates_total = 0;
  size_t completed_total = 0;
  size_t open_total = 0;
  size_t baselines_total = 0;
  size_t controls_total = 0;
  size_t noisy_controls_total = 0;
  size_t rejected_total = 0;
  size_t blocker_total = 0;
  size_t product_gated_total = 0;
  size_t lower_bound_total = 0;
  size_t run_failed_total = 0;
  size_t warmup_review_total = 0;
  size_t setup_context_total = 0;
  size_t large_mul_campaign_total = 0;

  for (size_t index = 0; index < set.count; ++index) {
    const CompareRow *row = &set.rows[index];
    if (!compare_row_has_progress_signal(row)) continue;
    if (compare_streq(row->operation, "mul-large-cpu-campaign") ||
        compare_streq(row->operation, "mul-large-cpu-toom-branch") ||
        compare_streq(row->operation, "mul-large-cpu-toom-view-branch") ||
        compare_streq(row->operation, "mul-large-cpu-toom-ws-branch") ||
        compare_streq(row->operation, "mul-large-cpu-toom-full-ws") ||
        compare_streq(row->operation, "mul-large-cpu-toom-full-audit") ||
        compare_streq(row->operation, "mul-large-toom-full-deep-point") ||
        compare_streq(row->operation, "mul-large-toom-full-deep-audit") ||
        compare_streq(row->operation, "mul-large-toom-depth-point") ||
        compare_streq(row->operation, "mul-large-toom-depth-scout") ||
        compare_streq(row->operation, "mul-large-toom-leaf-point") ||
        compare_streq(row->operation, "mul-large-toom-leaf-scout") ||
        compare_streq(row->operation, "mul-large-toom-leaf48-point") ||
        compare_streq(row->operation, "mul-large-toom-leaf48-scout") ||
        compare_streq(row->operation, "mul-large-toom-div2-point") ||
        compare_streq(row->operation, "mul-large-toom-div2-scout") ||
        compare_streq(row->operation, "mul-large-toom-div3-point") ||
        compare_streq(row->operation, "mul-large-toom-div3-scout") ||
        compare_streq(row->operation, "mul-large-toom-div2-div3-point") ||
        compare_streq(row->operation, "mul-large-toom-div2-div3-scout") ||
        compare_streq(row->operation, "mul-large-toom-cmb-leaf48-point") ||
        compare_streq(row->operation, "mul-large-toom-cmb-leaf48-scout") ||
        compare_streq(row->operation, "mul-large-toom-cmb-depth3-point") ||
        compare_streq(row->operation, "mul-large-toom-cmb-depth3-scout") ||
        compare_streq(row->operation, "mul-large-toom-cmb-l48d3-point") ||
        compare_streq(row->operation, "mul-large-toom-cmb-l48d3-scout") ||
        compare_streq(row->operation, "mul-large-toom-cmb-l48d3-fpt") ||
        compare_streq(row->operation, "mul-large-toom-cmb-l48d3-full") ||
        compare_streq(row->operation, "mul-large-toom-cmb-hand-pt") ||
        compare_streq(row->operation, "mul-large-toom-cmb-hand") ||
        compare_streq(row->operation, "mul-large-toom-cmb-tourn-pt") ||
        compare_streq(row->operation, "mul-large-toom-cmb-tourn") ||
        compare_streq(row->operation, "mul-large-toom-cmb-reuse-pt") ||
        compare_streq(row->operation, "mul-large-toom-cmb-reuse") ||
        compare_streq(row->operation, "mul-large-toom-cmb-map-pt") ||
        compare_streq(row->operation, "mul-large-toom-cmb-map") ||
        compare_streq(row->operation, "mul-large-toom-cmb-map-ctrl-pt") ||
        compare_streq(row->operation, "mul-large-toom-cmb-map-ctrl") ||
        compare_streq(row->operation, "mul-large-toom-cmb-reuse-map-pt") ||
        compare_streq(row->operation, "mul-large-toom-cmb-reuse-map") ||
        compare_streq(row->operation, "mul-large-toom-cmb-ripdiv-pt") ||
        compare_streq(row->operation, "mul-large-toom-cmb-ripdiv") ||
        compare_streq(row->operation, "mul-large-toom-cmb-l48d4-point") ||
        compare_streq(row->operation, "mul-large-toom-cmb-l48d4-scout") ||
        compare_streq(row->operation, "mul-large-toom-cmb-l32d4-point") ||
        compare_streq(row->operation, "mul-large-toom-cmb-l32d4-scout") ||
        compare_streq(row->operation, "mul-large-toom-cmb-ipdiv-pt") ||
        compare_streq(row->operation, "mul-large-toom-cmb-ipdiv") ||
        compare_streq(row->operation, "mul-large-toom-cmb-l64d3-point") ||
        compare_streq(row->operation, "mul-large-toom-cmb-l64d3-scout") ||
        compare_streq(row->operation, "mul-large-toom4-top-pt") ||
        compare_streq(row->operation, "mul-large-toom4-top") ||
        compare_streq(row->operation, "mul-large-toom4-top-reuse-pt") ||
        compare_streq(row->operation, "mul-large-toom4-top-reuse") ||
        compare_streq(row->operation, "mul-large-toom4-top-handoff-pt") ||
        compare_streq(row->operation, "mul-large-toom4-top-handoff") ||
        compare_streq(row->operation, "mul-large-toom-cmb-lower-point") ||
        compare_streq(row->operation, "mul-large-toom-cmb-lower-scout") ||
        compare_streq(row->operation, "mul-large-toom-cmb-route-point") ||
        compare_streq(row->operation, "mul-large-toom-cmb-route-audit")) {
      large_mul_campaign_total++;
      insert_progress_row(
        large_mul_campaign,
        &large_mul_campaign_count,
        sizeof(large_mul_campaign) / sizeof(large_mul_campaign[0]),
        row,
        (double)row->digits,
        0);
    }
    int run_failed_row = compare_row_is_run_failed(row);
    int lower_bound_row = !run_failed_row && compare_row_is_lower_bound(row);
    int warmup_review_row = compare_row_is_warmup_review(row);
    int setup_context_row = compare_row_has_setup_context(row);
    int control = compare_row_is_control(row);
    int baseline = compare_row_is_baseline_subject(row);
    int noisy_control = compare_row_is_noisy_control(row);
    int safety_rejected = compare_row_is_safety_rejected(row);
    int ready = compare_row_is_promotion_ready(row);
    int product_gate_blocked = compare_row_is_product_gated(row);
    const char *blocker_reason = compare_row_blocker_reason(row);
    if (run_failed_row) {
      run_failed_total++;
      insert_progress_row(
        run_failed,
        &run_failed_count,
        sizeof(run_failed) / sizeof(run_failed[0]),
        row,
        progress_open_score(row),
        1);
      continue;
    }
    if (lower_bound_row) {
      lower_bound_total++;
      insert_progress_row(
        lower_bound,
        &lower_bound_count,
        sizeof(lower_bound) / sizeof(lower_bound[0]),
        row,
        progress_open_score(row),
        1);
      continue;
    }
    if (warmup_review_row) {
      warmup_review_total++;
      insert_progress_row(
        warmup_review,
        &warmup_review_count,
        sizeof(warmup_review) / sizeof(warmup_review[0]),
        row,
        progress_open_score(row),
        1);
      continue;
    }
    if (setup_context_row) {
      setup_context_total++;
      insert_progress_row(
        setup_context,
        &setup_context_count,
        sizeof(setup_context) / sizeof(setup_context[0]),
        row,
        progress_open_score(row),
        1);
    }
    if (!control && !baseline) route_candidates_total++;
    if (baseline) {
      baselines_total++;
      insert_progress_row(
        baselines,
        &baseline_count,
        sizeof(baselines) / sizeof(baselines[0]),
        row,
        progress_open_score(row),
        1);
    }
    if (control) {
      controls_total++;
      insert_progress_row(
        controls,
        &control_count,
        sizeof(controls) / sizeof(controls[0]),
        row,
        progress_open_score(row),
        1);
    }
    if (noisy_control) noisy_controls_total++;
    if (safety_rejected && !control && !baseline) {
      rejected_total++;
      insert_progress_row(
        rejected,
        &rejected_count,
        sizeof(rejected) / sizeof(rejected[0]),
        row,
        progress_open_score(row),
        1);
    }
    if (product_gate_blocked && !control && !baseline) {
      product_gated_total++;
      insert_progress_row(
        product_gated,
        &product_gated_count,
        sizeof(product_gated) / sizeof(product_gated[0]),
        row,
        progress_open_score(row),
        1);
    }
    if (!control && !baseline && row->speed_ratio > 0.0 && row->speed_ratio < 1.0 &&
        blocker_reason && blocker_reason[0] &&
        (!ready || product_gate_blocked || safety_rejected || row->worst_pair_ratio > 1.0)) {
      blocker_total++;
      insert_progress_row(
        blockers,
        &blocker_count,
        sizeof(blockers) / sizeof(blockers[0]),
        row,
        1.0 / row->speed_ratio,
        1);
    }
    if (ready && !control && !baseline && !noisy_control && !safety_rejected) {
      if (!product_gate_blocked) {
        completed_total++;
        insert_progress_row(
          completed,
          &completed_count,
          sizeof(completed) / sizeof(completed[0]),
          row,
          row->speed_ratio > 0.0 ? 1.0 / row->speed_ratio : 0.0,
          1);
      } else {
        open_total++;
        insert_progress_row(
          open_rows,
          &open_count,
          sizeof(open_rows) / sizeof(open_rows[0]),
          row,
          progress_open_score(row),
          1);
      }
    } else if (!control && !baseline) {
      open_total++;
      insert_progress_row(
        open_rows,
        &open_count,
        sizeof(open_rows) / sizeof(open_rows[0]),
        row,
        progress_open_score(row),
        1);
    }
  }

  cb_printf(&buffer, "Artifact: %s\n", set.fingerprint);
  cb_printf(&buffer,
    "Rows: total=%zu routeCandidates=%zu routeCompleted=%zu routeOpen=%zu productGatedOpen=%zu warmupReviewRows=%zu setupContextRows=%zu baselineExcluded=%zu controlsExcluded=%zu noisyControls=%zu safetyRejected=%zu lowerBoundRows=%zu runFailedRows=%zu\n\n",
    set.count,
    route_candidates_total,
    completed_total,
    open_total,
    product_gated_total,
    warmup_review_total,
    setup_context_total,
    baselines_total,
    controls_total,
    noisy_controls_total,
    rejected_total,
    lower_bound_total,
    run_failed_total);

  cb_append(&buffer, "Product/backend route candidate rows observed (completed):\n");
  if (!completed_count) cb_append(&buffer, "  none\n");
  for (size_t index = 0; index < completed_count; ++index) append_progress_line(&buffer, completed[index].left, "completed");
  if (completed_total > completed_count) cb_printf(&buffer, "  ... %zu more completed rows\n", completed_total - completed_count);

  cb_append(&buffer, "\nFast-looking promotion blockers (median win, blocked by safety/product evidence):\n");
  if (!blocker_count) cb_append(&buffer, "  none\n");
  for (size_t index = 0; index < blocker_count; ++index) append_progress_blocker_line(&buffer, blockers[index].left);
  if (blocker_total > blocker_count) cb_printf(&buffer, "  ... %zu more blocked fast-looking rows\n", blocker_total - blocker_count);

  cb_append(&buffer, "\nOpen/noisy route rows observed:\n");
  if (!open_count) cb_append(&buffer, "  none\n");
  for (size_t index = 0; index < open_count; ++index) append_progress_line(&buffer, open_rows[index].left, "open");
  if (open_total > open_count) cb_printf(&buffer, "  ... %zu more open rows\n", open_total - open_count);

  cb_append(&buffer, "\nProduct-gated route rows observed (open until product-like proof passes):\n");
  if (!product_gated_count) cb_append(&buffer, "  none\n");
  for (size_t index = 0; index < product_gated_count; ++index) append_progress_line(&buffer, product_gated[index].left, "product-gated");
  if (product_gated_total > product_gated_count) cb_printf(&buffer, "  ... %zu more product-gated rows\n", product_gated_total - product_gated_count);

  cb_append(&buffer, "\nLarge multiply CPU campaign rows observed (power-of-two anchors, in-between spots, and Toom branch rows):\n");
  if (!large_mul_campaign_count) cb_append(&buffer, "  none\n");
  for (size_t index = 0; index < large_mul_campaign_count; ++index) {
    append_progress_line(&buffer, large_mul_campaign[index].left, "campaign");
  }
  if (large_mul_campaign_total > large_mul_campaign_count) {
    cb_printf(&buffer, "  ... %zu more large multiply campaign rows\n", large_mul_campaign_total - large_mul_campaign_count);
  }

  cb_append(&buffer, "\nSetup/warmup context rows observed (reported, not scored):\n");
  if (!setup_context_count) cb_append(&buffer, "  none\n");
  for (size_t index = 0; index < setup_context_count; ++index) append_progress_line(&buffer, setup_context[index].left, "setup-context");
  if (setup_context_total > setup_context_count) cb_printf(&buffer, "  ... %zu more setup-context rows\n", setup_context_total - setup_context_count);

  cb_append(&buffer, "\nWarmup-review rows observed (setup too expensive for ordinary route progress):\n");
  if (!warmup_review_count) cb_append(&buffer, "  none\n");
  for (size_t index = 0; index < warmup_review_count; ++index) append_progress_line(&buffer, warmup_review[index].left, "warmup-review");
  if (warmup_review_total > warmup_review_count) cb_printf(&buffer, "  ... %zu more warmup-review rows\n", warmup_review_total - warmup_review_count);

  cb_append(&buffer, "\nSafety-rejected rows observed:\n");
  if (!rejected_count) cb_append(&buffer, "  none\n");
  for (size_t index = 0; index < rejected_count; ++index) append_progress_line(&buffer, rejected[index].left, "rejected");
  if (rejected_total > rejected_count) cb_printf(&buffer, "  ... %zu more rejected rows\n", rejected_total - rejected_count);

  cb_append(&buffer, "\nLower-bound/incomplete rows observed:\n");
  if (!lower_bound_count) cb_append(&buffer, "  none\n");
  for (size_t index = 0; index < lower_bound_count; ++index) append_progress_line(&buffer, lower_bound[index].left, "lower-bound");
  if (lower_bound_total > lower_bound_count) cb_printf(&buffer, "  ... %zu more lower-bound rows\n", lower_bound_total - lower_bound_count);

  cb_append(&buffer, "\nRun-failed rows observed:\n");
  if (!run_failed_count) cb_append(&buffer, "  none\n");
  for (size_t index = 0; index < run_failed_count; ++index) append_progress_line(&buffer, run_failed[index].left, "run-failed");
  if (run_failed_total > run_failed_count) cb_printf(&buffer, "  ... %zu more run-failed rows\n", run_failed_total - run_failed_count);

  cb_append(&buffer, "\nBaseline/current rows observed (excluded from route candidate totals):\n");
  if (!baseline_count) cb_append(&buffer, "  none\n");
  for (size_t index = 0; index < baseline_count; ++index) append_progress_line(&buffer, baselines[index].left, "baseline");
  if (baselines_total > baseline_count) cb_printf(&buffer, "  ... %zu more baseline rows\n", baselines_total - baseline_count);

  cb_append(&buffer, "\nControl/noise rows observed (excluded from completed candidate totals):\n");
  if (!control_count) cb_append(&buffer, "  none\n");
  for (size_t index = 0; index < control_count; ++index) append_progress_line(&buffer, controls[index].left, "control");
  if (controls_total > control_count) cb_printf(&buffer, "  ... %zu more control rows\n", controls_total - control_count);

  cb_append(&buffer,
    "\nRule: route-completed progress excludes baseline/current, duplicate-control, noisy-control, product-gated, warmup-review, lower-bound/incomplete, and run-failed rows. Setup/warmup context rows are reported for review but are not scored as throughput unless a row explicitly promotes setup into the benchmark policy. Median wins still stay open when worst-pair, control, baseline, noAutoRoute, forced-neighbor, deep-confirmation, review-warmup, timeout, run failure, or safety labels reject them.\n");

  compare_set_clear(&set);
  return cb_take(&buffer);
}

char *xray_benchmark_compare_tsv_text(const char *left_tsv, const char *right_tsv) {
  CompareSet left;
  CompareSet right;
  CompareBuffer buffer = {0};
  int left_ok = parse_compare_set(left_tsv, &left, "left");
  int right_ok = parse_compare_set(right_tsv, &right, "right");

  cb_append(&buffer, "BENCHMARK CROSS-BUILD REVIEW\n");
  if (!left_ok || !right_ok) {
    cb_printf(&buffer, "Error: %s%s%s\n",
      left_ok ? "" : left.error,
      (!left_ok && !right_ok) ? "; " : "",
      right_ok ? "" : right.error);
    if (left_ok) compare_set_clear(&left);
    if (right_ok) compare_set_clear(&right);
    return cb_take(&buffer);
  }

  cb_printf(&buffer, "Left:  %s\n", left.fingerprint);
  cb_printf(&buffer, "Right: %s\n", right.fingerprint);

  ComparePair both_ready[12] = {0};
  ComparePair one_ready[12] = {0};
  ComparePair worst_rejected[12] = {0};
  size_t both_ready_count = 0;
  size_t one_ready_count = 0;
  size_t worst_rejected_count = 0;
  size_t matched = 0;
  size_t both_ready_total = 0;
  size_t one_ready_total = 0;
  size_t worst_rejected_total = 0;

  for (size_t index = 0; index < left.count; ++index) {
    const CompareRow *left_row = &left.rows[index];
    const CompareRow *right_row = find_row(&right, left_row->key);
    if (!right_row) continue;
    matched++;
    int left_ready = left_row->replacement_ready;
    int right_ready = right_row->replacement_ready;
    double max_ratio = left_row->speed_ratio > right_row->speed_ratio ? left_row->speed_ratio : right_row->speed_ratio;
    double max_worst = left_row->worst_pair_ratio > right_row->worst_pair_ratio ? left_row->worst_pair_ratio : right_row->worst_pair_ratio;
    ComparePair pair = {left_row, right_row, max_ratio};
    if (left_ready && right_ready) {
      both_ready_total++;
      insert_pair(both_ready, &both_ready_count, sizeof(both_ready) / sizeof(both_ready[0]), pair, 0);
    } else if (left_ready != right_ready) {
      one_ready_total++;
      pair.score = max_worst;
      insert_pair(one_ready, &one_ready_count, sizeof(one_ready) / sizeof(one_ready[0]), pair, 1);
    }
    if (median_win_worst_pair_rejected(left_row) || median_win_worst_pair_rejected(right_row)) {
      worst_rejected_total++;
      pair.score = max_worst;
      insert_pair(worst_rejected, &worst_rejected_count, sizeof(worst_rejected) / sizeof(worst_rejected[0]), pair, 1);
    }
  }

  cb_printf(&buffer,
    "Rows: left=%zu right=%zu matched=%zu bothReady=%zu oneBuildOnly=%zu worstPairRejected=%zu\n\n",
    left.count,
    right.count,
    matched,
    both_ready_total,
    one_ready_total,
    worst_rejected_total);

  cb_append(&buffer, "Both-build ready rows (best max ratio):\n");
  if (!both_ready_count) cb_append(&buffer, "  none\n");
  for (size_t index = 0; index < both_ready_count; ++index) append_pair_line(&buffer, &both_ready[index]);

  cb_append(&buffer, "\nReady in one build only:\n");
  if (!one_ready_count) cb_append(&buffer, "  none\n");
  for (size_t index = 0; index < one_ready_count; ++index) append_pair_line(&buffer, &one_ready[index]);

  cb_append(&buffer, "\nMedian wins rejected by worst-pair safety:\n");
  if (!worst_rejected_count) cb_append(&buffer, "  none\n");
  for (size_t index = 0; index < worst_rejected_count; ++index) append_pair_line(&buffer, &worst_rejected[index]);

  cb_append(&buffer,
    "\nRule: promote only rows that are ready in both artifacts for the same row key, with no worst-pair regression in either build fingerprint.\n");

  compare_set_clear(&left);
  compare_set_clear(&right);
  return cb_take(&buffer);
}
