#include "nssm.h"

extern const TCHAR *exit_action_strings[];

static int service_registry_path(const TCHAR *service_name, bool parameters, const TCHAR *sub, TCHAR *buffer, unsigned long buflen) {
  int ret;

  if (parameters) {
    if (sub) ret = _sntprintf_s(buffer, buflen, _TRUNCATE, NSSM_REGISTRY _T("\\") NSSM_REG_PARAMETERS _T("\\%s"), service_name, sub);
    else ret = _sntprintf_s(buffer, buflen, _TRUNCATE, NSSM_REGISTRY _T("\\") NSSM_REG_PARAMETERS, service_name);
  }
  else ret = _sntprintf_s(buffer, buflen, _TRUNCATE, NSSM_REGISTRY, service_name);

  return ret;
}

static long open_registry_key(const TCHAR *registry, REGSAM sam, HKEY *key, bool must_exist) {
  long error;

  if (sam & KEY_SET_VALUE) {
    error = RegCreateKeyEx(HKEY_LOCAL_MACHINE, registry, 0, 0, REG_OPTION_NON_VOLATILE, sam, 0, key, 0);
    if (error != ERROR_SUCCESS) {
      *key = 0;
      log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OPENKEY_FAILED, registry, error_string(GetLastError()), 0);
      return error;
    }
  }
  else {
    error = RegOpenKeyEx(HKEY_LOCAL_MACHINE, registry, 0, sam, key);
    if (error != ERROR_SUCCESS) {
      *key = 0;
      if (error != ERROR_FILE_NOT_FOUND || must_exist) log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OPENKEY_FAILED, registry, error_string(GetLastError()), 0);
    }
  }

  return error;
}

static HKEY open_registry_key(const TCHAR *registry, REGSAM sam, bool must_exist) {
  HKEY key;
  long error = open_registry_key(registry, sam, &key, must_exist);
  return key;
}

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
  const TCHAR *path = nssm_unquoted_imagepath();

  /* Try to register the module but don't worry so much on failure */
  RegSetValueEx(key, _T("EventMessageFile"), 0, REG_SZ, (const unsigned char *) path, (unsigned long) (_tcslen(path) +  1) * sizeof(TCHAR));
  unsigned long types = EVENTLOG_INFORMATION_TYPE | EVENTLOG_WARNING_TYPE | EVENTLOG_ERROR_TYPE;
  RegSetValueEx(key, _T("TypesSupported"), 0, REG_DWORD, (const unsigned char *) &types, sizeof(types));

  return 0;
}

long enumerate_registry_values(HKEY key, unsigned long *index, TCHAR *name, unsigned long namelen) {
  unsigned long type;
  unsigned long datalen = namelen;
  long error = RegEnumValue(key, *index, name, &datalen, 0, &type, 0, 0);
  if (error == ERROR_SUCCESS) ++*index;
  return error;
}

