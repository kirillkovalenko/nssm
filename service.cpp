#include "nssm.h"

bool is_admin;
SERVICE_STATUS service_status;
SERVICE_STATUS_HANDLE service_handle;
HANDLE process_handle;
HANDLE wait_handle;
unsigned long pid;
static char service_name[SERVICE_NAME_LENGTH];
char exe[EXE_LENGTH];
char flags[CMD_LENGTH];
char dir[MAX_PATH];
bool stopping;
bool allow_restart;
unsigned long throttle_delay;
unsigned long stop_method;
unsigned long kill_console_delay;
unsigned long kill_window_delay;
unsigned long kill_threads_delay;
CRITICAL_SECTION throttle_section;
CONDITION_VARIABLE throttle_condition;
HANDLE throttle_timer;
LARGE_INTEGER throttle_duetime;
bool use_critical_section;
FILETIME creation_time;

extern imports_t imports;

static enum { NSSM_EXIT_RESTART, NSSM_EXIT_IGNORE, NSSM_EXIT_REALLY, NSSM_EXIT_UNCLEAN } exit_actions;
static const char *exit_action_strings[] = { "Restart", "Ignore", "Exit", "Suicide", 0 };

static unsigned long throttle;

static inline int throttle_milliseconds() {
  /* pow() operates on doubles. */
  int ret = 1; for (unsigned long i = 1; i < throttle; i++) ret *= 2;
  return ret * 1000;
}

/* Connect to the service manager */
SC_HANDLE open_service_manager() {
  SC_HANDLE ret = OpenSCManager(0, SERVICES_ACTIVE_DATABASE, SC_MANAGER_ALL_ACCESS);
  if (! ret) {
    if (is_admin) log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OPENSCMANAGER_FAILED, 0);
    return 0;
  }

  return ret;
}

/* About to install the service */
int pre_install_service(int argc, char **argv) {
  /* Show the dialogue box if we didn't give the service name and path */
  if (argc < 2) return nssm_gui(IDD_INSTALL, argv[0]);

  /* Arguments are optional */
  char *flags;
  size_t flagslen = 0;
  size_t s = 0;
  int i;
  for (i = 2; i < argc; i++) flagslen += strlen(argv[i]) + 1;
  if (! flagslen) flagslen = 1;

  flags = (char *) HeapAlloc(GetProcessHeap(), 0, flagslen);
  if (! flags) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, "flags", "pre_install_service()", 0);
    return 2;
  }
  ZeroMemory(flags, flagslen);

  /*
    This probably isn't UTF8-safe and should use std::string or something
    but it's been broken for the best part of a decade and due for a rewrite
    anyway so it'll do as a quick-'n'-dirty fix.  Note that we don't free
    the flags buffer but as the program exits that isn't a big problem.
  */
  for (i = 2; i < argc; i++) {
    size_t len = strlen(argv[i]);
    memmove(flags + s, argv[i], len);
    s += len;
    if (i < argc - 1) flags[s++] = ' ';
  }

  return install_service(argv[0], argv[1], flags);
}

/* About to remove the service */
int pre_remove_service(int argc, char **argv) {
  /* Show dialogue box if we didn't pass service name and "confirm" */
  if (argc < 2) return nssm_gui(IDD_REMOVE, argv[0]);
  if (str_equiv(argv[1], "confirm")) return remove_service(argv[0]);
  print_message(stderr, NSSM_MESSAGE_PRE_REMOVE_SERVICE);
  return 100;
}

