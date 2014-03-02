#include "nssm.h"

extern const TCHAR *exit_action_strings[];

int create_messages() {
  HKEY key;

  TCHAR registry[KEY_LENGTH];
  if (_sntprintf_s(registry, _countof(registry), _TRUNCATE, _T("SYSTEM\\CurrentControlSet\\Services\\EventLog\\Application\\%s"), NSSM) < 0) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, _T("eventlog registry"), _T("create_messages()"), 0);
    return 1;
  }

  if (RegCreateKeyEx(HKEY_LOCAL_MACHINE, registry, 0, 0, REG_OPTION_NON_VOLATILE, KEY_WRITE, 0, &key, 0) != ERROR_SUCCESS) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OPENKEY_FAILED, registry, error_string(GetLastError()), 0);
    return 2;
  }

  /* Get path of this program */
  TCHAR path[PATH_LENGTH];
  GetModuleFileName(0, path, _countof(path));

  /* Try to register the module but don't worry so much on failure */
  RegSetValueEx(key, _T("EventMessageFile"), 0, REG_SZ, (const unsigned char *) path, (unsigned long) (_tcslen(path) +  1) * sizeof(TCHAR));
  unsigned long types = EVENTLOG_INFORMATION_TYPE | EVENTLOG_WARNING_TYPE | EVENTLOG_ERROR_TYPE;
  RegSetValueEx(key, _T("TypesSupported"), 0, REG_DWORD, (const unsigned char *) &types, sizeof(types));

  return 0;
}