int create_parameters(nssm_service_t *service, bool editing) {
  /* Try to open the registry */
  HKEY key = open_registry(service->name, KEY_WRITE);
  if (! key) return 1;

  /* Remember parameters in case we need to delete them. */
  TCHAR registry[KEY_LENGTH];
  int ret = service_registry_path(service->name, true, 0, registry, _countof(registry));

  /* Try to create the parameters */
  if (set_expand_string(key, NSSM_REG_EXE, service->exe)) {
    if (ret > 0) RegDeleteKey(HKEY_LOCAL_MACHINE, registry);
    RegCloseKey(key);
    return 2;
  }
  if (set_expand_string(key, NSSM_REG_FLAGS, service->flags)) {
    if (ret > 0) RegDeleteKey(HKEY_LOCAL_MACHINE, registry);
    RegCloseKey(key);
    return 3;
  }
  if (set_expand_string(key, NSSM_REG_DIR, service->dir)) {
    if (ret > 0) RegDeleteKey(HKEY_LOCAL_MACHINE, registry);
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
  if (! service->kill_process_tree) set_number(key, NSSM_REG_KILL_PROCESS_TREE, 0);
  else if (editing) RegDeleteValue(key, NSSM_REG_KILL_PROCESS_TREE);
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
    if (service->stdout_copy_and_truncate) set_createfile_parameter(key, NSSM_REG_STDOUT, NSSM_REG_STDIO_COPY_AND_TRUNCATE, 1);
    else if (editing) delete_createfile_parameter(key, NSSM_REG_STDOUT, NSSM_REG_STDIO_COPY_AND_TRUNCATE);
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
    if (service->stderr_copy_and_truncate) set_createfile_parameter(key, NSSM_REG_STDERR, NSSM_REG_STDIO_COPY_AND_TRUNCATE, 1);
    else if (editing) delete_createfile_parameter(key, NSSM_REG_STDERR, NSSM_REG_STDIO_COPY_AND_TRUNCATE);
  }
  if (service->timestamp_log) set_number(key, NSSM_REG_TIMESTAMP_LOG, 1);
  else if (editing) RegDeleteValue(key, NSSM_REG_TIMESTAMP_LOG);
  if (service->hook_share_output_handles) set_number(key, NSSM_REG_HOOK_SHARE_OUTPUT_HANDLES, 1);
  else if (editing) RegDeleteValue(key, NSSM_REG_HOOK_SHARE_OUTPUT_HANDLES);
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
  if (service->rotate_delay != NSSM_ROTATE_DELAY) set_number(key, NSSM_REG_ROTATE_DELAY, service->rotate_delay);
  else if (editing) RegDeleteValue(key, NSSM_REG_ROTATE_DELAY);
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
  if (service_registry_path(service_name, true, NSSM_REG_EXIT, registry, _countof(registry)) < 0) {
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
  unsigned long envsize;

  *envlen = 0;

  /* Dummy test to find buffer size */
  unsigned long ret = RegQueryValueEx(key, value, 0, &type, NULL, &envsize);
  if (ret != ERROR_SUCCESS) {
    *env = 0;
    /* The service probably doesn't have any environment configured */
    if (ret == ERROR_FILE_NOT_FOUND) return 0;
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_QUERYVALUE_FAILED, value, error_string(ret), 0);
    return 1;
  }

  if (type != REG_MULTI_SZ) {
    log_event(EVENTLOG_WARNING_TYPE, NSSM_EVENT_INVALID_ENVIRONMENT_STRING_TYPE, value, service_name, 0);
    *env = 0;
    return 2;
  }

  /* Minimum usable environment would be A= NULL NULL. */
  if (envsize < 4 * sizeof(TCHAR)) {
    *env = 0;
    return 3;
  }

  /* Previously initialised? */
  if (*env) HeapFree(GetProcessHeap(), 0, *env);

  *env = (TCHAR *) HeapAlloc(GetProcessHeap(), 0, envsize);
  if (! *env) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, value, _T("get_environment()"), 0);
    return 4;
  }

  /* Actually get the strings. */
  ret = RegQueryValueEx(key, value, 0, &type, (unsigned char *) *env, &envsize);
  if (ret != ERROR_SUCCESS) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_QUERYVALUE_FAILED, value, error_string(ret), 0);
    HeapFree(GetProcessHeap(), 0, *env);
    *env = 0;
    return 5;
  }

  /* Value retrieved by RegQueryValueEx() is SIZE not COUNT. */
  *envlen = (unsigned long) environment_length(*env);

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
    HeapFree(GetProcessHeap(), 0, buffer);

    if (ret == ERROR_FILE_NOT_FOUND) {
      if (! must_exist) return 0;
    }

    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_QUERYVALUE_FAILED, value, error_string(ret), 0);
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
}

int set_expand_string(HKEY key, TCHAR *value, TCHAR *string) {
  return set_string(key, value, string, true);
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

  log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_QUERYVALUE_FAILED, value, error_string(ret), 0);
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

