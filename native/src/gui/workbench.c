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
  GtkWidget *notebook;
  GtkWidget *tab_buttons[4];
  GtkWidget *input_view;
  GtkWidget *run_button;
  GtkWidget *cancel_button;
  GtkWidget *language_button;
  GtkWidget *scan_depth_combo;
  GtkWidget *proof_strategy_combo;
  GtkWidget *factor_bits_combo;
  GtkWidget *primality_combo;
  GtkWidget *threads_combo;
  GtkWidget *memory_combo;
  GtkWidget *toy_button;
  GtkWidget *challenge_button;
  GtkWidget *fermat12_button;
  GtkWidget *factor_label;
  GtkWidget *proof_label;
  GtkWidget *cyclo_label;
  GtkWidget *bench_label;
  GtkWidget *metric_factor_label;
  GtkWidget *metric_cyclo_label;
  GtkWidget *metric_bench_label;
  GtkWidget *xray_cyclo_label;
  GtkWidget *solver_proof_label;
  GtkWidget *json_view;
  GtkWidget *log_view;
  GtkWidget *benchmark_view;
  guint live_timer_id;
  unsigned long run_started_ms;
  unsigned int run_pulse;
  int run_active;
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
  char *benchmark_text;
  char log_line[2048];
} RunResult;

typedef struct RunJob {
  AppState *app;
  char *input;
  XrayRunConfig config;
} RunJob;

static const char *payam_paper_url = "https://amathz.com/my_gfn.html";
static const char *rsa260_value =
  "22112825529529666435281085255026230927612089502470015394413748319128822941402001986512729726569746599085900330031400051170742204560859276357953757185954298838958709229238491006703034124620545784566413664540684214361293017694020846391065875914794251435144458199";

static char *gui_strdup(const char *text) {
  size_t length = text ? strlen(text) : 0;
  char *copy = (char *)calloc(length + 1, 1);
  if (!copy) return NULL;
  if (length) memcpy(copy, text, length);
  return copy;
}

static void benchmark_display_operation(const XrayBenchmarkResult *row, char *out, size_t out_size) {
  if (!row || !out || out_size == 0) return;
  const char *operation = row->operation[0] ? row->operation : row->name;
  if (strcmp(row->operation, "mul-threshold") == 0) {
    const char *threshold = strstr(row->detail, "threshold=");
    if (threshold) {
      char *end = NULL;
      unsigned long limbs = strtoul(threshold + strlen("threshold="), &end, 10);
      if (end && end != threshold + strlen("threshold=")) {
        snprintf(out, out_size, "mul threshold %lu limbs", limbs);
        return;
      }
    }
  }
  if (strcmp(row->operation, "muladd-bmi2-adx") == 0) {
    snprintf(out, out_size, "muladd BMI2/ADX");
    return;
  }
  if (strcmp(row->operation, "muladd-unroll4") == 0) {
    snprintf(out, out_size, "muladd unroll4");
    return;
  }
  if (strcmp(row->operation, "muladd-unroll8") == 0) {
    snprintf(out, out_size, "muladd unroll8");
    return;
  }
  if (strcmp(row->operation, "mul-unroll4-vs-scratch") == 0) {
    const char *threshold = strstr(row->detail, "leafThreshold=");
    if (threshold) {
      char *end = NULL;
      unsigned long limbs = strtoul(threshold + strlen("leafThreshold="), &end, 10);
      if (end && end != threshold + strlen("leafThreshold=")) {
        snprintf(out, out_size, "unroll4 vs scratch leaf %lu", limbs);
        return;
      }
    }
    snprintf(out, out_size, "unroll4 vs scratch");
    return;
  }
  if (strcmp(row->operation, "mul-unroll4-vs-gmp") == 0) {
    const char *threshold = strstr(row->detail, "leafThreshold=");
    if (threshold) {
      char *end = NULL;
      unsigned long limbs = strtoul(threshold + strlen("leafThreshold="), &end, 10);
      if (end && end != threshold + strlen("leafThreshold=")) {
        snprintf(out, out_size, "unroll4 vs GMP leaf %lu", limbs);
        return;
      }
    }
    snprintf(out, out_size, "unroll4 vs GMP");
    return;
  }
  if (strcmp(row->operation, "mul-unroll4-deep-vs-gmp") == 0) {
    const char *threshold = strstr(row->detail, "leafThreshold=");
    if (threshold) {
      char *end = NULL;
      unsigned long limbs = strtoul(threshold + strlen("leafThreshold="), &end, 10);
      if (end && end != threshold + strlen("leafThreshold=")) {
        snprintf(out, out_size, "unroll4 deep vs GMP leaf %lu", limbs);
        return;
      }
    }
    snprintf(out, out_size, "unroll4 deep vs GMP");
    return;
  }
  if (strcmp(row->operation, "mul-toom3-unroll4-vs-scratch") == 0 ||
      strcmp(row->operation, "mul-toom3-unroll4-vs-gmp") == 0 ||
      strcmp(row->operation, "mul-toom3-unroll4-deep-vs-gmp") == 0) {
    const char *threshold = strstr(row->detail, "leafThreshold=");
    if (threshold) {
      char *end = NULL;
      unsigned long limbs = strtoul(threshold + strlen("leafThreshold="), &end, 10);
      if (end && end != threshold + strlen("leafThreshold=")) {
        snprintf(out, out_size, "%s %lu",
          strcmp(row->operation, "mul-toom3-unroll4-deep-vs-gmp") == 0 ? "Toom-3+unroll4 deep vs GMP leaf" :
          (strcmp(row->operation, "mul-toom3-unroll4-vs-gmp") == 0 ? "Toom-3+unroll4 vs GMP leaf" : "Toom-3+unroll4 vs scratch leaf"),
          limbs);
        return;
      }
    }
  }
  if (strcmp(row->operation, "mul-toom3") == 0 || strcmp(row->operation, "mul-toom3-vs-scratch") == 0) {
    const char *threshold = strstr(row->detail, "leafThreshold=");
    if (threshold) {
      char *end = NULL;
      unsigned long limbs = strtoul(threshold + strlen("leafThreshold="), &end, 10);
      if (end && end != threshold + strlen("leafThreshold=")) {
        snprintf(out, out_size, "%s %lu",
          strcmp(row->operation, "mul-toom3-vs-scratch") == 0 ? "Toom-3 vs scratch leaf" : "mul Toom-3 leaf",
          limbs);
        return;
      }
    }
  }
  snprintf(out, out_size, "%s", operation);
}

static char *format_benchmark_report_text(const XrayBenchmarkReport *report) {
  if (!report || !report->result_count) {
    return gui_strdup(
      "BENCHMARK RESULTS\n"
      "Run Proof with benchmarks enabled to populate live primitive timings.\n");
  }

  size_t capacity = 4096 + report->result_count * 360;
  char *text = (char *)calloc(capacity, 1);
  if (!text) return NULL;
  char *cpu_summary = xray_cpu_features_summary(&report->cpu);
  size_t used = 0;
  used += (size_t)snprintf(text + used, capacity - used,
    "BENCHMARK RESULTS\n"
    "%s\n"
    "Passed: %zu/%zu   Scratch rows: %zu   Replacement-ready: %zu   Oracle-only: %zu   Blocked: %zu   Elapsed: %lums\n\n"
    "%-30s %-8s %-14s %-7s %10s %10s %7s %8s\n",
    cpu_summary ? cpu_summary : "CPU: unavailable",
    report->passed_count,
    report->result_count,
    report->scratch_count,
    report->replacement_ready_count,
    report->oracle_only_count,
    report->blocked_count,
    report->elapsed_ms,
    "Operation",
    "Digits",
    "Adoption",
    "Ready",
    "ScratchUs",
    "GmpUs",
    "Ratio",
    "Stable");
  free(cpu_summary);
  used += (size_t)snprintf(text + used, capacity - used,
    "%-30s %-8s %-14s %-7s %10s %10s %7s %8s\n",
    "------------------------------",
    "--------",
    "--------------",
    "-------",
    "----------",
    "----------",
    "-------",
    "--------");

  for (size_t index = 0; index < report->result_count && used < capacity; ++index) {
    const XrayBenchmarkResult *row = &report->results[index];
    if (strcmp(row->category, "scratch-vs-gmp") != 0) continue;
    used += (size_t)snprintf(text + used, capacity - used,
      "%-30s %-8zu %-14s %-7s %10llu %10llu %7.2f %3zu/%-4zu\n",
      row->operation,
      row->digits,
      row->adoption,
      row->replacement_ready ? "yes" : "no",
      row->scratch_us,
      row->gmp_us,
      row->speed_ratio,
      row->stable_sample_count,
      row->sample_count);
  }

  used += (size_t)snprintf(text + used, capacity - used,
    "\nKERNEL PROBES\n"
    "%-30s %-8s %-18s %-20s %7s %8s\n"
    "%-30s %-8s %-18s %-20s %7s %8s\n",
    "Operation",
    "Digits",
    "Status",
    "Adoption",
    "Ratio",
    "Stable",
    "------------------------------",
    "--------",
    "------------------",
    "--------------------",
    "-------",
    "--------");

  for (size_t index = 0; index < report->result_count && used < capacity; ++index) {
    const XrayBenchmarkResult *row = &report->results[index];
    if (strcmp(row->category, "kernel-probe") != 0) continue;
    char operation_label[64];
    benchmark_display_operation(row, operation_label, sizeof(operation_label));
    used += (size_t)snprintf(text + used, capacity - used,
      "%-30s %-8zu %-18s %-20s %7.2f %3zu/%-4zu\n",
      operation_label,
      row->digits,
      row->status,
      row->adoption,
      row->speed_ratio,
      row->stable_sample_count,
      row->sample_count);
  }

  used += (size_t)snprintf(text + used, capacity - used,
    "\nRule: scratch replacements and kernel promote-candidates require exact parity, a paired median inside the configured gate, and enough paired-sample wins for that row. Five-sample kernel rows require 4 wins; deep nine-sample rows require 8.\n");
  return text;
}