int create_parameters(nssm_service_t *service, bool editing) {
  /* Try to open the registry */
  HKEY key = open_registry(service->name, KEY_WRITE);
  if (! key) return 1;

  /* Try to create the parameters */
  if (set_expand_string(key, NSSM_REG_EXE, service->exe)) {
    RegDeleteKey(HKEY_LOCAL_MACHINE, NSSM_REGISTRY);
    RegCloseKey(key);
    return 2;
  }
  if (set_expand_string(key, NSSM_REG_FLAGS, service->flags)) {
    RegDeleteKey(HKEY_LOCAL_MACHINE, NSSM_REGISTRY);
    RegCloseKey(key);
    return 3;
  }
  if (set_expand_string(key, NSSM_REG_DIR, service->dir)) {
    RegDeleteKey(HKEY_LOCAL_MACHINE, NSSM_REGISTRY);
    RegCloseKey(key);
    return 4;
  }

  /* Other non-default parameters. May fail. */
  if (service->priority != NORMAL_PRIORITY_CLASS) set_number(key, NSSM_REG_PRIORITY, service->priority);
  else if (editing) RegDeleteValue(key, NSSM_REG_PRIORITY);
  if (service->affinity) {
    TCHAR *string;
    if (! affinity_mask_to_string(service->affinity, &string)) {
      if (RegSetValueEx(key, NSSM_REG_AFFINITY, 0, REG_SZ, (const unsigned char *) string, (unsigned long) (_tcslen(string) + 1) * sizeof(TCHAR)) != ERROR_SUCCESS) {
        log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_SETVALUE_FAILED, NSSM_REG_AFFINITY, error_string(GetLastError()), 0);
        HeapFree(GetProcessHeap(), 0, string);
        return 5;
      }
    }
    if (string) HeapFree(GetProcessHeap(), 0, string);
  }
  else if (editing) RegDeleteValue(key, NSSM_REG_AFFINITY);
  unsigned long stop_method_skip = ~service->stop_method;
  if (stop_method_skip) set_number(key, NSSM_REG_STOP_METHOD_SKIP, stop_method_skip);
  else if (editing) RegDeleteValue(key, NSSM_REG_STOP_METHOD_SKIP);
  if (service->default_exit_action < NSSM_NUM_EXIT_ACTIONS) create_exit_action(service->name, exit_action_strings[service->default_exit_action], editing);
  if (service->restart_delay) set_number(key, NSSM_REG_RESTART_DELAY, service->restart_delay);
  else if (editing) RegDeleteValue(key, NSSM_REG_RESTART_DELAY);
  if (service->throttle_delay != NSSM_RESET_THROTTLE_RESTART) set_number(key, NSSM_REG_THROTTLE, service->throttle_delay);
  else if (editing) RegDeleteValue(key, NSSM_REG_THROTTLE);
  if (service->kill_console_delay != NSSM_KILL_CONSOLE_GRACE_PERIOD) set_number(key, NSSM_REG_KILL_CONSOLE_GRACE_PERIOD, service->kill_console_delay);
  else if (editing) RegDeleteValue(key, NSSM_REG_KILL_CONSOLE_GRACE_PERIOD);
  if (service->kill_window_delay != NSSM_KILL_WINDOW_GRACE_PERIOD) set_number(key, NSSM_REG_KILL_WINDOW_GRACE_PERIOD, service->kill_window_delay);
  else if (editing) RegDeleteValue(key, NSSM_REG_KILL_WINDOW_GRACE_PERIOD);
  if (service->kill_threads_delay != NSSM_KILL_THREADS_GRACE_PERIOD) set_number(key, NSSM_REG_KILL_THREADS_GRACE_PERIOD, service->kill_threads_delay);
  else if (editing) RegDeleteValue(key, NSSM_REG_KILL_THREADS_GRACE_PERIOD);
  if (service->stdin_path[0] || editing) {
    if (service->stdin_path[0]) set_expand_string(key, NSSM_REG_STDIN, service->stdin_path);
    else if (editing) RegDeleteValue(key, NSSM_REG_STDIN);
    if (service->stdin_sharing != NSSM_STDIN_SHARING) set_createfile_parameter(key, NSSM_REG_STDIN, NSSM_REG_STDIO_SHARING, service->stdin_sharing);
    else if (editing) delete_createfile_parameter(key, NSSM_REG_STDIN, NSSM_REG_STDIO_SHARING);
    if (service->stdin_disposition != NSSM_STDIN_DISPOSITION) set_createfile_parameter(key, NSSM_REG_STDIN, NSSM_REG_STDIO_DISPOSITION, service->stdin_disposition);
    else if (editing) delete_createfile_parameter(key, NSSM_REG_STDIN, NSSM_REG_STDIO_DISPOSITION);
    if (service->stdin_flags != NSSM_STDIN_FLAGS) set_createfile_parameter(key, NSSM_REG_STDIN, NSSM_REG_STDIO_FLAGS, service->stdin_flags);
    else if (editing) delete_createfile_parameter(key, NSSM_REG_STDIN, NSSM_REG_STDIO_FLAGS);
  }
  if (service->stdout_path[0] || editing) {
    if (service->stdout_path[0]) set_expand_string(key, NSSM_REG_STDOUT, service->stdout_path);
    else if (editing) RegDeleteValue(key, NSSM_REG_STDOUT);
    if (service->stdout_sharing != NSSM_STDOUT_SHARING) set_createfile_parameter(key, NSSM_REG_STDOUT, NSSM_REG_STDIO_SHARING, service->stdout_sharing);
    else if (editing) delete_createfile_parameter(key, NSSM_REG_STDOUT, NSSM_REG_STDIO_SHARING);
    if (service->stdout_disposition != NSSM_STDOUT_DISPOSITION) set_createfile_parameter(key, NSSM_REG_STDOUT, NSSM_REG_STDIO_DISPOSITION, service->stdout_disposition);
    else if (editing) delete_createfile_parameter(key, NSSM_REG_STDOUT, NSSM_REG_STDIO_DISPOSITION);
    if (service->stdout_flags != NSSM_STDOUT_FLAGS) set_createfile_parameter(key, NSSM_REG_STDOUT, NSSM_REG_STDIO_FLAGS, service->stdout_flags);
    else if (editing) delete_createfile_parameter(key, NSSM_REG_STDOUT, NSSM_REG_STDIO_FLAGS);
  }
  if (service->stderr_path[0] || editing) {
    if (service->stderr_path[0]) set_expand_string(key, NSSM_REG_STDERR, service->stderr_path);
    else if (editing) RegDeleteValue(key, NSSM_REG_STDERR);
    if (service->stderr_sharing != NSSM_STDERR_SHARING) set_createfile_parameter(key, NSSM_REG_STDERR, NSSM_REG_STDIO_SHARING, service->stderr_sharing);
    else if (editing) delete_createfile_parameter(key, NSSM_REG_STDERR, NSSM_REG_STDIO_SHARING);
    if (service->stderr_disposition != NSSM_STDERR_DISPOSITION) set_createfile_parameter(key, NSSM_REG_STDERR, NSSM_REG_STDIO_DISPOSITION, service->stderr_disposition);
    else if (editing) delete_createfile_parameter(key, NSSM_REG_STDERR, NSSM_REG_STDIO_DISPOSITION);
    if (service->stderr_flags != NSSM_STDERR_FLAGS) set_createfile_parameter(key, NSSM_REG_STDERR, NSSM_REG_STDIO_FLAGS, service->stderr_flags);
    else if (editing) delete_createfile_parameter(key, NSSM_REG_STDERR, NSSM_REG_STDIO_FLAGS);
  }
  if (service->rotate_files) set_number(key, NSSM_REG_ROTATE, 1);
  else if (editing) RegDeleteValue(key, NSSM_REG_ROTATE);
  if (service->rotate_stdout_online) set_number(key, NSSM_REG_ROTATE_ONLINE, 1);
  else if (editing) RegDeleteValue(key, NSSM_REG_ROTATE_ONLINE);
  if (service->rotate_seconds) set_number(key, NSSM_REG_ROTATE_SECONDS, service->rotate_seconds);
  else if (editing) RegDeleteValue(key, NSSM_REG_ROTATE_SECONDS);
  if (service->rotate_bytes_low) set_number(key, NSSM_REG_ROTATE_BYTES_LOW, service->rotate_bytes_low);
  else if (editing) RegDeleteValue(key, NSSM_REG_ROTATE_BYTES_LOW);
  if (service->rotate_bytes_high) set_number(key, NSSM_REG_ROTATE_BYTES_HIGH, service->rotate_bytes_high);
  else if (editing) RegDeleteValue(key, NSSM_REG_ROTATE_BYTES_HIGH);
  if (service->no_console) set_number(key, NSSM_REG_NO_CONSOLE, 1);
  else if (editing) RegDeleteValue(key, NSSM_REG_NO_CONSOLE);

  /* Environment */
  if (service->env) {
    if (RegSetValueEx(key, NSSM_REG_ENV, 0, REG_MULTI_SZ, (const unsigned char *) service->env, (unsigned long) service->envlen * sizeof(TCHAR)) != ERROR_SUCCESS) {
      log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_SETVALUE_FAILED, NSSM_REG_ENV, error_string(GetLastError()), 0);
    }
  }
  else if (editing) RegDeleteValue(key, NSSM_REG_ENV);
  if (service->env_extra) {
    if (RegSetValueEx(key, NSSM_REG_ENV_EXTRA, 0, REG_MULTI_SZ, (const unsigned char *) service->env_extra, (unsigned long) service->env_extralen * sizeof(TCHAR)) != ERROR_SUCCESS) {
      log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_SETVALUE_FAILED, NSSM_REG_ENV_EXTRA, error_string(GetLastError()), 0);
    }
  }
  else if (editing) RegDeleteValue(key, NSSM_REG_ENV_EXTRA);

  /* Close registry. */
  RegCloseKey(key);

  return 0;
}

