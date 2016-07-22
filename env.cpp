#include "nssm.h"

/*
  Environment block is of the form:

    KEY1=VALUE1 NULL
    KEY2=VALUE2 NULL
    NULL

  A single variable KEY=VALUE has length 15:

    KEY=VALUE (13) NULL (1)
    NULL (1)

  Environment variable names are case-insensitive!
*/

/* Find the length in characters of an environment block. */
size_t environment_length(TCHAR *env) {
  size_t len = 0;

  TCHAR *s;
  for (s = env; ; s++) {
    len++;
    if (*s == _T('\0')) {
      if (*(s + 1) == _T('\0')) {
        len++;
        break;
      }
    }
  }

  return len;
}

/* Copy an environment block. */
TCHAR *copy_environment_block(TCHAR *env) {
  TCHAR *newenv;
  if (copy_double_null(env, (unsigned long) environment_length(env), &newenv)) return 0;
  return newenv;
}

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
      for (t++; *t; t++);
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

/*
  Verify an environment block.
  Returns:  1 if environment is invalid.
            0 if environment is OK.
           -1 on error.
*/
int test_environment(TCHAR *env) {
  TCHAR *path = (TCHAR *) nssm_imagepath();
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

/*
  Duplicate an environment block returned by GetEnvironmentStrings().
  Since such a block is by definition readonly, and duplicate_environment()
  modifies its inputs, this function takes a copy of the input and operates
  on that.
*/
void duplicate_environment_strings(TCHAR *env) {
  TCHAR *newenv = copy_environment_block(env);
  if (! newenv) return;

  duplicate_environment(newenv);
  HeapFree(GetProcessHeap(), 0, newenv);
}

/* Safely get a copy of the current environment. */
TCHAR *copy_environment() {
  TCHAR *rawenv = GetEnvironmentStrings();
  if (! rawenv) return NULL;
  TCHAR *env = copy_environment_block(rawenv);
  FreeEnvironmentStrings(rawenv);
  return env;
}

/*
  Create a new block with all the strings of the first block plus a new string.
  If the key is already present its value will be overwritten in place.
  If the key is blank or empty the new block will still be allocated and have
  non-zero length.
*/
int append_to_environment_block(TCHAR *env, unsigned long envlen, TCHAR *string, TCHAR **newenv, unsigned long *newlen) {
  size_t keylen = 0;
  if (string && string[0]) {
    for (; string[keylen]; keylen++) {
      if (string[keylen] == _T('=')) {
        keylen++;
        break;
      }
    }
  }
  return append_to_double_null(env, envlen, newenv, newlen, string, keylen, false);
}

/*
  Create a new block with all the strings of the first block minus the given
  string.
  If the key is not present the new block will be a copy of the original.
  If the string is KEY=VALUE the key will only be removed if its value is
  VALUE.
  If the string is just KEY the key will unconditionally be removed.
  If removing the string results in an empty list the new block will still be
  allocated and have non-zero length.
*/
int remove_from_environment_block(TCHAR *env, unsigned long envlen, TCHAR *string, TCHAR **newenv, unsigned long *newlen) {
  if (! string || ! string[0] || string[0] == _T('=')) return 1;

  TCHAR *key = 0;
  size_t len = _tcslen(string);
  size_t i;
  for (i = 0; i < len; i++) if (string[i] == _T('=')) break;

  /* Rewrite KEY to KEY= but leave KEY=VALUE alone. */
  size_t keylen = len;
  if (i == len) keylen++;

  key = (TCHAR *) HeapAlloc(GetProcessHeap(), 0, (keylen + 1) * sizeof(TCHAR));
  if (! key) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, _T("key"), _T("remove_from_environment_block()"), 0);
    return 2;
  }
  memmove(key, string, len * sizeof(TCHAR));
  if (keylen > len) key[keylen - 1] = _T('=');
  key[keylen] = _T('\0');

  int ret = remove_from_double_null(env, envlen, newenv, newlen, key, keylen, false);
  HeapFree(GetProcessHeap(), 0, key);

  return ret;
}