static const char *workbench_css =
  "window { background: #060809; color: #d8dedf; font-family: Segoe UI, Inter, sans-serif; }"
  "headerbar.window-titlebar { background: #111315; border-bottom: 1px solid #252b2f; padding: 0 8px; min-height: 28px; }"
  "headerbar.window-titlebar button { min-height: 22px; min-width: 32px; padding: 0; margin: 2px 3px; }"
  ".window-title { color: #e4e8ea; font-size: 14px; font-weight: 500; }"
  ".window-control { color: #d7dcde; font-size: 13px; padding: 0 9px; }"
  ".chrome { background: #080b0d; border-bottom: 1px solid #20272b; padding: 0 10px; min-height: 40px; }"
  ".logo { color: #24dff3; font-size: 23px; font-weight: 800; font-family: Georgia, serif; padding: 0 12px 0 6px; }"
  ".menu-label { color: #b8bec1; font-size: 13px; padding: 0 10px; }"
  ".top-tab { min-width: 150px; padding: 6px 20px; margin: 0 2px; color: #cfd5d8; background: #101416; border: 1px solid #283035; border-radius: 4px; font-size: 16px; font-weight: 500; }"
  ".top-tab:hover { background: #141b1e; border-color: #39464c; }"
  ".top-tab.selected { color: #33e6f4; background: #10242a; border-color: #1e7180; border-bottom: 2px solid #1bd8f0; }"
  ".top-status { color: #a8afb2; font-size: 12px; padding: 0 8px; }"
  ".status-dot { color: #4dcc5f; font-size: 14px; }"
  ".settings-glyph { color: #aeb8bb; font-size: 16px; padding: 0 4px; }"
  ".surface { background: #060809; }"
  ".workarea { background: #060809; }"
  ".rail { background: #0b0f11; border: 1px solid #242a2e; padding: 12px; }"
  ".stage { background: #0a0e10; border: 1px solid #252c31; padding: 12px; }"
  ".inspector { background: #0b0f11; border: 1px solid #252c31; padding: 10px; }"
  ".panel { background: #090d0f; border: 1px solid #2a3035; border-radius: 0; padding: 11px; }"
  ".subpanel { background: #080c0e; border: 1px solid #252b2f; border-radius: 0; padding: 10px; }"
  ".section-title { color: #cfd6d8; font-size: 11px; font-weight: 700; letter-spacing: .6px; text-transform: uppercase; }"
  ".micro-title { color: #cbd2d4; font-size: 10px; font-weight: 650; letter-spacing: .5px; text-transform: uppercase; }"
  ".field-label { color: #c4cbce; font-size: 12px; }"
  ".subtitle { color: #9aa4a8; font-size: 12px; }"
  ".micro { color: #7f898d; font-size: 11px; }"
  ".heading { color: #eef5f7; font-size: 17px; font-weight: 700; }"
  ".formula { color: #dceff3; font-family: Cascadia Mono, Consolas, monospace; font-size: 13px; font-weight: 700; }"
  ".mono { font-family: Cascadia Mono, Consolas, monospace; color: #d5dee1; font-size: 12px; }"
  ".mono-small { font-family: Cascadia Mono, Consolas, monospace; color: #aeb7ba; font-size: 11px; }"
  ".line-gutter { background: #101416; color: #6f7a7e; border: 1px solid #252c31; border-right: 0; padding: 8px 7px; font-family: Cascadia Mono, Consolas, monospace; font-size: 12px; }"
  ".metric { background: #0d1214; border: 1px solid #2a3035; border-radius: 0; padding: 8px; }"
  ".metric-title { color: #8c969a; font-size: 10px; font-weight: 700; letter-spacing: .7px; text-transform: uppercase; }"
  ".metric-value { color: #eef5f7; font-size: 12px; font-weight: 650; }"
  ".table-header { color: #cdd5d8; font-family: Cascadia Mono, Consolas, monospace; font-size: 11px; font-weight: 700; }"
  ".table-cell { color: #c7d0d3; font-family: Cascadia Mono, Consolas, monospace; font-size: 11px; padding: 3px 6px; }"
  ".table-cell.good, .good { color: #25e8ef; font-weight: 750; }"
  ".table-cell.warn, .warn { color: #d8a827; font-weight: 750; }"
  ".table-cell.bad, .bad { color: #ff4d55; font-weight: 750; }"
  ".muted { color: #929da1; }"
  ".control-row { padding: 2px 0; }"
  ".select-face { background: #0a0e10; color: #d5dcde; border: 1px solid #2b3338; border-radius: 3px; padding: 4px 8px; font-size: 12px; }"
  ".run-button { background: #10272d; color: #28e5f6; border: 1px solid #27dced; border-radius: 3px; padding: 9px 12px; font-size: 17px; font-weight: 700; }"
  ".run-button:hover { background: #12323a; border-color: #62f1ff; }"
  ".reset-button { background: #0b0f11; color: #8a9498; border: 1px solid #30383d; border-radius: 3px; padding: 9px 12px; font-size: 12px; }"
  ".danger { background: #281315; color: #ff8e95; border-color: #79343a; }"
  ".paper-link { color: #2bd5ea; border-color: #26353b; background: #0a1114; font-size: 12px; }"
  "textview, textview text { background: #0b0f11; color: #d9e1e4; font-family: Cascadia Mono, Consolas, monospace; font-size: 12px; }"
  "textview { border: 1px solid #252c31; }"
  "entry, spinbutton { background: #080c0e; color: #d8e0e3; border: 1px solid #2a3035; border-radius: 3px; padding: 6px; }"
  "button { background: #11171a; color: #d7dde0; border: 1px solid #30383d; border-radius: 3px; padding: 6px 9px; font-weight: 600; }"
  "button:hover { background: #172024; border-color: #3e4a50; }"
  "scrolledwindow, viewport, notebook, notebook stack { background: #060809; }"
  "notebook > header { min-height: 0; padding: 0; border: 0; background: #060809; }"
  "notebook tab { min-height: 0; padding: 0; margin: 0; border: 0; }"
  ".log { background: #080b0d; border-top: 1px solid #22292d; border-left: 1px solid #22292d; border-right: 1px solid #22292d; padding: 6px; color: #aeb8bb; }"
  ".statusbar { background: #090c0e; border-top: 1px solid #20272b; padding: 6px 12px; }";

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