int create_exit_action(TCHAR *service_name, const TCHAR *action_string, bool editing) {
  /* Get registry */
  TCHAR registry[KEY_LENGTH];
  if (_sntprintf_s(registry, _countof(registry), _TRUNCATE, NSSM_REGISTRY _T("\\%s"), service_name, NSSM_REG_EXIT) < 0) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, _T("NSSM_REG_EXIT"), _T("create_exit_action()"), 0);
    return 1;
  }

  /* Try to open the registry */
  HKEY key;
  unsigned long disposition;
  if (RegCreateKeyEx(HKEY_LOCAL_MACHINE, registry, 0, 0, REG_OPTION_NON_VOLATILE, KEY_WRITE, 0, &key, &disposition) != ERROR_SUCCESS) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OPENKEY_FAILED, registry, error_string(GetLastError()), 0);
    return 2;
  }

  /* Do nothing if the key already existed */
  if (disposition == REG_OPENED_EXISTING_KEY && ! editing) {
    RegCloseKey(key);
    return 0;
  }

  /* Create the default value */
  if (RegSetValueEx(key, 0, 0, REG_SZ, (const unsigned char *) action_string, (unsigned long) (_tcslen(action_string) + 1) * sizeof(TCHAR)) != ERROR_SUCCESS) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_SETVALUE_FAILED, NSSM_REG_EXIT, error_string(GetLastError()), 0);
    RegCloseKey(key);
    return 3;
  }

  /* Close registry */
  RegCloseKey(key);

  return 0;
}

