#include "nssm.h"

extern unsigned long tls_index;
extern bool is_admin;

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

void check_admin() {
  is_admin = false;

  /* Lifted from MSDN examples */
  PSID AdministratorsGroup;
  SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;
  if (! AllocateAndInitializeSid(&NtAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &AdministratorsGroup)) return;
  CheckTokenMembership(0, AdministratorsGroup, /*XXX*/(PBOOL) &is_admin);
  FreeSid(AdministratorsGroup);
}

int main(int argc, char **argv) {
  /* Remember if we are admin */
  check_admin();

  /* Elevate */
  if (argc > 1) {
    if (str_equiv(argv[1], "install") || str_equiv(argv[1], "remove")) {
      if (! is_admin) {
        fprintf(stderr, "Administrator access is needed to %s a service.\n", argv[1]);
        exit(100);
      }
    }

    /* Valid commands are install or remove */
    if (str_equiv(argv[1], "install")) {
      exit(pre_install_service(argc - 2, argv + 2));
    }
    if (str_equiv(argv[1], "remove")) {
      exit(pre_remove_service(argc - 2, argv + 2));
    }
  }

  /* Thread local storage for error message buffer */
  tls_index = TlsAlloc();

  /* Register messages */
  if (is_admin) create_messages();

  /* Start service magic */
  SERVICE_TABLE_ENTRY table[] = { { NSSM, service_main }, { 0, 0 } };
  if (! StartServiceCtrlDispatcher(table)) {
    unsigned long error = GetLastError();
    /* User probably ran nssm with no argument */
    if (error == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) exit(usage(1));
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_DISPATCHER_FAILED, error_string(error), 0);
    exit(100);
  }

  /* And nothing more to do */
  exit(0);
}
