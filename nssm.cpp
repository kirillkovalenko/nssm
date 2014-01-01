#include "nssm.h"

extern unsigned long tls_index;
extern bool is_admin;
extern imports_t imports;

/* Are two strings case-insensitively equivalent? */
int str_equiv(const TCHAR *a, const TCHAR *b) {
  size_t len = _tcslen(a);
  if (_tcslen(b) != len) return 0;
  if (_tcsnicmp(a, b, len)) return 0;
  return 1;
}

/* Remove basename of a path. */
void strip_basename(TCHAR *buffer) {
  size_t len = _tcslen(buffer);
  size_t i;
  for (i = len; i && buffer[i] != _T('\\') && buffer[i] != _T('/'); i--);
  /* X:\ is OK. */
  if (i && buffer[i - 1] == _T(':')) i++;
  buffer[i] = _T('\0');
}

/* How to use me correctly */
int usage(int ret) {
  if (GetConsoleWindow()) print_message(stderr, NSSM_MESSAGE_USAGE, NSSM_VERSION, NSSM_DATE);
  else popup_message(0, MB_OK, NSSM_MESSAGE_USAGE, NSSM_VERSION, NSSM_DATE);
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

/* See if we were launched from a console window. */
static void check_console() {
  /* If we're running in a service context there will be no console window. */
  HWND console = GetConsoleWindow();
  if (! console) return;

  unsigned long pid;
  if (! GetWindowThreadProcessId(console, &pid)) return;

  /*
    If the process associated with the console window handle is the same as
    this process, we were not launched from an existing console.  The user
    probably double-clicked our executable.
  */
  if (GetCurrentProcessId() != pid) return;

  /* We close our new console so that subsequent messages appear in a popup. */
  FreeConsole();
}

int _tmain(int argc, TCHAR **argv) {
  check_console();

  /* Remember if we are admin */
  check_admin();

  /* Elevate */
  if (argc > 1) {
    /* Valid commands are install, edit or remove */
    if (str_equiv(argv[1], _T("install"))) {
      if (! is_admin) {
        print_message(stderr, NSSM_MESSAGE_NOT_ADMINISTRATOR_CANNOT_INSTALL);
        exit(100);
      }
      exit(pre_install_service(argc - 2, argv + 2));
    }
    if (str_equiv(argv[1], _T("edit"))) {
      if (! is_admin) {
        print_message(stderr, NSSM_MESSAGE_NOT_ADMINISTRATOR_CANNOT_EDIT);
        exit(100);
      }
      exit(pre_edit_service(argc - 2, argv + 2));
    }
    if (str_equiv(argv[1], _T("remove"))) {
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
