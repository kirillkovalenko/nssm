#include "nssm.h"

extern unsigned long tls_index;
extern bool is_admin;
extern imports_t imports;

static TCHAR unquoted_imagepath[PATH_LENGTH];
static TCHAR imagepath[PATH_LENGTH];
static TCHAR imageargv0[PATH_LENGTH];

void nssm_exit(int status) {
  free_imports();
  unsetup_utf8();
  exit(status);
}

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

/* User requested us to print our version. */
static bool is_version(const TCHAR *s) {
  if (! s || ! *s) return false;
  /* /version */
  if (*s == '/') s++;
  else if (*s == '-') {
    /* -v, -V, -version, --version */
    s++;
    if (*s == '-') s++;
    else if (str_equiv(s, _T("v"))) return true;
  }
  if (str_equiv(s, _T("version"))) return true;
  return false;
}

int str_number(const TCHAR *string, unsigned long *number) {
  TCHAR *bogus;
  return str_number(string, number, &bogus);
}

/* Does a char need to be escaped? */
static bool needs_escape(const TCHAR c) {
  if (c == _T('"')) return true;
  if (c == _T('&')) return true;
  if (c == _T('%')) return true;
  if (c == _T('^')) return true;
  if (c == _T('<')) return true;
  if (c == _T('>')) return true;
  if (c == _T('|')) return true;
  return false;
}

/* Does a char need to be quoted? */
static bool needs_quote(const TCHAR c) {
  if (c == _T(' ')) return true;
  if (c == _T('\t')) return true;
  if (c == _T('\n')) return true;
  if (c == _T('\v')) return true;
  if (c == _T('"')) return true;
  if (c == _T('*')) return true;
  return needs_escape(c);
}

/* https://blogs.msdn.microsoft.com/twistylittlepassagesallalike/2011/04/23/everyone-quotes-command-line-arguments-the-wrong-way/ */
/* http://www.robvanderwoude.com/escapechars.php */
int quote(const TCHAR *unquoted, TCHAR *buffer, size_t buflen) {
  size_t i, j, n;
  size_t len = _tcslen(unquoted);
  if (len > buflen - 1) return 1;

  bool escape = false;
  bool quotes = false;

  for (i = 0; i < len; i++) {
    if (needs_escape(unquoted[i])) {
      escape = quotes = true;
      break;
    }
    if (needs_quote(unquoted[i])) quotes = true;
  }
  if (! quotes) {
    memmove(buffer, unquoted, (len + 1) * sizeof(TCHAR));
    return 0;
  }

  /* "" */
  size_t quoted_len = 2;
  if (escape) quoted_len += 2;
  for (i = 0; ; i++) {
    n = 0;

    while (i != len && unquoted[i] == _T('\\')) {
      i++;
      n++;
    }

    if (i == len) {
      quoted_len += n * 2;
      break;
    }
    else if (unquoted[i] == _T('"')) quoted_len += n * 2 + 2;
    else quoted_len += n + 1;
    if (needs_escape(unquoted[i])) quoted_len += n;
  }
  if (quoted_len > buflen - 1) return 1;

  TCHAR *s = buffer;
  if (escape) *s++ = _T('^');
  *s++ = _T('"');

  for (i = 0; ; i++) {
    n = 0;

    while (i != len && unquoted[i] == _T('\\')) {
      i++;
      n++;
    }

    if (i == len) {
      for (j = 0; j < n * 2; j++) {
        if (escape) *s++ = _T('^');
        *s++ = _T('\\');
      }
      break;
    }
    else if (unquoted[i] == _T('"')) {
      for (j = 0; j < n * 2 + 1; j++) {
        if (escape) *s++ = _T('^');
        *s++ = _T('\\');
      }
      if (escape && needs_escape(unquoted[i])) *s++ = _T('^');
      *s++ = unquoted[i];
    }
    else {
      for (j = 0; j < n; j++) {
        if (escape) *s++ = _T('^');
        *s++ = _T('\\');
      }
      if (escape && needs_escape(unquoted[i])) *s++ = _T('^');
      *s++ = unquoted[i];
    }
  }
  if (escape) *s++ = _T('^');
  *s++ = _T('"');
  *s++ = _T('\0');

  return 0;
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
  if ((! GetConsoleWindow() || ! GetStdHandle(STD_OUTPUT_HANDLE)) && GetProcessWindowStation()) popup_message(0, MB_OK, NSSM_MESSAGE_USAGE, NSSM_VERSION, NSSM_CONFIGURATION, NSSM_DATE);
  else print_message(stderr, NSSM_MESSAGE_USAGE, NSSM_VERSION, NSSM_CONFIGURATION, NSSM_DATE);
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
  sei.lpFile = (TCHAR *) nssm_imagepath();

  TCHAR *args = (TCHAR *) HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, EXE_LENGTH * sizeof(TCHAR));
  if (! args) {
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

  HeapFree(GetProcessHeap(), 0, (void *) args);
  return exitcode;
}

