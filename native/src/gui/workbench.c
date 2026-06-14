#include "workbench.h"
#include "xray_workbench.h"

#include <stdlib.h>
#include <string.h>

typedef struct AppState {
  GtkWidget *window;
  GtkWidget *input_view;
  GtkWidget *run_button;
  GtkWidget *cancel_button;
  GtkWidget *language_button;
  GtkWidget *factor_label;
  GtkWidget *proof_label;
  GtkWidget *cyclo_label;
  GtkWidget *bench_label;
  GtkWidget *json_view;
  GtkWidget *log_view;
  volatile int cancel_requested;
  int persian;
} AppState;

typedef struct RunResult {
  AppState *app;
  char *json;
  char factor_status[32];
  char proof_status[96];
  char cyclo_status[96];
  char bench_status[96];
  char log_line[256];
} RunResult;

typedef struct RunJob {
  AppState *app;
  char *input;
} RunJob;

static const char *workbench_css =
  "window { background: #07110f; color: #e8f0ec; font-family: Segoe UI, Inter, sans-serif; }"
  ".topbar { background: #0b1815; border-bottom: 1px solid #1e3934; padding: 12px 16px; }"
  ".brand { font-size: 22px; font-weight: 800; letter-spacing: 0; color: #f4fbf7; }"
  ".subtitle { color: #9eb3ad; font-size: 12px; }"
  ".rail { background: #0d1d19; border-right: 1px solid #1e3934; padding: 14px; }"
  ".panel { background: #0c1916; border: 1px solid #1b3430; padding: 14px; }"
  ".section-title { color: #f1d982; font-size: 12px; font-weight: 800; letter-spacing: 2px; text-transform: uppercase; }"
  "textview, textview text { background: #07110f; color: #e8f0ec; font-family: Cascadia Mono, Consolas, monospace; font-size: 13px; }"
  "entry, spinbutton { background: #07110f; color: #e8f0ec; border: 1px solid #26423d; border-radius: 4px; padding: 7px; }"
  "button { background: #102722; color: #dce9e4; border: 1px solid #254942; border-radius: 5px; padding: 8px 12px; font-weight: 650; }"
  "button:hover { background: #14322c; }"
  ".primary { background: #5bd7dc; color: #06100e; border-color: #85f2f2; }"
  ".danger { background: #311613; color: #ffb7aa; border-color: #7d3027; }"
  ".good { color: #72f0d6; font-weight: 800; }"
  ".warn { color: #f1d982; font-weight: 800; }"
  ".bad { color: #ff8f7c; font-weight: 800; }"
  "notebook > header { background: #081310; border-bottom: 1px solid #1e3934; }"
  "notebook tab { padding: 8px 14px; background: #0a1714; border: 1px solid #1e3934; }"
  "notebook tab:checked { background: #12312b; color: #72f0d6; }"
  ".log { background: #050b0a; border-top: 1px solid #1e3934; padding: 8px; color: #9eb3ad; }";