static GtkWidget *build_window_titlebar(void) {
  GtkWidget *header = gtk_header_bar_new();
  gtk_widget_add_css_class(header, "window-titlebar");
  gtk_widget_set_size_request(header, -1, 28);
  gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(header), TRUE);
  GtkWidget *title = label_with_width("Number X-Ray Workbench", "window-title", 40, FALSE);
  gtk_label_set_xalign(GTK_LABEL(title), 0.5f);
  gtk_header_bar_set_title_widget(GTK_HEADER_BAR(header), title);
  return header;
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
  int width = 1540;
  int height = 950;
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
        if (usable_width >= 2600) layout = XRAY_LAYOUT_WIDE;
        else if (usable_width >= 1300) layout = XRAY_LAYOUT_DESKTOP;
        else layout = XRAY_LAYOUT_COMPACT;

        int target_width = (usable_width * 96) / 100;
        int target_height = (usable_height * 96) / 100;
        int max_width = layout == XRAY_LAYOUT_WIDE ? 1880 : 1560;
        int max_height = layout == XRAY_LAYOUT_WIDE ? 1060 : 1010;
        int min_width = layout == XRAY_LAYOUT_COMPACT ? 1180 : 1420;
        int min_height = layout == XRAY_LAYOUT_COMPACT ? 720 : 820;
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
  if (app->layout == XRAY_LAYOUT_WIDE) return 420;
  if (app->layout == XRAY_LAYOUT_DESKTOP) return 360;
  return 315;
}

static int inspector_width(const AppState *app) {
  if (app->layout == XRAY_LAYOUT_WIDE) return 430;
  if (app->layout == XRAY_LAYOUT_DESKTOP) return 390;
  return 330;
}

static int center_width(const AppState *app) {
  if (app->layout == XRAY_LAYOUT_WIDE) return 980;
  if (app->layout == XRAY_LAYOUT_DESKTOP) return 760;
  return 670;
}

static char *text_view_text(GtkWidget *view) {
  GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
  GtkTextIter start;
  GtkTextIter end;
  gtk_text_buffer_get_bounds(buffer, &start, &end);
  return gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
}

static void set_text_view(GtkWidget *view, const char *text) {
  if (!view) return;
  GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
  gtk_text_buffer_set_text(buffer, text ? text : "", -1);
}

static void set_label_if(GtkWidget *label, const char *text) {
  if (label) gtk_label_set_text(GTK_LABEL(label), text ? text : "");
}

static void set_run_status(AppState *app, const char *factor, const char *proof, const char *cyclo, const char *bench) {
  set_label_if(app->metric_factor_label, factor);
  set_label_if(app->metric_cyclo_label, cyclo);
  set_label_if(app->metric_bench_label, bench);
  set_label_if(app->xray_cyclo_label, cyclo);
  set_label_if(app->solver_proof_label, proof);
  set_label_if(app->factor_label, factor);
  set_label_if(app->proof_label, proof);
  set_label_if(app->cyclo_label, cyclo);
  set_label_if(app->bench_label, bench);
}

static const char *live_stage(unsigned int pulse) {
  static const char *stages[] = {
    "Ingest",
    "Normalize",
    "Factor",
    "Cyclotomic",
    "GNFS scaffold",
    "Benchmark",
    "Assemble"
  };
  return stages[(pulse / 4U) % (sizeof(stages) / sizeof(stages[0]))];
}

static void set_live_log(AppState *app) {
  unsigned long elapsed = app->run_started_ms ? xray_now_ms() - app->run_started_ms : 0;
  const char *stage = app->cancel_requested ? "Cancel checkpoint" : live_stage(app->run_pulse);
  char activity[4] = "...";
  activity[app->run_pulse % 4U] = '\0';
  char log_line[2048];
  snprintf(log_line, sizeof(log_line),
    "RUNNING%s\n"
    "Elapsed: %lu.%03lus\n"
    "Active stage: %s\n"
    "Worker: background proof run is active; UI pulse is live.\n"
    "Cancel: %s\n\n"
    "Live surfaces now update while the engine runs. Exact factors, cyclotomic candidates, benchmark rows, and JSON still settle only after the verified report is assembled.",
    activity,
    elapsed / 1000UL,
    elapsed % 1000UL,
    stage,
    app->cancel_requested ? "requested; waiting for solver checkpoint" : "available");
  set_text_view(app->log_view, log_line);
}

static gboolean live_run_tick(gpointer user_data) {
  AppState *app = (AppState *)user_data;
  if (!app || !app->run_active) {
    if (app) app->live_timer_id = 0;
    return G_SOURCE_REMOVE;
  }

  app->run_pulse++;
  unsigned long elapsed = app->run_started_ms ? xray_now_ms() - app->run_started_ms : 0;
  char factor[80];
  char proof[128];
  char cyclo[128];
  char bench[128];
  snprintf(factor, sizeof(factor), app->cancel_requested ? "Factor Solver: CANCEL REQUESTED" : "Factor Solver: RUNNING");
  snprintf(proof, sizeof(proof), "Product proof: running | elapsed %lu.%03lus", elapsed / 1000UL, elapsed % 1000UL);
  snprintf(cyclo, sizeof(cyclo), "Live stage: %s | pulse %u", app->cancel_requested ? "Cancel checkpoint" : live_stage(app->run_pulse), app->run_pulse);
  snprintf(bench, sizeof(bench), "Report assembling | worker active%s", app->cancel_requested ? " | cancel pending" : "");
  set_run_status(app, factor, proof, cyclo, bench);
  gtk_button_set_label(GTK_BUTTON(app->run_button), app->cancel_requested ? "CANCELLING..." : "RUNNING...");
  set_live_log(app);
  return G_SOURCE_CONTINUE;
}

static char *chunk_decimal(const char *value, size_t width) {
  size_t length = strlen(value);
  size_t lines = length / width + (length % width ? 1u : 0u);
  size_t capacity = length + lines + 1u;
  char *out = (char *)calloc(capacity, sizeof(char));
  if (!out) return NULL;
  size_t pos = 0;
  for (size_t index = 0; index < length; index += width) {
    size_t count = width;
    if (index + count > length) count = length - index;
    memcpy(out + pos, value + index, count);
    pos += count;
    out[pos++] = '\n';
  }
  out[pos] = '\0';
  return out;
}

static GtkWidget *section_box(const char *css_class, int spacing) {
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, spacing);
  gtk_widget_add_css_class(box, css_class);
  return box;
}

static GtkWidget *field_face(const char *text) {
  GtkWidget *label = label_with_width(text, "select-face", 36, FALSE);
  gtk_widget_set_hexpand(label, TRUE);
  return label;
}

static GtkWidget *combo_face(const char **items, int active_index) {
  GtkWidget *combo = gtk_combo_box_text_new();
  for (int index = 0; items[index]; ++index) {
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), items[index]);
  }
  gtk_combo_box_set_active(GTK_COMBO_BOX(combo), active_index);
  gtk_widget_add_css_class(combo, "select-face");
  gtk_widget_set_hexpand(combo, TRUE);
  return combo;
}

static GtkWidget *combo_row(const char *label, const char **items, int active_index, GtkWidget **combo_out) {
  GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_add_css_class(row, "control-row");
  GtkWidget *name = label_with_width(label, "field-label", 20, FALSE);
  gtk_widget_set_size_request(name, 138, -1);
  gtk_box_append(GTK_BOX(row), name);
  GtkWidget *combo = combo_face(items, active_index);
  gtk_box_append(GTK_BOX(row), combo);
  gtk_box_append(GTK_BOX(row), label_with_width("?", "micro", 2, FALSE));
  if (combo_out) *combo_out = combo;
  return row;
}

static GtkWidget *control_row(const char *label, const char *value, int help) {
  GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_add_css_class(row, "control-row");
  GtkWidget *name = label_with_width(label, "field-label", 20, FALSE);
  gtk_widget_set_size_request(name, 138, -1);
  gtk_box_append(GTK_BOX(row), name);
  gtk_box_append(GTK_BOX(row), field_face(value));
  if (help) gtk_box_append(GTK_BOX(row), label_with_width("?", "micro", 2, FALSE));
  return row;
}

static GtkWidget *table_label(const char *text, const char *css_class, int width_chars) {
  GtkWidget *label = label_with_width(text, NULL, width_chars, FALSE);
  char classes[64];
  snprintf(classes, sizeof(classes), "%s", css_class ? css_class : "");
  char *token = strtok(classes, " ");
  while (token) {
    gtk_widget_add_css_class(label, token);
    token = strtok(NULL, " ");
  }
  gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
  return label;
}

static void set_source_rgb(cairo_t *cr, double r, double g, double b) {
  cairo_set_source_rgb(cr, r / 255.0, g / 255.0, b / 255.0);
}

static void draw_text(cairo_t *cr, const char *text, double x, double y, double size, double r, double g, double b) {
  cairo_save(cr);
  cairo_select_font_face(cr, "Cascadia Mono", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, size);
  set_source_rgb(cr, r, g, b);
  cairo_move_to(cr, x, y);
  cairo_show_text(cr, text);
  cairo_restore(cr);
  cairo_new_path(cr);
}