/* Install the service */
int install_service(char *name, char *exe, char *flags) {
  /* Open service manager */
  SC_HANDLE services = open_service_manager();
  if (! services) {
    print_message(stderr, NSSM_MESSAGE_OPEN_SERVICE_MANAGER_FAILED);
    return 2;
  }

  /* Get path of this program */
  char path[MAX_PATH];
  GetModuleFileName(0, path, MAX_PATH);

  /* Construct command */
  char command[CMD_LENGTH];
  size_t pathlen = strlen(path);
  if (pathlen + 1 >= VALUE_LENGTH) {
    print_message(stderr, NSSM_MESSAGE_PATH_TOO_LONG, NSSM);
    return 3;
  }
  if (_snprintf_s(command, sizeof(command), _TRUNCATE, "\"%s\"", path) < 0) {
    print_message(stderr, NSSM_MESSAGE_OUT_OF_MEMORY_FOR_IMAGEPATH);
    return 4;
  }

  /* Work out directory name */
  size_t len = strlen(exe);
  size_t i;
  for (i = len; i && exe[i] != '\\' && exe[i] != '/'; i--);
  char dir[MAX_PATH];
  memmove(dir, exe, i);
  dir[i] = '\0';

  /* Create the service */
  SC_HANDLE service = CreateService(services, name, name, SC_MANAGER_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START, SERVICE_ERROR_NORMAL, command, 0, 0, 0, 0, 0);
  if (! service) {
    print_message(stderr, NSSM_MESSAGE_CREATESERVICE_FAILED);
    CloseServiceHandle(services);
    return 5;
  }

  /* Now we need to put the parameters into the registry */
  if (create_parameters(name, exe, flags, dir)) {
    print_message(stderr, NSSM_MESSAGE_CREATE_PARAMETERS_FAILED);
    DeleteService(service);
    CloseServiceHandle(services);
    return 6;
  }

  set_service_recovery(service, name);

  /* Cleanup */
  CloseServiceHandle(service);
  CloseServiceHandle(services);

  print_message(stdout, NSSM_MESSAGE_SERVICE_INSTALLED, name);
  return 0;
}

/* Remove the service */
int remove_service(char *name) {
  /* Open service manager */
  SC_HANDLE services = open_service_manager();
  if (! services) {
    print_message(stderr, NSSM_MESSAGE_OPEN_SERVICE_MANAGER_FAILED);
    return 2;
  }

  /* Try to open the service */
  SC_HANDLE service = OpenService(services, name, SC_MANAGER_ALL_ACCESS);
  if (! service) {
    print_message(stderr, NSSM_MESSAGE_OPENSERVICE_FAILED);
    CloseServiceHandle(services);
    return 3;
  }

  /* Try to delete the service */
  if (! DeleteService(service)) {
    print_message(stderr, NSSM_MESSAGE_DELETESERVICE_FAILED);
    CloseServiceHandle(service);
    CloseServiceHandle(services);
    return 4;
  }

  /* Cleanup */
  CloseServiceHandle(service);
  CloseServiceHandle(services);

  print_message(stdout, NSSM_MESSAGE_SERVICE_REMOVED, name);
  return 0;
}

/* Service initialisation */
void WINAPI service_main(unsigned long argc, char **argv) {
  if (_snprintf_s(service_name, sizeof(service_name), _TRUNCATE, "%s", argv[0]) < 0) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, "service_name", "service_main()", 0);
    return;
  }

  /* We can use a condition variable in a critical section on Vista or later. */
  if (imports.SleepConditionVariableCS && imports.WakeConditionVariable) use_critical_section = true;
  else use_critical_section = false;

  /* Initialise status */
  ZeroMemory(&service_status, sizeof(service_status));
  service_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS | SERVICE_INTERACTIVE_PROCESS;
  service_status.dwControlsAccepted = SERVICE_ACCEPT_SHUTDOWN | SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_PAUSE_CONTINUE;
  service_status.dwWin32ExitCode = NO_ERROR;
  service_status.dwServiceSpecificExitCode = 0;
  service_status.dwCheckPoint = 0;
  service_status.dwWaitHint = NSSM_WAITHINT_MARGIN;

  /* Signal we AREN'T running the server */
  process_handle = 0;
  pid = 0;

  /* Register control handler */
  service_handle = RegisterServiceCtrlHandlerEx(NSSM, service_control_handler, 0);
  if (! service_handle) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_REGISTERSERVICECTRLHANDER_FAILED, error_string(GetLastError()), 0);
    return;
  }

  log_service_control(service_name, 0, true);

  service_status.dwCurrentState = SERVICE_START_PENDING;
  service_status.dwWaitHint = throttle_delay + NSSM_WAITHINT_MARGIN;
  SetServiceStatus(service_handle, &service_status);

  if (is_admin) {
    /* Try to create the exit action parameters; we don't care if it fails */
    create_exit_action(argv[0], exit_action_strings[0]);

    set_service_recovery(0, service_name);
  }

  /* Used for signalling a resume if the service pauses when throttled. */
  if (use_critical_section) InitializeCriticalSection(&throttle_section);
  else {
    throttle_timer = CreateWaitableTimer(0, 1, 0);
    if (! throttle_timer) {
      log_event(EVENTLOG_WARNING_TYPE, NSSM_EVENT_CREATEWAITABLETIMER_FAILED, service_name, error_string(GetLastError()), 0);
    }
  }

  monitor_service();
}

