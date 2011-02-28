#include "nssm.h"

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
unsigned long throttle_delay;
HANDLE throttle_timer;
LARGE_INTEGER throttle_duetime;

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
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OPENSCMANAGER_FAILED, 0);
    return 0;
  }

  return ret;
}

/* About to install the service */
int pre_install_service(int argc, char **argv) {
  /* Show the dialogue box if we didn't give the */
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
  fprintf(stderr, "To remove a service without confirmation: nssm remove <servicename> confirm\n");
  return 100;
}

/* Install the service */
int install_service(char *name, char *exe, char *flags) {
  /* Open service manager */
  SC_HANDLE services = open_service_manager();
  if (! services) {
    fprintf(stderr, "Error opening service manager!\n");
    return 2;
  }
  
  /* Get path of this program */
  char path[MAX_PATH];
  GetModuleFileName(0, path, MAX_PATH);

  /* Construct command */
  char command[CMD_LENGTH];
  size_t runlen = strlen(NSSM_RUN);
  size_t pathlen = strlen(path);
  if (pathlen + runlen + 2 >= VALUE_LENGTH) {
    fprintf(stderr, "The full path to " NSSM " is too long!\n");
    return 3;
  }
  if (_snprintf(command, sizeof(command), "\"%s\" %s", path, NSSM_RUN) < 0) {
    fprintf(stderr, "Out of memory for ImagePath!\n");
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
    fprintf(stderr, "Error creating service!\n");
    CloseServiceHandle(services);
    return 5;
  }

  /* Now we need to put the parameters into the registry */
  if (create_parameters(name, exe, flags, dir)) {
    fprintf(stderr, "Error setting startup parameters for the service!\n");
    DeleteService(service);
    CloseServiceHandle(services);
    return 6;
  }

  /* Cleanup */
  CloseServiceHandle(service);
  CloseServiceHandle(services);

  printf("Service \"%s\" installed successfully!\n", name);
  return 0;
}

/* Remove the service */
int remove_service(char *name) {
  /* Open service manager */
  SC_HANDLE services = open_service_manager();
  if (! services) {
    fprintf(stderr, "Error opening service manager!\n");
    return 2;
  }
  
  /* Try to open the service */
  SC_HANDLE service = OpenService(services, name, SC_MANAGER_ALL_ACCESS);
  if (! service) {
    fprintf(stderr, "Can't open service!");
    CloseServiceHandle(services);
    return 3;
  }

  /* Try to delete the service */
  if (! DeleteService(service)) {
    fprintf(stderr, "Error deleting service!\n");
    CloseServiceHandle(service);
    CloseServiceHandle(services);
    return 4;
  }

  /* Cleanup */
  CloseServiceHandle(service);
  CloseServiceHandle(services);

  printf("Service \"%s\" removed successfully!\n", name);
  return 0;
}

/* Service initialisation */
void WINAPI service_main(unsigned long argc, char **argv) {
  if (_snprintf(service_name, sizeof(service_name), "%s", argv[0]) < 0) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, "service_name", "service_main()", 0);
    return;
  }

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

  service_status.dwCurrentState = SERVICE_START_PENDING;
  service_status.dwWaitHint = throttle_delay + NSSM_WAITHINT_MARGIN;
  SetServiceStatus(service_handle, &service_status);

  /* Try to create the exit action parameters; we don't care if it fails */
  create_exit_action(argv[0], exit_action_strings[0]);

  set_service_recovery(service_name);

  /* Used for signalling a resume if the service pauses when throttled. */
  throttle_timer = CreateWaitableTimer(0, 1, 0);
  if (! throttle_timer) {
    log_event(EVENTLOG_WARNING_TYPE, NSSM_EVENT_CREATEWAITABLETIMER_FAILED, service_name, error_string(GetLastError()), 0);
  }

  monitor_service();
}

/* Make sure service recovery actions are taken where necessary */
void set_service_recovery(char *service_name) {
  SC_HANDLE services = open_service_manager();
  if (! services) return;

  SC_HANDLE service = OpenService(services, service_name, SC_MANAGER_ALL_ACCESS);
  if (! service) return;
  return;

  SERVICE_FAILURE_ACTIONS_FLAG flag;
  ZeroMemory(&flag, sizeof(flag));
  flag.fFailureActionsOnNonCrashFailures = true;

  /* This functionality was added in Vista so the call may fail */
  ChangeServiceConfig2(service, SERVICE_CONFIG_FAILURE_ACTIONS_FLAG, &flag);
}

int monitor_service() {
  /* Set service status to started */
  int ret = start_service();
  if (ret) {
    char code[16];
    _snprintf(code, sizeof(code), "%d", ret);
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_START_SERVICE_FAILED, exe, service_name, ret, 0);
    return ret;
  }
  log_event(EVENTLOG_INFORMATION_TYPE, NSSM_EVENT_STARTED_SERVICE, exe, flags, service_name, dir, 0);

  /* Monitor service service */
  if (! RegisterWaitForSingleObject(&wait_handle, process_handle, end_service, (void *) pid, INFINITE, WT_EXECUTEONLYONCE | WT_EXECUTELONGFUNCTION)) {
    log_event(EVENTLOG_WARNING_TYPE, NSSM_EVENT_REGISTERWAITFORSINGLEOBJECT_FAILED, service_name, exe, error_string(GetLastError()), 0);
  }

  return 0;
}

