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
  GtkWidget *fermat12_button;
  GtkWidget *factor_label;
  GtkWidget *proof_label;
  GtkWidget *cyclo_label;
  GtkWidget *bench_label;
  GtkWidget *json_view;
  GtkWidget *log_view;
  volatile int cancel_requested;
  int persian;
  int wide_layout;
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

static const char *payam_paper_url = "https://michaelreaton.github.io/number-x-ray-cabinet/assets/Payam_Idea.pdf";

static const char *workbench_css =
  "window { background: #07110f; color: #e8f0ec; font-family: Segoe UI, Inter, sans-serif; }"
  ".topbar { background: #0b1815; border-bottom: 1px solid #1e3934; padding: 8px 16px; }"
  ".brand { font-size: 22px; font-weight: 800; letter-spacing: 0; color: #f4fbf7; }"
  ".subtitle { color: #9eb3ad; font-size: 12px; }"
  ".rail { background: #0d1d19; border-right: 1px solid #1e3934; padding: 14px; }"
  ".panel { background: #0c1916; border: 1px solid #1b3430; padding: 14px; }"
  ".section-title { color: #f1d982; font-size: 12px; font-weight: 800; letter-spacing: 2px; text-transform: uppercase; }"
  "textview, textview text { background: #07110f; color: #e8f0ec; font-family: Cascadia Mono, Consolas, monospace; font-size: 13px; }"
  "entry, spinbutton { background: #07110f; color: #e8f0ec; border: 1px solid #26423d; border-radius: 4px; padding: 7px; }"
  "button { background: #102722; color: #dce9e4; border: 1px solid #254942; border-radius: 5px; padding: 6px 10px; font-weight: 650; }"
  "button:hover { background: #14322c; }"
  ".primary { background: #5bd7dc; color: #06100e; border-color: #85f2f2; }"
  ".danger { background: #311613; color: #ffb7aa; border-color: #7d3027; }"
  ".paper-link { color: #5bd7dc; border-color: #2d5d56; background: #0a211e; }"
  ".good { color: #72f0d6; font-weight: 800; }"
  ".warn { color: #f1d982; font-weight: 800; }"
  ".bad { color: #ff8f7c; font-weight: 800; }"
  "notebook > header { background: #081310; border-bottom: 1px solid #1e3934; }"
  "notebook tab { padding: 8px 10px; background: #0a1714; border: 1px solid #1e3934; }"
  "notebook tab:checked { background: #12312b; color: #72f0d6; }"
  ".log { background: #050b0a; border-top: 1px solid #1e3934; padding: 8px; color: #9eb3ad; }";

