#include "workbench.h"
#include "xray_workbench.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum XrayLayoutProfile {
  XRAY_LAYOUT_COMPACT = 0,
  XRAY_LAYOUT_DESKTOP = 1,
  XRAY_LAYOUT_WIDE = 2
} XrayLayoutProfile;

typedef struct AppState {
  GtkWidget *window;
  GtkWidget *input_view;
  GtkWidget *run_button;
  GtkWidget *cancel_button;
  GtkWidget *language_button;
  GtkWidget *toy_button;
  GtkWidget *challenge_button;
  GtkWidget *fermat12_button;
  GtkWidget *factor_label;
  GtkWidget *proof_label;
  GtkWidget *cyclo_label;
  GtkWidget *bench_label;
  GtkWidget *json_view;
  GtkWidget *log_view;
  volatile int cancel_requested;
  int persian;
  XrayLayoutProfile layout;
} AppState;

typedef struct RunResult {
  AppState *app;
  char *json;
  char factor_status[80];
  char proof_status[128];
  char cyclo_status[128];
  char bench_status[128];
  char log_line[384];
} RunResult;

typedef struct RunJob {
  AppState *app;
  char *input;
} RunJob;

static const char *payam_paper_url = "https://michaelreaton.github.io/number-x-ray-cabinet/assets/Payam_Idea.pdf";
static const char *rsa260_value =
  "22112825529529666435281085255026230927612089502470015394413748319128822941402001986512729726569746599085900330031400051170742204560859276357953757185954298838958709229238491006703034124620545784566413664540684214361293017694020846391065875914794251435144458199";

static const char *workbench_css =
  "window { background: #050b0a; color: #e8f1ed; font-family: Segoe UI, Inter, sans-serif; }"
  ".topbar { background: #07110f; border-bottom: 1px solid #24443f; padding: 12px 20px; }"
  ".brand { font-size: 24px; font-weight: 800; color: #f5fbf8; }"
  ".subtitle { color: #9fb6af; font-size: 12px; }"
  ".micro { color: #6f8b84; font-size: 11px; }"
  ".top-chip { color: #72f0d6; background: #0b2420; border: 1px solid #24514b; border-radius: 999px; padding: 5px 10px; font-size: 11px; font-weight: 700; }"
  ".rail { background: #0a1714; border-right: 1px solid #1f3935; padding: 18px; }"
  ".panel { background: #0b1815; border: 1px solid #1e3934; border-radius: 6px; padding: 16px; }"
  ".stage { background: #081411; border: 1px solid #29544d; border-radius: 6px; padding: 18px; }"
  ".inspector { background: #0a1714; border-left: 1px solid #1f3935; padding: 16px; }"
  ".surface { background: #050b0a; }"
  ".section-title { color: #f1d982; font-size: 11px; font-weight: 800; letter-spacing: 2px; text-transform: uppercase; }"
  ".heading { color: #eef8f4; font-size: 19px; font-weight: 800; }"
  ".formula { color: #dcfffb; font-family: Cascadia Mono, Consolas, monospace; font-size: 22px; font-weight: 800; }"
  ".mono { font-family: Cascadia Mono, Consolas, monospace; color: #d5ebe5; }"
  ".metric { background: #0e211d; border: 1px solid #254942; border-radius: 6px; padding: 12px; }"
  ".metric-title { color: #78948d; font-size: 10px; font-weight: 800; letter-spacing: 1px; text-transform: uppercase; }"
  ".metric-value { color: #f0fbf7; font-size: 14px; font-weight: 750; }"
  ".stage-row { background: #0d1f1b; border: 1px solid #1e3934; border-radius: 5px; padding: 9px 10px; }"
  ".stage-index { color: #6d8c85; font-family: Cascadia Mono, Consolas, monospace; font-size: 12px; }"
  ".paper-link { color: #5bd7dc; border-color: #2d5d56; background: #0a211e; }"
  ".good { color: #72f0d6; font-weight: 800; }"
  ".warn { color: #f1d982; font-weight: 800; }"
  ".bad { color: #ff8f7c; font-weight: 800; }"
  ".muted { color: #9fb6af; }"
  "textview, textview text { background: #06100e; color: #e8f1ed; font-family: Cascadia Mono, Consolas, monospace; font-size: 13px; }"
  "entry, spinbutton { background: #06100e; color: #e8f1ed; border: 1px solid #26423d; border-radius: 4px; padding: 8px; }"
  "button { background: #102721; color: #e2efea; border: 1px solid #2a5149; border-radius: 5px; padding: 8px 12px; font-weight: 700; }"
  "button:hover { background: #16362f; border-color: #4f8a80; }"
  ".primary { background: #65dde1; color: #04120f; border-color: #94f8f8; }"
  ".danger { background: #351713; color: #ffb7aa; border-color: #843227; }"
  "scrolledwindow, viewport, notebook, notebook stack { background: #050b0a; }"
  "notebook > header { background: #081310; border-bottom: 1px solid #1e3934; }"
  "notebook tab { padding: 9px 18px; background: #0a1714; border: 1px solid #1e3934; }"
  "notebook tab:checked { background: #12312b; color: #72f0d6; }"
  ".log { background: #050b0a; border-top: 1px solid #1e3934; padding: 8px; color: #9fb6af; }";