static void draw_scan_pipeline(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data) {
  (void)area;
  (void)user_data;
  const char *labels[] = {"Ingest", "Sieve", "Structure", "Rings", "Ladder", "ECPP", "Assemble", "Verify"};
  int count = 8;
  double left = 46.0;
  double right = width - 58.0;
  double y = height * 0.38;
  double step = (right - left) / (double)(count - 1);
  cairo_set_line_width(cr, 1.0);
  for (int i = 0; i < count - 1; ++i) {
    if (i < 3) set_source_rgb(cr, 31, 211, 229);
    else set_source_rgb(cr, 78, 86, 90);
    cairo_move_to(cr, left + step * i + 12, y);
    cairo_line_to(cr, left + step * (i + 1) - 12, y);
    cairo_stroke(cr);
  }
  for (int i = 0; i < count; ++i) {
    double x = left + step * i;
    int active = i <= 3;
    set_source_rgb(cr, active ? 31 : 84, active ? 211 : 91, active ? 229 : 96);
    cairo_arc(cr, x, y, i == 3 ? 8.0 : 7.0, 0, 6.28318);
    cairo_stroke(cr);
    if (i < 3) draw_text(cr, "v", x - 3.3, y + 3.8, 10.0, 31, 211, 229);
    if (i == 3) {
      set_source_rgb(cr, 31, 211, 229);
      cairo_arc(cr, x, y, 4.5, 0, 6.28318);
      cairo_fill(cr);
    }
    draw_text(cr, labels[i], x - 22.0, y + 30.0, 11.0, active ? 196 : 135, active ? 205 : 143, active ? 209 : 148);
  }
}

static void draw_candidate_rings(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data) {
  (void)area;
  (void)user_data;
  double left = 106.0;
  double right = width - 86.0;
  double top = 38.0;
  double bottom = height - 18.0;
  int rows = 8;
  double row_h = (bottom - top) / (double)rows;
  int powers[] = {0, 16, 32, 48, 64, 80, 96, 112, 128};

  draw_text(cr, "CANDIDATE RINGS (Residue Classes)", 4, 15, 11, 208, 216, 219);
  cairo_set_line_width(cr, 0.8);
  for (int i = 0; i < 9; ++i) {
    double x = left + (right - left) * (double)i / 8.0;
    set_source_rgb(cr, 54, 62, 67);
    cairo_move_to(cr, x, top - 10);
    cairo_line_to(cr, x, top - 4);
    cairo_stroke(cr);
    char label[16];
    snprintf(label, sizeof(label), "2^%d", powers[i]);
    draw_text(cr, label, x - 13, top - 14, 10, 183, 191, 195);
  }

  for (int row = 0; row < rows; ++row) {
    double y = top + row_h * row + row_h * 0.5;
    char label[32];
    snprintf(label, sizeof(label), "R%d  mod  2^%d", row + 1, (row + 1) * 16);
    draw_text(cr, label, 24, y + 4, 11, 199, 207, 210);
    set_source_rgb(cr, 42, 49, 54);
    cairo_move_to(cr, left, y);
    cairo_line_to(cr, right, y);
    cairo_stroke(cr);
    double start = left + (right - left) * (0.10 + row * 0.075);
    double end = left + (right - left) * (0.44 + row * 0.075);
    if (end > right - 8) end = right - 8;
    if (row == rows - 1) set_source_rgb(cr, 213, 161, 39);
    else set_source_rgb(cr, 31, 196, 216);
    cairo_move_to(cr, start, y);
    cairo_line_to(cr, end, y);
    cairo_stroke(cr);
    int dots = row < 2 ? 4 : row < 5 ? 3 : 2;
    for (int d = 0; d < dots; ++d) {
      double x = start + (end - start) * (double)d / (double)(dots - 1);
      cairo_arc(cr, x, y, 4.0, 0, 6.28318);
      cairo_fill(cr);
    }
    char alive[16];
    snprintf(alive, sizeof(alive), "%d alive", row < 1 ? 6 : row < 3 ? 4 - row : row < 6 ? 2 : 1);
    draw_text(cr, alive, right + 16, y + 4, 11, row == rows - 1 ? 213 : 31, row == rows - 1 ? 161 : 196, row == rows - 1 ? 39 : 216);
  }
}

static void draw_evidence_timeline(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data) {
  (void)area;
  (void)user_data;
  const char *times[] = {"14:32:11", "14:32:11", "14:32:15", "14:32:18", "14:32:24", "14:32:31", "", "", ""};
  const char *labels[] = {"Ingest", "Sieve", "Structure", "Rings", "Ladder L0-L6", "L7", "ECPP", "Assemble", "Complete"};
  const char *sub[] = {"", "(-bit primes)", "(F12 check)", "(8 levels)", "Verified", "In Progress", "(Prime Proofs)", "& Verify", ""};
  int count = 9;
  double left = 32.0;
  double right = width - 72.0;
  double y = 28.0;
  double step = (right - left) / (double)(count - 1);
  draw_text(cr, "EVIDENCE TIMELINE", 2, 11, 11, 208, 216, 219);
  for (int i = 0; i < count - 1; ++i) {
    if (i < 5) set_source_rgb(cr, 31, 196, 216);
    else set_source_rgb(cr, 83, 91, 96);
    cairo_move_to(cr, left + step * i, y);
    cairo_line_to(cr, left + step * (i + 1), y);
    cairo_stroke(cr);
  }
  for (int i = 0; i < count; ++i) {
    double x = left + step * i;
    if (i < 6) set_source_rgb(cr, 31, 196, 216);
    else set_source_rgb(cr, 83, 91, 96);
    cairo_arc(cr, x, y, 5.0, 0, 6.28318);
    cairo_stroke(cr);
    if (i < 6) {
      cairo_arc(cr, x, y, 2.8, 0, 6.28318);
      cairo_fill(cr);
    }
    draw_text(cr, times[i], x - 24, y + 23, 10, 205, 213, 216);
    draw_text(cr, labels[i], x - 21, y + 39, 10, 167, 176, 180);
    draw_text(cr, sub[i], x - 24, y + 53, 9, 132, 142, 147);
  }
}

static GtkWidget *drawing_area(int width, int height, GtkDrawingAreaDrawFunc draw_func) {
  GtkWidget *area = gtk_drawing_area_new();
  gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(area), width);
  gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(area), height);
  gtk_widget_set_hexpand(area, TRUE);
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), draw_func, NULL, NULL);
  return area;
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
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_NEVER);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), child);
  return scroll;
}

static GtkWidget *build_metric_grid(AppState *app) {
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
  gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
  gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
  gtk_grid_attach(GTK_GRID(grid), metric_box(&app->metric_factor_label, "factor status", "Solver ready", "good"), 0, 0, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), metric_box(&app->metric_cyclo_label, "x-ray candidates", "Awaiting scan", "warn"), 1, 0, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), metric_box(&app->metric_bench_label, "benchmark ladder", "Not run", "warn"), 2, 0, 1, 1);
  return grid;
}

