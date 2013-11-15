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
int check_parent(char *, PROCESSENTRY32 *, unsigned long, FILETIME *, FILETIME *);
int CALLBACK kill_window(HWND, LPARAM);
int kill_threads(char *, SERVICE_STATUS_HANDLE, SERVICE_STATUS *, kill_t *);
int kill_console(char *, SERVICE_STATUS_HANDLE, SERVICE_STATUS *, HANDLE, unsigned long);
int kill_process(char *, SERVICE_STATUS_HANDLE, SERVICE_STATUS *, unsigned long, HANDLE, unsigned long, unsigned long);
void kill_process_tree(char *, SERVICE_STATUS_HANDLE, SERVICE_STATUS *, unsigned long, unsigned long, unsigned long, unsigned long, FILETIME *, FILETIME *);

#endif