static void set_margins(GtkWidget *widget, int top, int right, int bottom, int left) {
  gtk_widget_set_margin_top(widget, top);
  gtk_widget_set_margin_end(widget, right);
  gtk_widget_set_margin_bottom(widget, bottom);
  gtk_widget_set_margin_start(widget, left);
}

static GtkWidget *label_with_width(const char *text, const char *css_class, int max_width, gboolean wrap) {
  GtkWidget *label = gtk_label_new(text);
  gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
  gtk_label_set_wrap(GTK_LABEL(label), wrap);
  gtk_label_set_wrap_mode(GTK_LABEL(label), PANGO_WRAP_WORD_CHAR);
  if (max_width > 0) gtk_label_set_max_width_chars(GTK_LABEL(label), max_width);
  if (css_class) gtk_widget_add_css_class(label, css_class);
  return label;
}

static GtkWidget *label_with_class(const char *text, const char *css_class) {
  return label_with_width(text, css_class, 72, TRUE);
}

static GtkWidget *paper_link_button(const char *label) {
  GtkWidget *link = gtk_link_button_new_with_label(payam_paper_url, label);
  gtk_widget_add_css_class(link, "paper-link");
  return link;
}

static GtkWidget *scrolled_text_view(GtkWidget **view_out, gboolean editable) {
  GtkWidget *scroll = gtk_scrolled_window_new();
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  GtkWidget *view = gtk_text_view_new();
  gtk_text_view_set_monospace(GTK_TEXT_VIEW(view), TRUE);
  gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_WORD_CHAR);
  gtk_text_view_set_editable(GTK_TEXT_VIEW(view), editable);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), view);
  if (view_out) *view_out = view;
  return scroll;
}

static int clamp_int(int value, int minimum, int maximum) {
  if (value < minimum) return minimum;
  if (value > maximum) return maximum;
  return value;
}

static XrayLayoutProfile set_adaptive_window_size(GtkWindow *window) {
  int width = 1500;
  int height = 900;
  XrayLayoutProfile layout = XRAY_LAYOUT_DESKTOP;
  GdkDisplay *display = gdk_display_get_default();
  if (display) {
    GListModel *monitors = gdk_display_get_monitors(display);
    if (monitors && g_list_model_get_n_items(monitors) > 0) {
      GdkMonitor *monitor = GDK_MONITOR(g_list_model_get_item(monitors, 0));
      if (monitor) {
        GdkRectangle geometry;
        gdk_monitor_get_geometry(monitor, &geometry);
        int usable_width = geometry.width > 120 ? geometry.width - 96 : geometry.width;
        int usable_height = geometry.height > 120 ? geometry.height - 96 : geometry.height;
        if (usable_width >= 3000) layout = XRAY_LAYOUT_WIDE;
        else if (usable_width >= 2200) layout = XRAY_LAYOUT_DESKTOP;
        else layout = XRAY_LAYOUT_COMPACT;

        int target_width = (usable_width * 92) / 100;
        int target_height = (usable_height * 88) / 100;
        int max_width = layout == XRAY_LAYOUT_WIDE ? 1760 : 1380;
        int max_height = layout == XRAY_LAYOUT_WIDE ? 1000 : 900;
        int min_width = layout == XRAY_LAYOUT_COMPACT ? 1040 : 1280;
        int min_height = layout == XRAY_LAYOUT_COMPACT ? 680 : 760;
        width = clamp_int(target_width, usable_width >= min_width ? min_width : usable_width, max_width);
        height = clamp_int(target_height, usable_height >= min_height ? min_height : usable_height, max_height);
        g_object_unref(monitor);
      }
    }
  }
  gtk_window_set_default_size(window, width, height);
  return layout;
}

static int rail_width(const AppState *app) {
  if (app->layout == XRAY_LAYOUT_WIDE) return 340;
  if (app->layout == XRAY_LAYOUT_DESKTOP) return 280;
  return 250;
}

static int inspector_width(const AppState *app) {
  if (app->layout == XRAY_LAYOUT_WIDE) return 360;
  if (app->layout == XRAY_LAYOUT_DESKTOP) return 280;
  return 250;
}

static int center_width(const AppState *app) {
  if (app->layout == XRAY_LAYOUT_WIDE) return 980;
  if (app->layout == XRAY_LAYOUT_DESKTOP) return 820;
  return 680;
}

static char *text_view_text(GtkWidget *view) {
  GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
  GtkTextIter start;
  GtkTextIter end;
  gtk_text_buffer_get_bounds(buffer, &start, &end);
  return gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
}

static void set_text_view(GtkWidget *view, const char *text) {
  GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
  gtk_text_buffer_set_text(buffer, text ? text : "", -1);
}

