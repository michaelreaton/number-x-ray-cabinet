#include "xray_workbench.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static char *xray_strdup_local(const char *text) {
  size_t length = text ? strlen(text) : 0;
  char *copy = (char *)calloc(length + 1, 1);
  if (!copy) return NULL;
  if (length) memcpy(copy, text, length);
  return copy;
}

static void set_error(char **error_message, const char *message) {
  if (error_message) *error_message = xray_strdup_local(message);
}

unsigned long xray_now_ms(void) {
  return (unsigned long)((clock() * 1000ULL) / CLOCKS_PER_SEC);
}

XrayFactorConfig xray_factor_default_config(void) {
  XrayFactorConfig config;
  config.trial_limit = 50000;
  config.fermat_iterations = 2000;
  config.rho_iterations = 1200;
  config.max_passes = 96;
  config.time_budget_ms = 5000;
  config.cancel_flag = NULL;
  return config;
}

int xray_parse_integer(const char *raw, mpz_t out, char **normalized, char **error_message) {
  if (normalized) *normalized = NULL;
  if (error_message) *error_message = NULL;
  if (!raw) {
    set_error(error_message, "Input is empty.");
    return 0;
  }

  size_t raw_len = strlen(raw);
  char *digits = (char *)calloc(raw_len + 1, 1);
  if (!digits) {
    set_error(error_message, "Out of memory while parsing input.");
    return 0;
  }

  size_t used = 0;
  int saw_digit = 0;
  for (size_t index = 0; index < raw_len; ++index) {
    unsigned char ch = (unsigned char)raw[index];
    if (isdigit(ch)) {
      digits[used++] = (char)ch;
      saw_digit = 1;
      continue;
    }
    if (ch == '-' && !saw_digit) {
      free(digits);
      set_error(error_message, "Input must be a positive integer.");
      return 0;
    }
    if (ch == '.' || ch == 'e' || ch == 'E') {
      free(digits);
      set_error(error_message, "Decimal or exponent notation is ambiguous for exact integer scans.");
      return 0;
    }
  }

  while (used > 1 && digits[0] == '0') {
    memmove(digits, digits + 1, used);
    used--;
  }

  if (!used) {
    free(digits);
    set_error(error_message, "No decimal digits were found.");
    return 0;
  }

  if (mpz_set_str(out, digits, 10) != 0 || mpz_sgn(out) <= 0) {
    free(digits);
    set_error(error_message, "Input must be a positive integer.");
    return 0;
  }

  if (normalized) *normalized = digits;
  else free(digits);
  return 1;
}

char *xray_preview_decimal(const mpz_t value, size_t max_chars) {
  char *full = mpz_get_str(NULL, 10, value);
  if (!full) return NULL;
  size_t length = strlen(full);
  if (length <= max_chars || max_chars < 8) return full;

  size_t head = (max_chars - 1) / 2;
  size_t tail = max_chars - 1 - head;
  char *preview = (char *)calloc(max_chars + 4, 1);
  if (!preview) {
    free(full);
    return NULL;
  }
  memcpy(preview, full, head);
  memcpy(preview + head, "...", 3);
  memcpy(preview + head + 3, full + length - tail, tail);
  free(full);
  return preview;
}

int xray_is_probable_prime(const mpz_t value, int rounds) {
  if (mpz_cmp_ui(value, 2) < 0) return 0;
  return mpz_probab_prime_p(value, rounds > 0 ? rounds : 25) > 0;
}

int xray_integer_nth_root(mpz_t root, const mpz_t value, unsigned long n) {
  mpz_t rem;
  mpz_init(rem);
  mpz_rootrem(root, rem, value, n);
  int exact = mpz_sgn(rem) == 0;
  mpz_clear(rem);
  return exact;
}

static int is_prime_ulong(unsigned long value) {
  if (value < 2) return 0;
  if (value == 2) return 1;
  if ((value & 1UL) == 0) return 0;
  for (unsigned long d = 3; d * d <= value; d += 2) {
    if (value % d == 0) return 0;
  }
  return 1;
}

int xray_small_factor(mpz_t factor, const mpz_t value, unsigned long limit) {
  if (mpz_cmp_ui(value, 2) < 0) return 0;
  if (mpz_divisible_ui_p(value, 2)) {
    if (mpz_cmp_ui(value, 2) == 0) return 0;
    mpz_set_ui(factor, 2);
    return 1;
  }

  for (unsigned long p = 3; p <= limit; p += 2) {
    if (!is_prime_ulong(p)) continue;
    if (mpz_cmp_ui(value, p) == 0) return 0;
    if (mpz_divisible_ui_p(value, p)) {
      mpz_set_ui(factor, p);
      return 1;
    }
  }
  return 0;
}

