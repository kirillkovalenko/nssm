#include "nssm.h"

extern unsigned long tls_index;
extern bool is_admin;
extern imports_t imports;

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
  print_message(stderr, NSSM_MESSAGE_USAGE, NSSM_VERSION, NSSM_DATE);
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
    /* Valid commands are install or remove */
    if (str_equiv(argv[1], "install")) {
      if (! is_admin) {
        print_message(stderr, NSSM_MESSAGE_NOT_ADMINISTRATOR_CANNOT_INSTALL);
        exit(100);
      }
      exit(pre_install_service(argc - 2, argv + 2));
    }
    if (str_equiv(argv[1], "remove")) {
      if (! is_admin) {
        print_message(stderr, NSSM_MESSAGE_NOT_ADMINISTRATOR_CANNOT_REMOVE);
        exit(100);
      }
      exit(pre_remove_service(argc - 2, argv + 2));
    }
  }

  /* Thread local storage for error message buffer */
  tls_index = TlsAlloc();

  /* Register messages */
  if (is_admin) create_messages();

  /*
    Optimisation for Windows 2000:
    When we're run from the command line the StartServiceCtrlDispatcher() call
    will time out after a few seconds on Windows 2000.  On newer versions the
    call returns instantly.  Check for stdin first and only try to call the
    function if there's no input stream found.  Although it's possible that
    we're running with input redirected it's much more likely that we're
    actually running as a service.
    This will save time when running with no arguments from a command prompt.
  */
  if (_fileno(stdin) < 0) {
    /* Set up function pointers. */
    if (get_imports()) exit(111);

    /* Start service magic */
    SERVICE_TABLE_ENTRY table[] = { { NSSM, service_main }, { 0, 0 } };
    if (! StartServiceCtrlDispatcher(table)) {
      unsigned long error = GetLastError();
      /* User probably ran nssm with no argument */
      if (error == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) exit(usage(1));
      log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_DISPATCHER_FAILED, error_string(error), 0);
      free_imports();
      exit(100);
    }
  }
  else exit(usage(1));

  /* And nothing more to do */
  exit(0);
}