static GtkWidget *metric_box(GtkWidget **value_out, const char *title, const char *value, const char *value_class) {
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
  gtk_widget_add_css_class(box, "metric");
  gtk_box_append(GTK_BOX(box), label_with_width(title, "metric-title", 28, FALSE));
  GtkWidget *value_label = label_with_width(value, "metric-value", 46, TRUE);
  if (value_class) gtk_widget_add_css_class(value_label, value_class);
  gtk_box_append(GTK_BOX(box), value_label);
  if (value_out) *value_out = value_label;
  return box;
}

static GtkWidget *stage_row(const char *index, const char *state, const char *detail, const char *state_class) {
  GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_widget_add_css_class(row, "stage-row");
  GtkWidget *index_label = label_with_width(index, "stage-index", 10, FALSE);
  gtk_widget_set_size_request(index_label, 58, -1);
  gtk_box_append(GTK_BOX(row), index_label);
  GtkWidget *state_label = label_with_width(state, state_class, 16, FALSE);
  gtk_widget_set_size_request(state_label, 118, -1);
  gtk_box_append(GTK_BOX(row), state_label);
  GtkWidget *detail_label = label_with_class(detail, "muted");
  gtk_widget_set_hexpand(detail_label, TRUE);
  gtk_box_append(GTK_BOX(row), detail_label);
  return row;
}

static GtkWidget *page_scroll(GtkWidget *child) {
  GtkWidget *scroll = gtk_scrolled_window_new();
  gtk_widget_add_css_class(scroll, "surface");
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), child);
  return scroll;
}

static GtkWidget *build_metric_grid(AppState *app) {
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
  gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
  gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
  gtk_grid_attach(GTK_GRID(grid), metric_box(&app->factor_label, "factor status", "Solver ready", "good"), 0, 0, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), metric_box(&app->cyclo_label, "x-ray candidates", "Awaiting scan", "warn"), 1, 0, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), metric_box(&app->bench_label, "benchmark ladder", "Not run", "warn"), 2, 0, 1, 1);
  return grid;
}

static GtkWidget *build_xray_page(AppState *app) {
  (void)app;
  GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
  gtk_widget_add_css_class(page, "surface");
  set_margins(page, 16, 16, 16, 16);

  GtkWidget *stage = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_add_css_class(stage, "stage");
  gtk_box_append(GTK_BOX(stage), label_with_class("CYCLOTOMIC SCAN CHAMBER", "section-title"));
  gtk_box_append(GTK_BOX(stage), label_with_width("N  ?=  Phi_n(b)", "formula", 48, FALSE));
  gtk_box_append(GTK_BOX(stage), label_with_class("The native scanner profiles the integer, ranks candidate n values, then labels exact equality separately from evidence.", "subtitle"));

  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
  gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
  gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
  gtk_grid_attach(GTK_GRID(grid), metric_box(NULL, "candidate window", "n = 3..512", "good"), 0, 0, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), metric_box(NULL, "base estimate", "integer root envelope", "warn"), 1, 0, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), metric_box(NULL, "exact gate", "Phi_n(b) product check", "good"), 2, 0, 1, 1);
  gtk_box_append(GTK_BOX(stage), grid);

  gtk_box_append(GTK_BOX(stage), stage_row("RING 1", "Exact", "Phi_3(10)=111 and Phi_5(2)=31 stay visible as counterexamples to shortcut claims.", "good"));
  gtk_box_append(GTK_BOX(stage), stage_row("RING 2", "Stress", "Fermat F12 loads as 2^4096 + 1 = Phi_8192(2), a large structure target.", "warn"));
  gtk_box_append(GTK_BOX(stage), stage_row("RING 3", "Guarded", "Large challenge composites can be profiled, but not declared solved without exact factors.", "bad"));
  gtk_box_append(GTK_BOX(page), stage);

  GtkWidget *evidence = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_widget_add_css_class(evidence, "panel");
  gtk_box_append(GTK_BOX(evidence), label_with_class("Evidence Timeline", "section-title"));
  gtk_box_append(GTK_BOX(evidence), stage_row("01", "Profile", "Normalize messy decimal input and record digit/bit length.", "good"));
  gtk_box_append(GTK_BOX(evidence), stage_row("02", "Screen", "Bounded candidates are ranked by root proximity and exact-evaluation feasibility.", "warn"));
  gtk_box_append(GTK_BOX(evidence), stage_row("03", "Verify", "Exact matches require direct Phi_n(b) equality; hints remain evidence.", "good"));
  gtk_box_append(GTK_BOX(page), evidence);
  return page_scroll(page);
}

