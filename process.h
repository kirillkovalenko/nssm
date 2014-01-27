#ifndef PROCESS_H
#define PROCESS_H

#include <tlhelp32.h>

typedef struct {
  unsigned long pid;
  unsigned long exitcode;
  int signalled;
} kill_t;

int get_process_creation_time(HANDLE, FILETIME *);
int get_process_exit_time(HANDLE, FILETIME *);
int check_parent(TCHAR *, PROCESSENTRY32 *, unsigned long, FILETIME *, FILETIME *);
int CALLBACK kill_window(HWND, LPARAM);
int kill_threads(nssm_service_t *, kill_t *);
int kill_console(nssm_service_t *, kill_t *);
int kill_process(nssm_service_t *, HANDLE, unsigned long, unsigned long);
void kill_process_tree(nssm_service_t *, unsigned long, unsigned long, unsigned long);

#endif