/* Make sure service recovery actions are taken where necessary */
void set_service_recovery(SC_HANDLE service, char *service_name) {
  SC_HANDLE services = 0;

  if (! service) {
    services = open_service_manager();
    if (! services) return;

    service = OpenService(services, service_name, SC_MANAGER_ALL_ACCESS);
    if (! service) return;
  }

  SERVICE_FAILURE_ACTIONS_FLAG flag;
  ZeroMemory(&flag, sizeof(flag));
  flag.fFailureActionsOnNonCrashFailures = true;

  /* This functionality was added in Vista so the call may fail */
  if (! ChangeServiceConfig2(service, SERVICE_CONFIG_FAILURE_ACTIONS_FLAG, &flag)) {
    unsigned long error = GetLastError();
    /* Pre-Vista we expect to fail with ERROR_INVALID_LEVEL */
    if (error != ERROR_INVALID_LEVEL) {
      log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_CHANGESERVICECONFIG2_FAILED, service_name, error_string(error), 0);
    }
  }

  if (services) {
    CloseServiceHandle(service);
    CloseServiceHandle(services);
  }
}

int monitor_service() {
  /* Set service status to started */
  int ret = start_service();
  if (ret) {
    char code[16];
    _snprintf_s(code, sizeof(code), _TRUNCATE, "%d", ret);
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_START_SERVICE_FAILED, exe, service_name, ret, 0);
    return ret;
  }
  log_event(EVENTLOG_INFORMATION_TYPE, NSSM_EVENT_STARTED_SERVICE, exe, flags, service_name, dir, 0);

  /* Monitor service */
  if (! RegisterWaitForSingleObject(&wait_handle, process_handle, end_service, (void *) pid, INFINITE, WT_EXECUTEONLYONCE | WT_EXECUTELONGFUNCTION)) {
    log_event(EVENTLOG_WARNING_TYPE, NSSM_EVENT_REGISTERWAITFORSINGLEOBJECT_FAILED, service_name, exe, error_string(GetLastError()), 0);
  }

  return 0;
}

char *service_control_text(unsigned long control) {
  switch (control) {
    /* HACK: there is no SERVICE_CONTROL_START constant */
    case 0: return "START";
    case SERVICE_CONTROL_STOP: return "STOP";
    case SERVICE_CONTROL_SHUTDOWN: return "SHUTDOWN";
    case SERVICE_CONTROL_PAUSE: return "PAUSE";
    case SERVICE_CONTROL_CONTINUE: return "CONTINUE";
    case SERVICE_CONTROL_INTERROGATE: return "INTERROGATE";
    default: return 0;
  }
}