static GtkWidget *build_solver_page(AppState *app) {
  GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
  gtk_widget_add_css_class(page, "surface");
  set_margins(page, 16, 16, 16, 16);
  gtk_box_append(GTK_BOX(page), build_metric_grid(app));

  GtkWidget *proof = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_widget_add_css_class(proof, "panel");
  gtk_box_append(GTK_BOX(proof), label_with_class("PROOF LADDER", "section-title"));
  app->proof_label = label_with_class("Proof is accepted only when factor product equals the input.", "subtitle");
  gtk_box_append(GTK_BOX(proof), app->proof_label);
  gtk_box_append(GTK_BOX(proof), stage_row("A", "Parse", "Messy paste normalization preserves the exact decimal integer.", "good"));
  gtk_box_append(GTK_BOX(proof), stage_row("B", "Trial", "Small-factor sweep removes cheap composites before heavier work.", "good"));
  gtk_box_append(GTK_BOX(proof), stage_row("C", "Prime", "Miller-Rabin probable-prime checks classify resolved cofactors.", "warn"));
  gtk_box_append(GTK_BOX(proof), stage_row("D", "Search", "Perfect powers, Fermat offsets, and Pollard Rho run inside explicit budgets.", "warn"));
  gtk_box_append(GTK_BOX(proof), stage_row("E", "Account", "Solved means every reported factor multiplies back to the input.", "good"));
  gtk_box_append(GTK_BOX(page), proof);

  GtkWidget *methods = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_widget_add_css_class(methods, "stage");
  gtk_box_append(GTK_BOX(methods), label_with_class("LIVE METHOD SURFACE", "section-title"));
  gtk_box_append(GTK_BOX(methods), label_with_width("trial division  |  perfect power  |  Fermat  |  Pollard Rho  |  recursive accounting", "mono", 96, FALSE));
  gtk_box_append(GTK_BOX(methods), label_with_class("This is still a bounded workbench, not a GNFS implementation. Large composites remain unresolved locally unless a product-verified factorization is found.", "subtitle"));
  gtk_box_append(GTK_BOX(page), methods);
  return page_scroll(page);
}

static GtkWidget *build_benchmark_page(void) {
  GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
  gtk_widget_add_css_class(page, "surface");
  set_margins(page, 16, 16, 16, 16);
  GtkWidget *ladder = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_widget_add_css_class(ladder, "panel");
  gtk_box_append(GTK_BOX(ladder), label_with_class("BENCHMARK LADDER", "section-title"));
  gtk_box_append(GTK_BOX(ladder), stage_row("10403", "Solve", "101 x 103 should product-verify quickly.", "good"));
  gtk_box_append(GTK_BOX(ladder), stage_row("8051", "Solve", "83 x 97 exercises Pollard Rho and accounting.", "good"));
  gtk_box_append(GTK_BOX(ladder), stage_row("Carmichael", "Classify", "Composite values must not be mistaken for proved primes.", "warn"));
  gtk_box_append(GTK_BOX(ladder), stage_row("F12", "Structure", "Large Phi_8192(2) sample stresses cyclotomic discovery.", "warn"));
  gtk_box_append(GTK_BOX(ladder), stage_row("260-digit test", "Unsolved", "Challenge fixture must finish without a false factor claim.", "bad"));
  gtk_box_append(GTK_BOX(page), ladder);
  return page_scroll(page);
}

static GtkWidget *build_json_page(AppState *app) {
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_widget_add_css_class(box, "surface");
  set_margins(box, 16, 16, 16, 16);
  gtk_box_append(GTK_BOX(box), label_with_class("REPORT JSON", "section-title"));
  GtkWidget *json_scroll = scrolled_text_view(&app->json_view, FALSE);
  gtk_widget_set_vexpand(json_scroll, TRUE);
  gtk_box_append(GTK_BOX(box), json_scroll);
  set_text_view(app->json_view, "{\n  \"status\": \"ready\",\n  \"note\": \"Run Proof to export exact factors, unresolved cofactors, cyclotomic evidence, benchmark metadata, and source notes.\"\n}\n");
  return box;
}

static void set_language(AppState *app) {
  if (app->persian) {
    gtk_button_set_label(GTK_BUTTON(app->language_button), "EN");
    gtk_label_set_text(GTK_LABEL(app->factor_label), "حل‌گر آماده است");
    gtk_label_set_text(GTK_LABEL(app->proof_label), "اثبات فقط وقتی پذیرفته می‌شود که حاصل‌ضرب عامل‌ها برابر ورودی باشد.");
    gtk_label_set_text(GTK_LABEL(app->cyclo_label), "در انتظار اسکن");
    gtk_label_set_text(GTK_LABEL(app->bench_label), "نردبان اجرا نشده");
    if (app->toy_button) gtk_button_set_label(GTK_BUTTON(app->toy_button), "نمونه 10403");
    if (app->challenge_button) gtk_button_set_label(GTK_BUTTON(app->challenge_button), "آزمون ۲۶۰ رقمی");
    if (app->fermat12_button) gtk_button_set_label(GTK_BUTTON(app->fermat12_button), "فرما F12");
  } else {
    gtk_button_set_label(GTK_BUTTON(app->language_button), "FA");
    gtk_label_set_text(GTK_LABEL(app->factor_label), "Solver ready");
    gtk_label_set_text(GTK_LABEL(app->proof_label), "Proof is accepted only when factor product equals the input.");
    gtk_label_set_text(GTK_LABEL(app->cyclo_label), "Awaiting scan");
    gtk_label_set_text(GTK_LABEL(app->bench_label), "Not run");
    if (app->toy_button) gtk_button_set_label(GTK_BUTTON(app->toy_button), "Toy 10403");
    if (app->challenge_button) gtk_button_set_label(GTK_BUTTON(app->challenge_button), "260-digit test");
    if (app->fermat12_button) gtk_button_set_label(GTK_BUTTON(app->fermat12_button), "Fermat F12");
  }
}

