#include "xray_workbench.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct ExprParser {
  const char *start;
  const char *cursor;
  char error[192];
} ExprParser;

static char *xray_strdup_expr(const char *text) {
  size_t length = text ? strlen(text) : 0;
  char *copy = (char *)calloc(length + 1, 1);
  if (!copy) return NULL;
  if (length) memcpy(copy, text, length);
  return copy;
}

static void set_parse_error(ExprParser *parser, const char *message) {
  if (parser && !parser->error[0]) snprintf(parser->error, sizeof(parser->error), "%s", message);
}

static void skip_ws(ExprParser *parser) {
  while (isspace((unsigned char)*parser->cursor)) parser->cursor++;
}

static int match_char(ExprParser *parser, char expected) {
  skip_ws(parser);
  if (*parser->cursor != expected) return 0;
  parser->cursor++;
  return 1;
}

static int starts_identifier(const char *text, const char *identifier) {
  size_t length = strlen(identifier);
  for (size_t index = 0; index < length; ++index) {
    if (!text[index]) return 0;
    if (tolower((unsigned char)text[index]) != tolower((unsigned char)identifier[index])) return 0;
  }
  return !isalnum((unsigned char)text[length]) && text[length] != '_';
}

static int mpz_to_ulong_checked(ExprParser *parser, const mpz_t value, unsigned long *out, const char *label) {
  if (mpz_sgn(value) < 0 || !mpz_fits_ulong_p(value)) {
    char message[160];
    snprintf(message, sizeof(message), "%s must fit in an unsigned machine word.", label);
    set_parse_error(parser, message);
    return 0;
  }
  *out = (unsigned long)mpz_get_ui(value);
  return 1;
}

static int parse_expression(ExprParser *parser, mpz_t out);

static int parse_number(ExprParser *parser, mpz_t out) {
  skip_ws(parser);
  const char *begin = parser->cursor;
  size_t capacity = strlen(begin) + 1;
  char *digits = (char *)calloc(capacity, 1);
  if (!digits) {
    set_parse_error(parser, "Out of memory while parsing number.");
    return 0;
  }
  size_t used = 0;
  int saw_digit = 0;
  while (*parser->cursor) {
    unsigned char ch = (unsigned char)*parser->cursor;
    if (isdigit(ch)) {
      digits[used++] = (char)ch;
      saw_digit = 1;
      parser->cursor++;
      continue;
    }
    if (ch == '_' && saw_digit) {
      parser->cursor++;
      continue;
    }
    if (ch == ',' && saw_digit && isdigit((unsigned char)parser->cursor[1])) {
      parser->cursor++;
      continue;
    }
    if (ch == '.' || ch == 'e' || ch == 'E') {
      free(digits);
      set_parse_error(parser, "Decimal or exponent notation is ambiguous for exact integer scans.");
      return 0;
    }
    break;
  }
  if (!saw_digit) {
    free(digits);
    return 0;
  }
  if (mpz_set_str(out, digits, 10) != 0) {
    free(digits);
    set_parse_error(parser, "Unable to parse decimal integer.");
    return 0;
  }
  free(digits);
  (void)begin;
  return 1;
}