/* Strip CR and replace LF with NULL.  */
int unformat_double_null(TCHAR *formatted, unsigned long formattedlen, TCHAR **dn, unsigned long *newlen) {
  unsigned long i, j;
  *newlen = 0;

  /* Don't count trailing NULLs. */
  for (i = 0; i < formattedlen; i++) {
    if (! formatted[i]) {
      formattedlen = i;
      break;
    }
  }

  if (! formattedlen) {
    *dn = 0;
    return 0;
  }

  for (i = 0; i < formattedlen; i++) if (formatted[i] != _T('\r')) ++*newlen;

  /* Skip blank lines. */
  for (i = 0; i < formattedlen; i++) {
    if (formatted[i] == _T('\r') && formatted[i + 1] == _T('\n')) {
      /* This is the last CRLF. */
      if (i >= formattedlen - 2) break;

      /*
        Strip at the start of the block or if the next characters are
        CRLF too.
      */
      if (! i || (formatted[i + 2] == _T('\r') && formatted[i + 3] == _T('\n'))) {
        for (j = i + 2; j < formattedlen; j++) formatted[j - 2] = formatted[j];
        formatted[formattedlen--] = _T('\0');
        formatted[formattedlen--] = _T('\0');
        i--;
        --*newlen;
      }
    }
  }

  /* Must end with two NULLs. */
  *newlen += 2;

  *dn = (TCHAR *) HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, *newlen * sizeof(TCHAR));
  if (! *dn) return 1;

  for (i = 0, j = 0; i < formattedlen; i++) {
    if (formatted[i] == _T('\r')) continue;
    if (formatted[i] == _T('\n')) (*dn)[j] = _T('\0');
    else (*dn)[j] = formatted[i];
    j++;
  }

  return 0;
}

/* Copy a block. */
int copy_double_null(TCHAR *dn, unsigned long dnlen, TCHAR **newdn) {
  if (! newdn) return 1;

  *newdn = 0;
  if (! dn) return 0;

  *newdn = (TCHAR *) HeapAlloc(GetProcessHeap(), 0, dnlen * sizeof(TCHAR));
  if (! *newdn) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, _T("dn"), _T("copy_double_null()"), 0);
    return 2;
  }

  memmove(*newdn, dn, dnlen * sizeof(TCHAR));
  return 0;
}

/*
  Create a new block with all the strings of the first block plus a new string.
  The new string may be specified as <key> <delimiter> <value> and the keylen
  gives the offset into the string to compare against existing entries.
  If the key is already present its value will be overwritten in place.
  If the key is blank or empty the new block will still be allocated and have
  non-zero length.
*/
int append_to_double_null(TCHAR *dn, unsigned long dnlen, TCHAR **newdn, unsigned long *newlen, TCHAR *append, size_t keylen, bool case_sensitive) {
  if (! append || ! append[0]) return copy_double_null(dn, dnlen, newdn);
  size_t appendlen = _tcslen(append);
  int (*fn)(const TCHAR *, const TCHAR *, size_t) = (case_sensitive) ? _tcsncmp : _tcsnicmp;

  /* Identify the key, if any, or treat the whole string as the key. */
  TCHAR *key = 0;
  if (! keylen || keylen > appendlen) keylen = appendlen;
  key = (TCHAR *) HeapAlloc(GetProcessHeap(), 0, (keylen + 1) * sizeof(TCHAR));
  if (! key) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, _T("key"), _T("append_to_double_null()"), 0);
    return 1;
  }
  memmove(key, append, keylen * sizeof(TCHAR));
  key[keylen] = _T('\0');

  /* Find the length of the block not including any existing key. */
  size_t len = 0;
  TCHAR *s;
  for (s = dn; *s; s++) {
    if (fn(s, key, keylen)) len += _tcslen(s) + 1;
    for ( ; *s; s++);
  }

  /* Account for new entry. */
  len += _tcslen(append) + 1;

  /* Account for trailing NULL. */
  len++;

  /* Allocate a new block. */
  *newdn = (TCHAR *) HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, len * sizeof(TCHAR));
  if (! *newdn) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, _T("newdn"), _T("append_to_double_null()"), 0);
    HeapFree(GetProcessHeap(), 0, key);
    return 2;
  }

  /* Copy existing entries.*/
  *newlen = (unsigned long) len;
  TCHAR *t = *newdn;
  TCHAR *u;
  bool replaced = false;
  for (s = dn; *s; s++) {
    if (fn(s, key, keylen)) u = s;
    else {
      u = append;
      replaced = true;
    }
    len = _tcslen(u) + 1;
    memmove(t, u, len * sizeof(TCHAR));
    t += len;
    for ( ; *s; s++);
  }

  /* Add the entry if it wasn't already replaced.  The buffer was zeroed. */
  if (! replaced) memmove(t, append, _tcslen(append) * sizeof(TCHAR));

  HeapFree(GetProcessHeap(), 0, key);
  return 0;
}

