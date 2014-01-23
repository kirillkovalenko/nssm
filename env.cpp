#include "nssm.h"

/*
  The environment block starts with variables of the form
  =C:=C:\Windows\System32 which we ignore.
*/
TCHAR *useful_environment(TCHAR *rawenv) {
  TCHAR *env = rawenv;

  if (env) {
    while (*env == _T('=')) {
      for ( ; *env; env++);
      env++;
    }
  }

  return env;
}

/* Expand an environment variable.  Must call HeapFree() on the result. */
TCHAR *expand_environment_string(TCHAR *string) {
  unsigned long len;

  len = ExpandEnvironmentStrings(string, 0, 0);
  if (! len) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_EXPANDENVIRONMENTSTRINGS_FAILED, string, error_string(GetLastError()), 0);
    return 0;
  }

  TCHAR *ret = (TCHAR *) HeapAlloc(GetProcessHeap(), 0, len * sizeof(TCHAR));
  if (! ret) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, _T("ExpandEnvironmentStrings()"), _T("expand_environment_string"), 0);
    return 0;
  }

  if (! ExpandEnvironmentStrings(string, ret, len)) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_EXPANDENVIRONMENTSTRINGS_FAILED, string, error_string(GetLastError()), 0);
    HeapFree(GetProcessHeap(), 0, ret);
    return 0;
  }

  return ret;
}

/*
  Set all the environment variables from an environment block in the current
  environment or remove all the variables in the block from the current
  environment.
*/
static int set_environment_block(TCHAR *env, bool set) {
  int ret = 0;

  TCHAR *s, *t;
  for (s = env; *s; s++) {
    for (t = s; *t && *t != _T('='); t++);
    if (*t == _T('=')) {
      *t = _T('\0');
      if (set) {
        TCHAR *expanded = expand_environment_string(++t);
        if (expanded) {
          if (! SetEnvironmentVariable(s, expanded)) ret++;
          HeapFree(GetProcessHeap(), 0, expanded);
        }
        else {
          if (! SetEnvironmentVariable(s, t)) ret++;
        }
      }
      else {
        if (! SetEnvironmentVariable(s, NULL)) ret++;
      }
      for (t++ ; *t; t++);
    }
    s = t;
  }

  return ret;
}

int set_environment_block(TCHAR *env) {
  return set_environment_block(env, true);
}

static int unset_environment_block(TCHAR *env) {
  return set_environment_block(env, false);
}

/* Remove all variables from the process environment. */
int clear_environment() {
  TCHAR *rawenv = GetEnvironmentStrings();
  TCHAR *env = useful_environment(rawenv);

  int ret = unset_environment_block(env);

  if (rawenv) FreeEnvironmentStrings(rawenv);

  return ret;
}

/* Set the current environment to exactly duplicate an environment block. */
int duplicate_environment(TCHAR *rawenv) {
  int ret = clear_environment();
  TCHAR *env = useful_environment(rawenv);
  ret += set_environment_block(env);
  return ret;
}

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