static int parse_primary(ExprParser *parser, mpz_t out) {
  skip_ws(parser);
  if (match_char(parser, '(')) {
    if (!parse_expression(parser, out)) return 0;
    if (!match_char(parser, ')')) {
      set_parse_error(parser, "Expected closing parenthesis.");
      return 0;
    }
    return 1;
  }

  if (isdigit((unsigned char)*parser->cursor)) {
    return parse_number(parser, out);
  }

  if (starts_identifier(parser->cursor, "Fermat")) {
    parser->cursor += 6;
    if (!match_char(parser, '(')) {
      set_parse_error(parser, "Expected Fermat(k).");
      return 0;
    }
    mpz_t index_value;
    mpz_init(index_value);
    int ok = parse_expression(parser, index_value);
    unsigned long k = 0;
    if (ok) ok = mpz_to_ulong_checked(parser, index_value, &k, "Fermat index");
    mpz_clear(index_value);
    if (!ok) return 0;
    if (!match_char(parser, ')')) {
      set_parse_error(parser, "Expected closing parenthesis after Fermat index.");
      return 0;
    }
    if (k > 20) {
      set_parse_error(parser, "Fermat(k) is limited to k <= 20 for local exact evaluation.");
      return 0;
    }
    unsigned long exponent = 1UL << k;
    mpz_set_ui(out, 1);
    mpz_mul_2exp(out, out, exponent);
    mpz_add_ui(out, out, 1);
    return 1;
  }

  if (starts_identifier(parser->cursor, "Phi") || starts_identifier(parser->cursor, "Cyclotomic")) {
    int is_phi = starts_identifier(parser->cursor, "Phi");
    parser->cursor += is_phi ? 3 : 10;
    if (!match_char(parser, '(')) {
      set_parse_error(parser, "Expected Phi(n,b) or Cyclotomic(n,b).");
      return 0;
    }
    mpz_t n_value, base;
    mpz_inits(n_value, base, NULL);
    int ok = parse_expression(parser, n_value);
    if (ok && !match_char(parser, ',')) {
      set_parse_error(parser, "Expected comma between cyclotomic n and base.");
      ok = 0;
    }
    if (ok) ok = parse_expression(parser, base);
    if (ok && !match_char(parser, ')')) {
      set_parse_error(parser, "Expected closing parenthesis after cyclotomic base.");
      ok = 0;
    }
    unsigned long n = 0;
    if (ok) ok = mpz_to_ulong_checked(parser, n_value, &n, "Cyclotomic n");
    if (ok && (n == 0 || n > 65536UL)) {
      set_parse_error(parser, "Cyclotomic n must be between 1 and 65536.");
      ok = 0;
    }
    if (ok) ok = xray_cyclotomic_eval_ui(out, (unsigned int)n, base);
    if (!ok && !parser->error[0]) set_parse_error(parser, "Cyclotomic value is outside the supported exact evaluator.");
    mpz_clears(n_value, base, NULL);
    return ok;
  }

  if (starts_identifier(parser->cursor, "Pow")) {
    parser->cursor += 3;
    if (!match_char(parser, '(')) {
      set_parse_error(parser, "Expected Pow(a,b).");
      return 0;
    }
    mpz_t base, exponent_value;
    mpz_inits(base, exponent_value, NULL);
    int ok = parse_expression(parser, base);
    if (ok && !match_char(parser, ',')) {
      set_parse_error(parser, "Expected comma between Pow base and exponent.");
      ok = 0;
    }
    if (ok) ok = parse_expression(parser, exponent_value);
    if (ok && !match_char(parser, ')')) {
      set_parse_error(parser, "Expected closing parenthesis after Pow exponent.");
      ok = 0;
    }
    unsigned long exponent = 0;
    if (ok) ok = mpz_to_ulong_checked(parser, exponent_value, &exponent, "Pow exponent");
    if (ok) mpz_pow_ui(out, base, exponent);
    mpz_clears(base, exponent_value, NULL);
    return ok;
  }

  set_parse_error(parser, "Expected an integer, function call, or parenthesized expression.");
  return 0;
}

static int parse_postfix(ExprParser *parser, mpz_t out) {
  if (!parse_primary(parser, out)) return 0;
  while (match_char(parser, '!')) {
    unsigned long n = 0;
    if (!mpz_to_ulong_checked(parser, out, &n, "Factorial input")) return 0;
    if (n > 10000UL) {
      set_parse_error(parser, "Factorial input is limited to 10000 for local evaluation.");
      return 0;
    }
    mpz_fac_ui(out, n);
  }
  return 1;
}

static int parse_unary(ExprParser *parser, mpz_t out) {
  skip_ws(parser);
  if (match_char(parser, '+')) return parse_unary(parser, out);
  if (match_char(parser, '-')) {
    if (!parse_unary(parser, out)) return 0;
    mpz_neg(out, out);
    return 1;
  }
  return parse_postfix(parser, out);
}

static int parse_power(ExprParser *parser, mpz_t out) {
  if (!parse_unary(parser, out)) return 0;
  skip_ws(parser);
  if (match_char(parser, '^')) {
    mpz_t exponent_value;
    mpz_init(exponent_value);
    int ok = parse_power(parser, exponent_value);
    unsigned long exponent = 0;
    if (ok) ok = mpz_to_ulong_checked(parser, exponent_value, &exponent, "Exponent");
    if (ok) mpz_pow_ui(out, out, exponent);
    mpz_clear(exponent_value);
    return ok;
  }
  return 1;
}

static int parse_term(ExprParser *parser, mpz_t out) {
  if (!parse_power(parser, out)) return 0;
  for (;;) {
    skip_ws(parser);
    char op = *parser->cursor;
    if (op != '*' && op != '/' && op != '%') break;
    parser->cursor++;
    mpz_t rhs;
    mpz_init(rhs);
    int ok = parse_power(parser, rhs);
    if (ok && (op == '/' || op == '%') && mpz_sgn(rhs) == 0) {
      set_parse_error(parser, "Division by zero.");
      ok = 0;
    }
    if (ok && op == '*') mpz_mul(out, out, rhs);
    else if (ok && op == '/') {
      if (!mpz_divisible_p(out, rhs)) {
        set_parse_error(parser, "Division must be exact for integer scans.");
        ok = 0;
      } else {
        mpz_tdiv_q(out, out, rhs);
      }
    } else if (ok && op == '%') {
      mpz_mod(out, out, rhs);
    }
    mpz_clear(rhs);
    if (!ok) return 0;
  }
  return 1;
}