static GtkWidget *build_xray_page(AppState *app) {
  GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_add_css_class(page, "surface");
  set_margins(page, 0, 0, 0, 0);

  GtkWidget *stage = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_add_css_class(stage, "stage");
  GtkWidget *title_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_box_append(GTK_BOX(title_row), label_with_class("X-RAY SCAN CHAMBER", "section-title"));
  app->xray_cyclo_label = label_with_width("Rings awaiting proof run", "micro", 42, FALSE);
  gtk_widget_set_halign(app->xray_cyclo_label, GTK_ALIGN_END);
  gtk_widget_set_hexpand(app->xray_cyclo_label, TRUE);
  gtk_box_append(GTK_BOX(title_row), app->xray_cyclo_label);
  gtk_box_append(GTK_BOX(stage), title_row);

  gtk_box_append(GTK_BOX(stage), drawing_area(760, 72, draw_scan_pipeline));
  gtk_box_append(GTK_BOX(stage), drawing_area(760, 238, draw_candidate_rings));

  GtkWidget *ladder = section_box("subpanel", 6);
  gtk_box_append(GTK_BOX(ladder), label_with_class("FACTOR PROOF LADDER", "section-title"));
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
  gtk_grid_set_row_spacing(GTK_GRID(grid), 2);
  const char *headers[] = {"Level", "Modulus", "Ring Candidate", "Result", "Evidence"};
  int widths[] = {8, 14, 28, 13, 34};
  for (int col = 0; col < 5; ++col) {
    gtk_grid_attach(GTK_GRID(grid), table_label(headers[col], "table-header", widths[col]), col, 0, 1, 1);
  }
  const char *rows[][5] = {
    {"L0", "mod 2^16", "R1 = 0xC0F3", "Pass", "16-bit residue verified"},
    {"L1", "mod 2^32", "R2 = 0x21A7B9C1", "Pass", "Hensel lift consistent"},
    {"L2", "mod 2^48", "R3 = 0x7E3D91A6B2C4", "Pass", "Carry-safe lift"},
    {"L3", "mod 2^64", "R4 = 0x9B6F2C7E1D5A3F91", "Pass", "64-bit residue verified"},
    {"L4", "mod 2^80", "R5 = 0xD18E4F6A9C0B7D224E11", "Pass", "No contradiction"},
    {"L5", "mod 2^96", "R6 = 0xB7F4E91C2A6D8E3F0B9A77C", "Pass", "Lift stable"},
    {"L6", "mod 2^112", "R7 = 0x8C9A2D1E7F3B6C4A9D2E1F7B", "Pass", "Lift stable"},
    {"L7", "mod 2^128", "R8 = 0xF3A1B7C9D5E4F2A866C3D9E1F2A3", "In Progress", "Extending to next modulus..."}
  };
  for (int row = 0; row < 8; ++row) {
    const char *tone = row == 7 ? "table-cell warn" : "table-cell";
    gtk_grid_attach(GTK_GRID(grid), table_label(rows[row][0], tone, widths[0]), 0, row + 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), table_label(rows[row][1], tone, widths[1]), 1, row + 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), table_label(rows[row][2], row == 7 ? "table-cell warn" : "table-cell good", widths[2]), 2, row + 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), table_label(rows[row][3], row == 7 ? "table-cell warn" : "table-cell good", widths[3]), 3, row + 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), table_label(rows[row][4], tone, widths[4]), 4, row + 1, 1, 1);
  }
  gtk_box_append(GTK_BOX(ladder), grid);
  gtk_box_append(GTK_BOX(stage), ladder);

  gtk_box_append(GTK_BOX(stage), drawing_area(760, 80, draw_evidence_timeline));
  gtk_box_append(GTK_BOX(page), stage);
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
  app->solver_proof_label = label_with_class("Proof is accepted only when factor product equals the input.", "subtitle");
  gtk_box_append(GTK_BOX(proof), app->solver_proof_label);
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

static GtkWidget *build_benchmark_page(AppState *app) {
  GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
  gtk_widget_add_css_class(page, "surface");
  set_margins(page, 16, 16, 16, 16);
  gtk_box_append(GTK_BOX(page), label_with_class("LIVE BENCHMARK RESULTS", "section-title"));
  GtkWidget *benchmark_scroll = scrolled_text_view(&app->benchmark_view, FALSE);
  gtk_widget_set_vexpand(benchmark_scroll, TRUE);
  gtk_widget_set_size_request(benchmark_scroll, -1, app->layout == XRAY_LAYOUT_COMPACT ? 260 : 360);
  gtk_box_append(GTK_BOX(page), benchmark_scroll);
  set_text_view(app->benchmark_view,
    "BENCHMARK RESULTS\n"
    "Run Proof to measure scratch bigint primitives against GMP.\n\n"
    "The table will show parse/add/sub/mul/mod/div/gcd/powmod timings, adoption status, and replacement-ready gates.\n");

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
    set_label_if(app->metric_factor_label, "در انتظار اجرا");
    set_label_if(app->metric_cyclo_label, "در انتظار اسکن");
    set_label_if(app->metric_bench_label, "اجرا نشده");
    set_label_if(app->xray_cyclo_label, "حلقه‌ها در انتظار اجرای اثبات");
    set_label_if(app->solver_proof_label, "اثبات فقط وقتی پذیرفته می‌شود که حاصل‌ضرب عامل‌ها برابر ورودی باشد.");
    set_label_if(app->factor_label, "در انتظار اجرا");
    set_label_if(app->proof_label, "وضعیت: هنوز راستی‌آزمایی نشده");
    set_label_if(app->cyclo_label, "اثرهای سیکلوتومیک پس از اجرای اثبات بررسی می‌شوند.\nعدد ورودی می‌تواند RSA-260، F12، یا هر عدد صحیح دیگری باشد.");
    set_label_if(app->bench_label, "وضعیت: آماده");
    if (app->toy_button) gtk_button_set_label(GTK_BUTTON(app->toy_button), "نمونه 10403");
    if (app->challenge_button) gtk_button_set_label(GTK_BUTTON(app->challenge_button), "آزمون ۲۶۰ رقمی");
    if (app->fermat12_button) gtk_button_set_label(GTK_BUTTON(app->fermat12_button), "فرما F12");
  } else {
    gtk_button_set_label(GTK_BUTTON(app->language_button), "FA");
    set_label_if(app->metric_factor_label, "Solver ready");
    set_label_if(app->metric_cyclo_label, "Awaiting scan");
    set_label_if(app->metric_bench_label, "Not run");
    set_label_if(app->xray_cyclo_label, "Rings awaiting proof run");
    set_label_if(app->solver_proof_label, "Proof is accepted only when factor product equals the input.");
    set_label_if(app->factor_label, "AWAITING RUN");
    set_label_if(app->proof_label, "Status: NOT VERIFIED YET");
    set_label_if(app->cyclo_label, "Cyclotomic fingerprints are checked after a proof run.\nThe input can be RSA-260, F12, or any other integer.");
    set_label_if(app->bench_label, "Status: READY");
    if (app->toy_button) gtk_button_set_label(GTK_BUTTON(app->toy_button), "Toy 10403");
    if (app->challenge_button) gtk_button_set_label(GTK_BUTTON(app->challenge_button), "260-digit test");
    if (app->fermat12_button) gtk_button_set_label(GTK_BUTTON(app->fermat12_button), "Fermat F12");
  }
}

static char *combo_text(GtkWidget *combo, const char *fallback) {
  if (!combo) return g_strdup(fallback);
  char *text = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combo));
  if (!text) return g_strdup(fallback);
  return text;
}

static XrayRunConfig config_from_ui(AppState *app) {
  XrayRunConfig config = xray_run_default_config();
  config.cancel_flag = &app->cancel_requested;
  char *depth = combo_text(app->scan_depth_combo, "Deep");
  char *strategy = combo_text(app->proof_strategy_combo, "Deterministic");
  char *factor_bits = combo_text(app->factor_bits_combo, "128");
  char *primality = combo_text(app->primality_combo, "Probable-prime proof");
  char *threads = combo_text(app->threads_combo, "Auto (12)");
  char *memory = combo_text(app->memory_combo, "16 GB");

  snprintf(config.scan_depth, sizeof(config.scan_depth), "%s", depth);
  snprintf(config.proof_strategy, sizeof(config.proof_strategy), "%s", strategy);
  snprintf(config.primality_mode, sizeof(config.primality_mode), "%s", primality);
  config.threads = strstr(threads, "16") ? 16U : strstr(threads, "8") ? 8U : strstr(threads, "4") ? 4U : 12U;
  config.memory_mb = strstr(memory, "32") ? 32768UL : strstr(memory, "8") ? 8192UL : 16384UL;

  if (strstr(depth, "Standard")) {
    config.cyclotomic.n_max = 128;
    config.cyclotomic.time_budget_ms = 3000;
    config.enable_gnfs_stage_proof = 0;
  } else if (strstr(depth, "GNFS")) {
    config.cyclotomic.n_max = 8192;
    config.cyclotomic.time_budget_ms = 6000;
    config.enable_gnfs_stage_proof = 1;
  } else {
    config.cyclotomic.n_max = 512;
    config.cyclotomic.time_budget_ms = 5000;
    config.enable_gnfs_stage_proof = 1;
  }

  if (strstr(strategy, "Aggressive")) {
    config.factor.trial_limit = 250000;
    config.factor.fermat_iterations = 10000;
    config.factor.rho_iterations = 10000;
    config.factor.brent_iterations = 50000;
    config.factor.pm1_bound = 50000;
    config.factor.time_budget_ms = 12000;
  } else if (strstr(strategy, "GNFS")) {
    config.factor.trial_limit = 50000;
    config.factor.fermat_iterations = 2000;
    config.factor.rho_iterations = 1200;
    config.factor.brent_iterations = 8000;
    config.factor.pm1_bound = 10000;
    config.factor.time_budget_ms = 5000;
    config.enable_gnfs_stage_proof = 1;
  }

  if (strstr(factor_bits, "64")) {
    config.factor.trial_limit = 25000;
    config.factor.brent_iterations /= 2;
  } else if (strstr(factor_bits, "256")) {
    config.factor.trial_limit = 500000;
    config.factor.brent_iterations *= 2;
    config.factor.pm1_bound *= 2;
  }

  g_free(depth);
  g_free(strategy);
  g_free(factor_bits);
  g_free(primality);
  g_free(threads);
  g_free(memory);
  return config;
}

