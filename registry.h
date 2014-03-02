#ifndef REGISTRY_H
#define REGISTRY_H

#define NSSM_REGISTRY _T("SYSTEM\\CurrentControlSet\\Services\\%s\\Parameters")
#define NSSM_REGISTRY_GROUPS _T("SYSTEM\\CurrentControlSet\\Control\\ServiceGroupOrder")
#define NSSM_REG_GROUPS _T("List")
#define NSSM_REG_EXE _T("Application")
#define NSSM_REG_FLAGS _T("AppParameters")
#define NSSM_REG_DIR _T("AppDirectory")
#define NSSM_REG_ENV _T("AppEnvironment")
#define NSSM_REG_ENV_EXTRA _T("AppEnvironmentExtra")
#define NSSM_REG_EXIT _T("AppExit")
#define NSSM_REG_RESTART_DELAY _T("AppRestartDelay")
#define NSSM_REG_THROTTLE _T("AppThrottle")
#define NSSM_REG_STOP_METHOD_SKIP _T("AppStopMethodSkip")
#define NSSM_REG_KILL_CONSOLE_GRACE_PERIOD _T("AppStopMethodConsole")
#define NSSM_REG_KILL_WINDOW_GRACE_PERIOD _T("AppStopMethodWindow")
#define NSSM_REG_KILL_THREADS_GRACE_PERIOD _T("AppStopMethodThreads")
#define NSSM_REG_STDIN _T("AppStdin")
#define NSSM_REG_STDOUT _T("AppStdout")
#define NSSM_REG_STDERR _T("AppStderr")
#define NSSM_REG_STDIO_SHARING _T("ShareMode")
#define NSSM_REG_STDIO_DISPOSITION _T("CreationDisposition")
#define NSSM_REG_STDIO_FLAGS _T("FlagsAndAttributes")
#define NSSM_REG_ROTATE _T("AppRotateFiles")
#define NSSM_REG_ROTATE_ONLINE _T("AppRotateOnline")
#define NSSM_REG_ROTATE_SECONDS _T("AppRotateSeconds")
#define NSSM_REG_ROTATE_BYTES_LOW _T("AppRotateBytes")
#define NSSM_REG_ROTATE_BYTES_HIGH _T("AppRotateBytesHigh")
#define NSSM_REG_PRIORITY _T("AppPriority")
#define NSSM_REG_AFFINITY _T("AppAffinity")
#define NSSM_REG_NO_CONSOLE _T("AppNoConsole")
#define NSSM_STDIO_LENGTH 29

HKEY open_registry(const TCHAR *, const TCHAR *, REGSAM sam);
HKEY open_registry(const TCHAR *, REGSAM sam);
int create_messages();
int create_parameters(nssm_service_t *, bool);
int create_exit_action(TCHAR *, const TCHAR *, bool);
int get_environment(TCHAR *, HKEY, TCHAR *, TCHAR **, unsigned long *);
int get_string(HKEY, TCHAR *, TCHAR *, unsigned long, bool, bool, bool);
int get_string(HKEY, TCHAR *, TCHAR *, unsigned long, bool);
int expand_parameter(HKEY, TCHAR *, TCHAR *, unsigned long, bool, bool);
int expand_parameter(HKEY, TCHAR *, TCHAR *, unsigned long, bool);
int set_string(HKEY, TCHAR *, TCHAR *, bool);
int set_string(HKEY, TCHAR *, TCHAR *);
int set_expand_string(HKEY, TCHAR *, TCHAR *);
int set_number(HKEY, TCHAR *, unsigned long);
int get_number(HKEY, TCHAR *, unsigned long *, bool);
int get_number(HKEY, TCHAR *, unsigned long *);
int format_double_null(TCHAR *, unsigned long, TCHAR **, unsigned long *);
int unformat_double_null(TCHAR *, unsigned long, TCHAR **, unsigned long *);
void override_milliseconds(TCHAR *, HKEY, TCHAR *, unsigned long *, unsigned long, unsigned long);
int get_io_parameters(nssm_service_t *, HKEY);
int get_parameters(nssm_service_t *, STARTUPINFO *);
int get_exit_action(const TCHAR *, unsigned long *, TCHAR *, bool *);

#endif