void log_service_control(char *service_name, unsigned long control, bool handled) {
  char *text = service_control_text(control);
  unsigned long event;

  if (! text) {
    /* "0x" + 8 x hex + NULL */
    text = (char *) HeapAlloc(GetProcessHeap(), 0, 11);
    if (! text) {
      log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, "control code", "log_service_control()", 0);
      return;
    }
    if (_snprintf_s(text, 11, _TRUNCATE, "0x%08x", control) < 0) {
      log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, "control code", "log_service_control()", 0);
      HeapFree(GetProcessHeap(), 0, text);
      return;
    }

    event = NSSM_EVENT_SERVICE_CONTROL_UNKNOWN;
  }
  else if (handled) event = NSSM_EVENT_SERVICE_CONTROL_HANDLED;
  else event = NSSM_EVENT_SERVICE_CONTROL_NOT_HANDLED;

  log_event(EVENTLOG_INFORMATION_TYPE, event, service_name, text, 0);

  if (event == NSSM_EVENT_SERVICE_CONTROL_UNKNOWN) {
    HeapFree(GetProcessHeap(), 0, text);
  }
}

/* Service control handler */
unsigned long WINAPI service_control_handler(unsigned long control, unsigned long event, void *data, void *context) {
  switch (control) {
    case SERVICE_CONTROL_INTERROGATE:
      /* We always keep the service status up-to-date so this is a no-op. */
      return NO_ERROR;

    case SERVICE_CONTROL_SHUTDOWN:
    case SERVICE_CONTROL_STOP:
      log_service_control(service_name, control, true);
      stop_service(0, true, true);
      return NO_ERROR;

    case SERVICE_CONTROL_CONTINUE:
      log_service_control(service_name, control, true);
      throttle = 0;
      if (use_critical_section) imports.WakeConditionVariable(&throttle_condition);
      else {
        if (! throttle_timer) return ERROR_CALL_NOT_IMPLEMENTED;
        ZeroMemory(&throttle_duetime, sizeof(throttle_duetime));
        SetWaitableTimer(throttle_timer, &throttle_duetime, 0, 0, 0, 0);
      }
      service_status.dwCurrentState = SERVICE_CONTINUE_PENDING;
      service_status.dwWaitHint = throttle_milliseconds() + NSSM_WAITHINT_MARGIN;
      log_event(EVENTLOG_INFORMATION_TYPE, NSSM_EVENT_RESET_THROTTLE, service_name, 0);
      SetServiceStatus(service_handle, &service_status);
      return NO_ERROR;

    case SERVICE_CONTROL_PAUSE:
      /*
        We don't accept pause messages but it isn't possible to register
        only for continue messages so we have to handle this case.
      */
      log_service_control(service_name, control, false);
      return ERROR_CALL_NOT_IMPLEMENTED;
  }

  /* Unknown control */
  log_service_control(service_name, control, false);
  return ERROR_CALL_NOT_IMPLEMENTED;
}

/* Start the service */
int start_service() {
  stopping = false;
  allow_restart = true;

  if (process_handle) return 0;

  /* Allocate a STARTUPINFO structure for a new process */
  STARTUPINFO si;
  ZeroMemory(&si, sizeof(si));
  si.cb = sizeof(si);

  /* Allocate a PROCESSINFO structure for the process */
  PROCESS_INFORMATION pi;
  ZeroMemory(&pi, sizeof(pi));

  /* Get startup parameters */
  char *env = 0;
  int ret = get_parameters(service_name, exe, sizeof(exe), flags, sizeof(flags), dir, sizeof(dir), &env, &throttle_delay, &stop_method, &kill_console_delay, &kill_window_delay, &kill_threads_delay, &si);
  if (ret) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_GET_PARAMETERS_FAILED, service_name, 0);
    return stop_service(2, true, true);
  }

  /* Launch executable with arguments */
  char cmd[CMD_LENGTH];
  if (_snprintf_s(cmd, sizeof(cmd), _TRUNCATE, "\"%s\" %s", exe, flags) < 0) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, "command line", "start_service", 0);
    close_output_handles(&si);
    return stop_service(2, true, true);
  }

  throttle_restart();

  bool inherit_handles = false;
  if (si.dwFlags & STARTF_USESTDHANDLES) inherit_handles = true;
  if (! CreateProcess(0, cmd, 0, 0, inherit_handles, 0, env, dir, &si, &pi)) {
    unsigned long error = GetLastError();
    if (error == ERROR_INVALID_PARAMETER && env) log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_CREATEPROCESS_FAILED_INVALID_ENVIRONMENT, service_name, exe, NSSM_REG_ENV, 0);
    else log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_CREATEPROCESS_FAILED, service_name, exe, error_string(error), 0);
    close_output_handles(&si);
    return stop_service(3, true, true);
  }
  process_handle = pi.hProcess;
  pid = pi.dwProcessId;

  if (get_process_creation_time(process_handle, &creation_time)) ZeroMemory(&creation_time, sizeof(creation_time));

  close_output_handles(&si);

  /* Wait for a clean startup. */
  if (WaitForSingleObject(process_handle, throttle_delay) == WAIT_TIMEOUT) throttle = 0;

  /* Signal successful start */
  service_status.dwCurrentState = SERVICE_RUNNING;
  SetServiceStatus(service_handle, &service_status);

  return 0;
}