static gboolean finish_run(gpointer data) {
  RunResult *result = (RunResult *)data;
  AppState *app = result->app;
  app->run_active = 0;
  if (app->live_timer_id) {
    g_source_remove(app->live_timer_id);
    app->live_timer_id = 0;
  }
  gtk_widget_set_sensitive(app->run_button, TRUE);
  gtk_button_set_label(GTK_BUTTON(app->run_button), "RUN PROOF");
  gtk_widget_set_sensitive(app->cancel_button, FALSE);
  gtk_widget_set_visible(app->cancel_button, FALSE);
  set_run_status(app, result->factor_status, result->proof_status, result->cyclo_status, result->bench_status);
  set_text_view(app->json_view, result->json);
  set_text_view(app->benchmark_view, result->benchmark_text);
  set_text_view(app->log_view, result->log_line);
  free(result->json);
  free(result->benchmark_text);
  free(result);
  return G_SOURCE_REMOVE;
}

static gpointer run_worker(gpointer data) {
  RunJob *job = (RunJob *)data;
  XrayWorkbenchReport report;
  xray_workbench_run(job->input, &job->config, &report);

  RunResult *result = (RunResult *)calloc(1, sizeof(*result));
  result->app = job->app;
  result->json = gui_strdup(report.json ? report.json : "{}");
  result->benchmark_text = format_benchmark_report_text(&report.benchmark);
  snprintf(result->factor_status, sizeof(result->factor_status), "Factor Solver: %s", report.factor.status[0] ? report.factor.status : "invalid");
  snprintf(result->proof_status, sizeof(result->proof_status), "Product proof: %s | factors %zu | unresolved %zu | %lu ms",
    report.factor.product_verified ? "verified" : "not verified",
    report.factor.factor_count,
    report.factor.unresolved_count,
    report.factor.elapsed_ms);
  snprintf(result->cyclo_status, sizeof(result->cyclo_status), "Scanned %zu candidates | exact %zu | %lu ms",
    report.cyclotomic.scanned,
    report.cyclotomic.exact_matches,
    report.cyclotomic.elapsed_ms);
  snprintf(result->bench_status, sizeof(result->bench_status), "Bench %zu/%zu | scratch ready %zu/%zu | oracle %zu | GNFS %zu",
    report.benchmark.passed_count,
    report.benchmark.result_count,
    report.benchmark.replacement_ready_count,
    report.benchmark.scratch_count,
    report.benchmark.oracle_only_count,
    report.gnfs.stage_count);
  snprintf(result->log_line, sizeof(result->log_line),
    "RUN COMPLETE\n"
    "Expression: %s\n"
    "Normalized: %s\n"
    "Run dir: %s\n"
    "Factor status: %s | accounting=%s | productVerified=%s | factors=%zu | unresolved=%zu\n"
    "Cyclotomic: scanned=%zu | exact=%zu | timedOut=%s\n"
    "GNFS stage proof: %s | stages=%zu\n\n"
    "%s",
    report.expression.raw ? report.expression.raw : "",
    report.expression.normalized ? report.expression.normalized : "invalid",
    report.run_dir ? report.run_dir : "n/a",
    report.factor.status[0] ? report.factor.status : "invalid",
    report.factor.accounting_verified ? "verified" : "failed",
    report.factor.product_verified ? "true" : "false",
    report.factor.factor_count,
    report.factor.unresolved_count,
    report.cyclotomic.scanned,
    report.cyclotomic.exact_matches,
    report.cyclotomic.timed_out ? "true" : "false",
    report.gnfs.status[0] ? report.gnfs.status : "skipped",
    report.gnfs.stage_count,
    report.events_jsonl ? report.events_jsonl : "");

  xray_workbench_report_clear(&report);
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
  app->run_active = 1;
  app->run_started_ms = xray_now_ms();
  app->run_pulse = 0;
  gtk_widget_set_sensitive(app->run_button, FALSE);
  gtk_button_set_label(GTK_BUTTON(app->run_button), "RUNNING...");
  gtk_widget_set_visible(app->cancel_button, TRUE);
  gtk_widget_set_sensitive(app->cancel_button, TRUE);
  set_run_status(app,
    "Factor Solver: RUNNING",
    "Product proof: running | awaiting verified report",
    "Live stage: Ingest | pulse 0",
    "Report assembling | worker active");
  set_text_view(app->json_view,
    "{\n"
    "  \"status\": \"running\",\n"
    "  \"note\": \"Native proof worker is active. Final JSON replaces this when the verified report is assembled.\"\n"
    "}\n");
  set_text_view(app->benchmark_view,
    "BENCHMARK RESULTS\n"
    "Running native proof worker...\n\n"
    "The table will populate when the benchmark report is assembled.\n"
    "You can keep this tab open; the final scratch-vs-GMP rows replace this placeholder automatically.\n");
  set_live_log(app);
  if (!app->live_timer_id) app->live_timer_id = g_timeout_add(250, live_run_tick, app);

  RunJob *job = (RunJob *)calloc(1, sizeof(*job));
  job->app = app;
  job->input = input;
  job->config = config_from_ui(app);
  GThread *thread = g_thread_new("xray-proof-worker", run_worker, job);
  g_thread_unref(thread);
}

static void on_cancel_clicked(GtkButton *button, gpointer user_data) {
  (void)button;
  AppState *app = (AppState *)user_data;
  app->cancel_requested = 1;
  gtk_widget_set_sensitive(app->cancel_button, FALSE);
  gtk_button_set_label(GTK_BUTTON(app->run_button), "CANCELLING...");
  set_run_status(app,
    "Factor Solver: CANCEL REQUESTED",
    "Product proof: stopping at checkpoint",
    "Live stage: Cancel checkpoint",
    "Report assembling | cancel pending");
  set_live_log(app);
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
  set_text_view(app->input_view, "Fermat(12)");
  set_text_view(app->log_view, "Loaded Fermat(12) = 2^4096 + 1 = Phi(8192, 2). Structure target, not a factorization promise.");
  if (app->scan_depth_combo) gtk_combo_box_set_active(GTK_COMBO_BOX(app->scan_depth_combo), 2);
}

static void on_save_clicked(GtkButton *button, gpointer user_data) {
  (void)button;
  AppState *app = (AppState *)user_data;
  set_text_view(app->log_view, "Runs are saved automatically under the runs/ workspace folder when Run Proof completes.");
}

static void on_reset_clicked(GtkButton *button, gpointer user_data) {
  (void)button;
  AppState *app = (AppState *)user_data;
  set_text_view(app->input_view, "2^12 + 1");
  set_text_view(app->log_view, "Workspace reset. Paste an exact integer expression, load a sample, then run proof.");
  if (app->scan_depth_combo) gtk_combo_box_set_active(GTK_COMBO_BOX(app->scan_depth_combo), 1);
  if (app->proof_strategy_combo) gtk_combo_box_set_active(GTK_COMBO_BOX(app->proof_strategy_combo), 0);
  if (app->factor_bits_combo) gtk_combo_box_set_active(GTK_COMBO_BOX(app->factor_bits_combo), 1);
}

