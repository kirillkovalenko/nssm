#include "nssm.h"

extern const char *exit_action_strings[];

int create_messages() {
  HKEY key;

  char registry[KEY_LENGTH];
  if (_snprintf_s(registry, sizeof(registry), _TRUNCATE, "SYSTEM\\CurrentControlSet\\Services\\EventLog\\Application\\%s", NSSM) < 0) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, "eventlog registry", "create_messages()", 0);
    return 1;
  }

  if (RegCreateKeyEx(HKEY_LOCAL_MACHINE, registry, 0, 0, REG_OPTION_NON_VOLATILE, KEY_WRITE, 0, &key, 0) != ERROR_SUCCESS) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OPENKEY_FAILED, registry, error_string(GetLastError()), 0);
    return 2;
  }

  /* Get path of this program */
  char path[MAX_PATH];
  GetModuleFileName(0, path, MAX_PATH);

  /* Try to register the module but don't worry so much on failure */
  RegSetValueEx(key, "EventMessageFile", 0, REG_SZ, (const unsigned char *) path, (unsigned long) strlen(path) + 1);
  unsigned long types = EVENTLOG_INFORMATION_TYPE | EVENTLOG_WARNING_TYPE | EVENTLOG_ERROR_TYPE;
  RegSetValueEx(key, "TypesSupported", 0, REG_DWORD, (const unsigned char *) &types, sizeof(types));

  return 0;
}

int create_parameters(nssm_service_t *service) {
  /* Get registry */
  char registry[KEY_LENGTH];
  if (_snprintf_s(registry, sizeof(registry), _TRUNCATE, NSSM_REGISTRY, service->name) < 0) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, "NSSM_REGISTRY", "create_parameters()", 0);
    return 1;
  }

  /* Try to open the registry */
  HKEY key;
  if (RegCreateKeyEx(HKEY_LOCAL_MACHINE, registry, 0, 0, REG_OPTION_NON_VOLATILE, KEY_WRITE, 0, &key, 0) != ERROR_SUCCESS) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OPENKEY_FAILED, registry, error_string(GetLastError()), 0);
    return 2;
  }

  /* Try to create the parameters */
  if (set_expand_string(key, NSSM_REG_EXE, service->exe)) {
    RegDeleteKey(HKEY_LOCAL_MACHINE, NSSM_REGISTRY);
    RegCloseKey(key);
    return 3;
  }
  if (set_expand_string(key, NSSM_REG_FLAGS, service->flags)) {
    RegDeleteKey(HKEY_LOCAL_MACHINE, NSSM_REGISTRY);
    RegCloseKey(key);
    return 4;
  }
  if (set_expand_string(key, NSSM_REG_DIR, service->dir)) {
    RegDeleteKey(HKEY_LOCAL_MACHINE, NSSM_REGISTRY);
    RegCloseKey(key);
    return 5;
  }

  /* Other non-default parameters. May fail. */
  unsigned long stop_method_skip = ~service->stop_method;
  if (stop_method_skip) set_number(key, NSSM_REG_STOP_METHOD_SKIP, stop_method_skip);
  if (service->default_exit_action < NSSM_NUM_EXIT_ACTIONS) create_exit_action(service->name, exit_action_strings[service->default_exit_action]);
  if (service->throttle_delay != NSSM_RESET_THROTTLE_RESTART) set_number(key, NSSM_REG_THROTTLE, service->throttle_delay);
  if (service->kill_console_delay != NSSM_KILL_CONSOLE_GRACE_PERIOD) set_number(key, NSSM_REG_KILL_CONSOLE_GRACE_PERIOD, service->kill_console_delay);
  if (service->kill_window_delay != NSSM_KILL_WINDOW_GRACE_PERIOD) set_number(key, NSSM_REG_KILL_WINDOW_GRACE_PERIOD, service->kill_window_delay);
  if (service->kill_threads_delay != NSSM_KILL_THREADS_GRACE_PERIOD) set_number(key, NSSM_REG_KILL_THREADS_GRACE_PERIOD, service->kill_threads_delay);
  if (service->stdin_path[0]) set_expand_string(key, NSSM_REG_STDIN, service->stdin_path);
  if (service->stdout_path[0]) set_expand_string(key, NSSM_REG_STDOUT, service->stdout_path);
  if (service->stderr_path[0]) set_expand_string(key, NSSM_REG_STDERR, service->stderr_path);

  /* Close registry. */
  RegCloseKey(key);

  return 0;
}

int create_exit_action(char *service_name, const char *action_string) {
  /* Get registry */
  char registry[KEY_LENGTH];
  if (_snprintf_s(registry, sizeof(registry), _TRUNCATE, NSSM_REGISTRY "\\%s", service_name, NSSM_REG_EXIT) < 0) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, "NSSM_REG_EXIT", "create_exit_action()", 0);
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
  if (disposition == REG_OPENED_EXISTING_KEY) {
    RegCloseKey(key);
    return 0;
  }

  /* Create the default value */
  if (RegSetValueEx(key, 0, 0, REG_SZ, (const unsigned char *) action_string, (unsigned long) strlen(action_string) + 1) != ERROR_SUCCESS) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_SETVALUE_FAILED, NSSM_REG_EXIT, error_string(GetLastError()), 0);
    RegCloseKey(key);
    return 3;
  }

  /* Close registry */
  RegCloseKey(key);

  return 0;
}