static gboolean finish_run(gpointer data) {
  RunResult *result = (RunResult *)data;
  AppState *app = result->app;
  gtk_widget_set_sensitive(app->run_button, TRUE);
  gtk_widget_set_sensitive(app->cancel_button, FALSE);
  gtk_widget_set_visible(app->cancel_button, FALSE);
  gtk_label_set_text(GTK_LABEL(app->factor_label), result->factor_status);
  gtk_label_set_text(GTK_LABEL(app->proof_label), result->proof_status);
  gtk_label_set_text(GTK_LABEL(app->cyclo_label), result->cyclo_status);
  gtk_label_set_text(GTK_LABEL(app->bench_label), result->bench_status);
  set_text_view(app->json_view, result->json);
  set_text_view(app->log_view, result->log_line);
  free(result->json);
  free(result);
  return G_SOURCE_REMOVE;
}

static gpointer run_worker(gpointer data) {
  RunJob *job = (RunJob *)data;
  AppState *app = job->app;
  XrayFactorConfig factor_config = xray_factor_default_config();
  factor_config.cancel_flag = &app->cancel_requested;
  XrayCyclotomicConfig cyclo_config = xray_cyclotomic_default_config();

  XrayFactorReport factor;
  XrayCyclotomicReport cyclotomic;
  XrayBenchmarkReport benchmark;
  xray_factor_solve(job->input, &factor_config, &factor);
  xray_cyclotomic_scan(job->input, &cyclo_config, &cyclotomic);
  xray_benchmark_run(&benchmark);
  char *json = xray_workbench_report_json(&factor, &cyclotomic, &benchmark);

  RunResult *result = (RunResult *)calloc(1, sizeof(*result));
  result->app = app;
  result->json = json;
  snprintf(result->factor_status, sizeof(result->factor_status), "Factor Solver: %s", factor.status);
  snprintf(result->proof_status, sizeof(result->proof_status), "Product proof: %s | factors %zu | unresolved %zu | %lu ms",
    factor.product_verified ? "verified" : "not verified",
    factor.factor_count,
    factor.unresolved_count,
    factor.elapsed_ms);
  snprintf(result->cyclo_status, sizeof(result->cyclo_status), "Scanned %zu candidates | exact %zu | %lu ms",
    cyclotomic.scanned,
    cyclotomic.exact_matches,
    cyclotomic.elapsed_ms);
  snprintf(result->bench_status, sizeof(result->bench_status), "%zu/%zu passed | %lu ms",
    benchmark.passed_count,
    benchmark.result_count,
    benchmark.elapsed_ms);
  snprintf(result->log_line, sizeof(result->log_line),
    "Run complete. status=%s | accounting=%s | productVerified=%s | cyclotomicExact=%zu | large composites remain unresolved unless exact factors are product-verified.",
    factor.status,
    factor.accounting_verified ? "verified" : "failed",
    factor.product_verified ? "true" : "false",
    cyclotomic.exact_matches);

  xray_factor_report_clear(&factor);
  xray_cyclotomic_report_clear(&cyclotomic);
  xray_benchmark_report_clear(&benchmark);
  free(job->input);
  free(job);
  g_idle_add(finish_run, result);
  return NULL;
}

static void on_run_clicked(GtkButton *button, gpointer user_data) {
  (void)button;
  AppState *app = (AppState *)user_data;
  char *input = text_view_text(app->input_view);
  app->cancel_requested = 0;
  gtk_widget_set_sensitive(app->run_button, FALSE);
  gtk_widget_set_visible(app->cancel_button, TRUE);
  gtk_widget_set_sensitive(app->cancel_button, TRUE);
  set_text_view(app->log_view, "Running native proof worker. UI remains responsive; cancel stops at the next solver checkpoint.");

  RunJob *job = (RunJob *)calloc(1, sizeof(*job));
  job->app = app;
  job->input = input;
  GThread *thread = g_thread_new("xray-proof-worker", run_worker, job);
  g_thread_unref(thread);
}

static void on_cancel_clicked(GtkButton *button, gpointer user_data) {
  (void)button;
  AppState *app = (AppState *)user_data;
  app->cancel_requested = 1;
  gtk_widget_set_sensitive(app->cancel_button, FALSE);
  set_text_view(app->log_view, "Cancel requested. The worker will stop at the next solver checkpoint.");
}

static void on_language_clicked(GtkButton *button, gpointer user_data) {
  (void)button;
  AppState *app = (AppState *)user_data;
  app->persian = !app->persian;
  set_language(app);
}