static GtkWidget *label_with_class(const char *text, const char *css_class) {
  GtkWidget *label = gtk_label_new(text);
  gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
  gtk_label_set_wrap(GTK_LABEL(label), TRUE);
  gtk_label_set_wrap_mode(GTK_LABEL(label), PANGO_WRAP_WORD_CHAR);
  gtk_label_set_max_width_chars(GTK_LABEL(label), 48);
  if (css_class) gtk_widget_add_css_class(label, css_class);
  return label;
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

static int set_adaptive_window_size(GtkWindow *window) {
  int width = 1100;
  int height = 700;
  int wide_layout = 0;
  GdkDisplay *display = gdk_display_get_default();
  if (display) {
    GListModel *monitors = gdk_display_get_monitors(display);
    if (monitors && g_list_model_get_n_items(monitors) > 0) {
      GdkMonitor *monitor = GDK_MONITOR(g_list_model_get_item(monitors, 0));
      if (monitor) {
        GdkRectangle geometry;
        gdk_monitor_get_geometry(monitor, &geometry);
        int scale = gdk_monitor_get_scale_factor(monitor);
        if (scale < 1) scale = 1;
        int logical_width = geometry.width / scale;
        int logical_height = geometry.height / scale;
        int usable_width = logical_width > 120 ? logical_width - 80 : logical_width;
        int usable_height = logical_height > 120 ? logical_height - 80 : logical_height;
        wide_layout = usable_width >= 2000;
        width = wide_layout ? 1360 : (usable_width > 1100 ? 1100 : usable_width);
        height = usable_height >= 820 ? 850 : usable_height;
        if (width < 900) width = 900;
        if (height < 580) height = 580;
        g_object_unref(monitor);
      }
    }
  }
  gtk_window_set_default_size(window, width, height);
  return wide_layout;
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
    if (app->fermat12_button) gtk_button_set_label(GTK_BUTTON(app->fermat12_button), "بارگذاری فرما F12");
  } else {
    gtk_button_set_label(GTK_BUTTON(app->language_button), "EN");
    gtk_label_set_text(GTK_LABEL(app->factor_label), "Solver ready.");
    gtk_label_set_text(GTK_LABEL(app->proof_label), "Proof is accepted only when factor product equals the input.");
    gtk_label_set_text(GTK_LABEL(app->cyclo_label), "Cyclotomic candidates appear after a run.");
    gtk_label_set_text(GTK_LABEL(app->bench_label), "Benchmark ladder has not run.");
    if (app->fermat12_button) gtk_button_set_label(GTK_BUTTON(app->fermat12_button), "Load Fermat F12");
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
  gtk_widget_set_visible(app->cancel_button, TRUE);
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
  gtk_widget_set_sensitive(app->cancel_button, FALSE);
  set_text_view(app->log_view, "Cancel requested. The worker will stop at the next solver checkpoint.");
}

static void on_language_clicked(GtkButton *button, gpointer user_data) {
  (void)button;
  AppState *app = (AppState *)user_data;
  app->persian = !app->persian;
  set_language(app);
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
  set_text_view(app->log_view, "Loaded Fermat F12 = 2^4096 + 1 = Phi_8192(2). Use this as a cyclotomic stress example, not a local factorization claim.");
  void (*free_func)(void *, size_t);
  mp_get_memory_functions(NULL, NULL, &free_func);
  free_func(decimal, strlen(decimal) + 1u);
  mpz_clear(value);
}

static GtkWidget *build_input_rail(AppState *app) {
  GtkWidget *rail = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_add_css_class(rail, "rail");
  gtk_widget_set_hexpand(rail, FALSE);
  gtk_widget_set_size_request(rail, 220, -1);
  gtk_box_append(GTK_BOX(rail), label_with_class("INPUT", "section-title"));
  GtkWidget *input_scroll = scrolled_text_view(&app->input_view, TRUE);
  gtk_widget_set_vexpand(input_scroll, FALSE);
  gtk_widget_set_size_request(input_scroll, -1, 130);
  gtk_box_append(GTK_BOX(rail), input_scroll);
  set_text_view(app->input_view, "10403");

  app->run_button = gtk_button_new_with_label("Run Proof");
  gtk_widget_add_css_class(app->run_button, "primary");
  g_signal_connect(app->run_button, "clicked", G_CALLBACK(on_run_clicked), app);
  gtk_box_append(GTK_BOX(rail), app->run_button);

  app->cancel_button = gtk_button_new_with_label("Cancel");
  gtk_widget_add_css_class(app->cancel_button, "danger");
  gtk_widget_set_sensitive(app->cancel_button, FALSE);
  gtk_widget_set_visible(app->cancel_button, FALSE);
  g_signal_connect(app->cancel_button, "clicked", G_CALLBACK(on_cancel_clicked), app);
  gtk_box_append(GTK_BOX(rail), app->cancel_button);

  app->language_button = gtk_button_new_with_label("EN");
  g_signal_connect(app->language_button, "clicked", G_CALLBACK(on_language_clicked), app);
  gtk_box_append(GTK_BOX(rail), app->language_button);

  app->fermat12_button = gtk_button_new_with_label("Load Fermat F12");
  g_signal_connect(app->fermat12_button, "clicked", G_CALLBACK(on_fermat12_clicked), app);
  gtk_box_append(GTK_BOX(rail), app->fermat12_button);

  gtk_box_append(GTK_BOX(rail), paper_link_button("Open Payam Paper"));
  return rail;
}

static GtkWidget *build_center_tabs(AppState *app) {
  GtkWidget *notebook = gtk_notebook_new();
  gtk_notebook_set_scrollable(GTK_NOTEBOOK(notebook), TRUE);
  gtk_widget_set_hexpand(notebook, TRUE);
  gtk_widget_set_vexpand(notebook, TRUE);
  gtk_widget_set_size_request(notebook, app->wide_layout ? 520 : 320, -1);

  GtkWidget *cyclo_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_widget_add_css_class(cyclo_box, "panel");
  gtk_box_append(GTK_BOX(cyclo_box), label_with_class("Cyclotomic X-Ray", "section-title"));
  app->cyclo_label = label_with_class("Cyclotomic candidates appear after a run.", "warn");
  gtk_box_append(GTK_BOX(cyclo_box), app->cyclo_label);
  gtk_box_append(GTK_BOX(cyclo_box), label_with_class("Ranked candidates combine exact Phi_n(b), root proximity, and bounded evidence. Exact means exact; everything else stays evidence.", "subtitle"));
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), cyclo_box, gtk_label_new("X-Ray"));

  GtkWidget *factor_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_widget_add_css_class(factor_box, "panel");
  gtk_box_append(GTK_BOX(factor_box), label_with_class("Factor Solver", "section-title"));
  app->factor_label = label_with_class("Solver ready.", "good");
  app->proof_label = label_with_class("Proof is accepted only when factor product equals the input.", "subtitle");
  gtk_box_append(GTK_BOX(factor_box), app->factor_label);
  gtk_box_append(GTK_BOX(factor_box), app->proof_label);
  gtk_box_append(GTK_BOX(factor_box), label_with_class("Methods: trial division, probable-prime acceptance, perfect powers, Fermat offsets, Pollard Rho, recursive accounting.", "subtitle"));
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), factor_box, gtk_label_new("Solver"));

  GtkWidget *bench_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_widget_add_css_class(bench_box, "panel");
  gtk_box_append(GTK_BOX(bench_box), label_with_class("Benchmark Ladder", "section-title"));
  app->bench_label = label_with_class("Benchmark ladder has not run.", "warn");
  gtk_box_append(GTK_BOX(bench_box), app->bench_label);
  gtk_box_append(GTK_BOX(bench_box), label_with_class("Targets include 10403, 8051, prime powers, Carmichael values, cyclotomic fixtures Phi3(10), Phi5(2), Phi8(2), and Fermat F12 as a large structured probe.", "subtitle"));
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), bench_box, gtk_label_new("Bench"));

  GtkWidget *json_scroll = scrolled_text_view(&app->json_view, FALSE);
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), json_scroll, gtk_label_new("JSON"));
  gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), 1);
  return notebook;
}

