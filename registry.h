#ifndef REGISTRY_H
#define REGISTRY_H

#define NSSM_REGISTRY "SYSTEM\\CurrentControlSet\\Services\\%s\\Parameters"
#define NSSM_REG_EXE "Application"
#define NSSM_REG_FLAGS "AppParameters"
#define NSSM_REG_DIR "AppDirectory"

int create_parameters(char *, char *, char *, char *);
int get_parameters(char *, char *, int, char *, int, char *, int);

#endif
