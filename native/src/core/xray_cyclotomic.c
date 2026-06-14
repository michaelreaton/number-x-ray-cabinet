#include "xray_workbench.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Poly {
  long long *coeff;
  unsigned int degree;
} Poly;

static void poly_clear(Poly *poly) {
  if (!poly) return;
  free(poly->coeff);
  poly->coeff = NULL;
  poly->degree = 0;
}

static int poly_xn_minus_one(Poly *poly, unsigned int n) {
  poly->coeff = (long long *)calloc(n + 1, sizeof(long long));
  if (!poly->coeff) return 0;
  poly->degree = n;
  poly->coeff[0] = -1;
  poly->coeff[n] = 1;
  return 1;
}

static int cyclotomic_poly(Poly *out, unsigned int n);

static int poly_divide_monic(Poly *dividend, const Poly *divisor) {
  if (dividend->degree < divisor->degree || divisor->coeff[divisor->degree] != 1) return 0;
  unsigned int q_degree = dividend->degree - divisor->degree;
  long long *rem = (long long *)calloc(dividend->degree + 1, sizeof(long long));
  long long *quot = (long long *)calloc(q_degree + 1, sizeof(long long));
  if (!rem || !quot) {
    free(rem);
    free(quot);
    return 0;
  }
  memcpy(rem, dividend->coeff, sizeof(long long) * (dividend->degree + 1));
  for (int k = (int)q_degree; k >= 0; --k) {
    long long scale = rem[divisor->degree + (unsigned int)k];
    quot[k] = scale;
    for (unsigned int j = 0; j <= divisor->degree; ++j) {
      rem[j + (unsigned int)k] -= scale * divisor->coeff[j];
    }
  }
  free(rem);
  free(dividend->coeff);
  dividend->coeff = quot;
  dividend->degree = q_degree;
  return 1;
}

static int cyclotomic_poly(Poly *out, unsigned int n) {
  if (!n || n > 512) return 0;
  Poly poly = {0};
  if (!poly_xn_minus_one(&poly, n)) return 0;
  for (unsigned int d = 1; d < n; ++d) {
    if (n % d != 0) continue;
    Poly divisor = {0};
    if (!cyclotomic_poly(&divisor, d)) {
      poly_clear(&poly);
      return 0;
    }
    int ok = poly_divide_monic(&poly, &divisor);
    poly_clear(&divisor);
    if (!ok) {
      poly_clear(&poly);
      return 0;
    }
  }
  *out = poly;
  return 1;
}

unsigned int xray_phi_ui(unsigned int n) {
  if (!n) return 0;
  unsigned int result = n;
  unsigned int value = n;
  for (unsigned int p = 2; p * p <= value; ++p) {
    if (value % p != 0) continue;
    while (value % p == 0) value /= p;
    result = (result / p) * (p - 1);
  }
  if (value > 1) result = (result / value) * (value - 1);
  return result;
}

int xray_cyclotomic_eval_ui(mpz_t out, unsigned int n, const mpz_t base) {
  if (n >= 2 && (n & (n - 1U)) == 0) {
    unsigned int exponent = n / 2U;
    mpz_pow_ui(out, base, exponent);
    mpz_add_ui(out, out, 1);
    return 1;
  }
  Poly poly = {0};
  if (!cyclotomic_poly(&poly, n)) return 0;
  mpz_set_ui(out, 0);
  for (int index = (int)poly.degree; index >= 0; --index) {
    mpz_mul(out, out, base);
    if (poly.coeff[index] >= 0) mpz_add_ui(out, out, (unsigned long)poly.coeff[index]);
    else mpz_sub_ui(out, out, (unsigned long)(-poly.coeff[index]));
  }
  poly_clear(&poly);
  return 1;
}

XrayCyclotomicConfig xray_cyclotomic_default_config(void) {
  XrayCyclotomicConfig config;
  config.n_min = 3;
  config.n_max = 128;
  config.base_window = 2;
  config.report_limit = 64;
  config.time_budget_ms = 3000;
  return config;
}

static void append_candidate(XrayCyclotomicReport *report, const XrayCyclotomicCandidate *candidate) {
  size_t limit = report->config.report_limit ? report->config.report_limit : 64;
  if (report->candidate_count < limit) {
    XrayCyclotomicCandidate *next = (XrayCyclotomicCandidate *)realloc(report->candidates, sizeof(XrayCyclotomicCandidate) * (report->candidate_count + 1));
    if (!next) return;
    report->candidates = next;
    report->candidates[report->candidate_count++] = *candidate;
  } else {
    size_t worst = 0;
    for (size_t index = 1; index < report->candidate_count; ++index) {
      if (report->candidates[index].score < report->candidates[worst].score) worst = index;
    }
    if (candidate->score > report->candidates[worst].score) {
      free(report->candidates[worst].base);
      report->candidates[worst] = *candidate;
    } else {
      free(candidate->base);
    }
  }
}