int set_environment(char *service_name, HKEY key, char *value, char **env, unsigned long *envlen) {
  unsigned long type = REG_MULTI_SZ;

  /* Dummy test to find buffer size */
  unsigned long ret = RegQueryValueEx(key, value, 0, &type, NULL, envlen);
  if (ret != ERROR_SUCCESS) {
    *envlen = 0;
    /* The service probably doesn't have any environment configured */
    if (ret == ERROR_FILE_NOT_FOUND) return 0;
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_QUERYVALUE_FAILED, value, error_string(GetLastError()), 0);
    return 1;
  }

  if (type != REG_MULTI_SZ) {
    log_event(EVENTLOG_WARNING_TYPE, NSSM_EVENT_INVALID_ENVIRONMENT_STRING_TYPE, value, service_name, 0);
    return 2;
  }

  /* Probably not possible */
  if (! *envlen) return 0;

  /* Previously initialised? */
  if (*env) HeapFree(GetProcessHeap(), 0, *env);

  *env = (char *) HeapAlloc(GetProcessHeap(), 0, *envlen);
  if (! *env) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, value, "set_environment()", 0);
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

int expand_parameter(HKEY key, char *value, char *data, unsigned long datalen, bool sanitise, bool must_exist) {
  unsigned char *buffer = (unsigned char *) HeapAlloc(GetProcessHeap(), 0, datalen);
  if (! buffer) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, value, "expand_parameter()", 0);
    return 1;
  }

  ZeroMemory(data, datalen);

  unsigned long type = REG_EXPAND_SZ;
  unsigned long buflen = datalen;

  unsigned long ret = RegQueryValueEx(key, value, 0, &type, buffer, &buflen);
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
  if (sanitise) PathUnquoteSpaces((LPSTR) buffer);

  /* Technically we shouldn't expand environment strings from REG_SZ values */
  if (type != REG_EXPAND_SZ) {
    memmove(data, buffer, buflen);
    HeapFree(GetProcessHeap(), 0, buffer);
    return 0;
  }

  ret = ExpandEnvironmentStrings((char *) buffer, data, datalen);
  if (! ret || ret > datalen) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_EXPANDENVIRONMENTSTRINGS_FAILED, buffer, error_string(GetLastError()), 0);
    HeapFree(GetProcessHeap(), 0, buffer);
    return 3;
  }

  HeapFree(GetProcessHeap(), 0, buffer);
  return 0;
}

int expand_parameter(HKEY key, char *value, char *data, unsigned long datalen, bool sanitise) {
  return expand_parameter(key, value, data, datalen, sanitise, true);
}

/*
  Sets a string in the registry.
  Returns: 0 if it was set.
           1 on error.
*/
int set_expand_string(HKEY key, char *value, char *string) {
  if (RegSetValueEx(key, value, 0, REG_EXPAND_SZ, (const unsigned char *) string, (unsigned long) strlen(string) + 1) == ERROR_SUCCESS) return 0;
  log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_SETVALUE_FAILED, value, error_string(GetLastError()), 0);
  return 1;
}

