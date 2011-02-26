#include "nssm.h"

extern unsigned long tls_index;

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
  fprintf(stderr, "Usage: nssm <option> [args]\n\n");
  fprintf(stderr, "To show service installation GUI:\n\n");
  fprintf(stderr, "        nssm install [<servicename>]\n\n");
  fprintf(stderr, "To install a service without confirmation:\n\n");
  fprintf(stderr, "        nssm install <servicename> <app> [<args>]\n\n");
  fprintf(stderr, "To show service removal GUI:\n\n");
  fprintf(stderr, "        nssm remove [<servicename>]\n\n");
  fprintf(stderr, "To remove a service without confirmation:\n\n");
  fprintf(stderr, "        nssm remove <servicename> confirm\n");
  return(ret);
}

int main(int argc, char **argv) {
  /* Require an argument since users may try to run nssm directly */
  if (argc == 1) exit(usage(1));

  /* Valid commands are install or remove */
  if (str_equiv(argv[1], "install")) {
    exit(pre_install_service(argc - 2, argv + 2));
  }
  if (str_equiv(argv[1], "remove")) {
    exit(pre_remove_service(argc - 2, argv + 2));
  }
  /* Undocumented: "run" is used to actually do service stuff */
  if (! str_equiv(argv[1], NSSM_RUN)) exit(usage(2));

  /* Thread local storage for error message buffer */
  tls_index = TlsAlloc();

  /* Register messages */
  create_messages();

  /* Start service magic */
  SERVICE_TABLE_ENTRY table[] = { { NSSM, service_main }, { 0, 0 } };
  if (! StartServiceCtrlDispatcher(table)) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_DISPATCHER_FAILED, error_string(GetLastError()), 0);
    return 100;
  }

  /* And nothing more to do */
  return 0;
}