static int compare_candidates(const void *left, const void *right) {
  const XrayCyclotomicCandidate *a = (const XrayCyclotomicCandidate *)left;
  const XrayCyclotomicCandidate *b = (const XrayCyclotomicCandidate *)right;
  if (a->score < b->score) return 1;
  if (a->score > b->score) return -1;
  return (int)a->n - (int)b->n;
}

static double score_difference(const mpz_t diff, const mpz_t target) {
  if (mpz_sgn(diff) == 0) return 1.0;
  size_t target_digits = mpz_sizeinbase(target, 10);
  size_t diff_digits = mpz_sizeinbase(diff, 10);
  if (diff_digits >= target_digits + 1) return 0.0;
  return 1.0 - ((double)diff_digits / (double)(target_digits + 1));
}

int xray_cyclotomic_scan(const char *raw_input, const XrayCyclotomicConfig *config_input, XrayCyclotomicReport *report) {
  if (!report) return 0;
  memset(report, 0, sizeof(*report));
  XrayCyclotomicConfig config = config_input ? *config_input : xray_cyclotomic_default_config();
  if (!config.n_min) config.n_min = 1;
  if (config.n_max < config.n_min) config.n_max = config.n_min;
  if (!config.report_limit) config.report_limit = 64;
  report->config = config;

  unsigned long started = xray_now_ms();
  mpz_t target;
  mpz_init(target);
  char *normalized = NULL;
  char *error = NULL;
  if (!xray_parse_integer(raw_input, target, &normalized, &error)) {
    report->input = error;
    mpz_clear(target);
    return 0;
  }
  report->input = normalized;

  mpz_t root, base, value, diff, abs_diff;
  mpz_inits(root, base, value, diff, abs_diff, NULL);
  for (unsigned int n = config.n_min; n <= config.n_max; ++n) {
    if (config.time_budget_ms && xray_now_ms() - started > config.time_budget_ms) {
      report->timed_out = 1;
      break;
    }
    unsigned int phi = xray_phi_ui(n);
    if (!phi || phi > 256 || n > 512) continue;
    xray_integer_nth_root(root, target, phi);
    for (int offset = -(int)config.base_window; offset <= (int)config.base_window; ++offset) {
      if (offset < 0 && mpz_cmp_ui(root, (unsigned int)(-offset) + 1U) < 0) continue;
      mpz_set(base, root);
      if (offset >= 0) mpz_add_ui(base, base, (unsigned long)offset);
      else mpz_sub_ui(base, base, (unsigned long)(-offset));
      if (mpz_cmp_ui(base, 2) < 0) continue;
      if (!xray_cyclotomic_eval_ui(value, n, base)) continue;
      report->scanned++;
      mpz_sub(diff, value, target);
      mpz_abs(abs_diff, diff);
      XrayCyclotomicCandidate candidate;
      memset(&candidate, 0, sizeof(candidate));
      candidate.n = n;
      candidate.phi = phi;
      candidate.base = mpz_get_str(NULL, 10, base);
      candidate.exact_match = mpz_sgn(diff) == 0;
      candidate.score = score_difference(abs_diff, target);
      if (candidate.exact_match) {
        snprintf(candidate.verdict, sizeof(candidate.verdict), "exact");
        report->exact_matches++;
      } else if (candidate.score >= 0.75) {
        snprintf(candidate.verdict, sizeof(candidate.verdict), "strong-evidence");
      } else if (candidate.score >= 0.4) {
        snprintf(candidate.verdict, sizeof(candidate.verdict), "weak-evidence");
      } else {
        snprintf(candidate.verdict, sizeof(candidate.verdict), "no-match");
      }
      char *preview = xray_preview_decimal(abs_diff, 72);
      snprintf(candidate.difference_preview, sizeof(candidate.difference_preview), "%s", preview ? preview : "n/a");
      free(preview);
      append_candidate(report, &candidate);
    }
  }
  qsort(report->candidates, report->candidate_count, sizeof(XrayCyclotomicCandidate), compare_candidates);
  report->elapsed_ms = xray_now_ms() - started;
  mpz_clears(root, base, value, diff, abs_diff, target, NULL);
  return 1;
}

void xray_cyclotomic_report_clear(XrayCyclotomicReport *report) {
  if (!report) return;
  free(report->input);
  for (size_t index = 0; index < report->candidate_count; ++index) free(report->candidates[index].base);
  free(report->candidates);
  memset(report, 0, sizeof(*report));
}