/*
  Create a new block with all the string of the first block minus the given
  string.
  The keylen parameter gives the offset into the string to compare against
  existing entries.  If a substring of existing value matches the string to
  the given length it will be removed.
  If the last entry is removed the new block will still be allocated and
  have non-zero length.
*/
int remove_from_double_null(TCHAR *dn, unsigned long dnlen, TCHAR **newdn, unsigned long *newlen, TCHAR *remove, size_t keylen, bool case_sensitive) {
  if (! remove || !remove[0]) return copy_double_null(dn, dnlen, newdn);
  size_t removelen = _tcslen(remove);
  int (*fn)(const TCHAR *, const TCHAR *, size_t) = (case_sensitive) ? _tcsncmp : _tcsnicmp;

  /* Identify the key, if any, or treat the whole string as the key. */
  TCHAR *key = 0;
  if (! keylen || keylen > removelen) keylen = removelen;
  key = (TCHAR *) HeapAlloc(GetProcessHeap(), 0, (keylen + 1) * sizeof(TCHAR));
  if (! key) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, _T("key"), _T("remove_from_double_null()"), 0);
    return 1;
  }
  memmove(key, remove, keylen * sizeof(TCHAR));
  key[keylen] = _T('\0');

  /* Find the length of the block not including any existing key. */
  size_t len = 0;
  TCHAR *s;
  for (s = dn; *s; s++) {
    if (fn(s, key, keylen)) len += _tcslen(s) + 1;
    for ( ; *s; s++);
  }

  /* Account for trailing NULL. */
  if (++len < 2) len = 2;

  /* Allocate a new block. */
  *newdn = (TCHAR *) HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, len * sizeof(TCHAR));
  if (! *newdn) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, _T("newdn"), _T("remove_from_double_null()"), 0);
    HeapFree(GetProcessHeap(), 0, key);
    return 2;
  }

  /* Copy existing entries.*/
  *newlen = (unsigned long) len;
  TCHAR *t = *newdn;
  for (s = dn; *s; s++) {
    if (fn(s, key, keylen)) {
      len = _tcslen(s) + 1;
      memmove(t, s, len * sizeof(TCHAR));
      t += len;
    }
    for ( ; *s; s++);
  }

  HeapFree(GetProcessHeap(), 0, key);
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
      else log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_QUERYVALUE_FAILED, value, error_string(ret), 0);
    }
  }
  else ok = true;

  if (! ok) *buffer = default_value;
}

/* Open the key of the service itself Services\<service_name>. */
HKEY open_service_registry(const TCHAR *service_name, REGSAM sam, bool must_exist) {
  /* Get registry */
  TCHAR registry[KEY_LENGTH];
  if (service_registry_path(service_name, false, 0, registry, _countof(registry)) < 0) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, NSSM_REGISTRY, _T("open_service_registry()"), 0);
    return 0;
  }

  return open_registry_key(registry, sam, must_exist);
}

