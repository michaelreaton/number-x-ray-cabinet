#include "workbench.h"

#ifdef _WIN32
#include <stdlib.h>
#include <windows.h>
#include <shellapi.h>
#endif

static int run_workbench(int argc, char **argv) {
  GtkApplication *application = gtk_application_new("org.payam.xray.workbench", G_APPLICATION_FLAGS_NONE);
  g_signal_connect(application, "activate", G_CALLBACK(xray_workbench_activate), NULL);
  int status = g_application_run(G_APPLICATION(application), argc, argv);
  g_object_unref(application);
  return status;
}

int main(int argc, char **argv) {
  return run_workbench(argc, argv);
}

#ifdef _WIN32
static char **windows_command_line_argv(int *argc_out) {
  int argc = 0;
  LPWSTR *wide_argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (!wide_argv) {
    *argc_out = 0;
    return NULL;
  }

  char **argv = (char **)calloc((size_t)argc + 1u, sizeof(char *));
  if (!argv) {
    LocalFree(wide_argv);
    *argc_out = 0;
    return NULL;
  }

  for (int i = 0; i < argc; ++i) {
    int needed = WideCharToMultiByte(CP_UTF8, 0, wide_argv[i], -1, NULL, 0, NULL, NULL);
    if (needed <= 0) continue;
    argv[i] = (char *)calloc((size_t)needed, sizeof(char));
    if (!argv[i]) continue;
    WideCharToMultiByte(CP_UTF8, 0, wide_argv[i], -1, argv[i], needed, NULL, NULL);
  }

  LocalFree(wide_argv);
  *argc_out = argc;
  return argv;
}

static void windows_command_line_free(char **argv, int argc) {
  if (!argv) return;
  for (int i = 0; i < argc; ++i) {
    free(argv[i]);
  }
  free(argv);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous_instance, LPSTR command_line, int show_command) {
  (void)instance;
  (void)previous_instance;
  (void)command_line;
  (void)show_command;
  int argc = 0;
  char **argv = windows_command_line_argv(&argc);
  int status = run_workbench(argc, argv);
  windows_command_line_free(argv, argc);
  return status;
}
#endif