int xray_perfect_power_factor(mpz_t base, unsigned long *exponent, const mpz_t value, unsigned long max_exponent) {
  if (mpz_cmp_ui(value, 4) < 0) return 0;
  size_t bit_count = mpz_sizeinbase(value, 2);
  unsigned long ceiling = max_exponent && max_exponent < bit_count ? max_exponent : (unsigned long)bit_count;
  mpz_t rem;
  mpz_init(rem);
  for (unsigned long exp = 2; exp <= ceiling; ++exp) {
    mpz_rootrem(base, rem, value, exp);
    if (mpz_sgn(rem) == 0 && mpz_cmp_ui(base, 1) > 0) {
      if (exponent) *exponent = exp;
      mpz_clear(rem);
      return 1;
    }
  }
  mpz_clear(rem);
  return 0;
}

int xray_fermat_factor(mpz_t factor, mpz_t cofactor, const mpz_t value, unsigned long iterations) {
  if (iterations == 0 || mpz_even_p(value)) return 0;

  mpz_t a, rem, b2, b, candidate;
  mpz_inits(a, rem, b2, b, candidate, NULL);
  mpz_sqrtrem(a, rem, value);
  if (mpz_sgn(rem) != 0) mpz_add_ui(a, a, 1);

  for (unsigned long offset = 0; offset < iterations; ++offset) {
    mpz_mul(b2, a, a);
    mpz_sub(b2, b2, value);
    mpz_sqrtrem(b, rem, b2);
    if (mpz_sgn(rem) == 0) {
      mpz_sub(candidate, a, b);
      if (mpz_cmp_ui(candidate, 1) > 0 && mpz_cmp(candidate, value) < 0) {
        mpz_set(factor, candidate);
        mpz_tdiv_q(cofactor, value, factor);
        mpz_clears(a, rem, b2, b, candidate, NULL);
        return 1;
      }
    }
    mpz_add_ui(a, a, 1);
  }

  mpz_clears(a, rem, b2, b, candidate, NULL);
  return 0;
}

static void rho_step(mpz_t x, const mpz_t c, const mpz_t mod) {
  mpz_mul(x, x, x);
  mpz_add(x, x, c);
  mpz_mod(x, x, mod);
}

int xray_pollard_rho_factor(mpz_t factor, mpz_t cofactor, const mpz_t value, unsigned long iterations) {
  if (iterations == 0 || mpz_cmp_ui(value, 4) < 0) return 0;
  if (mpz_even_p(value)) {
    mpz_set_ui(factor, 2);
    mpz_tdiv_q(cofactor, value, factor);
    return 1;
  }

  const unsigned long seeds[] = {2, 3, 5, 7, 11};
  const unsigned long constants[] = {1, 3, 5, 7};
  mpz_t x, y, c, d, diff;
  mpz_inits(x, y, c, d, diff, NULL);

  for (size_t si = 0; si < sizeof(seeds) / sizeof(seeds[0]); ++si) {
    for (size_t ci = 0; ci < sizeof(constants) / sizeof(constants[0]); ++ci) {
      mpz_set_ui(x, seeds[si]);
      mpz_set_ui(y, seeds[si]);
      mpz_set_ui(c, constants[ci]);
      mpz_set_ui(d, 1);

      for (unsigned long index = 0; index < iterations && mpz_cmp_ui(d, 1) == 0; ++index) {
        rho_step(x, c, value);
        rho_step(y, c, value);
        rho_step(y, c, value);
        mpz_sub(diff, x, y);
        mpz_abs(diff, diff);
        mpz_gcd(d, diff, value);
      }

      if (mpz_cmp_ui(d, 1) > 0 && mpz_cmp(d, value) < 0) {
        mpz_set(factor, d);
        mpz_tdiv_q(cofactor, value, factor);
        mpz_clears(x, y, c, d, diff, NULL);
        return 1;
      }
    }
  }

  mpz_clears(x, y, c, d, diff, NULL);
  return 0;
}

static int cancelled(const XrayFactorConfig *config) {
  return config && config->cancel_flag && *(config->cancel_flag);
}

static void append_step(XrayFactorReport *report, const char *method, const char *status, const mpz_t target, const char *detail, unsigned long started_at) {
  XrayFactorStep *next = (XrayFactorStep *)realloc(report->steps, sizeof(XrayFactorStep) * (report->step_count + 1));
  if (!next) return;
  report->steps = next;
  XrayFactorStep *step = &report->steps[report->step_count++];
  memset(step, 0, sizeof(*step));
  snprintf(step->method, sizeof(step->method), "%s", method);
  snprintf(step->status, sizeof(step->status), "%s", status);
  char *preview = xray_preview_decimal(target, 72);
  snprintf(step->target_preview, sizeof(step->target_preview), "%s", preview ? preview : "n/a");
  free(preview);
  snprintf(step->detail, sizeof(step->detail), "%s", detail ? detail : "");
  step->elapsed_ms = xray_now_ms() - started_at;
}