static void on_toy_clicked(GtkButton *button, gpointer user_data) {
  (void)button;
  AppState *app = (AppState *)user_data;
  set_text_view(app->input_view, "10403");
  set_text_view(app->log_view, "Loaded 10403 = 101 x 103. This should solve and product-verify quickly.");
}

static void on_challenge_clicked(GtkButton *button, gpointer user_data) {
  (void)button;
  AppState *app = (AppState *)user_data;
  set_text_view(app->input_view, rsa260_value);
  set_text_view(app->log_view, "Loaded a 260-digit RSA challenge fixture. This is a regression test for honest unresolved reporting, not the center of the interface.");
}

static void on_fermat12_clicked(GtkButton *button, gpointer user_data) {
  (void)button;
  AppState *app = (AppState *)user_data;
  mpz_t value;
  mpz_init_set_ui(value, 1u);
  mpz_mul_2exp(value, value, 4096u);
  mpz_add_ui(value, value, 1u);
  char *decimal = mpz_get_str(NULL, 10, value);
  set_text_view(app->input_view, decimal);
  set_text_view(app->log_view, "Loaded Fermat F12 = 2^4096 + 1 = Phi_8192(2). Structure target, not a factorization promise.");
  void (*free_func)(void *, size_t);
  mp_get_memory_functions(NULL, NULL, &free_func);
  free_func(decimal, strlen(decimal) + 1u);
  mpz_clear(value);
}

static GtkWidget *build_input_rail(AppState *app) {
  GtkWidget *rail = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_add_css_class(rail, "rail");
  gtk_widget_set_hexpand(rail, FALSE);
  gtk_widget_set_halign(rail, GTK_ALIGN_START);
  gtk_widget_set_size_request(rail, rail_width(app), -1);
  gtk_box_append(GTK_BOX(rail), label_with_width("TARGET INTEGER", "section-title", 28, FALSE));
  gtk_box_append(GTK_BOX(rail), label_with_width("Paste decimal text, separators, paper snippets, or a copied challenge value. The parser normalizes messy input before proof work.", "subtitle", 34, TRUE));

  GtkWidget *input_scroll = scrolled_text_view(&app->input_view, TRUE);
  gtk_widget_set_vexpand(input_scroll, FALSE);
  gtk_widget_set_size_request(input_scroll, rail_width(app) - 36, app->layout == XRAY_LAYOUT_COMPACT ? 160 : 190);
  gtk_box_append(GTK_BOX(rail), input_scroll);
  set_text_view(app->input_view, "10403");

  GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  app->run_button = gtk_button_new_with_label("Run Proof");
  gtk_widget_add_css_class(app->run_button, "primary");
  gtk_widget_set_hexpand(app->run_button, TRUE);
  g_signal_connect(app->run_button, "clicked", G_CALLBACK(on_run_clicked), app);
  gtk_box_append(GTK_BOX(actions), app->run_button);

  app->cancel_button = gtk_button_new_with_label("Cancel");
  gtk_widget_add_css_class(app->cancel_button, "danger");
  gtk_widget_set_sensitive(app->cancel_button, FALSE);
  gtk_widget_set_visible(app->cancel_button, FALSE);
  g_signal_connect(app->cancel_button, "clicked", G_CALLBACK(on_cancel_clicked), app);
  gtk_box_append(GTK_BOX(actions), app->cancel_button);
  gtk_box_append(GTK_BOX(rail), actions);

  GtkWidget *sample_grid = gtk_grid_new();
  gtk_grid_set_column_spacing(GTK_GRID(sample_grid), 8);
  gtk_grid_set_row_spacing(GTK_GRID(sample_grid), 8);
  gtk_grid_set_column_homogeneous(GTK_GRID(sample_grid), TRUE);
  app->toy_button = gtk_button_new_with_label("Toy 10403");
  g_signal_connect(app->toy_button, "clicked", G_CALLBACK(on_toy_clicked), app);
  gtk_grid_attach(GTK_GRID(sample_grid), app->toy_button, 0, 0, 1, 1);
  app->challenge_button = gtk_button_new_with_label("260-digit test");
  g_signal_connect(app->challenge_button, "clicked", G_CALLBACK(on_challenge_clicked), app);
  gtk_grid_attach(GTK_GRID(sample_grid), app->challenge_button, 1, 0, 1, 1);
  app->fermat12_button = gtk_button_new_with_label("Fermat F12");
  g_signal_connect(app->fermat12_button, "clicked", G_CALLBACK(on_fermat12_clicked), app);
  gtk_grid_attach(GTK_GRID(sample_grid), app->fermat12_button, 0, 1, 2, 1);
  gtk_box_append(GTK_BOX(rail), label_with_width("EXAMPLES", "section-title", 28, FALSE));
  gtk_box_append(GTK_BOX(rail), sample_grid);

  GtkWidget *config = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_add_css_class(config, "panel");
  gtk_box_append(GTK_BOX(config), label_with_width("LOCAL LIMITS", "section-title", 28, FALSE));
  gtk_box_append(GTK_BOX(config), label_with_width("Trial division, Miller-Rabin, perfect powers, Fermat, Pollard Rho, cyclotomic ranking.", "subtitle", 34, TRUE));
  gtk_box_append(GTK_BOX(config), label_with_width("Solved = exact product verification. Evidence stays evidence.", "mono", 34, TRUE));
  gtk_box_append(GTK_BOX(rail), config);

  GtkWidget *bottom = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  app->language_button = gtk_button_new_with_label("FA");
  g_signal_connect(app->language_button, "clicked", G_CALLBACK(on_language_clicked), app);
  gtk_box_append(GTK_BOX(bottom), app->language_button);
  GtkWidget *paper = paper_link_button("Payam Paper");
  gtk_widget_set_hexpand(paper, TRUE);
  gtk_box_append(GTK_BOX(bottom), paper);
  gtk_box_append(GTK_BOX(rail), bottom);
  return rail;
}

