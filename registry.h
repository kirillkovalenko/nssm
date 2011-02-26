#ifndef REGISTRY_H
#define REGISTRY_H

#define NSSM_REGISTRY "SYSTEM\\CurrentControlSet\\Services\\%s\\Parameters"
#define NSSM_REG_EXE "Application"
#define NSSM_REG_FLAGS "AppParameters"
#define NSSM_REG_DIR "AppDirectory"
#define NSSM_REG_EXIT "AppExit"
#define NSSM_REG_THROTTLE "AppThrottle"

int create_messages();
int create_parameters(char *, char *, char *, char *);
int create_exit_action(char *, const char *);
int expand_parameter(HKEY, char *, char *, unsigned long, bool);
int get_parameters(char *, char *, int, char *, int, char *, int, unsigned long *);
int get_exit_action(char *, unsigned long *, unsigned char *, bool *);

#endif
