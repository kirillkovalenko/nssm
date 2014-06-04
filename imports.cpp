#include "nssm.h"

imports_t imports;

/*
  Try to set up function pointers.
  In this first implementation it is not an error if we can't load them
  because we aren't currently trying to load any functions which we
  absolutely need.  If we later add some indispensible imports we can
  return non-zero here to force an application exit.
*/
HMODULE get_dll(const TCHAR *dll, unsigned long *error) {
  *error = 0;

  HMODULE ret = LoadLibrary(dll);
  if (! ret) {
    *error = GetLastError();
    log_event(EVENTLOG_WARNING_TYPE, NSSM_EVENT_LOADLIBRARY_FAILED, dll, error_string(*error), 0);
  }

  return ret;
}

FARPROC get_import(HMODULE library, const char *function, unsigned long *error) {
  *error = 0;

  FARPROC ret = GetProcAddress(library, function);
  if (! ret) {
    *error = GetLastError();
    TCHAR *function_name;
#ifdef UNICODE
    size_t buflen;
    mbstowcs_s(&buflen, NULL, 0, function, _TRUNCATE);
    function_name = (TCHAR *) HeapAlloc(GetProcessHeap(), 0, buflen * sizeof(TCHAR));
    if (function_name) mbstowcs_s(&buflen, function_name, buflen * sizeof(TCHAR), function, _TRUNCATE);
#else
    function_name = (TCHAR *) function;
#endif
    if (*error != ERROR_PROC_NOT_FOUND) log_event(EVENTLOG_WARNING_TYPE, NSSM_EVENT_GETPROCADDRESS_FAILED, function_name, error_string(*error), 0);
#ifdef UNICODE
    if (function_name) HeapFree(GetProcessHeap(), 0, function_name);
#endif
  }

  return ret;
}

int get_imports() {
  unsigned long error;

  ZeroMemory(&imports, sizeof(imports));

  imports.kernel32 = get_dll(_T("kernel32.dll"), &error);
  if (imports.kernel32) {
    imports.AttachConsole = (AttachConsole_ptr) get_import(imports.kernel32, "AttachConsole", &error);
    if (! imports.AttachConsole) {
      if (error != ERROR_PROC_NOT_FOUND) return 2;
    }

    imports.SleepConditionVariableCS = (SleepConditionVariableCS_ptr) get_import(imports.kernel32, "SleepConditionVariableCS", &error);
    if (! imports.SleepConditionVariableCS) {
      if (error != ERROR_PROC_NOT_FOUND) return 3;
    }

    imports.WakeConditionVariable = (WakeConditionVariable_ptr) get_import(imports.kernel32, "WakeConditionVariable", &error);
    if (! imports.WakeConditionVariable) {
      if (error != ERROR_PROC_NOT_FOUND) return 4;
    }
  }
  else if (error != ERROR_MOD_NOT_FOUND) return 1;

  imports.advapi32 = get_dll(_T("advapi32.dll"), &error);
  if (imports.advapi32) {
    imports.CreateWellKnownSid = (CreateWellKnownSid_ptr) get_import(imports.advapi32, "CreateWellKnownSid", &error);
    if (! imports.CreateWellKnownSid) {
      if (error != ERROR_PROC_NOT_FOUND) return 6;
    }
    imports.IsWellKnownSid = (IsWellKnownSid_ptr) get_import(imports.advapi32, "IsWellKnownSid", &error);
    if (! imports.IsWellKnownSid) {
      if (error != ERROR_PROC_NOT_FOUND) return 7;
    }
  }
  else if (error != ERROR_MOD_NOT_FOUND) return 5;

  return 0;
}

void free_imports() {
  if (imports.kernel32) FreeLibrary(imports.kernel32);
  if (imports.advapi32) FreeLibrary(imports.advapi32);
  ZeroMemory(&imports, sizeof(imports));
}