int get_environment(TCHAR *service_name, HKEY key, TCHAR *value, TCHAR **env, unsigned long *envlen) {
  unsigned long type = REG_MULTI_SZ;

  /* Dummy test to find buffer size */
  unsigned long ret = RegQueryValueEx(key, value, 0, &type, NULL, envlen);
  if (ret != ERROR_SUCCESS) {
    *env = 0;
    *envlen = 0;
    /* The service probably doesn't have any environment configured */
    if (ret == ERROR_FILE_NOT_FOUND) return 0;
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_QUERYVALUE_FAILED, value, error_string(GetLastError()), 0);
    return 1;
  }

  if (type != REG_MULTI_SZ) {
    *env = 0;
    *envlen = 0;
    log_event(EVENTLOG_WARNING_TYPE, NSSM_EVENT_INVALID_ENVIRONMENT_STRING_TYPE, value, service_name, 0);
    return 2;
  }

  /* Probably not possible */
  if (! *envlen) return 0;

  /* Previously initialised? */
  if (*env) HeapFree(GetProcessHeap(), 0, *env);

  *env = (TCHAR *) HeapAlloc(GetProcessHeap(), 0, *envlen);
  if (! *env) {
    *envlen = 0;
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, value, _T("get_environment()"), 0);
    return 3;
  }

  /* Actually get the strings */
  ret = RegQueryValueEx(key, value, 0, &type, (unsigned char *) *env, envlen);
  if (ret != ERROR_SUCCESS) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_QUERYVALUE_FAILED, value, error_string(GetLastError()), 0);
    HeapFree(GetProcessHeap(), 0, *env);
    *env = 0;
    *envlen = 0;
    return 4;
  }

  return 0;
}


int get_string(HKEY key, TCHAR *value, TCHAR *data, unsigned long datalen, bool expand, bool sanitise, bool must_exist) {
  TCHAR *buffer = (TCHAR *) HeapAlloc(GetProcessHeap(), 0, datalen);
  if (! buffer) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, value, _T("get_string()"), 0);
    return 1;
  }

  ZeroMemory(data, datalen);

  unsigned long type = REG_EXPAND_SZ;
  unsigned long buflen = datalen;

  unsigned long ret = RegQueryValueEx(key, value, 0, &type, (unsigned char *) buffer, &buflen);
  if (ret != ERROR_SUCCESS) {
    unsigned long error = GetLastError();
    HeapFree(GetProcessHeap(), 0, buffer);

    if (ret == ERROR_FILE_NOT_FOUND) {
      if (! must_exist) return 0;
    }

    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_QUERYVALUE_FAILED, value, error_string(error), 0);
    return 2;
  }

  /* Paths aren't allowed to contain quotes. */
  if (sanitise) PathUnquoteSpaces(buffer);

  /* Do we want to expand the string? */
  if (! expand) {
    if (type == REG_EXPAND_SZ) type = REG_SZ;
  }

  /* Technically we shouldn't expand environment strings from REG_SZ values */
  if (type != REG_EXPAND_SZ) {
    memmove(data, buffer, buflen);
    HeapFree(GetProcessHeap(), 0, buffer);
    return 0;
  }

  ret = ExpandEnvironmentStrings((TCHAR *) buffer, data, datalen);
  if (! ret || ret > datalen) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_EXPANDENVIRONMENTSTRINGS_FAILED, buffer, error_string(GetLastError()), 0);
    HeapFree(GetProcessHeap(), 0, buffer);
    return 3;
  }

  HeapFree(GetProcessHeap(), 0, buffer);
  return 0;
}

int get_string(HKEY key, TCHAR *value, TCHAR *data, unsigned long datalen, bool sanitise) {
  return get_string(key, value, data, datalen, false, sanitise, true);
}

int expand_parameter(HKEY key, TCHAR *value, TCHAR *data, unsigned long datalen, bool sanitise, bool must_exist) {
  return get_string(key, value, data, datalen, true, sanitise, must_exist);
}

int expand_parameter(HKEY key, TCHAR *value, TCHAR *data, unsigned long datalen, bool sanitise) {
  return expand_parameter(key, value, data, datalen, sanitise, true);
}

/*
  Sets a string in the registry.
  Returns: 0 if it was set.
           1 on error.
*/
int set_string(HKEY key, TCHAR *value, TCHAR *string, bool expand) {
  unsigned long type = expand ? REG_EXPAND_SZ : REG_SZ;
  if (RegSetValueEx(key, value, 0, type, (const unsigned char *) string, (unsigned long) (_tcslen(string) + 1) * sizeof(TCHAR)) == ERROR_SUCCESS) return 0;
  log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_SETVALUE_FAILED, value, error_string(GetLastError()), 0);
  return 1;
}

int set_string(HKEY key, TCHAR *value, TCHAR *string) {
  return set_string(key, value, string, false);
  return 1;
}

int set_expand_string(HKEY key, TCHAR *value, TCHAR *string) {
  return set_string(key, value, string, true);
  return 1;
}