static GtkWidget *build_center_tabs(AppState *app) {
  GtkWidget *notebook = gtk_notebook_new();
  gtk_notebook_set_scrollable(GTK_NOTEBOOK(notebook), TRUE);
  gtk_widget_set_hexpand(notebook, TRUE);
  gtk_widget_set_vexpand(notebook, TRUE);
  gtk_widget_set_size_request(notebook, app->layout == XRAY_LAYOUT_COMPACT ? 520 : 580, -1);

  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), build_xray_page(app), gtk_label_new("X-Ray"));
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), build_solver_page(app), gtk_label_new("Solver"));
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), build_benchmark_page(), gtk_label_new("Bench"));
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), build_json_page(app), gtk_label_new("JSON"));
  gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), 0);
  return notebook;
}

static GtkWidget *build_inspector(AppState *app, int stacked) {
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_add_css_class(box, "inspector");
  gtk_widget_set_hexpand(box, FALSE);
  gtk_widget_set_size_request(box, stacked ? center_width(app) : inspector_width(app), -1);

  gtk_box_append(GTK_BOX(box), label_with_class("PROOF INSPECTOR", "section-title"));
  gtk_box_append(GTK_BOX(box), metric_box(NULL, "verdict discipline", "Exact / Partial / Timeout / Unresolved", "good"));
  gtk_box_append(GTK_BOX(box), metric_box(NULL, "product proof", "Only exact multiplication closes a solve", "good"));
  gtk_box_append(GTK_BOX(box), metric_box(NULL, "challenge guardrail", "Unresolved locally until factors exist", "bad"));

  GtkWidget *payam = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_add_css_class(payam, "panel");
  gtk_box_append(GTK_BOX(payam), label_with_class("PAYAM CONNECTION", "section-title"));
  gtk_box_append(GTK_BOX(payam), label_with_class("Payam's paper asks whether a large integer hides cyclotomic construction. This workbench separates that structure evidence from exact factor proof.", "subtitle"));
  gtk_box_append(GTK_BOX(payam), paper_link_button("Open Payam's paper"));
  gtk_box_append(GTK_BOX(box), payam);

  GtkWidget *fermat = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_add_css_class(fermat, "panel");
  gtk_box_append(GTK_BOX(fermat), label_with_class("FERMAT F12", "section-title"));
  gtk_box_append(GTK_BOX(fermat), label_with_width("2^4096 + 1 = Phi_8192(2)", "mono", 42, FALSE));
  gtk_box_append(GTK_BOX(fermat), label_with_class("Large exact cyclotomic example for discovery. Not a local factorization promise.", "subtitle"));
  gtk_box_append(GTK_BOX(box), fermat);

  GtkWidget *export = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_add_css_class(export, "panel");
  gtk_box_append(GTK_BOX(export), label_with_class("REPORT", "section-title"));
  gtk_box_append(GTK_BOX(export), label_with_class("The JSON tab preserves full integer values, factors, unresolved cofactors, timings, limits, and source notes.", "subtitle"));
  gtk_box_append(GTK_BOX(box), export);
  return box;
}

