#ifndef SERVICE_H
#define SERVICE_H

/*
  MSDN says the commandline in CreateProcess() is limited to 32768 characters
  and the application name to MAX_PATH.
  A service name and service display name are limited to 256 characters.
  A registry key is limited to 255 characters.
  A registry value is limited to 16383 characters.
  Therefore we limit the service name to accommodate the path under HKLM.
*/
#define EXE_LENGTH PATH_LENGTH
#define CMD_LENGTH 32768
#define KEY_LENGTH 255
#define VALUE_LENGTH 16383
#define SERVICE_NAME_LENGTH 256

#define ACTION_LEN 16

#define NSSM_KERNEL_DRIVER _T("SERVICE_KERNEL_DRIVER")
#define NSSM_FILE_SYSTEM_DRIVER _T("SERVICE_FILE_SYSTEM_DRIVER")
#define NSSM_WIN32_OWN_PROCESS _T("SERVICE_WIN32_OWN_PROCESS")
#define NSSM_WIN32_SHARE_PROCESS _T("SERVICE_WIN32_SHARE_PROCESS")
#define NSSM_INTERACTIVE_PROCESS _T("SERVICE_INTERACTIVE_PROCESS")
#define NSSM_SHARE_INTERACTIVE_PROCESS NSSM_WIN32_SHARE_PROCESS _T("|") NSSM_INTERACTIVE_PROCESS
#define NSSM_UNKNOWN _T("?")

#define NSSM_ROTATE_OFFLINE 0
#define NSSM_ROTATE_ONLINE 1
#define NSSM_ROTATE_ONLINE_ASAP 2

typedef struct {
  bool native;
  TCHAR name[SERVICE_NAME_LENGTH];
  TCHAR displayname[SERVICE_NAME_LENGTH];
  TCHAR description[VALUE_LENGTH];
  unsigned long startup;
  TCHAR *username;
  size_t usernamelen;
  TCHAR *password;
  size_t passwordlen;
  unsigned long type;
  TCHAR image[PATH_LENGTH];
  TCHAR exe[EXE_LENGTH];
  TCHAR flags[VALUE_LENGTH];
  TCHAR dir[DIR_LENGTH];
  TCHAR *env;
  __int64 affinity;
  TCHAR *dependencies;
  unsigned long dependencieslen;
  unsigned long envlen;
  TCHAR *env_extra;
  unsigned long env_extralen;
  unsigned long priority;
  unsigned long no_console;
  TCHAR stdin_path[PATH_LENGTH];
  unsigned long stdin_sharing;
  unsigned long stdin_disposition;
  unsigned long stdin_flags;
  TCHAR stdout_path[PATH_LENGTH];
  unsigned long stdout_sharing;
  unsigned long stdout_disposition;
  unsigned long stdout_flags;
  bool use_stdout_pipe;
  HANDLE stdout_si;
  HANDLE stdout_pipe;
  HANDLE stdout_thread;
  unsigned long stdout_tid;
  TCHAR stderr_path[PATH_LENGTH];
  unsigned long stderr_sharing;
  unsigned long stderr_disposition;
  unsigned long stderr_flags;
  bool use_stderr_pipe;
  HANDLE stderr_si;
  HANDLE stderr_pipe;
  HANDLE stderr_thread;
  unsigned long stderr_tid;
  bool hook_share_output_handles;
  bool rotate_files;
  bool timestamp_log;
  bool stdout_copy_and_truncate;
  bool stderr_copy_and_truncate;
  unsigned long rotate_stdout_online;
  unsigned long rotate_stderr_online;
  unsigned long rotate_seconds;
  unsigned long rotate_bytes_low;
  unsigned long rotate_bytes_high;
  unsigned long rotate_delay;
  unsigned long default_exit_action;
  unsigned long restart_delay;
  unsigned long throttle_delay;
  unsigned long stop_method;
  unsigned long kill_console_delay;
  unsigned long kill_window_delay;
  unsigned long kill_threads_delay;
  bool kill_process_tree;
  SC_HANDLE handle;
  SERVICE_STATUS status;
  SERVICE_STATUS_HANDLE status_handle;
  HANDLE process_handle;
  unsigned long pid;
  HANDLE wait_handle;
  unsigned long exitcode;
  bool stopping;
  bool allow_restart;
  unsigned long throttle;
  CRITICAL_SECTION throttle_section;
  bool throttle_section_initialised;
  CRITICAL_SECTION hook_section;
  bool hook_section_initialised;
  CONDITION_VARIABLE throttle_condition;
  HANDLE throttle_timer;
  LARGE_INTEGER throttle_duetime;
  FILETIME nssm_creation_time;
  FILETIME creation_time;
  FILETIME exit_time;
  TCHAR *initial_env;
  unsigned long last_control;
  unsigned long start_requested_count;
  unsigned long start_count;
  unsigned long exit_count;
} nssm_service_t;