static GtkWidget *build_input_rail(AppState *app) {
  GtkWidget *rail = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_add_css_class(rail, "rail");
  gtk_widget_set_hexpand(rail, FALSE);
  gtk_widget_set_halign(rail, GTK_ALIGN_START);
  gtk_widget_set_size_request(rail, rail_width(app), -1);
  gtk_box_append(GTK_BOX(rail), label_with_width("INPUT & CONFIGURATION", "section-title", 30, FALSE));

  GtkWidget *input_header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_box_append(GTK_BOX(input_header), label_with_width("Number / expression", "field-label", 24, FALSE));
  GtkWidget *base = label_with_width("dec  v", "micro", 8, FALSE);
  gtk_widget_set_halign(base, GTK_ALIGN_END);
  gtk_widget_set_hexpand(base, TRUE);
  gtk_box_append(GTK_BOX(input_header), base);
  gtk_box_append(GTK_BOX(rail), input_header);

  GtkWidget *editor = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  GtkWidget *gutter = label_with_width("1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20", "line-gutter", 3, FALSE);
  gtk_box_append(GTK_BOX(editor), gutter);
  GtkWidget *input_scroll = scrolled_text_view(&app->input_view, TRUE);
  gtk_widget_set_vexpand(input_scroll, FALSE);
  gtk_widget_set_hexpand(input_scroll, TRUE);
  gtk_widget_set_size_request(input_scroll, rail_width(app) - 70, app->layout == XRAY_LAYOUT_COMPACT ? 190 : 220);
  gtk_box_append(GTK_BOX(editor), input_scroll);
  gtk_box_append(GTK_BOX(rail), editor);
  set_text_view(app->input_view, "Phi(8192, 2)");

  GtkWidget *meta = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
  gtk_box_append(GTK_BOX(meta), label_with_width("Parser: exact DSL", "micro", 18, FALSE));
  gtk_box_append(GTK_BOX(meta), label_with_width("Artifacts: runs/", "micro", 18, FALSE));
  gtk_box_append(GTK_BOX(meta), label_with_width("Truth: product proof", "micro", 22, FALSE));
  gtk_box_append(GTK_BOX(rail), meta);

  GtkWidget *preset_label = label_with_width("Configuration Preset", "field-label", 28, FALSE);
  gtk_box_append(GTK_BOX(rail), preset_label);
  GtkWidget *preset = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_box_append(GTK_BOX(preset), field_face("X-Ray Standard (Default)        v"));
  GtkWidget *save = gtk_button_new_with_label("Save");
  gtk_widget_add_css_class(save, "reset-button");
  g_signal_connect(save, "clicked", G_CALLBACK(on_save_clicked), app);
  gtk_box_append(GTK_BOX(preset), save);
  gtk_box_append(GTK_BOX(rail), preset);

  const char *depth_items[] = {"Standard", "Deep", "GNFS Stage Proof", NULL};
  const char *strategy_items[] = {"Deterministic", "Aggressive local", "GNFS scaffold", NULL};
  const char *factor_items[] = {"64", "128", "256", NULL};
  const char *prime_items[] = {"Probable-prime proof", "Strict accounting", "Research witness log", NULL};
  const char *thread_items[] = {"Auto (12)", "4", "8", "16", NULL};
  const char *memory_items[] = {"8 GB", "16 GB", "32 GB", NULL};
  gtk_box_append(GTK_BOX(rail), combo_row("Scan Depth", depth_items, 1, &app->scan_depth_combo));
  gtk_box_append(GTK_BOX(rail), combo_row("Proof Strategy", strategy_items, 0, &app->proof_strategy_combo));
  gtk_box_append(GTK_BOX(rail), combo_row("Max Factor Bits", factor_items, 1, &app->factor_bits_combo));
  gtk_box_append(GTK_BOX(rail), combo_row("Primality Proving", prime_items, 0, &app->primality_combo));
  gtk_box_append(GTK_BOX(rail), combo_row("Threads", thread_items, 0, &app->threads_combo));
  gtk_box_append(GTK_BOX(rail), combo_row("Memory Limit", memory_items, 1, &app->memory_combo));

  GtkWidget *samples = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  app->toy_button = gtk_button_new_with_label("10403");
  app->challenge_button = gtk_button_new_with_label("RSA-260");
  app->fermat12_button = gtk_button_new_with_label("F12");
  gtk_widget_add_css_class(app->toy_button, "reset-button");
  gtk_widget_add_css_class(app->challenge_button, "reset-button");
  gtk_widget_add_css_class(app->fermat12_button, "reset-button");
  g_signal_connect(app->toy_button, "clicked", G_CALLBACK(on_toy_clicked), app);
  g_signal_connect(app->challenge_button, "clicked", G_CALLBACK(on_challenge_clicked), app);
  g_signal_connect(app->fermat12_button, "clicked", G_CALLBACK(on_fermat12_clicked), app);
  gtk_box_append(GTK_BOX(samples), app->toy_button);
  gtk_box_append(GTK_BOX(samples), app->challenge_button);
  gtk_box_append(GTK_BOX(samples), app->fermat12_button);
  gtk_box_append(GTK_BOX(rail), samples);

  GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  app->run_button = gtk_button_new_with_label("RUN PROOF");
  gtk_widget_add_css_class(app->run_button, "run-button");
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

  GtkWidget *reset = gtk_button_new_with_label("Reset Workspace");
  gtk_widget_add_css_class(reset, "reset-button");
  gtk_widget_set_hexpand(reset, TRUE);
  g_signal_connect(reset, "clicked", G_CALLBACK(on_reset_clicked), app);
  gtk_box_append(GTK_BOX(rail), reset);
  return rail;
}

static void update_tab_buttons(AppState *app, int page) {
  for (int index = 0; index < 4; ++index) {
    if (!app->tab_buttons[index]) continue;
    gtk_widget_remove_css_class(app->tab_buttons[index], "selected");
    if (index == page) gtk_widget_add_css_class(app->tab_buttons[index], "selected");
  }
}

static void on_tab_clicked(GtkButton *button, gpointer user_data) {
  AppState *app = (AppState *)user_data;
  int page = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "page-index"));
  if (app->notebook) gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook), page);
  update_tab_buttons(app, page);
}

static GtkWidget *tab_button(AppState *app, const char *label, int page) {
  GtkWidget *button = gtk_button_new_with_label(label);
  gtk_widget_add_css_class(button, "top-tab");
  if (page == 0) gtk_widget_add_css_class(button, "selected");
  g_object_set_data(G_OBJECT(button), "page-index", GINT_TO_POINTER(page));
  g_signal_connect(button, "clicked", G_CALLBACK(on_tab_clicked), app);
  app->tab_buttons[page] = button;
  return button;
}

static void on_details_clicked(GtkButton *button, gpointer user_data) {
  (void)button;
  AppState *app = (AppState *)user_data;
  if (app->notebook) gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook), 3);
  update_tab_buttons(app, 3);
}

static GtkWidget *build_center_tabs(AppState *app) {
  GtkWidget *notebook = gtk_notebook_new();
  app->notebook = notebook;
  gtk_notebook_set_scrollable(GTK_NOTEBOOK(notebook), FALSE);
  gtk_notebook_set_show_tabs(GTK_NOTEBOOK(notebook), FALSE);
  gtk_notebook_set_show_border(GTK_NOTEBOOK(notebook), FALSE);
  gtk_widget_set_hexpand(notebook, TRUE);
  gtk_widget_set_vexpand(notebook, TRUE);
  gtk_widget_set_size_request(notebook, app->layout == XRAY_LAYOUT_COMPACT ? 560 : 620, -1);

  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), build_xray_page(app), gtk_label_new("X-Ray"));
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), build_solver_page(app), gtk_label_new("Solver"));
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), build_benchmark_page(app), gtk_label_new("Bench"));
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), build_json_page(app), gtk_label_new("JSON"));
  gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), 0);
  return notebook;
}