/*
  Set an unsigned long in the registry.
  Returns: 0 if it was set.
           1 on error.
*/
int set_number(HKEY key, TCHAR *value, unsigned long number) {
  if (RegSetValueEx(key, value, 0, REG_DWORD, (const unsigned char *) &number, sizeof(number)) == ERROR_SUCCESS) return 0;
  log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_SETVALUE_FAILED, value, error_string(GetLastError()), 0);
  return 1;
}

/*
  Query an unsigned long from the registry.
  Returns:  1 if a number was retrieved.
            0 if none was found and must_exist is false.
           -1 if none was found and must_exist is true.
           -2 otherwise.
*/
int get_number(HKEY key, TCHAR *value, unsigned long *number, bool must_exist) {
  unsigned long type = REG_DWORD;
  unsigned long number_len = sizeof(unsigned long);

  int ret = RegQueryValueEx(key, value, 0, &type, (unsigned char *) number, &number_len);
  if (ret == ERROR_SUCCESS) return 1;

  if (ret == ERROR_FILE_NOT_FOUND) {
    if (! must_exist) return 0;
  }

  log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_QUERYVALUE_FAILED, value, error_string(GetLastError()), 0);
  if (ret == ERROR_FILE_NOT_FOUND) return -1;

  return -2;
}

int get_number(HKEY key, TCHAR *value, unsigned long *number) {
  return get_number(key, value, number, true);
}

/* Replace NULL with CRLF. Leave NULL NULL as the end marker. */
int format_double_null(TCHAR *dn, unsigned long dnlen, TCHAR **formatted, unsigned long *newlen) {
  unsigned long i, j;
  *newlen = dnlen;

  if (! *newlen) {
    *formatted = 0;
    return 0;
  }

  for (i = 0; i < dnlen; i++) if (! dn[i] && dn[i + 1]) ++*newlen;

  *formatted = (TCHAR *) HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, *newlen * sizeof(TCHAR));
  if (! *formatted) {
    *newlen = 0;
    return 1;
  }

  for (i = 0, j = 0; i < dnlen; i++) {
    (*formatted)[j] = dn[i];
    if (! dn[i]) {
      if (dn[i + 1]) {
        (*formatted)[j] = _T('\r');
        (*formatted)[++j] = _T('\n');
      }
    }
    j++;
  }

  return 0;
}

/* Strip CR and replace LF with NULL. */
int unformat_double_null(TCHAR *dn, unsigned long dnlen, TCHAR **unformatted, unsigned long *newlen) {
  unsigned long i, j;
  *newlen = 0;

  if (! dnlen) {
    *unformatted = 0;
    return 0;
  }

  for (i = 0; i < dnlen; i++) if (dn[i] != _T('\r')) ++*newlen;

  /* Skip blank lines. */
  for (i = 0; i < dnlen; i++) {
    if (dn[i] == _T('\r') && dn[i + 1] == _T('\n')) {
      /* This is the last CRLF. */
      if (i >= dnlen - 2) break;

      /*
        Strip at the start of the block or if the next characters are
        CRLF too.
      */
      if (! i || (dn[i + 2] == _T('\r') && dn[i + 3] == _T('\n'))) {
        for (j = i + 2; j < dnlen; j++) dn[j - 2] = dn[j];
        dn[dnlen--] = _T('\0');
        dn[dnlen--] = _T('\0');
        i--;
        --*newlen;
      }
    }
  }

  /* Must end with two NULLs. */
  *newlen += 2;

  *unformatted = (TCHAR *) HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, *newlen * sizeof(TCHAR));
  if (! *unformatted) return 1;

  for (i = 0, j = 0; i < dnlen; i++) {
    if (dn[i] == _T('\r')) continue;
    if (dn[i] == _T('\n')) (*unformatted)[j] = _T('\0');
    else (*unformatted)[j] = dn[i];
    j++;
  }

  return 0;
}

void override_milliseconds(TCHAR *service_name, HKEY key, TCHAR *value, unsigned long *buffer, unsigned long default_value, unsigned long event) {
  unsigned long type = REG_DWORD;
  unsigned long buflen = sizeof(unsigned long);
  bool ok = false;
  unsigned long ret = RegQueryValueEx(key, value, 0, &type, (unsigned char *) buffer, &buflen);
  if (ret != ERROR_SUCCESS) {
    if (ret != ERROR_FILE_NOT_FOUND) {
      if (type != REG_DWORD) {
        TCHAR milliseconds[16];
        _sntprintf_s(milliseconds, _countof(milliseconds), _TRUNCATE, _T("%lu"), default_value);
        log_event(EVENTLOG_WARNING_TYPE, event, service_name, value, milliseconds, 0);
      }
      else log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_QUERYVALUE_FAILED, value, error_string(GetLastError()), 0);
    }
  }
  else ok = true;

  if (! ok) *buffer = default_value;
}

