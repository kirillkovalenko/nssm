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

/* Convert a string to a number. */
int str_number(const TCHAR *string, unsigned long *number, TCHAR **bogus) {
  if (! string) return 1;

  *number = _tcstoul(string, bogus, 0);
  if (**bogus) return 2;

  return 0;
}

int str_number(const TCHAR *string, unsigned long *number) {
  TCHAR *bogus;
  return str_number(string, number, &bogus);
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
  if (GetConsoleWindow()) print_message(stderr, NSSM_MESSAGE_USAGE, NSSM_VERSION, NSSM_CONFIGURATION, NSSM_DATE);
  else popup_message(0, MB_OK, NSSM_MESSAGE_USAGE, NSSM_VERSION, NSSM_CONFIGURATION, NSSM_DATE);
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

static int elevate(int argc, TCHAR **argv, unsigned long message) {
  print_message(stderr, message);

  SHELLEXECUTEINFO sei;
  ZeroMemory(&sei, sizeof(sei));
  sei.cbSize = sizeof(sei);
  sei.lpVerb = _T("runas");
  sei.lpFile = (TCHAR *) HeapAlloc(GetProcessHeap(), 0, PATH_LENGTH);
  if (! sei.lpFile) {
    print_message(stderr, NSSM_MESSAGE_OUT_OF_MEMORY, _T("GetModuleFileName()"), _T("elevate()"));
    return 111;
  }
  GetModuleFileName(0, (TCHAR *) sei.lpFile, PATH_LENGTH);

  TCHAR *args = (TCHAR *) HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, EXE_LENGTH * sizeof(TCHAR));
  if (! args) {
    HeapFree(GetProcessHeap(), 0, (void *) sei.lpFile);
    print_message(stderr, NSSM_MESSAGE_OUT_OF_MEMORY, _T("GetCommandLine()"), _T("elevate()"));
    return 111;
  }

  /* Get command line, which includes the path to NSSM, and skip that part. */
  _sntprintf_s(args, EXE_LENGTH, _TRUNCATE, _T("%s"), GetCommandLine());
  size_t s = _tcslen(argv[0]) + 1;
  if (args[0] == _T('"')) s += 2;
  while (isspace(args[s])) s++;

  sei.lpParameters = args + s;
  sei.nShow = SW_SHOW;

  unsigned long exitcode = 0;
  if (! ShellExecuteEx(&sei)) exitcode = 100;

  HeapFree(GetProcessHeap(), 0, (void *) sei.lpFile);
  HeapFree(GetProcessHeap(), 0, (void *) args);
  return exitcode;
}

int num_cpus() {
  DWORD_PTR i, affinity, system_affinity;
  if (! GetProcessAffinityMask(GetCurrentProcess(), &affinity, &system_affinity)) return 64;
  for (i = 0; system_affinity & (1LL << i); i++);
  return (int) i;
}

int _tmain(int argc, TCHAR **argv) {
  check_console();

#ifdef UNICODE
  /*
    Ensure we write in UTF-16 mode, so that non-ASCII characters don't get
    mangled.  If we were compiled in ANSI mode it won't work.
   */
  _setmode(_fileno(stdout), _O_U16TEXT);
  _setmode(_fileno(stderr), _O_U16TEXT);
#endif

  /* Remember if we are admin */
  check_admin();

  /* Set up function pointers. */
  if (get_imports()) exit(111);

  /* Elevate */
  if (argc > 1) {
    /*
      Valid commands are:
      start, stop, pause, continue, install, edit, get, set, reset, unset, remove
    */
    if (str_equiv(argv[1], _T("start"))) exit(control_service(NSSM_SERVICE_CONTROL_START, argc - 2, argv + 2));
    if (str_equiv(argv[1], _T("stop"))) exit(control_service(SERVICE_CONTROL_STOP, argc - 2, argv + 2));
    if (str_equiv(argv[1], _T("restart"))) {
      int ret = control_service(SERVICE_CONTROL_STOP, argc - 2, argv + 2);
      if (ret) exit(ret);
      exit(control_service(NSSM_SERVICE_CONTROL_START, argc - 2, argv + 2));
    }
    if (str_equiv(argv[1], _T("pause"))) exit(control_service(SERVICE_CONTROL_PAUSE, argc - 2, argv + 2));
    if (str_equiv(argv[1], _T("continue"))) exit(control_service(SERVICE_CONTROL_CONTINUE, argc - 2, argv + 2));
    if (str_equiv(argv[1], _T("status"))) exit(control_service(SERVICE_CONTROL_INTERROGATE, argc - 2, argv + 2));
    if (str_equiv(argv[1], _T("rotate"))) exit(control_service(NSSM_SERVICE_CONTROL_ROTATE, argc - 2, argv + 2));
    if (str_equiv(argv[1], _T("install"))) {
      if (! is_admin) exit(elevate(argc, argv, NSSM_MESSAGE_NOT_ADMINISTRATOR_CANNOT_INSTALL));
      exit(pre_install_service(argc - 2, argv + 2));
    }
    if (str_equiv(argv[1], _T("edit")) || str_equiv(argv[1], _T("get")) || str_equiv(argv[1], _T("set")) || str_equiv(argv[1], _T("reset")) || str_equiv(argv[1], _T("unset"))) {
      int ret = pre_edit_service(argc - 1, argv + 1);
      if (ret == 3 && ! is_admin && argc == 3) exit(elevate(argc, argv, NSSM_MESSAGE_NOT_ADMINISTRATOR_CANNOT_EDIT));
      /* There might be a password here. */
      for (int i = 0; i < argc; i++) SecureZeroMemory(argv[i], _tcslen(argv[i]) * sizeof(TCHAR));
      exit(ret);
    }
    if (str_equiv(argv[1], _T("remove"))) {
      if (! is_admin) exit(elevate(argc, argv, NSSM_MESSAGE_NOT_ADMINISTRATOR_CANNOT_REMOVE));
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
  if (! GetStdHandle(STD_INPUT_HANDLE)) {
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
