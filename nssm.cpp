#include "nssm.h"

/* String function */
int str_equiv(const char *a, const char *b) {
  int i;
  for (i = 0; ; i++) {
    if (tolower(b[i]) != tolower(a[i])) return 0;
    if (! a[i]) return 1;
  }
}

/* How to use me correctly */
int usage(int ret) {
  fprintf(stderr, "NSSM: The non-sucking service manager\n");
  fprintf(stderr, "Version %s, %s\n", NSSM_VERSION, NSSM_DATE);
  fprintf(stderr, "Usage: nssm option [args]\n");
  fprintf(stderr, "To install a service: nssm install [servicename]\n");
  fprintf(stderr, "To remove a service: nssm remove [servicename]\n");
  exit(ret);
}

int main(int argc, char **argv) {
  /* Require an argument since users may try to run nssm directly */
  if (argc == 1) exit(usage(1));

  /* Valid commands are install or remove */
  if (str_equiv(argv[1], "install")) exit(install_service(argv[2]));
  if (str_equiv(argv[1], "remove")) exit(remove_service(argv[2]));
  /* Undocumented: "run" is used to actually do service stuff */
  if (! str_equiv(argv[1], NSSM_RUN)) exit(usage(2));

  /* Start service magic */
  SERVICE_TABLE_ENTRY table[] = { { NSSM, service_main }, { 0, 0 } };
  if (! StartServiceCtrlDispatcher(table)) {
    char *message = error_string(GetLastError());
    eventprintf(EVENTLOG_ERROR_TYPE, "StartServiceCtrlDispatcher() failed: %s", message);
    if (message) LocalFree(message);
    return 100;
  }

  /* And nothing more to do */
  return 0;
}