HKEY open_registry(const TCHAR *service_name, const TCHAR *sub, REGSAM sam) {
  /* Get registry */
  TCHAR registry[KEY_LENGTH];
  HKEY key;
  int ret;

  if (sub) ret = _sntprintf_s(registry, _countof(registry), _TRUNCATE, NSSM_REGISTRY _T("\\%s"), service_name, sub);
  else ret = _sntprintf_s(registry, _countof(registry), _TRUNCATE, NSSM_REGISTRY, service_name);
  if (ret < 0) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, _T("NSSM_REGISTRY"), _T("open_registry()"), 0);
    return 0;
  }

  if (sam & KEY_WRITE) {
    if (RegCreateKeyEx(HKEY_LOCAL_MACHINE, registry, 0, 0, REG_OPTION_NON_VOLATILE, sam, 0, &key, 0) != ERROR_SUCCESS) {
      log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OPENKEY_FAILED, registry, error_string(GetLastError()), 0);
      return 0;
    }
  }
  else {
    if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, registry, 0, sam, &key) != ERROR_SUCCESS) {
      log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OPENKEY_FAILED, registry, error_string(GetLastError()), 0);
      return 0;
    }
  }

  return key;
}

HKEY open_registry(const TCHAR *service_name, REGSAM sam) {
  return open_registry(service_name, 0, sam);
}

int get_io_parameters(nssm_service_t *service, HKEY key) {
  /* stdin */
  if (get_createfile_parameters(key, NSSM_REG_STDIN, service->stdin_path, &service->stdin_sharing, NSSM_STDIN_SHARING, &service->stdin_disposition, NSSM_STDIN_DISPOSITION, &service->stdin_flags, NSSM_STDIN_FLAGS)) {
    service->stdin_sharing = service->stdin_disposition = service->stdin_flags = 0;
    ZeroMemory(service->stdin_path, _countof(service->stdin_path) * sizeof(TCHAR));
    return 1;
  }

  /* stdout */
  if (get_createfile_parameters(key, NSSM_REG_STDOUT, service->stdout_path, &service->stdout_sharing, NSSM_STDOUT_SHARING, &service->stdout_disposition, NSSM_STDOUT_DISPOSITION, &service->stdout_flags, NSSM_STDOUT_FLAGS)) {
    service->stdout_sharing = service->stdout_disposition = service->stdout_flags = 0;
    ZeroMemory(service->stdout_path, _countof(service->stdout_path) * sizeof(TCHAR));
    return 2;
  }

  /* stderr */
  if (get_createfile_parameters(key, NSSM_REG_STDERR, service->stderr_path, &service->stderr_sharing, NSSM_STDERR_SHARING, &service->stderr_disposition, NSSM_STDERR_DISPOSITION, &service->stderr_flags, NSSM_STDERR_FLAGS)) {
    service->stderr_sharing = service->stderr_disposition = service->stderr_flags = 0;
    ZeroMemory(service->stderr_path, _countof(service->stderr_path) * sizeof(TCHAR));
    return 3;
  }

  return 0;
}