static GtkWidget *label_with_class(const char *text, const char *css_class) {
  GtkWidget *label = gtk_label_new(text);
  gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
  gtk_label_set_wrap(GTK_LABEL(label), TRUE);
  if (css_class) gtk_widget_add_css_class(label, css_class);
  return label;
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

static void set_language(AppState *app) {
  if (app->persian) {
    gtk_button_set_label(GTK_BUTTON(app->language_button), "FA");
    gtk_label_set_text(GTK_LABEL(app->factor_label), "حل‌گر آماده است.");
    gtk_label_set_text(GTK_LABEL(app->proof_label), "اثبات فقط وقتی پذیرفته می‌شود که حاصل‌ضرب عامل‌ها برابر ورودی باشد.");
    gtk_label_set_text(GTK_LABEL(app->cyclo_label), "نامزدهای سیکلوتومیک پس از اجرا ظاهر می‌شوند.");
    gtk_label_set_text(GTK_LABEL(app->bench_label), "نردبان بنچمارک اجرا نشده است.");
  } else {
    gtk_button_set_label(GTK_BUTTON(app->language_button), "EN");
    gtk_label_set_text(GTK_LABEL(app->factor_label), "Solver ready.");
    gtk_label_set_text(GTK_LABEL(app->proof_label), "Proof is accepted only when factor product equals the input.");
    gtk_label_set_text(GTK_LABEL(app->cyclo_label), "Cyclotomic candidates appear after a run.");
    gtk_label_set_text(GTK_LABEL(app->bench_label), "Benchmark ladder has not run.");
  }
}

static gboolean finish_run(gpointer data) {
  RunResult *result = (RunResult *)data;
  AppState *app = result->app;
  gtk_widget_set_sensitive(app->run_button, TRUE);
  gtk_widget_set_sensitive(app->cancel_button, FALSE);
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
  snprintf(result->proof_status, sizeof(result->proof_status), "Product proof: %s · factors %zu · unresolved %zu",
    factor.product_verified ? "verified" : "not verified",
    factor.factor_count,
    factor.unresolved_count);
  snprintf(result->cyclo_status, sizeof(result->cyclo_status), "Cyclotomic X-Ray: scanned %zu · exact matches %zu · %lu ms",
    cyclotomic.scanned,
    cyclotomic.exact_matches,
    cyclotomic.elapsed_ms);
  snprintf(result->bench_status, sizeof(result->bench_status), "Benchmark Ladder: %zu/%zu passed · %lu ms",
    benchmark.passed_count,
    benchmark.result_count,
    benchmark.elapsed_ms);
  snprintf(result->log_line, sizeof(result->log_line), "Run complete. Status=%s, accounting=%s, RSA-260 remains unresolved unless productVerified=true.",
    factor.status,
    factor.accounting_verified ? "verified" : "failed");

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
  gtk_widget_set_sensitive(app->cancel_button, TRUE);
  set_text_view(app->log_view, "Running native proof worker...");

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
  set_text_view(app->log_view, "Cancel requested. The worker will stop at the next solver checkpoint.");
}

static void on_language_clicked(GtkButton *button, gpointer user_data) {
  (void)button;
  AppState *app = (AppState *)user_data;
  app->persian = !app->persian;
  set_language(app);
}

static GtkWidget *build_input_rail(AppState *app) {
  GtkWidget *rail = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_widget_add_css_class(rail, "rail");
  gtk_widget_set_size_request(rail, 320, -1);
  gtk_box_append(GTK_BOX(rail), label_with_class("INPUT", "section-title"));
  GtkWidget *input_scroll = scrolled_text_view(&app->input_view, TRUE);
  gtk_widget_set_vexpand(input_scroll, TRUE);
  gtk_box_append(GTK_BOX(rail), input_scroll);
  set_text_view(app->input_view, "10403");

  app->run_button = gtk_button_new_with_label("Run Proof");
  gtk_widget_add_css_class(app->run_button, "primary");
  g_signal_connect(app->run_button, "clicked", G_CALLBACK(on_run_clicked), app);
  gtk_box_append(GTK_BOX(rail), app->run_button);

  app->cancel_button = gtk_button_new_with_label("Cancel");
  gtk_widget_add_css_class(app->cancel_button, "danger");
  gtk_widget_set_sensitive(app->cancel_button, FALSE);
  g_signal_connect(app->cancel_button, "clicked", G_CALLBACK(on_cancel_clicked), app);
  gtk_box_append(GTK_BOX(rail), app->cancel_button);

  app->language_button = gtk_button_new_with_label("EN");
  g_signal_connect(app->language_button, "clicked", G_CALLBACK(on_language_clicked), app);
  gtk_box_append(GTK_BOX(rail), app->language_button);

  GtkWidget *source = label_with_class("Payam Paper: this native workbench reconstructs the visible X-ray idea and labels every result as proof, evidence, partial, timeout, or unresolved.", "subtitle");
  gtk_box_append(GTK_BOX(rail), source);
  return rail;
}

static GtkWidget *build_center_tabs(AppState *app) {
  GtkWidget *notebook = gtk_notebook_new();
  gtk_widget_set_hexpand(notebook, TRUE);
  gtk_widget_set_vexpand(notebook, TRUE);

  GtkWidget *cyclo_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_widget_add_css_class(cyclo_box, "panel");
  gtk_box_append(GTK_BOX(cyclo_box), label_with_class("Cyclotomic X-Ray", "section-title"));
  app->cyclo_label = label_with_class("Cyclotomic candidates appear after a run.", "warn");
  gtk_box_append(GTK_BOX(cyclo_box), app->cyclo_label);
  gtk_box_append(GTK_BOX(cyclo_box), label_with_class("Ranked candidates combine exact Phi_n(b), root proximity, and bounded evidence. Exact means exact; everything else stays evidence.", "subtitle"));
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), cyclo_box, gtk_label_new("Cyclotomic X-Ray"));

  GtkWidget *factor_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_widget_add_css_class(factor_box, "panel");
  gtk_box_append(GTK_BOX(factor_box), label_with_class("Factor Solver", "section-title"));
  app->factor_label = label_with_class("Solver ready.", "good");
  app->proof_label = label_with_class("Proof is accepted only when factor product equals the input.", "subtitle");
  gtk_box_append(GTK_BOX(factor_box), app->factor_label);
  gtk_box_append(GTK_BOX(factor_box), app->proof_label);
  gtk_box_append(GTK_BOX(factor_box), label_with_class("Methods: trial division, probable-prime acceptance, perfect powers, Fermat offsets, Pollard Rho, recursive accounting.", "subtitle"));
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), factor_box, gtk_label_new("Factor Solver"));

  GtkWidget *bench_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_widget_add_css_class(bench_box, "panel");
  gtk_box_append(GTK_BOX(bench_box), label_with_class("Benchmark Ladder", "section-title"));
  app->bench_label = label_with_class("Benchmark ladder has not run.", "warn");
  gtk_box_append(GTK_BOX(bench_box), app->bench_label);
  gtk_box_append(GTK_BOX(bench_box), label_with_class("Targets include 10403, 8051, prime powers, Carmichael values, and cyclotomic fixtures Phi3(10), Phi5(2), Phi8(2).", "subtitle"));
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), bench_box, gtk_label_new("Benchmarks"));

  GtkWidget *json_scroll = scrolled_text_view(&app->json_view, FALSE);
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), json_scroll, gtk_label_new("Report JSON"));
  return notebook;
}