/* Stop the service */
int stop_service(unsigned long exitcode, bool graceful, bool default_action) {
  allow_restart = false;
  if (wait_handle) UnregisterWait(wait_handle);

  if (default_action && ! exitcode && ! graceful) {
    log_event(EVENTLOG_INFORMATION_TYPE, NSSM_EVENT_GRACEFUL_SUICIDE, service_name, exe, exit_action_strings[NSSM_EXIT_UNCLEAN], exit_action_strings[NSSM_EXIT_UNCLEAN], exit_action_strings[NSSM_EXIT_UNCLEAN], exit_action_strings[NSSM_EXIT_REALLY] ,0);
    graceful = true;
  }

  /* Signal we are stopping */
  if (graceful) {
    service_status.dwCurrentState = SERVICE_STOP_PENDING;
    service_status.dwWaitHint = NSSM_WAITHINT_MARGIN;
    if (stop_method & NSSM_STOP_METHOD_CONSOLE && imports.AttachConsole) service_status.dwWaitHint += kill_console_delay;
    if (stop_method & NSSM_STOP_METHOD_WINDOW) service_status.dwWaitHint += kill_window_delay;
    if (stop_method & NSSM_STOP_METHOD_THREADS) service_status.dwWaitHint += kill_threads_delay;
    SetServiceStatus(service_handle, &service_status);
  }

  /* Nothing to do if service isn't running */
  if (pid) {
    /* Shut down service */
    log_event(EVENTLOG_INFORMATION_TYPE, NSSM_EVENT_TERMINATEPROCESS, service_name, exe, 0);
    kill_process(service_name, service_handle, &service_status, stop_method, process_handle, pid, 0);
  }
  else log_event(EVENTLOG_INFORMATION_TYPE, NSSM_EVENT_PROCESS_ALREADY_STOPPED, service_name, exe, 0);

  end_service((void *) pid, true);

  /* Signal we stopped */
  if (graceful) {
    service_status.dwCurrentState = SERVICE_STOPPED;
    if (exitcode) {
      service_status.dwWin32ExitCode = ERROR_SERVICE_SPECIFIC_ERROR;
      service_status.dwServiceSpecificExitCode = exitcode;
    }
    else {
      service_status.dwWin32ExitCode = NO_ERROR;
      service_status.dwServiceSpecificExitCode = 0;
    }
    SetServiceStatus(service_handle, &service_status);
  }

  return exitcode;
}