static GtkWidget *build_inspector(AppState *app) {
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_widget_add_css_class(box, "panel");
  gtk_widget_set_hexpand(box, FALSE);
  gtk_widget_set_size_request(box, app->wide_layout ? 300 : 220, -1);
  gtk_box_append(GTK_BOX(box), label_with_class("PROOF INSPECTOR", "section-title"));
  gtk_box_append(GTK_BOX(box), label_with_class("Product verified", "good"));
  gtk_box_append(GTK_BOX(box), label_with_class(app->wide_layout
    ? "Only true when every reported factor multiplies back to the input with no unresolved cofactors."
    : "Only exact product equality earns proof.", "subtitle"));
  gtk_box_append(GTK_BOX(box), label_with_class("RSA-260 unresolved", "bad"));
  gtk_box_append(GTK_BOX(box), label_with_class(app->wide_layout
    ? "Native local methods may probe RSA-260, but this app must not claim a solve without exact factors."
    : "No solve claim without exact factors.", "subtitle"));
  gtk_box_append(GTK_BOX(box), label_with_class("Payam connection", "warn"));
  gtk_box_append(GTK_BOX(box), label_with_class(app->wide_layout
    ? "The workbench connects Payam's cyclotomic X-ray idea to bounded native proof runs. Exact product verification is separated from evidence and conjectural structure."
    : "Payam's X-ray idea becomes proof, evidence, partial, timeout, and unresolved labels.", "subtitle"));
  gtk_box_append(GTK_BOX(box), label_with_class("Fermat F12 example", "warn"));
  gtk_box_append(GTK_BOX(box), label_with_class(app->wide_layout
    ? "F12 is 2^4096 + 1, also Phi_8192(2). It is a large exact cyclotomic target for discovery, not a promised local factorization."
    : "F12 = 2^4096 + 1 = Phi_8192(2). Structure example, not a factorization promise.", "subtitle"));
  if (app->wide_layout) {
    gtk_box_append(GTK_BOX(box), label_with_class("Export JSON", "warn"));
    gtk_box_append(GTK_BOX(box), label_with_class("The Report JSON tab preserves full integer values even when the UI truncates long previews.", "subtitle"));
  }
  return box;
}