static GtkWidget *build_inspector(void) {
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_widget_add_css_class(box, "panel");
  gtk_widget_set_size_request(box, 320, -1);
  gtk_box_append(GTK_BOX(box), label_with_class("PROOF INSPECTOR", "section-title"));
  gtk_box_append(GTK_BOX(box), label_with_class("Product verified", "good"));
  gtk_box_append(GTK_BOX(box), label_with_class("Only true when every reported factor multiplies back to the input with no unresolved cofactors.", "subtitle"));
  gtk_box_append(GTK_BOX(box), label_with_class("RSA-260 unresolved", "bad"));
  gtk_box_append(GTK_BOX(box), label_with_class("Native local methods may probe RSA-260, but this app must not claim a solve without exact factors.", "subtitle"));
  gtk_box_append(GTK_BOX(box), label_with_class("Export JSON", "warn"));
  gtk_box_append(GTK_BOX(box), label_with_class("The Report JSON tab preserves full integer values even when the UI truncates long previews.", "subtitle"));
  return box;
}

void xray_workbench_activate(GtkApplication *application, gpointer user_data) {
  (void)user_data;
  AppState *app = (AppState *)calloc(1, sizeof(*app));
  app->window = gtk_application_window_new(application);
  gtk_window_set_title(GTK_WINDOW(app->window), "Number X-Ray Workbench");
  gtk_window_set_default_size(GTK_WINDOW(app->window), 1360, 850);

  GtkCssProvider *provider = gtk_css_provider_new();
  gtk_css_provider_load_from_data(provider, workbench_css, -1);
  gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref(provider);

  GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_window_set_child(GTK_WINDOW(app->window), root);

  GtkWidget *topbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_add_css_class(topbar, "topbar");
  GtkWidget *title_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  gtk_box_append(GTK_BOX(title_box), label_with_class("Number X-Ray Workbench", "brand"));
  gtk_box_append(GTK_BOX(title_box), label_with_class("Native arbitrary-precision proof lab for Payam-style cyclotomic structure and factor evidence.", "subtitle"));
  gtk_box_append(GTK_BOX(topbar), title_box);
  gtk_box_append(GTK_BOX(root), topbar);

  GtkWidget *main_paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_widget_set_vexpand(main_paned, TRUE);

  GtkWidget *right_paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_paned_set_start_child(GTK_PANED(right_paned), build_center_tabs(app));
  gtk_paned_set_end_child(GTK_PANED(right_paned), build_inspector());
  gtk_paned_set_resize_start_child(GTK_PANED(right_paned), TRUE);
  gtk_paned_set_resize_end_child(GTK_PANED(right_paned), FALSE);
  gtk_paned_set_start_child(GTK_PANED(main_paned), build_input_rail(app));
  gtk_paned_set_end_child(GTK_PANED(main_paned), right_paned);
  gtk_paned_set_resize_start_child(GTK_PANED(main_paned), FALSE);
  gtk_paned_set_resize_end_child(GTK_PANED(main_paned), TRUE);
  gtk_box_append(GTK_BOX(root), main_paned);

  GtkWidget *log_scroll = scrolled_text_view(&app->log_view, FALSE);
  gtk_widget_add_css_class(log_scroll, "log");
  gtk_widget_set_size_request(log_scroll, -1, 96);
  set_text_view(app->log_view, "Ready. Paste an integer or run the benchmark sample.");
  gtk_box_append(GTK_BOX(root), log_scroll);

  set_language(app);
  gtk_window_present(GTK_WINDOW(app->window));
}