/*
  Set an unsigned long in the registry.
  Returns: 0 if it was set.
           1 on error.
*/
int set_number(HKEY key, char *value, unsigned long number) {
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
int get_number(HKEY key, char *value, unsigned long *number, bool must_exist) {
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

int get_number(HKEY key, char *value, unsigned long *number) {
  return get_number(key, value, number, true);
}

void override_milliseconds(char *service_name, HKEY key, char *value, unsigned long *buffer, unsigned long default_value, unsigned long event) {
  unsigned long type = REG_DWORD;
  unsigned long buflen = sizeof(unsigned long);
  bool ok = false;
  unsigned long ret = RegQueryValueEx(key, value, 0, &type, (unsigned char *) buffer, &buflen);
  if (ret != ERROR_SUCCESS) {
    if (ret != ERROR_FILE_NOT_FOUND) {
      if (type != REG_DWORD) {
        char milliseconds[16];
        _snprintf_s(milliseconds, sizeof(milliseconds), _TRUNCATE, "%lu", default_value);
        log_event(EVENTLOG_WARNING_TYPE, event, service_name, value, milliseconds, 0);
      }
      else log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_QUERYVALUE_FAILED, value, error_string(GetLastError()), 0);
    }
  }
  else ok = true;

  if (! ok) *buffer = default_value;
}

int get_parameters(nssm_service_t *service, STARTUPINFO *si) {
  unsigned long ret;

  /* Get registry */
  char registry[KEY_LENGTH];
  if (_snprintf_s(registry, sizeof(registry), _TRUNCATE, NSSM_REGISTRY, service->name) < 0) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, "NSSM_REGISTRY", "get_parameters()", 0);
    return 1;
  }

  /* Try to open the registry */
  HKEY key;
  if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, registry, 0, KEY_READ, &key) != ERROR_SUCCESS) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OPENKEY_FAILED, registry, error_string(GetLastError()), 0);
    return 2;
  }

  /* Try to get executable file - MUST succeed */
  if (expand_parameter(key, NSSM_REG_EXE, service->exe, sizeof(service->exe), false)) {
    RegCloseKey(key);
    return 3;
  }

  /* Try to get flags - may fail and we don't care */
  if (expand_parameter(key, NSSM_REG_FLAGS, service->flags, sizeof(service->flags), false)) {
    log_event(EVENTLOG_WARNING_TYPE, NSSM_EVENT_NO_FLAGS, NSSM_REG_FLAGS, service->name, service->exe, 0);
    ZeroMemory(service->flags, sizeof(service->flags));
  }

  /* Try to get startup directory - may fail and we fall back to a default */
  if (expand_parameter(key, NSSM_REG_DIR, service->dir, sizeof(service->dir), true) || ! service->dir[0]) {
    /* Our buffers are defined to be long enough for this to be safe */
    size_t i;
    for (i = strlen(service->exe); i && service->exe[i] != '\\' && service->exe[i] != '/'; i--);
    if (i) {
      memmove(service->dir, service->exe, i);
      service->dir[i] = '\0';
    }
    else {
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

  /* Try to get environment variables - may fail */
  set_environment(service->name, key, NSSM_REG_ENV, &service->env, &service->envlen);
  /* Environment variables to add to existing rather than replace - may fail. */
  set_environment(service->name, key, NSSM_REG_ENV_EXTRA, &service->env_extra, &service->env_extralen);

  if (service->env_extra) {
    /* Append these to any other environment variables set. */
    if (service->env) {
      /* Append extra variables to configured variables. */
      unsigned long envlen = service->envlen + service->env_extralen - 1;
      char *env = (char *) HeapAlloc(GetProcessHeap(), 0, envlen);
      if (env) {
        memmove(env, service->env, service->envlen - 1);
        memmove(env + service->envlen - 1, service->env_extra, service->env_extralen);

        HeapFree(GetProcessHeap(), 0, service->env);
        service->env = env;
        service->envlen = envlen;
      }
      else log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, "environment", "get_parameters()", 0);
    }
    else {
      /* Append extra variables to our environment. */
      char *env, *s;
      size_t envlen, len;

      env = service->env_extra;
      len = 0;
      while (*env) {
        envlen = strlen(env) + 1;
        for (s = env; *s && *s != '='; s++);
        if (*s == '=') *s++ = '\0';
        if (! SetEnvironmentVariable(env, s)) log_event(EVENTLOG_WARNING_TYPE, NSSM_EVENT_SETENVIRONMENTVARIABLE_FAILED, env, s, error_string(GetLastError()));
        env += envlen;
      }
    }
  }

  /* Try to get stdout and stderr */
  if (get_output_handles(key, si)) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_GET_OUTPUT_HANDLES_FAILED, service->name, 0);
    RegCloseKey(key);
    return 5;
  }

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

  /* Close registry */
  RegCloseKey(key);

  return 0;
}

int get_exit_action(char *service_name, unsigned long *ret, unsigned char *action, bool *default_action) {
  /* Are we returning the default action or a status-specific one? */
  *default_action = ! ret;

  /* Get registry */
  char registry[KEY_LENGTH];
  if (_snprintf_s(registry, sizeof(registry), _TRUNCATE, NSSM_REGISTRY "\\%s", service_name, NSSM_REG_EXIT) < 0) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, "NSSM_REG_EXIT", "get_exit_action()", 0);
    return 1;
  }

  /* Try to open the registry */
  HKEY key;
  long error = RegOpenKeyEx(HKEY_LOCAL_MACHINE, registry, 0, KEY_READ, &key);
  if (error != ERROR_SUCCESS && error != ERROR_FILE_NOT_FOUND) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OPENKEY_FAILED, registry, error_string(GetLastError()), 0);
    return 2;
  }

  unsigned long type = REG_SZ;
  unsigned long action_len = ACTION_LEN;

  char code[64];
  if (! ret) code[0] = '\0';
  else if (_snprintf_s(code, sizeof(code), _TRUNCATE, "%lu", *ret) < 0) {
    RegCloseKey(key);
    return get_exit_action(service_name, 0, action, default_action);
  }
  if (RegQueryValueEx(key, code, 0, &type, action, &action_len) != ERROR_SUCCESS) {
    RegCloseKey(key);
    /* Try again with * as the key if an exit code was defined */
    if (ret) return get_exit_action(service_name, 0, action, default_action);
    return 0;
  }

  /* Close registry */
  RegCloseKey(key);

  return 0;
}