void xray_workbench_activate(GtkApplication *application, gpointer user_data) {
  (void)user_data;
  AppState *app = (AppState *)calloc(1, sizeof(*app));
  app->window = gtk_application_window_new(application);
  gtk_window_set_title(GTK_WINDOW(app->window), "Number X-Ray Workbench");
  app->wide_layout = set_adaptive_window_size(GTK_WINDOW(app->window));

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
  GtkWidget *paper_top = paper_link_button("Payam Paper");
  gtk_widget_set_margin_start(paper_top, 18);
  gtk_box_append(GTK_BOX(topbar), paper_top);
  GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_hexpand(spacer, TRUE);
  gtk_box_append(GTK_BOX(topbar), spacer);
  gtk_box_append(GTK_BOX(root), topbar);

  GtkWidget *content = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_hexpand(content, TRUE);
  gtk_widget_set_vexpand(content, TRUE);
  gtk_box_append(GTK_BOX(content), build_input_rail(app));
  GtkWidget *center_scroll = gtk_scrolled_window_new();
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(center_scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(center_scroll), app->wide_layout ? 520 : 320);
  gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(center_scroll), 300);
  gtk_scrolled_window_set_max_content_width(GTK_SCROLLED_WINDOW(center_scroll), app->wide_layout ? 920 : 420);
  gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(center_scroll), app->wide_layout ? 650 : 420);
  gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(center_scroll), FALSE);
  gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(center_scroll), FALSE);
  gtk_widget_set_hexpand(center_scroll, app->wide_layout ? TRUE : FALSE);
  gtk_widget_set_vexpand(center_scroll, TRUE);
  gtk_widget_set_size_request(center_scroll, app->wide_layout ? 720 : 420, -1);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(center_scroll), build_center_tabs(app));
  gtk_box_append(GTK_BOX(content), center_scroll);

  GtkWidget *inspector_scroll = gtk_scrolled_window_new();
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(inspector_scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(inspector_scroll), app->wide_layout ? 300 : 220);
  gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(inspector_scroll), 300);
  gtk_scrolled_window_set_max_content_width(GTK_SCROLLED_WINDOW(inspector_scroll), app->wide_layout ? 360 : 220);
  gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(inspector_scroll), app->wide_layout ? 650 : 420);
  gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(inspector_scroll), FALSE);
  gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(inspector_scroll), FALSE);
  gtk_widget_set_hexpand(inspector_scroll, FALSE);
  gtk_widget_set_vexpand(inspector_scroll, TRUE);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(inspector_scroll), build_inspector(app));
  gtk_box_append(GTK_BOX(content), inspector_scroll);

  GtkWidget *log_scroll = scrolled_text_view(&app->log_view, FALSE);
  gtk_widget_add_css_class(log_scroll, "log");
  gtk_widget_set_size_request(log_scroll, -1, 56);
  set_text_view(app->log_view, "Ready. Paste an integer or run the benchmark sample.");

  GtkWidget *body_paned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
  gtk_widget_set_vexpand(body_paned, TRUE);
  gtk_paned_set_start_child(GTK_PANED(body_paned), content);
  gtk_paned_set_end_child(GTK_PANED(body_paned), log_scroll);
  gtk_paned_set_resize_start_child(GTK_PANED(body_paned), TRUE);
  gtk_paned_set_resize_end_child(GTK_PANED(body_paned), FALSE);
  gtk_paned_set_shrink_start_child(GTK_PANED(body_paned), TRUE);
  gtk_paned_set_shrink_end_child(GTK_PANED(body_paned), FALSE);
  gtk_paned_set_position(GTK_PANED(body_paned), app->wide_layout ? 710 : 520);
  gtk_box_append(GTK_BOX(root), body_paned);

  set_language(app);
  gtk_window_present(GTK_WINDOW(app->window));
}
