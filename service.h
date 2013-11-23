#ifndef SERVICE_H
#define SERVICE_H

/*
  MSDN says the commandline in CreateProcess() is limited to 32768 characters
  and the application name to MAX_PATH.
  A registry key is limited to 255 characters.
  A registry value is limited to 16383 characters.
  Therefore we limit the service name to accommodate the path under HKLM.
*/
#define EXE_LENGTH MAX_PATH
#define CMD_LENGTH 32768
#define KEY_LENGTH 255
#define VALUE_LENGTH 16383
#define SERVICE_NAME_LENGTH KEY_LENGTH - 55

#define ACTION_LEN 16

typedef struct {
  char name[SERVICE_NAME_LENGTH];
  char exe[EXE_LENGTH];
  char flags[VALUE_LENGTH];
  char dir[MAX_PATH];
  char *env;
  unsigned long envlen;
  char *env_extra;
  unsigned long env_extralen;
  char stdin_path[MAX_PATH];
  unsigned long stdin_sharing;
  unsigned long stdin_disposition;
  unsigned long stdin_flags;
  char stdout_path[MAX_PATH];
  unsigned long stdout_sharing;
  unsigned long stdout_disposition;
  unsigned long stdout_flags;
  char stderr_path[MAX_PATH];
  unsigned long stderr_sharing;
  unsigned long stderr_disposition;
  unsigned long stderr_flags;
  unsigned long default_exit_action;
  unsigned long throttle_delay;
  unsigned long stop_method;
  unsigned long kill_console_delay;
  unsigned long kill_window_delay;
  unsigned long kill_threads_delay;
  SC_HANDLE handle;
  SERVICE_STATUS status;
  SERVICE_STATUS_HANDLE status_handle;
  HANDLE process_handle;
  unsigned long pid;
  HANDLE wait_handle;
  bool stopping;
  bool allow_restart;
  unsigned long throttle;
  CRITICAL_SECTION throttle_section;
  bool throttle_section_initialised;
  CONDITION_VARIABLE throttle_condition;
  HANDLE throttle_timer;
  LARGE_INTEGER throttle_duetime;
  FILETIME creation_time;
  FILETIME exit_time;
} nssm_service_t;

void WINAPI service_main(unsigned long, char **);
char *service_control_text(unsigned long);
void log_service_control(char *, unsigned long, bool);
unsigned long WINAPI service_control_handler(unsigned long, unsigned long, void *, void *);

nssm_service_t *alloc_nssm_service();
void set_nssm_service_defaults(nssm_service_t *);
void cleanup_nssm_service(nssm_service_t *);
SC_HANDLE open_service_manager();
int pre_install_service(int, char **);
int pre_remove_service(int, char **);
int install_service(nssm_service_t *);
int remove_service(nssm_service_t *);
void set_service_recovery(nssm_service_t *);
int monitor_service(nssm_service_t *);
int start_service(nssm_service_t *);
int stop_service(nssm_service_t *, unsigned long, bool, bool);
void CALLBACK end_service(void *, unsigned char);
void throttle_restart(nssm_service_t *);
int await_shutdown(nssm_service_t *, char *, unsigned long);

#endif