/* Open a subkey of the service Services\<service_name>\<sub>. */
long open_registry(const TCHAR *service_name, const TCHAR *sub, REGSAM sam, HKEY *key, bool must_exist) {
  /* Get registry */
  TCHAR registry[KEY_LENGTH];
  if (service_registry_path(service_name, true, sub, registry, _countof(registry)) < 0) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, NSSM_REGISTRY, _T("open_registry()"), 0);
    return 0;
  }

  return open_registry_key(registry, sam, key, must_exist);
}

HKEY open_registry(const TCHAR *service_name, const TCHAR *sub, REGSAM sam, bool must_exist) {
  HKEY key;
  long error = open_registry(service_name, sub, sam, &key, must_exist);
  return key;
}

HKEY open_registry(const TCHAR *service_name, const TCHAR *sub, REGSAM sam) {
  return open_registry(service_name, sub, sam, true);
}

HKEY open_registry(const TCHAR *service_name, REGSAM sam) {
  return open_registry(service_name, 0, sam, true);
}

int get_io_parameters(nssm_service_t *service, HKEY key) {
  /* stdin */
  if (get_createfile_parameters(key, NSSM_REG_STDIN, service->stdin_path, &service->stdin_sharing, NSSM_STDIN_SHARING, &service->stdin_disposition, NSSM_STDIN_DISPOSITION, &service->stdin_flags, NSSM_STDIN_FLAGS, 0)) {
    service->stdin_sharing = service->stdin_disposition = service->stdin_flags = 0;
    ZeroMemory(service->stdin_path, _countof(service->stdin_path) * sizeof(TCHAR));
    return 1;
  }

  /* stdout */
  if (get_createfile_parameters(key, NSSM_REG_STDOUT, service->stdout_path, &service->stdout_sharing, NSSM_STDOUT_SHARING, &service->stdout_disposition, NSSM_STDOUT_DISPOSITION, &service->stdout_flags, NSSM_STDOUT_FLAGS, &service->stdout_copy_and_truncate)) {
    service->stdout_sharing = service->stdout_disposition = service->stdout_flags = 0;
    ZeroMemory(service->stdout_path, _countof(service->stdout_path) * sizeof(TCHAR));
    return 2;
  }

  /* stderr */
  if (get_createfile_parameters(key, NSSM_REG_STDERR, service->stderr_path, &service->stderr_sharing, NSSM_STDERR_SHARING, &service->stderr_disposition, NSSM_STDERR_DISPOSITION, &service->stderr_flags, NSSM_STDERR_FLAGS, &service->stderr_copy_and_truncate)) {
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

  /* Try to get environment variables - may fail */
  get_environment(service->name, key, NSSM_REG_ENV, &service->env, &service->envlen);
  /* Environment variables to add to existing rather than replace - may fail. */
  get_environment(service->name, key, NSSM_REG_ENV_EXTRA, &service->env_extra, &service->env_extralen);

  /* Set environment if we are starting the service. */
  if (si) set_service_environment(service);

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

  /* Try to get priority - may fail. */
  unsigned long priority;
  if (get_number(key, NSSM_REG_PRIORITY, &priority, false) == 1) {
    if (priority == (priority & priority_mask())) service->priority = priority;
    else log_event(EVENTLOG_WARNING_TYPE, NSSM_EVENT_BOGUS_PRIORITY, service->name, NSSM_REG_PRIORITY, 0);
  }

  /* Try to get hook I/O sharing - may fail. */
  unsigned long hook_share_output_handles;
  if (get_number(key, NSSM_REG_HOOK_SHARE_OUTPUT_HANDLES, &hook_share_output_handles, false) == 1) {
    if (hook_share_output_handles) service->hook_share_output_handles = true;
    else service->hook_share_output_handles = false;
  }
  else hook_share_output_handles = false;
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
  /* Log timestamping requires a logging thread.*/
  unsigned long timestamp_log;
  if (get_number(key, NSSM_REG_TIMESTAMP_LOG, &timestamp_log, false) == 1) {
    if (timestamp_log) service->timestamp_log = true;
    else service->timestamp_log = false;
  }
  else service->timestamp_log = false;

  /* Hook I/O sharing and online rotation need a pipe. */
  service->use_stdout_pipe = service->rotate_stdout_online || service->timestamp_log || hook_share_output_handles;
  service->use_stderr_pipe = service->rotate_stderr_online || service->timestamp_log || hook_share_output_handles;
  if (get_number(key, NSSM_REG_ROTATE_SECONDS, &service->rotate_seconds, false) != 1) service->rotate_seconds = 0;
  if (get_number(key, NSSM_REG_ROTATE_BYTES_LOW, &service->rotate_bytes_low, false) != 1) service->rotate_bytes_low = 0;
  if (get_number(key, NSSM_REG_ROTATE_BYTES_HIGH, &service->rotate_bytes_high, false) != 1) service->rotate_bytes_high = 0;
  override_milliseconds(service->name, key, NSSM_REG_ROTATE_DELAY, &service->rotate_delay, NSSM_ROTATE_DELAY, NSSM_EVENT_BOGUS_THROTTLE);

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
      else log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_QUERYVALUE_FAILED, NSSM_REG_STOP_METHOD_SKIP, error_string(ret), 0);
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

  /* Try to get process tree settings - may fail. */
  unsigned long kill_process_tree;
  if (get_number(key, NSSM_REG_KILL_PROCESS_TREE, &kill_process_tree, false) == 1) {
    if (kill_process_tree) service->kill_process_tree = true;
    else service->kill_process_tree = false;
  }
  else service->kill_process_tree = true;

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

int set_hook(const TCHAR *service_name, const TCHAR *hook_event, const TCHAR *hook_action, TCHAR *cmd) {
  /* Try to open the registry */
  TCHAR registry[KEY_LENGTH];
  if (_sntprintf_s(registry, _countof(registry), _TRUNCATE, _T("%s\\%s"), NSSM_REG_HOOK, hook_event) < 0) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, _T("hook registry"), _T("set_hook()"), 0);
    return 1;
  }

  HKEY key;
  long error;

  /* Don't create keys needlessly. */
  if (! _tcslen(cmd)) {
    key = open_registry(service_name, registry, KEY_READ, false);
    if (! key) return 0;
    error = RegQueryValueEx(key, hook_action, 0, 0, 0, 0);
    RegCloseKey(key);
    if (error == ERROR_FILE_NOT_FOUND) return 0;
  }

  key = open_registry(service_name, registry, KEY_WRITE);
  if (! key) return 1;

  int ret = 1;
  if (_tcslen(cmd)) ret = set_string(key, (TCHAR *) hook_action, cmd, true);
  else {
    error = RegDeleteValue(key, hook_action);
    if (error == ERROR_SUCCESS || error == ERROR_FILE_NOT_FOUND) ret = 0;
  }

  /* Close registry */
  RegCloseKey(key);

  return ret;
}

int get_hook(const TCHAR *service_name, const TCHAR *hook_event, const TCHAR *hook_action, TCHAR *buffer, unsigned long buflen) {
  /* Try to open the registry */
  TCHAR registry[KEY_LENGTH];
  if (_sntprintf_s(registry, _countof(registry), _TRUNCATE, _T("%s\\%s"), NSSM_REG_HOOK, hook_event) < 0) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, _T("hook registry"), _T("get_hook()"), 0);
    return 1;
  }
  HKEY key;
  long error = open_registry(service_name, registry, KEY_READ, &key, false);
  if (! key) {
    if (error == ERROR_FILE_NOT_FOUND) {
      ZeroMemory(buffer, buflen);
      return 0;
    }
    return 1;
  }

  int ret = expand_parameter(key, (TCHAR *) hook_action, buffer, buflen, true, false);

  /* Close registry */
  RegCloseKey(key);

  return ret;
}