/* Service control handler */
unsigned long WINAPI service_control_handler(unsigned long control, unsigned long event, void *data, void *context) {
  switch (control) {
    case SERVICE_CONTROL_SHUTDOWN:
    case SERVICE_CONTROL_STOP:
      stop_service(0, true, true);
      return NO_ERROR;

    case SERVICE_CONTROL_CONTINUE:
      if (! throttle_timer) return ERROR_CALL_NOT_IMPLEMENTED;
      throttle = 0;
      ZeroMemory(&throttle_duetime, sizeof(throttle_duetime));
      SetWaitableTimer(throttle_timer, &throttle_duetime, 0, 0, 0, 0);
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
      return ERROR_CALL_NOT_IMPLEMENTED;
  }

  /* Unknown control */
  return ERROR_CALL_NOT_IMPLEMENTED;
}

/* Start the service */
int start_service() {
  stopping = false;

  if (process_handle) return 0;

  /* Allocate a STARTUPINFO structure for a new process */
  STARTUPINFO si;
  ZeroMemory(&si, sizeof(si));
  si.cb = sizeof(si);

  /* Allocate a PROCESSINFO structure for the process */
  PROCESS_INFORMATION pi;
  ZeroMemory(&pi, sizeof(pi));

  /* Get startup parameters */
  int ret = get_parameters(service_name, exe, sizeof(exe), flags, sizeof(flags), dir, sizeof(dir), &throttle_delay);
  if (ret) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_GET_PARAMETERS_FAILED, service_name, 0);
    return stop_service(2, true, true);
  }

  /* Launch executable with arguments */
  char cmd[CMD_LENGTH];
  if (_snprintf(cmd, sizeof(cmd), "%s %s", exe, flags) < 0) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, "command line", "start_service", 0);
    return stop_service(2, true, true);
  }

  throttle_restart();

  if (! CreateProcess(0, cmd, 0, 0, false, 0, 0, dir, &si, &pi)) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_CREATEPROCESS_FAILED, service_name, exe, error_string(GetLastError()), 0);
    return stop_service(3, true, true);
  }
  process_handle = pi.hProcess;
  pid = pi.dwProcessId;

  /* Signal successful start */
  service_status.dwCurrentState = SERVICE_RUNNING;
  SetServiceStatus(service_handle, &service_status);

  /* Wait for a clean startup. */
  if (WaitForSingleObject(process_handle, throttle_delay) == WAIT_TIMEOUT) throttle = 0;

  return 0;
}

/* Stop the service */
int stop_service(unsigned long exitcode, bool graceful, bool default_action) {
  if (default_action && ! exitcode && ! graceful) {
    log_event(EVENTLOG_INFORMATION_TYPE, NSSM_EVENT_GRACEFUL_SUICIDE, service_name, exe, exit_action_strings[NSSM_EXIT_UNCLEAN], exit_action_strings[NSSM_EXIT_UNCLEAN], exit_action_strings[NSSM_EXIT_UNCLEAN], exit_action_strings[NSSM_EXIT_REALLY] ,0);
    graceful = true;
  }

  /* Signal we are stopping */
  if (graceful) {
    service_status.dwCurrentState = SERVICE_STOP_PENDING;
    service_status.dwWaitHint = NSSM_KILL_WINDOW_GRACE_PERIOD + NSSM_KILL_THREADS_GRACE_PERIOD + NSSM_WAITHINT_MARGIN;
    SetServiceStatus(service_handle, &service_status);
  }

  /* Nothing to do if server isn't running */
  if (pid) {
    /* Shut down server */
    log_event(EVENTLOG_INFORMATION_TYPE, NSSM_EVENT_TERMINATEPROCESS, service_name, exe, 0);
    kill_process(service_name, process_handle, pid, 0);
    process_handle = 0;
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
  GetExitCodeProcess(process_handle, &exitcode);

  /* Clean up. */
  kill_process_tree(service_name, pid, exitcode, pid);

  /*
    The why argument is true if our wait timed out or false otherwise.
    Our wait is infinite so why will never be true when called by the system.
    If it is indeed true, assume we were called from stop_service() because
    this is a controlled shutdown, and don't take any restart action.
  */
  if (why) return;

  char code[16];
  _snprintf(code, sizeof(code), "%d", exitcode);
  log_event(EVENTLOG_INFORMATION_TYPE, NSSM_EVENT_ENDED_SERVICE, exe, service_name, code, 0);

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
      exit(stop_service(exitcode, false, default_action));
    break;
  }
}

void throttle_restart() {
  /* This can't be a restart if the service is already running. */
  if (! throttle++) return;

  int ms = throttle_milliseconds();

  if (throttle > 7) throttle = 8;

  char threshold[8], milliseconds[8];
  _snprintf(threshold, sizeof(threshold), "%d", throttle_delay);
  _snprintf(milliseconds, sizeof(milliseconds), "%d", ms);
  log_event(EVENTLOG_WARNING_TYPE, NSSM_EVENT_THROTTLED, service_name, threshold, milliseconds, 0);

  if (throttle_timer) {
    ZeroMemory(&throttle_duetime, sizeof(throttle_duetime));
    throttle_duetime.QuadPart = 0 - (ms * 10000LL);
    SetWaitableTimer(throttle_timer, &throttle_duetime, 0, 0, 0, 0);
  }

  service_status.dwCurrentState = SERVICE_PAUSED;
  SetServiceStatus(service_handle, &service_status);

  if (throttle_timer) WaitForSingleObject(throttle_timer, INFINITE);
  else Sleep(ms);
}
