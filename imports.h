#ifndef IMPORTS_H
#define IMPORTS_H

/* Some functions don't have decorated versions. */
#ifdef UNICODE
#define QUERYFULLPROCESSIMAGENAME_EXPORT "QueryFullProcessImageNameW"
#else
#define QUERYFULLPROCESSIMAGENAME_EXPORT "QueryFullProcessImageNameA"
#endif

typedef BOOL (WINAPI *AttachConsole_ptr)(DWORD);
typedef BOOL (WINAPI *SleepConditionVariableCS_ptr)(PCONDITION_VARIABLE, PCRITICAL_SECTION, DWORD);
typedef BOOL (WINAPI *QueryFullProcessImageName_ptr)(HANDLE, unsigned long, LPTSTR, unsigned long *);
typedef void (WINAPI *WakeConditionVariable_ptr)(PCONDITION_VARIABLE);
typedef BOOL (WINAPI *CreateWellKnownSid_ptr)(WELL_KNOWN_SID_TYPE, SID *, SID *, unsigned long *);
typedef BOOL (WINAPI *IsWellKnownSid_ptr)(SID *, WELL_KNOWN_SID_TYPE);

typedef struct {
  HMODULE kernel32;
  HMODULE advapi32;
  AttachConsole_ptr AttachConsole;
  SleepConditionVariableCS_ptr SleepConditionVariableCS;
  QueryFullProcessImageName_ptr QueryFullProcessImageName;
  WakeConditionVariable_ptr WakeConditionVariable;
  CreateWellKnownSid_ptr CreateWellKnownSid;
  IsWellKnownSid_ptr IsWellKnownSid;
} imports_t;

HMODULE get_dll(const TCHAR *, unsigned long *);
FARPROC get_import(HMODULE, const char *, unsigned long *);
int get_imports();
void free_imports();

#endif
