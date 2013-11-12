#ifndef IMPORTS_H
#define IMPORTS_H

typedef BOOL (WINAPI *AttachConsole_ptr)(DWORD);
typedef BOOL (WINAPI *SleepConditionVariableCS_ptr)(PCONDITION_VARIABLE, PCRITICAL_SECTION, DWORD);
typedef void (WINAPI *WakeConditionVariable_ptr)(PCONDITION_VARIABLE);

typedef struct {
  HMODULE kernel32;
  AttachConsole_ptr AttachConsole;
  SleepConditionVariableCS_ptr SleepConditionVariableCS;
  WakeConditionVariable_ptr WakeConditionVariable;
} imports_t;

HMODULE get_dll(const char *, unsigned long *);
FARPROC get_import(HMODULE, const char *, unsigned long *);
int get_imports();
void free_imports();

#endif
