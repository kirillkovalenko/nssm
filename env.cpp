#include "nssm.h"

/* Replace NULL with CRLF. Leave NULL NULL as the end marker. */
int format_environment(TCHAR *env, unsigned long envlen, TCHAR **formatted, unsigned long *newlen) {
  unsigned long i, j;
  *newlen = envlen;

  if (! *newlen) {
    *formatted = 0;
    return 0;
  }

  for (i = 0; i < envlen; i++) if (! env[i] && env[i + 1]) ++*newlen;

  *formatted = (TCHAR *) HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, *newlen * sizeof(TCHAR));
  if (! *formatted) {
    *newlen = 0;
    return 1;
  }

  for (i = 0, j = 0; i < envlen; i++) {
    (*formatted)[j] = env[i];
    if (! env[i]) {
      if (env[i + 1]) {
        (*formatted)[j] = _T('\r');
        (*formatted)[++j] = _T('\n');
      }
    }
    j++;
  }

  return 0;
}

/* Strip CR and replace LF with NULL. */
int unformat_environment(TCHAR *env, unsigned long envlen, TCHAR **unformatted, unsigned long *newlen) {
  unsigned long i, j;
  *newlen = 0;

  if (! envlen) {
    *unformatted = 0;
    return 0;
  }

  for (i = 0; i < envlen; i++) if (env[i] != _T('\r')) ++*newlen;
  /* Must end with two NULLs. */
  *newlen += 2;

  *unformatted = (TCHAR *) HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, *newlen * sizeof(TCHAR));
  if (! *unformatted) return 1;

  for (i = 0, j = 0; i < envlen; i++) {
    if (env[i] == _T('\r')) continue;
    if (env[i] == _T('\n')) (*unformatted)[j] = _T('\0');
    else (*unformatted)[j] = env[i];
    j++;
  }

  return 0;
}

/*
  Verify an environment block.
  Returns:  1 if environment is invalid.
            0 if environment is OK.
           -1 on error.
*/
int test_environment(TCHAR *env) {
  TCHAR path[PATH_LENGTH];
  GetModuleFileName(0, path, _countof(path));
  STARTUPINFO si;
  ZeroMemory(&si, sizeof(si));
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi;
  ZeroMemory(&pi, sizeof(pi));
  unsigned long flags = CREATE_SUSPENDED;
#ifdef UNICODE
  flags |= CREATE_UNICODE_ENVIRONMENT;
#endif

  /*
    Try to relaunch ourselves but with the candidate environment set.
    Assuming no solar flare activity, the only reason this would fail is if
    the environment were invalid.
  */
  if (CreateProcess(0, path, 0, 0, 0, flags, env, 0, &si, &pi)) {
    TerminateProcess(pi.hProcess, 0);
  }
  else {
    unsigned long error = GetLastError();
    if (error == ERROR_INVALID_PARAMETER) return 1;
    else return -1;
  }

  return 0;
}