/* Callback function triggered when the server exits */
void CALLBACK end_service(void *arg, unsigned char why) {
  if (stopping) return;

  stopping = true;

  pid = (unsigned long) arg;

  /* Check exit code */
  unsigned long exitcode = 0;
  char code[16];
  FILETIME exit_time;
  GetExitCodeProcess(process_handle, &exitcode);
  if (exitcode == STILL_ACTIVE || get_process_exit_time(process_handle, &exit_time)) GetSystemTimeAsFileTime(&exit_time);
  CloseHandle(process_handle);

  /*
    Log that the service ended BEFORE logging about killing the process
    tree.  See below for the possible values of the why argument.
  */
  if (! why) {
    _snprintf_s(code, sizeof(code), _TRUNCATE, "%lu", exitcode);
    log_event(EVENTLOG_INFORMATION_TYPE, NSSM_EVENT_ENDED_SERVICE, exe, service_name, code, 0);
  }

  /* Clean up. */
  if (exitcode == STILL_ACTIVE) exitcode = 0;
  kill_process_tree(service_name, service_handle, &service_status, stop_method, pid, exitcode, pid, &creation_time, &exit_time);

  /*
    The why argument is true if our wait timed out or false otherwise.
    Our wait is infinite so why will never be true when called by the system.
    If it is indeed true, assume we were called from stop_service() because
    this is a controlled shutdown, and don't take any restart action.
  */
  if (why) return;
  if (! allow_restart) return;

  /* What action should we take? */
  int action = NSSM_EXIT_RESTART;
  unsigned char action_string[ACTION_LEN];
  bool default_action;
  if (! get_exit_action(service_name, &exitcode, action_string, &default_action)) {
    for (int i = 0; exit_action_strings[i]; i++) {
      if (! _strnicmp((const char *) action_string, exit_action_strings[i], ACTION_LEN)) {
        action = i;
        break;
      }
    }
  }

  process_handle = 0;
  pid = 0;
  switch (action) {
    /* Try to restart the service or return failure code to service manager */
    case NSSM_EXIT_RESTART:
      log_event(EVENTLOG_INFORMATION_TYPE, NSSM_EVENT_EXIT_RESTART, service_name, code, exit_action_strings[action], exe, 0);
      while (monitor_service()) {
        log_event(EVENTLOG_WARNING_TYPE, NSSM_EVENT_RESTART_SERVICE_FAILED, exe, service_name, 0);
        Sleep(30000);
      }
    break;

    /* Do nothing, just like srvany would */
    case NSSM_EXIT_IGNORE:
      log_event(EVENTLOG_INFORMATION_TYPE, NSSM_EVENT_EXIT_IGNORE, service_name, code, exit_action_strings[action], exe, 0);
      Sleep(INFINITE);
    break;

    /* Tell the service manager we are finished */
    case NSSM_EXIT_REALLY:
      log_event(EVENTLOG_INFORMATION_TYPE, NSSM_EVENT_EXIT_REALLY, service_name, code, exit_action_strings[action], 0);
      stop_service(exitcode, true, default_action);
    break;

    /* Fake a crash so pre-Vista service managers will run recovery actions. */
    case NSSM_EXIT_UNCLEAN:
      log_event(EVENTLOG_INFORMATION_TYPE, NSSM_EVENT_EXIT_UNCLEAN, service_name, code, exit_action_strings[action], 0);
      stop_service(exitcode, false, default_action);
      free_imports();
      exit(exitcode);
    break;
  }
}

void throttle_restart() {
  /* This can't be a restart if the service is already running. */
  if (! throttle++) return;

  int ms = throttle_milliseconds();

  if (throttle > 7) throttle = 8;

  char threshold[8], milliseconds[8];
  _snprintf_s(threshold, sizeof(threshold), _TRUNCATE, "%lu", throttle_delay);
  _snprintf_s(milliseconds, sizeof(milliseconds), _TRUNCATE, "%lu", ms);
  log_event(EVENTLOG_WARNING_TYPE, NSSM_EVENT_THROTTLED, service_name, threshold, milliseconds, 0);

  if (use_critical_section) EnterCriticalSection(&throttle_section);
  else if (throttle_timer) {
    ZeroMemory(&throttle_duetime, sizeof(throttle_duetime));
    throttle_duetime.QuadPart = 0 - (ms * 10000LL);
    SetWaitableTimer(throttle_timer, &throttle_duetime, 0, 0, 0, 0);
  }

  service_status.dwCurrentState = SERVICE_PAUSED;
  SetServiceStatus(service_handle, &service_status);

  if (use_critical_section) {
    imports.SleepConditionVariableCS(&throttle_condition, &throttle_section, ms);
    LeaveCriticalSection(&throttle_section);
  }
  else {
    if (throttle_timer) WaitForSingleObject(throttle_timer, INFINITE);
    else Sleep(ms);
  }
}

