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
  char *key;
  char *name;
  char *operation;
  char *display;
  char *variant;
  char *status;
  char *adoption;
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
  free(row->key);
  free(row->name);
  free(row->operation);
  free(row->display);
  free(row->variant);
  free(row->status);
  free(row->adoption);
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
    row.name = compare_strdup(field_at(fields, field_count, col_name));
    row.operation = compare_strdup(field_at(fields, field_count, col_operation));
    row.variant = make_row_variant(field_at(fields, field_count, col_detail));
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
    if (!row.name || !row.operation || !row.display || !row.variant || !row.status || !row.adoption || !row.build_config ||
        !row.ipo || !row.compiler || !row.compiler_version || !row.key || !append_compare_row(set, &row)) {
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
