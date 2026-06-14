#include "xray_workbench.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define XRAY_MKDIR(path) _mkdir(path)
#else
#include <sys/stat.h>
#define XRAY_MKDIR(path) mkdir(path, 0775)
#endif

static char *xray_strdup_gnfs(const char *text) {
  size_t length = text ? strlen(text) : 0;
  char *copy = (char *)calloc(length + 1, 1);
  if (!copy) return NULL;
  if (length) memcpy(copy, text, length);
  return copy;
}

static void ensure_dir(const char *path) {
  if (!path || !*path) return;
  if (XRAY_MKDIR(path) != 0 && errno != EEXIST) {
    /* Artifact creation is best-effort for the stage-proof scaffold. */
  }
}

static void path_join(char *out, size_t out_size, const char *left, const char *right) {
  snprintf(out, out_size, "%s/%s", left ? left : "", right ? right : "");
}

static int write_text_file(const char *path, const char *text) {
  FILE *file = NULL;
#ifdef _WIN32
  if (fopen_s(&file, path, "wb") != 0) file = NULL;
#else
  file = fopen(path, "wb");
#endif
  if (!file) return 0;
  if (text) fputs(text, file);
  fclose(file);
  return 1;
}

static void append_stage(XrayGnfsReport *report, const char *name, const char *status, const char *artifact, const char *detail, unsigned long started) {
  XrayGnfsStage *next = (XrayGnfsStage *)realloc(report->stages, sizeof(XrayGnfsStage) * (report->stage_count + 1));
  if (!next) return;
  report->stages = next;
  XrayGnfsStage *stage = &report->stages[report->stage_count++];
  memset(stage, 0, sizeof(*stage));
  snprintf(stage->name, sizeof(stage->name), "%s", name ? name : "");
  snprintf(stage->status, sizeof(stage->status), "%s", status ? status : "");
  snprintf(stage->artifact, sizeof(stage->artifact), "%s", artifact ? artifact : "");
  snprintf(stage->detail, sizeof(stage->detail), "%s", detail ? detail : "");
  stage->elapsed_ms = xray_now_ms() - started;
}

static void write_stage_artifact(XrayGnfsReport *report, const char *dir, const char *filename, const char *name, const char *detail, unsigned long started) {
  char path[512];
  path_join(path, sizeof(path), dir, filename);
  char body[1024];
  snprintf(body, sizeof(body),
    "{\n"
    "  \"stage\": \"%s\",\n"
    "  \"status\": \"stage-proof\",\n"
    "  \"detail\": \"%s\",\n"
    "  \"note\": \"Inspectable scaffold artifact; not a completed GNFS implementation yet.\"\n"
    "}\n",
    name,
    detail);
  int ok = write_text_file(path, body);
  append_stage(report, name, ok ? "artifact-written" : "artifact-failed", path, detail, started);
}

int xray_gnfs_stage_proof(const char *normalized_input, const char *run_dir, XrayGnfsReport *report) {
  if (!report) return 0;
  memset(report, 0, sizeof(*report));
  unsigned long started = xray_now_ms();
  report->input = xray_strdup_gnfs(normalized_input ? normalized_input : "");
  report->run_dir = xray_strdup_gnfs(run_dir ? run_dir : "");

  char artifacts_dir[512];
  char gnfs_dir[512];
  if (run_dir && *run_dir) {
    path_join(artifacts_dir, sizeof(artifacts_dir), run_dir, "artifacts");
    ensure_dir(artifacts_dir);
    path_join(gnfs_dir, sizeof(gnfs_dir), artifacts_dir, "gnfs");
    ensure_dir(gnfs_dir);
  } else {
    snprintf(gnfs_dir, sizeof(gnfs_dir), ".");
  }

  write_stage_artifact(report, gnfs_dir, "01-polynomial-selection.json", "polynomial-selection",
    "Select candidate rational/algebraic polynomials and score size/root properties.", started);
  write_stage_artifact(report, gnfs_dir, "02-factor-bases.json", "factor-bases",
    "Emit rational and algebraic factor-base boundaries for a toy GNFS run.", started);
  write_stage_artifact(report, gnfs_dir, "03-relation-collection.json", "relation-collection",
    "Record lattice-sieving relation schema and smoothness thresholds.", started);
  write_stage_artifact(report, gnfs_dir, "04-filtering.json", "filtering",
    "Represent duplicate removal, singleton pruning, and relation graph compaction.", started);
  write_stage_artifact(report, gnfs_dir, "05-sparse-matrix.json", "sparse-matrix",
    "Emit sparse matrix dimensions and row/column checksum placeholders.", started);
  write_stage_artifact(report, gnfs_dir, "06-dependency-attempt.json", "dependency-attempt",
    "Record block-Wiedemann-style dependency attempt metadata.", started);
  write_stage_artifact(report, gnfs_dir, "07-square-root-attempt.json", "square-root-attempt",
    "Record square-root phase input contract and product-verification guard.", started);

  snprintf(report->status, sizeof(report->status), "stage-proof");
  report->elapsed_ms = xray_now_ms() - started;
  return 1;
}

void xray_gnfs_report_clear(XrayGnfsReport *report) {
  if (!report) return;
  free(report->input);
  free(report->run_dir);
  free(report->stages);
  memset(report, 0, sizeof(*report));
}