void WINAPI service_main(unsigned long, TCHAR **);
TCHAR *service_control_text(unsigned long);
TCHAR *service_status_text(unsigned long);
void log_service_control(TCHAR *, unsigned long, bool);
unsigned long WINAPI service_control_handler(unsigned long, unsigned long, void *, void *);

int affinity_mask_to_string(__int64, TCHAR **);
int affinity_string_to_mask(TCHAR *, __int64 *);
unsigned long priority_mask();
int priority_constant_to_index(unsigned long);
unsigned long priority_index_to_constant(int);

nssm_service_t *alloc_nssm_service();
void set_nssm_service_defaults(nssm_service_t *);
void cleanup_nssm_service(nssm_service_t *);
SC_HANDLE open_service_manager(unsigned long);
SC_HANDLE open_service(SC_HANDLE, TCHAR *, unsigned long, TCHAR *, unsigned long);
QUERY_SERVICE_CONFIG *query_service_config(const TCHAR *, SC_HANDLE);
int append_to_dependencies(TCHAR *, unsigned long, TCHAR *, TCHAR **, unsigned long *, int);
int remove_from_dependencies(TCHAR *, unsigned long, TCHAR *, TCHAR **, unsigned long *, int);
int set_service_dependencies(const TCHAR *, SC_HANDLE, TCHAR *);
int get_service_dependencies(const TCHAR *, SC_HANDLE, TCHAR **, unsigned long *, int);
int get_service_dependencies(const TCHAR *, SC_HANDLE, TCHAR **, unsigned long *);
int set_service_description(const TCHAR *, SC_HANDLE, TCHAR *);
int get_service_description(const TCHAR *, SC_HANDLE, unsigned long, TCHAR *);
int get_service_startup(const TCHAR *, SC_HANDLE, const QUERY_SERVICE_CONFIG *, unsigned long *);
int get_service_username(const TCHAR *, const QUERY_SERVICE_CONFIG *, TCHAR **, size_t *);
void set_service_environment(nssm_service_t *);
void unset_service_environment(nssm_service_t *);
int pre_install_service(int, TCHAR **);
int pre_remove_service(int, TCHAR **);
int pre_edit_service(int, TCHAR **);
int install_service(nssm_service_t *);
int remove_service(nssm_service_t *);
int edit_service(nssm_service_t *, bool);
int control_service(unsigned long, int, TCHAR **, bool);
int control_service(unsigned long, int, TCHAR **);
void set_service_recovery(nssm_service_t *);
int monitor_service(nssm_service_t *);
int start_service(nssm_service_t *);
int stop_service(nssm_service_t *, unsigned long, bool, bool);
void CALLBACK end_service(void *, unsigned char);
void throttle_restart(nssm_service_t *);
int await_single_handle(SERVICE_STATUS_HANDLE, SERVICE_STATUS *, HANDLE, TCHAR *, TCHAR *, unsigned long);
int list_nssm_services(int, TCHAR **);
int service_process_tree(int, TCHAR **);

#endif