void xray_workbench_activate(GtkApplication *application, gpointer user_data) {
  (void)user_data;
  AppState *app = (AppState *)calloc(1, sizeof(*app));
  app->window = gtk_application_window_new(application);
  gtk_window_set_title(GTK_WINDOW(app->window), "Number X-Ray Workbench");
  app->layout = set_adaptive_window_size(GTK_WINDOW(app->window));

  GtkCssProvider *provider = gtk_css_provider_new();
  gtk_css_provider_load_from_data(provider, workbench_css, -1);
  gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref(provider);

  GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_window_set_child(GTK_WINDOW(app->window), root);

  GtkWidget *topbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_widget_add_css_class(topbar, "topbar");
  GtkWidget *title_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  gtk_box_append(GTK_BOX(title_box), label_with_width("Number X-Ray Workbench", "brand", 48, FALSE));
  gtk_box_append(GTK_BOX(title_box), label_with_class("Native arbitrary-precision proof lab for Payam-style cyclotomic structure, factor evidence, and honest challenge reporting.", "subtitle"));
  gtk_widget_set_hexpand(title_box, TRUE);
  gtk_box_append(GTK_BOX(topbar), title_box);
  if (app->layout != XRAY_LAYOUT_COMPACT) {
    gtk_box_append(GTK_BOX(topbar), label_with_width("GTK4 + GMP", "top-chip", 16, FALSE));
    gtk_box_append(GTK_BOX(topbar), label_with_width("Proof-first", "top-chip", 16, FALSE));
    gtk_box_append(GTK_BOX(topbar), paper_link_button("Payam Paper"));
  }
  gtk_box_append(GTK_BOX(root), topbar);

  GtkWidget *content = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  set_margins(content, 10, 10, 8, 10);
  gtk_widget_set_hexpand(content, TRUE);
  gtk_widget_set_vexpand(content, TRUE);
  GtkWidget *rail_scroll = gtk_scrolled_window_new();
  gtk_widget_add_css_class(rail_scroll, "surface");
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(rail_scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(rail_scroll), rail_width(app));
  gtk_scrolled_window_set_max_content_width(GTK_SCROLLED_WINDOW(rail_scroll), rail_width(app));
  gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(rail_scroll), FALSE);
  gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(rail_scroll), FALSE);
  gtk_widget_set_size_request(rail_scroll, rail_width(app), -1);
  gtk_widget_set_hexpand(rail_scroll, FALSE);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(rail_scroll), build_input_rail(app));
  gtk_box_append(GTK_BOX(content), rail_scroll);

  GtkWidget *center_scroll = gtk_scrolled_window_new();
  gtk_widget_add_css_class(center_scroll, "surface");
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(center_scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(center_scroll), app->layout == XRAY_LAYOUT_COMPACT ? 600 : 720);
  gtk_scrolled_window_set_max_content_width(GTK_SCROLLED_WINDOW(center_scroll), center_width(app));
  gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(center_scroll), FALSE);
  gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(center_scroll), FALSE);
  gtk_widget_set_size_request(center_scroll, center_width(app), -1);
  gtk_widget_set_hexpand(center_scroll, TRUE);
  gtk_widget_set_vexpand(center_scroll, TRUE);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(center_scroll), build_center_tabs(app));

  GtkWidget *inspector_scroll = gtk_scrolled_window_new();
  gtk_widget_add_css_class(inspector_scroll, "surface");
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(inspector_scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(inspector_scroll), build_inspector(app, app->layout == XRAY_LAYOUT_COMPACT));
  if (app->layout == XRAY_LAYOUT_COMPACT) {
    GtkWidget *right_stack = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_hexpand(right_stack, TRUE);
    gtk_widget_set_vexpand(right_stack, TRUE);
    gtk_widget_set_vexpand(center_scroll, FALSE);
    gtk_widget_set_vexpand(inspector_scroll, TRUE);
    gtk_widget_set_size_request(center_scroll, center_width(app), 360);
    gtk_widget_set_size_request(inspector_scroll, center_width(app), 180);
    gtk_box_append(GTK_BOX(right_stack), center_scroll);
    gtk_box_append(GTK_BOX(right_stack), inspector_scroll);
    gtk_box_append(GTK_BOX(content), right_stack);
  } else {
    gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(inspector_scroll), inspector_width(app));
    gtk_box_append(GTK_BOX(content), center_scroll);
    gtk_box_append(GTK_BOX(content), inspector_scroll);
  }

  GtkWidget *log_scroll = scrolled_text_view(&app->log_view, FALSE);
  gtk_widget_add_css_class(log_scroll, "log");
  gtk_widget_set_size_request(log_scroll, -1, app->layout == XRAY_LAYOUT_COMPACT ? 82 : 106);
  set_text_view(app->log_view, "Ready. Load 10403 for a quick product proof, Fermat F12 for large cyclotomic structure, or the 260-digit fixture for honest unresolved reporting.");

  GtkWidget *body_paned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
  gtk_widget_set_vexpand(body_paned, TRUE);
  gtk_paned_set_start_child(GTK_PANED(body_paned), content);
  gtk_paned_set_end_child(GTK_PANED(body_paned), log_scroll);
  gtk_paned_set_resize_start_child(GTK_PANED(body_paned), TRUE);
  gtk_paned_set_resize_end_child(GTK_PANED(body_paned), FALSE);
  gtk_paned_set_shrink_start_child(GTK_PANED(body_paned), TRUE);
  gtk_paned_set_shrink_end_child(GTK_PANED(body_paned), FALSE);
  gtk_paned_set_position(GTK_PANED(body_paned), app->layout == XRAY_LAYOUT_WIDE ? 820 : app->layout == XRAY_LAYOUT_COMPACT ? 560 : 690);
  gtk_box_append(GTK_BOX(root), body_paned);

  set_language(app);
  gtk_window_present(GTK_WINDOW(app->window));
}