static int parse_expression(ExprParser *parser, mpz_t out) {
  if (!parse_term(parser, out)) return 0;
  for (;;) {
    skip_ws(parser);
    char op = *parser->cursor;
    if (op != '+' && op != '-') break;
    parser->cursor++;
    mpz_t rhs;
    mpz_init(rhs);
    int ok = parse_term(parser, rhs);
    if (ok && op == '+') mpz_add(out, out, rhs);
    else if (ok) mpz_sub(out, out, rhs);
    mpz_clear(rhs);
    if (!ok) return 0;
  }
  return 1;
}

static const char *expression_body(const char *raw) {
  const char *body = raw ? raw : "";
  const char *equals = strrchr(body, '=');
  if (equals) body = equals + 1;
  return body;
}

static int is_digits_only_payload(const char *text) {
  int saw_digit = 0;
  for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
    if (isdigit(*p)) {
      saw_digit = 1;
      continue;
    }
    if (isspace(*p) || *p == ',' || *p == '_') continue;
    return 0;
  }
  return saw_digit;
}

static int parse_digits_only(const char *text, mpz_t out, char **normalized, char **error_message) {
  size_t raw_len = strlen(text);
  char *digits = (char *)calloc(raw_len + 1, 1);
  if (!digits) {
    if (error_message) *error_message = xray_strdup_expr("Out of memory while parsing input.");
    return 0;
  }
  size_t used = 0;
  for (size_t index = 0; index < raw_len; ++index) {
    if (isdigit((unsigned char)text[index])) digits[used++] = text[index];
  }
  while (used > 1 && digits[0] == '0') {
    memmove(digits, digits + 1, used);
    used--;
  }
  if (!used || mpz_set_str(out, digits, 10) != 0 || mpz_sgn(out) <= 0) {
    free(digits);
    if (error_message) *error_message = xray_strdup_expr("Input must be a positive integer.");
    return 0;
  }
  if (normalized) *normalized = digits;
  else free(digits);
  return 1;
}

int xray_evaluate_expression(const char *raw, mpz_t out, XrayExpressionResult *result) {
  if (result) memset(result, 0, sizeof(*result));
  if (!raw) {
    if (result) {
      result->raw = xray_strdup_expr("");
      result->error = xray_strdup_expr("Input is empty.");
    }
    return 0;
  }

  const char *body = expression_body(raw);
  char *normalized = NULL;
  char *error = NULL;
  int ok = 0;
  if (is_digits_only_payload(body)) {
    ok = parse_digits_only(body, out, &normalized, &error);
  } else {
    ExprParser parser;
    memset(&parser, 0, sizeof(parser));
    parser.start = body;
    parser.cursor = body;
    ok = parse_expression(&parser, out);
    skip_ws(&parser);
    if (ok && *parser.cursor) {
      if (*parser.cursor == '.' || *parser.cursor == 'e' || *parser.cursor == 'E') {
        set_parse_error(&parser, "Decimal or exponent notation is ambiguous for exact integer scans.");
      } else {
        set_parse_error(&parser, "Unexpected trailing input in exact integer expression.");
      }
      ok = 0;
    }
    if (ok && mpz_sgn(out) <= 0) {
      set_parse_error(&parser, "Input must evaluate to a positive integer.");
      ok = 0;
    }
    if (ok) normalized = mpz_get_str(NULL, 10, out);
    else error = xray_strdup_expr(parser.error[0] ? parser.error : "Unable to evaluate exact integer expression.");
  }

  if (result) {
    result->raw = xray_strdup_expr(raw);
    result->normalized = normalized ? xray_strdup_expr(normalized) : NULL;
    result->error = error ? xray_strdup_expr(error) : NULL;
    result->ok = ok;
    if (ok) {
      result->digits = strlen(normalized);
      result->bit_length = mpz_sizeinbase(out, 2);
    }
  }
  free(normalized);
  free(error);
  return ok;
}

int xray_parse_integer(const char *raw, mpz_t out, char **normalized, char **error_message) {
  if (normalized) *normalized = NULL;
  if (error_message) *error_message = NULL;
  XrayExpressionResult expression;
  int ok = xray_evaluate_expression(raw, out, &expression);
  if (ok) {
    if (normalized) *normalized = xray_strdup_expr(expression.normalized);
  } else if (error_message) {
    *error_message = xray_strdup_expr(expression.error ? expression.error : "Unable to evaluate input.");
  }
  xray_expression_result_clear(&expression);
  return ok;
}

void xray_expression_result_clear(XrayExpressionResult *result) {
  if (!result) return;
  free(result->raw);
  free(result->normalized);
  free(result->error);
  memset(result, 0, sizeof(*result));
}
