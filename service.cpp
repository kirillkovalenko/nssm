#include "nssm.h"

SERVICE_STATUS service_status;
SERVICE_STATUS_HANDLE service_handle;
HANDLE wait_handle;
HANDLE pid;
char exe[MAX_PATH];
char flags[MAX_PATH];
char dir[MAX_PATH];

/* Connect to the service manager */
SC_HANDLE open_service_manager() {
  SC_HANDLE ret = OpenSCManager(0, SERVICES_ACTIVE_DATABASE, SC_MANAGER_ALL_ACCESS);
  if (! ret) {
    eventprintf(EVENTLOG_ERROR_TYPE, "Unable to connect to service manager!\nPerhaps you need to be an administrator...");
    return 0;
  }

  return ret;
}

/* Install the service */
int install_service(char *name) {
#ifdef GUI
  /* Show the dialogue box */
  return nssm_gui(IDD_INSTALL, name);
#else
  fprintf(stderr, "Unimplemented\n");
  return 1;
#endif
}

/* Remove the service */
int remove_service(char *name) {
#ifdef GUI
  return nssm_gui(IDD_REMOVE, name);
#else
  fprintf(stderr, "Unimplemented\n");
  return 1;
#endif
}

/* Service initialisation */
void WINAPI service_main(unsigned long argc, char **argv) {
  /* Initialise status */
  ZeroMemory(&service_status, sizeof(service_status));
  service_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS | SERVICE_INTERACTIVE_PROCESS;
  service_status.dwCurrentState = SERVICE_RUNNING;
  service_status.dwControlsAccepted = SERVICE_ACCEPT_SHUTDOWN | SERVICE_ACCEPT_STOP;
  service_status.dwWin32ExitCode = NO_ERROR;
  service_status.dwServiceSpecificExitCode = 0;
  service_status.dwCheckPoint = 0;
  service_status.dwWaitHint = 1000;

  /* Signal we AREN'T running the server */
  pid = 0;

  /* Get startup parameters */
  int ret = get_parameters(argv[0], exe, sizeof(exe), flags, sizeof(flags), dir, sizeof(dir));
  if (ret) {
    eventprintf(EVENTLOG_ERROR_TYPE, "service_main(): Can't get startup parameters: error %d", ret);
    return;
  }

  /* Register control handler */
  service_handle = RegisterServiceCtrlHandlerEx(NSSM, service_control_handler, 0);
  if (! service_handle) {
    eventprintf(EVENTLOG_ERROR_TYPE, "service_main(): RegisterServiceCtrlHandlerEx() failed: %s", error_string(GetLastError()));
    return;
  }

  monitor_service();
}

int monitor_service() {
  /* Set service status to started */
  int ret = start_service();
  if (ret) {
    eventprintf(EVENTLOG_ERROR_TYPE, "Can't start service: error code %d", ret);
    return ret;
  }
  eventprintf(EVENTLOG_INFORMATION_TYPE, "Started process %s %s in %s", exe, flags, dir);

  /* Monitor service service */
  if (! RegisterWaitForSingleObject(&wait_handle, pid, end_service, 0, INFINITE, WT_EXECUTEONLYONCE | WT_EXECUTELONGFUNCTION)) {
    eventprintf(EVENTLOG_WARNING_TYPE, "RegisterWaitForSingleObject() returned %s - service may claim to be still running when %s exits ", error_string(GetLastError()), exe);
  }

  return 0;
}

/* Service control handler */
unsigned long WINAPI service_control_handler(unsigned long control, unsigned long event, void *data, void *context) {
  switch (control) {
    case SERVICE_CONTROL_SHUTDOWN:
    case SERVICE_CONTROL_STOP:
      stop_service(0);
      return NO_ERROR;
  }

  /* Unknown control */
  return ERROR_CALL_NOT_IMPLEMENTED;
}

/* Start the service */
int start_service() {
  if (pid) return 0;

  /* Allocate a STARTUPINFO structure for a new process */
  STARTUPINFO si;
  ZeroMemory(&si, sizeof(si));
  si.cb = sizeof(si);

  /* Allocate a PROCESSINFO structure for the process */
  PROCESS_INFORMATION pi;
  ZeroMemory(&pi, sizeof(pi));

  /* Launch executable with arguments */
  char cmd[MAX_PATH];
  if (_snprintf(cmd, sizeof(cmd), "%s %s", exe, flags) < 0) {
    eventprintf(EVENTLOG_ERROR_TYPE, "Error constructing command line");
    return stop_service(2);
  }
  if (! CreateProcess(0, cmd, 0, 0, 0, 0, 0, dir, &si, &pi)) {
    eventprintf(EVENTLOG_ERROR_TYPE, "Can't launch %s.  CreateProcess() returned %s", exe, error_string(GetLastError()));
    return stop_service(3);
  }
  pid = pi.hProcess;

  /* Signal successful start */
  service_status.dwCurrentState = SERVICE_RUNNING;
  SetServiceStatus(service_handle, &service_status);

  return 0;
}

/* Stop the service */
int stop_service(unsigned long exitcode) {
  /* Signal we are stopping */
  service_status.dwCurrentState = SERVICE_STOP_PENDING;
  SetServiceStatus(service_handle, &service_status);

  /* Nothing to do if server isn't running */
  if (pid) {
    /* Shut down server */
    TerminateProcess(pid, 0);
    pid = 0;
  }

  /* Signal we stopped */
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

  return exitcode;
}

/* Callback function triggered when the server exits */
void CALLBACK end_service(void *arg, unsigned char why) {
  /* Check exit code */
  unsigned long ret = 0;
  GetExitCodeProcess(pid, &ret);

  /* Force an error code if none given, so system can restart this service */
  /*if (! ret) {
    eventprintf(EVENTLOG_INFORMATION_TYPE, "Process exited with return code 0 - overriding with return code 111 so the service manager will notice the failure");
    ret = 111;
  }
  else */eventprintf(EVENTLOG_INFORMATION_TYPE, "Process %s exited with return code %u", exe, ret);

  /* Try to restart the service or return failure code to service manager */
  pid = 0;
  while (monitor_service()) {
    eventprintf(EVENTLOG_INFORMATION_TYPE, "Failed to restart %s - sleeping ", exe, ret);
    Sleep(30000);
  }
}
