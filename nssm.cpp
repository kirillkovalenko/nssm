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

int num_cpus() {
  DWORD_PTR i, affinity, system_affinity;
  if (! GetProcessAffinityMask(GetCurrentProcess(), &affinity, &system_affinity)) return 64;
  for (i = 0; system_affinity & (1LL << i); i++);
  return (int) i;
}

static inline void block(unsigned int a, short x, short y, unsigned long n) {
  HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
  TCHAR s = _T(' ');

  unsigned long out;
  COORD c = { x, y };
  FillConsoleOutputAttribute(h, a, n, c, &out);
  FillConsoleOutputCharacter(h, s, n, c, &out);
}

static inline void R(short x, short y, unsigned long n) {
  block(BACKGROUND_RED | BACKGROUND_INTENSITY, x, y, n);
}

static inline void r(short x, short y, unsigned long n) {
  block(BACKGROUND_RED, x, y, n);
}

static inline void b(short x, short y, unsigned long n) {
  block(0, x, y, n);
}

void banner() {
  short y = 0;

  b(0, y, 80);
  y++;

  b(0, y, 80);
  y++;

  b(0, y, 80);
  y++;

  b(0, y, 80);
  y++;

  b(0, y, 80);
  r(18, y, 5); r(28, y, 4); r(41, y, 4); r(68, y, 1);
  R(6, y, 5); R(19, y, 4); R(29, y, 1); R(32, y, 3); R(42, y, 1); R(45, y, 3); R(52, y, 5); R(69, y, 4);
  y++;

  b(0, y, 80);
  r(8, y, 4); r(20, y, 1); r(28, y, 1); r(33, y, 3); r(41, y, 1); r(46, y, 3); r (57, y, 1);
  R(9, y, 2); R(21, y, 1); R(27, y, 1); R(34, y, 1); R(40, y, 1); R(47, y, 1); R(54, y, 3); R(68, y, 3);
  y++;

  b(0, y, 80);
  r(12, y, 1); r(20, y, 1); r(26, y, 1); r(34, y, 2); r(39, y, 1); r(47, y, 2); r(67, y, 2);
  R(9, y, 3); R(21, y, 1); R(27, y, 1); R(40, y, 1); R(54, y, 1); R(56, y, 2); R(67, y, 1); R(69, y, 2);
  y++;

  b(0, y, 80);
  r(9, y, 1); r(20, y, 1); r(26, y, 1); r (35, y, 1); r(39, y, 1); r(48, y, 1); r(58, y, 1);
  R(10, y, 3); R(21, y, 1); R(27, y, 1); R(40, y, 1); R(54, y, 1); R(56, y, 2); R(67, y, 1); R(69, y, 2);
  y++;

  b(0, y, 80);
  r(9, y, 1); r(56, y, 1); r(66, y, 2);
  R(11, y, 3); R(21, y, 1); R(26, y, 2); R(39, y, 2); R(54, y, 1); R(57, y, 2); R(69, y, 2);
  y++;

  b(0, y, 80);
  r(9, y, 1); r(26, y, 1); r(39, y, 1); r(59, y, 1);
  R(12, y, 3); R(21, y, 1); R(27, y, 2); R(40, y, 2); R(54, y, 1); R(57, y, 2); R(66, y, 1); R(69, y, 2);
  y++;

  b(0, y, 80);
  r(9, y, 1); r(12, y, 4); r(30, y, 1); r(43, y, 1); r(57, y, 1); r(65, y, 2);
  R(13, y, 2); R(21, y, 1); R(27, y, 3); R(40, y, 3); R(54, y, 1); R(58, y, 2); R(69, y, 2);
  y++;

  b(0, y, 80);
  r(9, y, 1); r(13, y, 4); r(27, y, 7); r(40, y, 7);
  R(14, y, 2); R(21, y, 1); R(28, y, 5); R(41, y, 5); R(54, y, 1); R(58, y, 2); R(65, y, 1); R(69, y, 2);
  y++;

  b(0, y, 80);
  r(9, y, 1); r(60, y, 1); r(65, y, 1);
  R(14, y, 3); R(21, y, 1); R(29, y, 6); R(42, y, 6); R(54, y, 1); R(58, y, 2); R(69, y, 2);
  y++;

  b(0, y, 80);
  r(9, y, 1); r(31, y, 1); r(44, y, 1); r(58, y, 1); r(64, y, 1);
  R(15, y, 3); R(21, y, 1); R(32, y, 4); R(45, y, 4); R(54, y, 1); R(59, y, 2); R(69, y, 2);
  y++;

  b(0, y, 80);
  r(9, y, 1); r(33, y, 1); r(46, y, 1); r(61, y, 1); r(64, y, 1);
  R(16, y, 3); R(21, y, 1); R(34, y, 2); R(47, y, 2); R(54, y, 1); R(59, y, 2); R(69, y, 2);
  y++;

  b(0, y, 80);
  r(9, y, 1); r(16, y, 4); r(36, y, 1); r(49, y, 1); r(59, y, 1); r(63, y, 1);
  R(17, y, 2); R(21, y, 1); R(34, y, 2); R(47, y, 2); R(54, y, 1); R(60, y, 2); R(69, y, 2);
  y++;

  b(0, y, 80);
  r(9, y, 1); r(17, y, 4); r(26, y, 1); r(36, y, 1); r(39, y, 1); r(49, y, 1);
  R(18, y, 2); R(21, y, 1); R(35, y, 1); R(48, y, 1); R(54, y, 1); R(60, y, 2); R(63, y, 1); R(69, y, 2);
  y++;

  b(0, y, 80);
  r(26, y, 2); r(39, y, 2); r(63, y, 1);
  R(9, y, 1); R(18, y, 4); R(35, y, 1); R(48, y, 1); R(54, y, 1); R(60, y, 3); R(69, y, 2);
  y++;

  b(0, y, 80);
  r(34, y, 1); r(47, y, 1); r(60, y, 1);
  R(9, y, 1); R(19, y, 3); R(26, y, 2); R(35, y, 1); R(39, y, 2); R(48, y, 1); R(54, y, 1); R(61, y, 2); R(69, y, 2);
  y++;

  b(0, y, 80);
  r(8, y, 1); r(35, y, 1); r(48, y, 1); r(62, y, 1); r(71, y, 1);
  R(9, y, 1); R(20, y, 2); R(26, y, 3); R(34, y, 1); R(39, y, 3); R(47, y, 1); R(54, y, 1); R(61, y, 1); R(69, y, 2);
  y++;

  b(0, y, 80);
  r(11, y, 1); r(26, y, 1); r(28, y, 5); r(39, y, 1); r(41, y, 5); r(51, y, 7); r(61, y, 1); r(66, y, 8);
  R(7, y, 4); R(21, y, 1); R(29, y, 1); R(33, y, 1); R(42, y, 1); R(46, y, 1); R(52, y, 5); R(67, y, 7);
  y++;

  b(0, y, 80);
  y++;

  b(0, y, 80);
  y++;
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
      if (! is_admin) {
        print_message(stderr, NSSM_MESSAGE_NOT_ADMINISTRATOR_CANNOT_INSTALL);
        exit(100);
      }
      exit(pre_install_service(argc - 2, argv + 2));
    }
    if (str_equiv(argv[1], _T("edit")) || str_equiv(argv[1], _T("get")) || str_equiv(argv[1], _T("set")) || str_equiv(argv[1], _T("reset")) || str_equiv(argv[1], _T("unset"))) {
      if (! is_admin) {
        print_message(stderr, NSSM_MESSAGE_NOT_ADMINISTRATOR_CANNOT_EDIT);
        exit(100);
      }
      int ret = pre_edit_service(argc - 1, argv + 1);
      /* There might be a password here. */
      for (int i = 0; i < argc; i++) SecureZeroMemory(argv[i], _tcslen(argv[i]) * sizeof(TCHAR));
      exit(ret);
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