int get_parameters(nssm_service_t *service, STARTUPINFO *si) {
  unsigned long ret;

  /* Try to open the registry */
  HKEY key = open_registry(service->name, KEY_READ);
  if (! key) return 1;

  /* Don't expand parameters when retrieving for the GUI. */
  bool expand = si ? true : false;

  /* Try to get executable file - MUST succeed */
  if (get_string(key, NSSM_REG_EXE, service->exe, sizeof(service->exe), expand, false, true)) {
    RegCloseKey(key);
    return 3;
  }

  /* Try to get flags - may fail and we don't care */
  if (get_string(key, NSSM_REG_FLAGS, service->flags, sizeof(service->flags), expand, false, true)) {
    log_event(EVENTLOG_WARNING_TYPE, NSSM_EVENT_NO_FLAGS, NSSM_REG_FLAGS, service->name, service->exe, 0);
    ZeroMemory(service->flags, sizeof(service->flags));
  }

  /* Try to get startup directory - may fail and we fall back to a default */
  if (get_string(key, NSSM_REG_DIR, service->dir, sizeof(service->dir), expand, true, true) || ! service->dir[0]) {
    _sntprintf_s(service->dir, _countof(service->dir), _TRUNCATE, _T("%s"), service->exe);
    strip_basename(service->dir);
    if (service->dir[0] == _T('\0')) {
      /* Help! */
      ret = GetWindowsDirectory(service->dir, sizeof(service->dir));
      if (! ret || ret > sizeof(service->dir)) {
        log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_NO_DIR_AND_NO_FALLBACK, NSSM_REG_DIR, service->name, 0);
        RegCloseKey(key);
        return 4;
      }
    }
    log_event(EVENTLOG_WARNING_TYPE, NSSM_EVENT_NO_DIR, NSSM_REG_DIR, service->name, service->dir, 0);
  }

  /* Try to get processor affinity - may fail. */
  TCHAR buffer[512];
  if (get_string(key, NSSM_REG_AFFINITY, buffer, sizeof(buffer), false, false, false) || ! buffer[0]) service->affinity = 0LL;
  else if (affinity_string_to_mask(buffer, &service->affinity)) {
    log_event(EVENTLOG_WARNING_TYPE, NSSM_EVENT_BOGUS_AFFINITY_MASK, service->name, buffer);
    service->affinity = 0LL;
  }
  else {
    DWORD_PTR affinity, system_affinity;

    if (GetProcessAffinityMask(GetCurrentProcess(), &affinity, &system_affinity)) {
      _int64 effective_affinity = service->affinity & system_affinity;
      if (effective_affinity != service->affinity) {
        TCHAR *system = 0;
        if (! affinity_mask_to_string(system_affinity, &system)) {
          TCHAR *effective = 0;
          if (! affinity_mask_to_string(effective_affinity, &effective)) {
            log_event(EVENTLOG_WARNING_TYPE, NSSM_EVENT_EFFECTIVE_AFFINITY_MASK, service->name, buffer, system, effective, 0);
          }
          HeapFree(GetProcessHeap(), 0, effective);
        }
        HeapFree(GetProcessHeap(), 0, system);
      }
    }
  }

  /* Try to get environment variables - may fail */
  get_environment(service->name, key, NSSM_REG_ENV, &service->env, &service->envlen);
  /* Environment variables to add to existing rather than replace - may fail. */
  get_environment(service->name, key, NSSM_REG_ENV_EXTRA, &service->env_extra, &service->env_extralen);

  /* Try to get priority - may fail. */
  unsigned long priority;
  if (get_number(key, NSSM_REG_PRIORITY, &priority, false) == 1) {
    if (priority == (priority & priority_mask())) service->priority = priority;
    else log_event(EVENTLOG_WARNING_TYPE, NSSM_EVENT_BOGUS_PRIORITY, service->name, NSSM_REG_PRIORITY, 0);
  }

  /* Try to get file rotation settings - may fail. */
  unsigned long rotate_files;
  if (get_number(key, NSSM_REG_ROTATE, &rotate_files, false) == 1) {
    if (rotate_files) service->rotate_files = true;
    else service->rotate_files = false;
  }
  else service->rotate_files = false;
  if (get_number(key, NSSM_REG_ROTATE_ONLINE, &rotate_files, false) == 1) {
    if (rotate_files) service->rotate_stdout_online = service->rotate_stderr_online = true;
    else service->rotate_stdout_online = service->rotate_stderr_online = false;
  }
  else service->rotate_stdout_online = service->rotate_stderr_online = false;
  if (get_number(key, NSSM_REG_ROTATE_SECONDS, &service->rotate_seconds, false) != 1) service->rotate_seconds = 0;
  if (get_number(key, NSSM_REG_ROTATE_BYTES_LOW, &service->rotate_bytes_low, false) != 1) service->rotate_bytes_low = 0;
  if (get_number(key, NSSM_REG_ROTATE_BYTES_HIGH, &service->rotate_bytes_high, false) != 1) service->rotate_bytes_high = 0;

  /* Try to get force new console setting - may fail. */
  if (get_number(key, NSSM_REG_NO_CONSOLE, &service->no_console, false) != 1) service->no_console = 0;

  /* Change to startup directory in case stdout/stderr are relative paths. */
  TCHAR cwd[PATH_LENGTH];
  GetCurrentDirectory(_countof(cwd), cwd);
  SetCurrentDirectory(service->dir);

  /* Try to get stdout and stderr */
  if (get_io_parameters(service, key)) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_GET_OUTPUT_HANDLES_FAILED, service->name, 0);
    RegCloseKey(key);
    SetCurrentDirectory(cwd);
    return 5;
  }

  /* Change back in case the startup directory needs to be deleted. */
  SetCurrentDirectory(cwd);

  /* Try to get mandatory restart delay */
  override_milliseconds(service->name, key, NSSM_REG_RESTART_DELAY, &service->restart_delay, 0, NSSM_EVENT_BOGUS_RESTART_DELAY);

  /* Try to get throttle restart delay */
  override_milliseconds(service->name, key, NSSM_REG_THROTTLE, &service->throttle_delay, NSSM_RESET_THROTTLE_RESTART, NSSM_EVENT_BOGUS_THROTTLE);

  /* Try to get service stop flags. */
  unsigned long type = REG_DWORD;
  unsigned long stop_method_skip;
  unsigned long buflen = sizeof(stop_method_skip);
  bool stop_ok = false;
  ret = RegQueryValueEx(key, NSSM_REG_STOP_METHOD_SKIP, 0, &type, (unsigned char *) &stop_method_skip, &buflen);
  if (ret != ERROR_SUCCESS) {
    if (ret != ERROR_FILE_NOT_FOUND) {
      if (type != REG_DWORD) {
        log_event(EVENTLOG_WARNING_TYPE, NSSM_EVENT_BOGUS_STOP_METHOD_SKIP, service->name, NSSM_REG_STOP_METHOD_SKIP, NSSM, 0);
      }
      else log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_QUERYVALUE_FAILED, NSSM_REG_STOP_METHOD_SKIP, error_string(GetLastError()), 0);
    }
  }
  else stop_ok = true;

  /* Try all methods except those requested to be skipped. */
  service->stop_method = ~0;
  if (stop_ok) service->stop_method &= ~stop_method_skip;

  /* Try to get kill delays - may fail. */
  override_milliseconds(service->name, key, NSSM_REG_KILL_CONSOLE_GRACE_PERIOD, &service->kill_console_delay, NSSM_KILL_CONSOLE_GRACE_PERIOD, NSSM_EVENT_BOGUS_KILL_CONSOLE_GRACE_PERIOD);
  override_milliseconds(service->name, key, NSSM_REG_KILL_WINDOW_GRACE_PERIOD, &service->kill_window_delay, NSSM_KILL_WINDOW_GRACE_PERIOD, NSSM_EVENT_BOGUS_KILL_WINDOW_GRACE_PERIOD);
  override_milliseconds(service->name, key, NSSM_REG_KILL_THREADS_GRACE_PERIOD, &service->kill_threads_delay, NSSM_KILL_THREADS_GRACE_PERIOD, NSSM_EVENT_BOGUS_KILL_THREADS_GRACE_PERIOD);

  /* Try to get default exit action. */
  bool default_action;
  service->default_exit_action = NSSM_EXIT_RESTART;
  TCHAR action_string[ACTION_LEN];
  if (! get_exit_action(service->name, 0, action_string, &default_action)) {
    for (int i = 0; exit_action_strings[i]; i++) {
      if (! _tcsnicmp((const TCHAR *) action_string, exit_action_strings[i], ACTION_LEN)) {
        service->default_exit_action = i;
        break;
      }
    }
  }

  /* Close registry */
  RegCloseKey(key);

  return 0;
}

