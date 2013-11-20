#ifndef REGISTRY_H
#define REGISTRY_H

#define NSSM_REGISTRY "SYSTEM\\CurrentControlSet\\Services\\%s\\Parameters"
#define NSSM_REG_EXE "Application"
#define NSSM_REG_FLAGS "AppParameters"
#define NSSM_REG_DIR "AppDirectory"
#define NSSM_REG_ENV "AppEnvironment"
#define NSSM_REG_EXIT "AppExit"
#define NSSM_REG_THROTTLE "AppThrottle"
#define NSSM_REG_STOP_METHOD_SKIP "AppStopMethodSkip"
#define NSSM_REG_KILL_CONSOLE_GRACE_PERIOD "AppStopMethodConsole"
#define NSSM_REG_KILL_WINDOW_GRACE_PERIOD "AppStopMethodWindow"
#define NSSM_REG_KILL_THREADS_GRACE_PERIOD "AppStopMethodThreads"
#define NSSM_REG_STDIN "AppStdin"
#define NSSM_REG_STDOUT "AppStdout"
#define NSSM_REG_STDERR "AppStderr"
#define NSSM_REG_STDIO_SHARING "ShareMode"
#define NSSM_REG_STDIO_DISPOSITION "CreationDisposition"
#define NSSM_REG_STDIO_FLAGS "FlagsAndAttributes"
#define NSSM_STDIO_LENGTH 29

int create_messages();
int create_parameters(nssm_service_t *);
int create_exit_action(char *, const char *);
int set_environment(char *, HKEY, char **);
int expand_parameter(HKEY, char *, char *, unsigned long, bool, bool);
int expand_parameter(HKEY, char *, char *, unsigned long, bool);
int set_expand_string(HKEY, char *, char *);
int set_number(HKEY, char *, unsigned long);
int get_number(HKEY, char *, unsigned long *, bool);
int get_number(HKEY, char *, unsigned long *);
void override_milliseconds(char *, HKEY, char *, unsigned long *, unsigned long, unsigned long);
int get_parameters(nssm_service_t *, STARTUPINFO *);
int get_exit_action(char *, unsigned long *, unsigned char *, bool *);

#endif
