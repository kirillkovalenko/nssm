#include "nssm.h"

imports_t imports;

/*
  Try to set up function pointers.
  In this first implementation it is not an error if we can't load them
  because we aren't currently trying to load any functions which we
  absolutely need.  If we later add some indispensible imports we can
  return non-zero here to force an application exit.
*/
HMODULE get_dll(const char *dll, unsigned long *error) {
  *error = 0;

  HMODULE ret = LoadLibrary(dll);
  if (! ret) {
    *error = GetLastError();
    log_event(EVENTLOG_WARNING_TYPE, NSSM_EVENT_LOADLIBRARY_FAILED, dll, error_string(*error));
  }

  return ret;
}

FARPROC get_import(HMODULE library, const char *function, unsigned long *error) {
  *error = 0;

  FARPROC ret = GetProcAddress(library, function);
  if (! ret) {
    *error = GetLastError();
    log_event(EVENTLOG_WARNING_TYPE, NSSM_EVENT_GETPROCADDRESS_FAILED, function, error_string(*error));
  }

  return ret;
}

int get_imports() {
  unsigned long error;

  ZeroMemory(&imports, sizeof(imports));

  imports.kernel32 = get_dll("kernel32.dll", &error);
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

  return 0;
}

void free_imports() {
  if (imports.kernel32) FreeLibrary(imports.kernel32);
  ZeroMemory(&imports, sizeof(imports));
}
