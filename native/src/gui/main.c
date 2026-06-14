#include "workbench.h"

int main(int argc, char **argv) {
  GtkApplication *application = gtk_application_new("org.payam.xray.workbench", G_APPLICATION_FLAGS_NONE);
  g_signal_connect(application, "activate", G_CALLBACK(xray_workbench_activate), NULL);
  int status = g_application_run(G_APPLICATION(application), argc, argv);
  g_object_unref(application);
  return status;
}