static void append_factor(XrayFactorReport *report, const mpz_t value, int probable_prime, const char *method) {
  char *text = mpz_get_str(NULL, 10, value);
  if (!text) return;
  for (size_t index = 0; index < report->factor_count; ++index) {
    if (strcmp(report->factors[index].value, text) == 0) {
      report->factors[index].exponent++;
      if (method && !strstr(report->factors[index].methods, method)) {
        size_t used = strlen(report->factors[index].methods);
        snprintf(report->factors[index].methods + used, sizeof(report->factors[index].methods) - used, "%s%s", used ? "," : "", method);
      }
      free(text);
      return;
    }
  }

  XrayFactorRecord *next = (XrayFactorRecord *)realloc(report->factors, sizeof(XrayFactorRecord) * (report->factor_count + 1));
  if (!next) {
    free(text);
    return;
  }
  report->factors = next;
  XrayFactorRecord *record = &report->factors[report->factor_count++];
  memset(record, 0, sizeof(*record));
  record->value = text;
  record->exponent = 1;
  record->probable_prime = probable_prime;
  snprintf(record->methods, sizeof(record->methods), "%s", method ? method : "unknown");
}

static void append_unresolved(XrayFactorReport *report, const mpz_t value, const char *note, int deferred) {
  XrayUnresolvedRecord *next = (XrayUnresolvedRecord *)realloc(report->unresolved, sizeof(XrayUnresolvedRecord) * (report->unresolved_count + 1));
  if (!next) return;
  report->unresolved = next;
  XrayUnresolvedRecord *record = &report->unresolved[report->unresolved_count++];
  memset(record, 0, sizeof(*record));
  record->value = mpz_get_str(NULL, 10, value);
  record->digits = record->value ? strlen(record->value) : 0;
  record->probable_prime = 0;
  record->deferred = deferred;
  snprintf(record->note, sizeof(record->note), "%s", note ? note : "");
}

static void queue_push(mpz_t **queue, size_t *count, size_t *capacity, const mpz_t value) {
  if (*count >= *capacity) {
    size_t next_capacity = *capacity ? *capacity * 2 : 16;
    mpz_t *next = (mpz_t *)realloc(*queue, sizeof(mpz_t) * next_capacity);
    if (!next) return;
    *queue = next;
    *capacity = next_capacity;
  }
  mpz_init_set((*queue)[*count], value);
  (*count)++;
}

static int queue_pop(mpz_t out, mpz_t *queue, size_t *count) {
  if (*count == 0) return 0;
  (*count)--;
  mpz_init_set(out, queue[*count]);
  mpz_clear(queue[*count]);
  return 1;
}

static void queue_clear(mpz_t *queue, size_t count) {
  for (size_t index = 0; index < count; ++index) mpz_clear(queue[index]);
  free(queue);
}

static int time_exceeded(unsigned long started_at, unsigned long budget_ms) {
  return budget_ms > 0 && (xray_now_ms() - started_at) > budget_ms;
}

