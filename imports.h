#ifndef IMPORTS_H
#define IMPORTS_H

typedef BOOL (WINAPI *AttachConsole_ptr)(DWORD);
typedef BOOL (WINAPI *SleepConditionVariableCS_ptr)(PCONDITION_VARIABLE, PCRITICAL_SECTION, DWORD);
typedef void (WINAPI *WakeConditionVariable_ptr)(PCONDITION_VARIABLE);
typedef BOOL (WINAPI *CreateWellKnownSid_ptr)(WELL_KNOWN_SID_TYPE, SID *, SID *, unsigned long *);
typedef BOOL (WINAPI *IsWellKnownSid_ptr)(SID *, WELL_KNOWN_SID_TYPE);

typedef struct {
  HMODULE kernel32;
  HMODULE advapi32;
  AttachConsole_ptr AttachConsole;
  SleepConditionVariableCS_ptr SleepConditionVariableCS;
  WakeConditionVariable_ptr WakeConditionVariable;
  CreateWellKnownSid_ptr CreateWellKnownSid;
  IsWellKnownSid_ptr IsWellKnownSid;
} imports_t;

HMODULE get_dll(const TCHAR *, unsigned long *);
FARPROC get_import(HMODULE, const char *, unsigned long *);
int get_imports();
void free_imports();

#endif