/*
  When responding to a stop (or any other) request we need to set dwWaitHint to
  the number of milliseconds we expect the operation to take, and optionally
  increase dwCheckPoint.  If dwWaitHint milliseconds elapses without the
  operation completing or dwCheckPoint increasing, the system will consider the
  service to be hung.

  However the system will consider the service to be hung after 30000
  milliseconds regardless of the value of dwWaitHint if dwCheckPoint has not
  changed.  Therefore if we want to wait longer than that we must periodically
  increase dwCheckPoint.

  Furthermore, it will consider the service to be hung after 60000 milliseconds
  regardless of the value of dwCheckPoint unless dwWaitHint is increased every
  time dwCheckPoint is also increased.

  Our strategy then is to retrieve the initial dwWaitHint and wait for
  NSSM_SHUTDOWN_CHECKPOINT milliseconds.  If the process is still running and
  we haven't finished waiting we increment dwCheckPoint and add whichever is
  smaller of NSSM_SHUTDOWN_CHECKPOINT or the remaining timeout to dwWaitHint.

  Only doing both these things will prevent the system from killing the service.

  Returns: 1 if the wait timed out.
           0 if the wait completed.
          -1 on error.
*/
int await_shutdown(char *function_name, char *service_name, SERVICE_STATUS_HANDLE service_handle, SERVICE_STATUS *service_status, HANDLE process_handle, unsigned long timeout) {
  unsigned long interval;
  unsigned long waithint;
  unsigned long ret;
  unsigned long waited;
  char interval_milliseconds[16];
  char timeout_milliseconds[16];
  char waited_milliseconds[16];
  char *function = function_name;

  /* Add brackets to function name. */
  size_t funclen = strlen(function_name) + 3;
  char *func = (char *) HeapAlloc(GetProcessHeap(), 0, funclen);
  if (func) {
    if (_snprintf_s(func, funclen, _TRUNCATE, "%s()", function_name) > -1) function = func;
  }

  _snprintf_s(timeout_milliseconds, sizeof(timeout_milliseconds), _TRUNCATE, "%lu", timeout);

  waithint = service_status->dwWaitHint;
  waited = 0;
  while (waited < timeout) {
    interval = timeout - waited;
    if (interval > NSSM_SHUTDOWN_CHECKPOINT) interval = NSSM_SHUTDOWN_CHECKPOINT;

    service_status->dwCurrentState = SERVICE_STOP_PENDING;
    service_status->dwWaitHint += interval;
    service_status->dwCheckPoint++;
    SetServiceStatus(service_handle, service_status);

    if (waited) {
      _snprintf_s(waited_milliseconds, sizeof(waited_milliseconds), _TRUNCATE, "%lu", waited);
      _snprintf_s(interval_milliseconds, sizeof(interval_milliseconds), _TRUNCATE, "%lu", interval);
      log_event(EVENTLOG_INFORMATION_TYPE, NSSM_EVENT_AWAITING_SHUTDOWN, function, service_name, waited_milliseconds, interval_milliseconds, timeout_milliseconds, 0);
    }

    switch (WaitForSingleObject(process_handle, interval)) {
      case WAIT_OBJECT_0:
        ret = 0;
        goto awaited;

      case WAIT_TIMEOUT:
        ret = 1;
      break;

      default:
        ret = -1;
        goto awaited;
    }

    waited += interval;
  }

awaited:
  if (func) HeapFree(GetProcessHeap(), 0, func);

  return ret;
}