int xray_factor_solve(const char *raw_input, const XrayFactorConfig *config_input, XrayFactorReport *report) {
  if (!report) return 0;
  memset(report, 0, sizeof(*report));
  XrayFactorConfig config = config_input ? *config_input : xray_factor_default_config();
  if (!config.trial_limit) config.trial_limit = 50000;
  if (!config.max_passes) config.max_passes = 96;
  if (!config.time_budget_ms) config.time_budget_ms = 5000;
  report->config = config;

  unsigned long started_at = xray_now_ms();
  mpz_t input;
  mpz_init(input);
  char *normalized = NULL;
  char *error = NULL;
  if (!xray_parse_integer(raw_input, input, &normalized, &error)) {
    snprintf(report->status, sizeof(report->status), "invalid");
    report->input = error ? error : xray_strdup_local("invalid");
    mpz_clear(input);
    return 0;
  }

  report->input = normalized;
  report->digits = strlen(normalized);
  report->bit_length = mpz_sizeinbase(input, 2);

  mpz_t *queue = NULL;
  size_t queue_count = 0;
  size_t queue_capacity = 0;
  queue_push(&queue, &queue_count, &queue_capacity, input);

  unsigned long passes = 0;
  while (queue_count > 0 && passes < config.max_passes) {
    if (cancelled(&config)) {
      report->cancelled = 1;
      break;
    }
    if (time_exceeded(started_at, config.time_budget_ms)) {
      report->timed_out = 1;
      break;
    }

    mpz_t current;
    if (!queue_pop(current, queue, &queue_count)) break;
    if (mpz_cmp_ui(current, 1) <= 0) {
      mpz_clear(current);
      continue;
    }
    passes++;

    if (xray_is_probable_prime(current, 25)) {
      append_factor(report, current, 1, "probable-prime");
      append_step(report, "probable-prime", "accepted", current, "mpz probable-prime test accepted factor", started_at);
      mpz_clear(current);
      continue;
    }

    mpz_t factor, cofactor;
    mpz_inits(factor, cofactor, NULL);
    if (xray_small_factor(factor, current, config.trial_limit)) {
      mpz_tdiv_q(cofactor, current, factor);
      append_step(report, "trial-division", "factor-found", current, "small prime divisor found", started_at);
      queue_push(&queue, &queue_count, &queue_capacity, cofactor);
      queue_push(&queue, &queue_count, &queue_capacity, factor);
      mpz_clears(factor, cofactor, current, NULL);
      continue;
    }
    append_step(report, "trial-division", "miss", current, "no small prime divisor in configured limit", started_at);

    unsigned long exponent = 0;
    if (xray_perfect_power_factor(factor, &exponent, current, 64)) {
      char detail[160];
      snprintf(detail, sizeof(detail), "perfect power base found with exponent %lu", exponent);
      append_step(report, "perfect-power", "factor-found", current, detail, started_at);
      for (unsigned long index = 0; index < exponent; ++index) {
        queue_push(&queue, &queue_count, &queue_capacity, factor);
      }
      mpz_clears(factor, cofactor, current, NULL);
      continue;
    }

    if (xray_fermat_factor(factor, cofactor, current, config.fermat_iterations)) {
      append_step(report, "fermat", "factor-found", current, "Fermat square offset yielded factor", started_at);
      queue_push(&queue, &queue_count, &queue_capacity, cofactor);
      queue_push(&queue, &queue_count, &queue_capacity, factor);
      mpz_clears(factor, cofactor, current, NULL);
      continue;
    }
    append_step(report, "fermat", config.fermat_iterations ? "miss" : "disabled", current, "no square offset inside configured budget", started_at);

    if (xray_pollard_rho_factor(factor, cofactor, current, config.rho_iterations)) {
      append_step(report, "pollard-rho", "factor-found", current, "bounded rho walk yielded factor", started_at);
      queue_push(&queue, &queue_count, &queue_capacity, cofactor);
      queue_push(&queue, &queue_count, &queue_capacity, factor);
      mpz_clears(factor, cofactor, current, NULL);
      continue;
    }
    append_step(report, "pollard-rho", config.rho_iterations ? "miss" : "disabled", current, "no factor inside rho budget", started_at);
    append_unresolved(report, current, "composite witness found but no browser/local-budget factor", 0);
    mpz_clears(factor, cofactor, current, NULL);
  }

  while (queue_count > 0) {
    mpz_t deferred;
    queue_pop(deferred, queue, &queue_count);
    append_unresolved(report, deferred, "deferred by pass/time/cancel budget", 1);
    mpz_clear(deferred);
  }
  queue_clear(queue, queue_count);

  mpz_t product, unresolved_product, temp, power;
  mpz_inits(product, unresolved_product, temp, power, NULL);
  mpz_set_ui(product, 1);
  mpz_set_ui(unresolved_product, 1);
  for (size_t index = 0; index < report->factor_count; ++index) {
    mpz_set_str(temp, report->factors[index].value, 10);
    mpz_pow_ui(power, temp, report->factors[index].exponent);
    mpz_mul(product, product, power);
  }
  for (size_t index = 0; index < report->unresolved_count; ++index) {
    mpz_set_str(temp, report->unresolved[index].value, 10);
    mpz_mul(unresolved_product, unresolved_product, temp);
  }
  mpz_mul(temp, product, unresolved_product);
  report->accounting_verified = mpz_cmp(temp, input) == 0;
  report->product_verified = report->unresolved_count == 0 && report->factor_count > 0 && mpz_cmp(product, input) == 0;
  report->elapsed_ms = xray_now_ms() - started_at;

  if (report->product_verified) snprintf(report->status, sizeof(report->status), "solved");
  else if (report->factor_count > 0) snprintf(report->status, sizeof(report->status), "partial");
  else if (report->cancelled) snprintf(report->status, sizeof(report->status), "cancelled");
  else if (report->timed_out) snprintf(report->status, sizeof(report->status), "timeout");
  else snprintf(report->status, sizeof(report->status), "unsolved");

  mpz_clears(product, unresolved_product, temp, power, input, NULL);
  return 1;
}

void xray_factor_report_clear(XrayFactorReport *report) {
  if (!report) return;
  free(report->input);
  for (size_t index = 0; index < report->factor_count; ++index) free(report->factors[index].value);
  for (size_t index = 0; index < report->unresolved_count; ++index) free(report->unresolved[index].value);
  free(report->factors);
  free(report->unresolved);
  free(report->steps);
  memset(report, 0, sizeof(*report));
}