/*
  Sets the string for the exit action corresponding to the exit code.

  ret is a pointer to an unsigned long containing the exit code.
  If ret is NULL, we retrieve the default exit action unconditionally.

  action is a buffer which receives the string.

  default_action is a pointer to a bool which is set to false if there
  was an explicit string for the given exit code, or true if we are
  returning the default action.

  Returns: 0 on success.
           1 on error.
*/
int get_exit_action(const TCHAR *service_name, unsigned long *ret, TCHAR *action, bool *default_action) {
  /* Are we returning the default action or a status-specific one? */
  *default_action = ! ret;

  /* Try to open the registry */
  HKEY key = open_registry(service_name, NSSM_REG_EXIT, KEY_READ);
  if (! key) return 1;

  unsigned long type = REG_SZ;
  unsigned long action_len = ACTION_LEN;

  TCHAR code[16];
  if (! ret) code[0] = _T('\0');
  else if (_sntprintf_s(code, _countof(code), _TRUNCATE, _T("%lu"), *ret) < 0) {
    RegCloseKey(key);
    return get_exit_action(service_name, 0, action, default_action);
  }
  if (RegQueryValueEx(key, code, 0, &type, (unsigned char *) action, &action_len) != ERROR_SUCCESS) {
    RegCloseKey(key);
    /* Try again with * as the key if an exit code was defined */
    if (ret) return get_exit_action(service_name, 0, action, default_action);
    return 0;
  }

  /* Close registry */
  RegCloseKey(key);

  return 0;
}
