#ifndef HOOK_H
#define HOOK_H

#define NSSM_HOOK_EVENT_START _T("Start")
#define NSSM_HOOK_EVENT_STOP _T("Stop")
#define NSSM_HOOK_EVENT_EXIT _T("Exit")
#define NSSM_HOOK_EVENT_POWER _T("Power")
#define NSSM_HOOK_EVENT_ROTATE _T("Rotate")

#define NSSM_HOOK_ACTION_PRE _T("Pre")
#define NSSM_HOOK_ACTION_POST _T("Post")
#define NSSM_HOOK_ACTION_CHANGE _T("Change")
#define NSSM_HOOK_ACTION_RESUME _T("Resume")

/* Hook name will be "<service> (<event>/<action>)" */
#define HOOK_NAME_LENGTH SERVICE_NAME_LENGTH * 2

#define NSSM_HOOK_VERSION 1

/* Hook ran successfully. */
#define NSSM_HOOK_STATUS_SUCCESS 0
/* No hook configured. */
#define NSSM_HOOK_STATUS_NOTFOUND 1
/* Hook requested abort. */
#define NSSM_HOOK_STATUS_ABORT 99
/* Internal error launching hook. */
#define NSSM_HOOK_STATUS_ERROR 100
/* Hook was not run. */
#define NSSM_HOOK_STATUS_NOTRUN 101
/* Hook timed out. */
#define NSSM_HOOK_STATUS_TIMEOUT 102
/* Hook returned non-zero. */
#define NSSM_HOOK_STATUS_FAILED 111

/* Version 1. */
#define NSSM_HOOK_ENV_VERSION _T("NSSM_HOOK_VERSION")
#define NSSM_HOOK_ENV_IMAGE_PATH _T("NSSM_EXE")
#define NSSM_HOOK_ENV_NSSM_CONFIGURATION _T("NSSM_CONFIGURATION")
#define NSSM_HOOK_ENV_NSSM_VERSION _T("NSSM_VERSION")
#define NSSM_HOOK_ENV_BUILD_DATE _T("NSSM_BUILD_DATE")
#define NSSM_HOOK_ENV_PID _T("NSSM_PID")
#define NSSM_HOOK_ENV_DEADLINE _T("NSSM_DEADLINE")
#define NSSM_HOOK_ENV_SERVICE_NAME _T("NSSM_SERVICE_NAME")
#define NSSM_HOOK_ENV_SERVICE_DISPLAYNAME _T("NSSM_SERVICE_DISPLAYNAME")
#define NSSM_HOOK_ENV_COMMAND_LINE _T("NSSM_COMMAND_LINE")
#define NSSM_HOOK_ENV_APPLICATION_PID _T("NSSM_APPLICATION_PID")
#define NSSM_HOOK_ENV_EVENT _T("NSSM_EVENT")
#define NSSM_HOOK_ENV_ACTION _T("NSSM_ACTION")
#define NSSM_HOOK_ENV_TRIGGER _T("NSSM_TRIGGER")
#define NSSM_HOOK_ENV_LAST_CONTROL _T("NSSM_LAST_CONTROL")
#define NSSM_HOOK_ENV_START_REQUESTED_COUNT _T("NSSM_START_REQUESTED_COUNT")
#define NSSM_HOOK_ENV_START_COUNT _T("NSSM_START_COUNT")
#define NSSM_HOOK_ENV_THROTTLE_COUNT _T("NSSM_THROTTLE_COUNT")
#define NSSM_HOOK_ENV_EXIT_COUNT _T("NSSM_EXIT_COUNT")
#define NSSM_HOOK_ENV_EXITCODE _T("NSSM_EXITCODE")
#define NSSM_HOOK_ENV_RUNTIME _T("NSSM_RUNTIME")
#define NSSM_HOOK_ENV_APPLICATION_RUNTIME _T("NSSM_APPLICATION_RUNTIME")

typedef struct {
  TCHAR name[HOOK_NAME_LENGTH];
  HANDLE thread_handle;
} hook_thread_data_t;

typedef struct {
  hook_thread_data_t *data;
  int num_threads;
} hook_thread_t;

bool valid_hook_name(const TCHAR *, const TCHAR *, bool);
void await_hook_threads(hook_thread_t *, SERVICE_STATUS_HANDLE, SERVICE_STATUS *, unsigned long);
int nssm_hook(hook_thread_t *, nssm_service_t *, TCHAR *, TCHAR *, unsigned long *, unsigned long, bool);
int nssm_hook(hook_thread_t *, nssm_service_t *, TCHAR *, TCHAR *, unsigned long *, unsigned long);
int nssm_hook(hook_thread_t *, nssm_service_t *, TCHAR *, TCHAR *, unsigned long *);

#endif