int num_cpus() {
  DWORD_PTR i, affinity, system_affinity;
  if (! GetProcessAffinityMask(GetCurrentProcess(), &affinity, &system_affinity)) return 64;
  for (i = 0; system_affinity & (1LL << i); i++);
  return (int) i;
}

const TCHAR *nssm_unquoted_imagepath() {
  return unquoted_imagepath;
}

const TCHAR *nssm_imagepath() {
  return imagepath;
}

const TCHAR *nssm_exe() {
  return imageargv0;
}

int _tmain(int argc, TCHAR **argv) {
  if (check_console()) setup_utf8();

  /* Remember if we are admin */
  check_admin();

  /* Set up function pointers. */
  if (get_imports()) nssm_exit(111);

  /* Remember our path for later. */
  _sntprintf_s(imageargv0, _countof(imageargv0), _TRUNCATE, _T("%s"), argv[0]);
  PathQuoteSpaces(imageargv0);
  GetModuleFileName(0, unquoted_imagepath, _countof(unquoted_imagepath));
  GetModuleFileName(0, imagepath, _countof(imagepath));
  PathQuoteSpaces(imagepath);

  /* Elevate */
  if (argc > 1) {
    /*
      Valid commands are:
      start, stop, pause, continue, install, edit, get, set, reset, unset, remove
      status, statuscode, rotate, list, processes, version
    */
    if (is_version(argv[1])) {
      _tprintf(_T("%s %s %s %s\n"), NSSM, NSSM_VERSION, NSSM_CONFIGURATION, NSSM_DATE);
      nssm_exit(0);
    }
    if (str_equiv(argv[1], _T("start"))) nssm_exit(control_service(NSSM_SERVICE_CONTROL_START, argc - 2, argv + 2));
    if (str_equiv(argv[1], _T("stop"))) nssm_exit(control_service(SERVICE_CONTROL_STOP, argc - 2, argv + 2));
    if (str_equiv(argv[1], _T("restart"))) {
      int ret = control_service(SERVICE_CONTROL_STOP, argc - 2, argv + 2);
      if (ret) nssm_exit(ret);
      nssm_exit(control_service(NSSM_SERVICE_CONTROL_START, argc - 2, argv + 2));
    }
    if (str_equiv(argv[1], _T("pause"))) nssm_exit(control_service(SERVICE_CONTROL_PAUSE, argc - 2, argv + 2));
    if (str_equiv(argv[1], _T("continue"))) nssm_exit(control_service(SERVICE_CONTROL_CONTINUE, argc - 2, argv + 2));
    if (str_equiv(argv[1], _T("status"))) nssm_exit(control_service(SERVICE_CONTROL_INTERROGATE, argc - 2, argv + 2));
    if (str_equiv(argv[1], _T("statuscode"))) nssm_exit(control_service(SERVICE_CONTROL_INTERROGATE, argc - 2, argv + 2, true));
    if (str_equiv(argv[1], _T("rotate"))) nssm_exit(control_service(NSSM_SERVICE_CONTROL_ROTATE, argc - 2, argv + 2));
    if (str_equiv(argv[1], _T("install"))) {
      if (! is_admin) nssm_exit(elevate(argc, argv, NSSM_MESSAGE_NOT_ADMINISTRATOR_CANNOT_INSTALL));
      create_messages();
      nssm_exit(pre_install_service(argc - 2, argv + 2));
    }
    if (str_equiv(argv[1], _T("edit")) || str_equiv(argv[1], _T("get")) || str_equiv(argv[1], _T("set")) || str_equiv(argv[1], _T("reset")) || str_equiv(argv[1], _T("unset")) || str_equiv(argv[1], _T("dump"))) {
      int ret = pre_edit_service(argc - 1, argv + 1);
      if (ret == 3 && ! is_admin && argc == 3) nssm_exit(elevate(argc, argv, NSSM_MESSAGE_NOT_ADMINISTRATOR_CANNOT_EDIT));
      /* There might be a password here. */
      for (int i = 0; i < argc; i++) SecureZeroMemory(argv[i], _tcslen(argv[i]) * sizeof(TCHAR));
      nssm_exit(ret);
    }
    if (str_equiv(argv[1], _T("list"))) nssm_exit(list_nssm_services(argc - 2, argv + 2));
    if (str_equiv(argv[1], _T("processes"))) nssm_exit(service_process_tree(argc - 2, argv + 2));
    if (str_equiv(argv[1], _T("remove"))) {
      if (! is_admin) nssm_exit(elevate(argc, argv, NSSM_MESSAGE_NOT_ADMINISTRATOR_CANNOT_REMOVE));
      nssm_exit(pre_remove_service(argc - 2, argv + 2));
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
      if (error == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) nssm_exit(usage(1));
      log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_DISPATCHER_FAILED, error_string(error), 0);
      nssm_exit(100);
    }
  }
  else nssm_exit(usage(1));

  /* And nothing more to do */
  nssm_exit(0);
}