static GtkWidget *build_inspector(AppState *app, int stacked) {
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_add_css_class(box, "inspector");
  gtk_widget_set_hexpand(box, FALSE);
  gtk_widget_set_size_request(box, stacked ? center_width(app) : inspector_width(app), -1);

  gtk_box_append(GTK_BOX(box), label_with_class("PROOF INSPECTOR", "section-title"));

  GtkWidget *product = section_box("panel", 8);
  gtk_box_append(GTK_BOX(product), label_with_class("PRODUCT VERIFICATION", "micro-title"));
  gtk_box_append(GTK_BOX(product), label_with_width(
    "Let n be the current input.\n"
    "Candidate factorization:\n"
    "(none accepted before a proof run)\n"
    "Reconstructed product:\n"
    "(computed only after exact factors are found)",
    "mono-small", 48, TRUE));
  app->proof_label = label_with_width("Status: NOT VERIFIED YET", "bad", 32, FALSE);
  gtk_box_append(GTK_BOX(product), app->proof_label);
  gtk_box_append(GTK_BOX(product), label_with_width("Rule: solved only means product-verified factors.", "mono-small", 48, FALSE));
  gtk_box_append(GTK_BOX(box), product);

  GtkWidget *unresolved = section_box("panel", 8);
  gtk_box_append(GTK_BOX(unresolved), label_with_class("CURRENT INPUT STATUS", "micro-title"));
  GtkWidget *unresolved_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_box_append(GTK_BOX(unresolved_row), label_with_width("Arbitrary integer target", "mono-small", 34, FALSE));
  app->factor_label = label_with_width("AWAITING RUN", "warn", 18, FALSE);
  gtk_widget_set_halign(app->factor_label, GTK_ALIGN_END);
  gtk_widget_set_hexpand(app->factor_label, TRUE);
  gtk_box_append(GTK_BOX(unresolved_row), app->factor_label);
  gtk_box_append(GTK_BOX(unresolved), unresolved_row);
  gtk_box_append(GTK_BOX(unresolved), label_with_width("Progress appears here after Run Proof.\nChallenge fixtures are samples, not separate app modes.\nNo valid factorization is accepted without product verification.", "mono-small", 48, TRUE));
  gtk_box_append(GTK_BOX(box), unresolved);

  GtkWidget *reference = section_box("panel", 8);
  gtk_box_append(GTK_BOX(reference), label_with_class("REFERENCE & STRUCTURE", "micro-title"));
  gtk_box_append(GTK_BOX(reference), label_with_width("Payam Paper (Open Access)", "field-label", 34, FALSE));
  gtk_box_append(GTK_BOX(reference), paper_link_button("https://amathz.com/my_gfn.html"));
  gtk_box_append(GTK_BOX(reference), label_with_width("Cyclotomic Structure Check", "field-label", 34, FALSE));
  app->bench_label = label_with_width("Status: READY", "warn", 24, FALSE);
  gtk_box_append(GTK_BOX(reference), app->bench_label);
  app->cyclo_label = label_with_width("Cyclotomic fingerprints are checked after a proof run.\nThe input can be RSA-260, F12, or any other integer.", "mono-small", 44, TRUE);
  gtk_box_append(GTK_BOX(reference), app->cyclo_label);
  GtkWidget *details = gtk_button_new_with_label("Details");
  gtk_widget_add_css_class(details, "reset-button");
  gtk_widget_set_halign(details, GTK_ALIGN_END);
  g_signal_connect(details, "clicked", G_CALLBACK(on_details_clicked), app);
  gtk_box_append(GTK_BOX(reference), details);
  gtk_box_append(GTK_BOX(box), reference);
  return box;
}

void xray_workbench_activate(GtkApplication *application, gpointer user_data) {
  (void)user_data;
  AppState *app = (AppState *)calloc(1, sizeof(*app));
  app->window = gtk_application_window_new(application);
  gtk_window_set_title(GTK_WINDOW(app->window), "Number X-Ray Workbench");
  gtk_window_set_titlebar(GTK_WINDOW(app->window), build_window_titlebar());
  app->layout = set_adaptive_window_size(GTK_WINDOW(app->window));

  GtkCssProvider *provider = gtk_css_provider_new();
  gtk_css_provider_load_from_data(provider, workbench_css, -1);
  gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref(provider);

  GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_window_set_child(GTK_WINDOW(app->window), root);

  GtkWidget *topbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
  gtk_widget_add_css_class(topbar, "chrome");
  gtk_box_append(GTK_BOX(topbar), label_with_width("X", "logo", 3, FALSE));
  const char *menus[] = {"File", "Edit", "View", "Tools", "Window", "Help"};
  for (int i = 0; i < 6; ++i) gtk_box_append(GTK_BOX(topbar), label_with_width(menus[i], "menu-label", 8, FALSE));
  GtkWidget *left_spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_hexpand(left_spacer, TRUE);
  gtk_box_append(GTK_BOX(topbar), left_spacer);
  gtk_box_append(GTK_BOX(topbar), tab_button(app, "X-Ray", 0));
  gtk_box_append(GTK_BOX(topbar), tab_button(app, "Solver", 1));
  gtk_box_append(GTK_BOX(topbar), tab_button(app, "Bench", 2));
  gtk_box_append(GTK_BOX(topbar), tab_button(app, "JSON", 3));
  GtkWidget *right_spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_hexpand(right_spacer, TRUE);
  gtk_box_append(GTK_BOX(topbar), right_spacer);
  gtk_box_append(GTK_BOX(topbar), label_with_width("●", "status-dot", 2, FALSE));
  gtk_box_append(GTK_BOX(topbar), label_with_width("Engine: Twin Parity v1.7.3", "top-status", 26, FALSE));
  gtk_box_append(GTK_BOX(topbar), label_with_width("|", "micro", 2, FALSE));
  gtk_box_append(GTK_BOX(topbar), label_with_width("Precision: 512-bit", "top-status", 20, FALSE));
  gtk_box_append(GTK_BOX(topbar), label_with_width("⚙", "settings-glyph", 2, FALSE));
  gtk_box_append(GTK_BOX(root), topbar);

  GtkWidget *content = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  gtk_widget_add_css_class(content, "workarea");
  set_margins(content, 6, 6, 6, 6);
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

  GtkWidget *right_area = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
  gtk_widget_set_hexpand(right_area, TRUE);
  gtk_widget_set_vexpand(right_area, TRUE);

  GtkWidget *top_region = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  gtk_widget_set_hexpand(top_region, TRUE);
  gtk_widget_set_vexpand(top_region, TRUE);

  GtkWidget *center_scroll = gtk_scrolled_window_new();
  gtk_widget_add_css_class(center_scroll, "surface");
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(center_scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
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
    gtk_box_append(GTK_BOX(top_region), right_stack);
  } else {
    gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(inspector_scroll), inspector_width(app));
    gtk_box_append(GTK_BOX(top_region), center_scroll);
    gtk_box_append(GTK_BOX(top_region), inspector_scroll);
  }
  gtk_box_append(GTK_BOX(right_area), top_region);

  GtkWidget *log_scroll = scrolled_text_view(&app->log_view, FALSE);
  gtk_widget_add_css_class(log_scroll, "log");
  gtk_widget_set_size_request(log_scroll, -1, app->layout == XRAY_LAYOUT_COMPACT ? 78 : 98);
  set_text_view(app->log_view,
    "RUN LOG\n"
    "Time          Level     Event                 Detail                                          Duration      Status\n"
    "--:--:--.---  L0        Ready                 Paste or edit any integer target               0.000s        IDLE\n"
    "--:--:--.---  L0        Ingest                Messy decimal input will be normalized         0.000s        IDLE\n"
    "--:--:--.---  L1        Sieve                 Small-factor and proof ladder are bounded      0.000s        IDLE\n"
    "--:--:--.---  L2        Structure             Payam-family fingerprints are evidence first   0.000s        IDLE\n"
    "--:--:--.---  L3        Account               Solved requires exact factor product proof     0.000s        IDLE\n");
  gtk_box_append(GTK_BOX(right_area), log_scroll);
  gtk_box_append(GTK_BOX(content), right_area);
  gtk_box_append(GTK_BOX(root), content);

  GtkWidget *statusbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_widget_add_css_class(statusbar, "statusbar");
  gtk_box_append(GTK_BOX(statusbar), label_with_width("Workspace: default.nxw", "micro", 26, FALSE));
  gtk_box_append(GTK_BOX(statusbar), label_with_width("|", "micro", 2, FALSE));
  gtk_box_append(GTK_BOX(statusbar), label_with_width("Last saved: 2025-05-20 14:32:11", "micro", 36, FALSE));
  app->language_button = gtk_button_new_with_label("FA");
  gtk_widget_add_css_class(app->language_button, "reset-button");
  g_signal_connect(app->language_button, "clicked", G_CALLBACK(on_language_clicked), app);
  gtk_box_append(GTK_BOX(statusbar), app->language_button);
  GtkWidget *status_spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_hexpand(status_spacer, TRUE);
  gtk_box_append(GTK_BOX(statusbar), status_spacer);
  gtk_box_append(GTK_BOX(statusbar), label_with_width("Threads: 12", "micro", 14, FALSE));
  gtk_box_append(GTK_BOX(statusbar), label_with_width("|", "micro", 2, FALSE));
  gtk_box_append(GTK_BOX(statusbar), label_with_width("CPU: 18%", "micro", 12, FALSE));
  gtk_box_append(GTK_BOX(statusbar), label_with_width("|", "micro", 2, FALSE));
  gtk_box_append(GTK_BOX(statusbar), label_with_width("RAM: 4.6 GB / 16 GB", "micro", 22, FALSE));
  gtk_box_append(GTK_BOX(statusbar), label_with_width("|", "micro", 2, FALSE));
  gtk_box_append(GTK_BOX(statusbar), label_with_width("Elapsed: 00:00:20", "micro", 20, FALSE));
  gtk_box_append(GTK_BOX(statusbar), label_with_width("●", "status-dot", 2, FALSE));
  gtk_box_append(GTK_BOX(root), statusbar);

  set_language(app);
  gtk_window_present(GTK_WINDOW(app->window));
}
