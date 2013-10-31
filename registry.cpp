#include "nssm.h"

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
  RegSetValueEx(key, "TypesSupported", 0, REG_DWORD, /*XXX*/(PBYTE) &types, sizeof(types));

  return 0;
}

int create_parameters(char *service_name, char *exe, char *flags, char *dir) {
  /* Get registry */
  char registry[KEY_LENGTH];
  if (_snprintf_s(registry, sizeof(registry), _TRUNCATE, NSSM_REGISTRY, service_name) < 0) {
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
  if (RegSetValueEx(key, NSSM_REG_EXE, 0, REG_EXPAND_SZ, (const unsigned char *) exe, (unsigned long) strlen(exe) + 1) != ERROR_SUCCESS) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_SETVALUE_FAILED, NSSM_REG_EXE, error_string(GetLastError()), 0);
    RegDeleteKey(HKEY_LOCAL_MACHINE, NSSM_REGISTRY);
    RegCloseKey(key);
    return 3;
  }
  if (RegSetValueEx(key, NSSM_REG_FLAGS, 0, REG_EXPAND_SZ, (const unsigned char *) flags, (unsigned long) strlen(flags) + 1) != ERROR_SUCCESS) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_SETVALUE_FAILED, NSSM_REG_FLAGS, error_string(GetLastError()), 0);
    RegDeleteKey(HKEY_LOCAL_MACHINE, NSSM_REGISTRY);
    RegCloseKey(key);
    return 4;
  }
  if (RegSetValueEx(key, NSSM_REG_DIR, 0, REG_EXPAND_SZ, (const unsigned char *) dir, (unsigned long) strlen(dir) + 1) != ERROR_SUCCESS) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_SETVALUE_FAILED, NSSM_REG_DIR, error_string(GetLastError()), 0);
    RegDeleteKey(HKEY_LOCAL_MACHINE, NSSM_REGISTRY);
    RegCloseKey(key);
    return 5;
  }

  /* Close registry */
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

int set_environment(char *service_name, HKEY key, char **env) {
  unsigned long type = REG_MULTI_SZ;
  unsigned long envlen = 0;

  /* Dummy test to find buffer size */
  unsigned long ret = RegQueryValueEx(key, NSSM_REG_ENV, 0, &type, NULL, &envlen);
  if (ret != ERROR_SUCCESS) {
    /* The service probably doesn't have any environment configured */
    if (ret == ERROR_FILE_NOT_FOUND) return 0;
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_QUERYVALUE_FAILED, NSSM_REG_ENV, error_string(GetLastError()), 0);
    return 1;
  }

  if (type != REG_MULTI_SZ) {
    log_event(EVENTLOG_WARNING_TYPE, NSSM_EVENT_INVALID_ENVIRONMENT_STRING_TYPE, NSSM_REG_ENV, service_name, 0);
    return 2;
  }

  /* Probably not possible */
  if (! envlen) return 0;

  *env = (char *) HeapAlloc(GetProcessHeap(), 0, envlen);
  if (! *env) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, "environment registry", "set_environment()", 0);
    return 3;
  }

  /* Actually get the strings */
  ret = RegQueryValueEx(key, NSSM_REG_ENV, 0, &type, (unsigned char *) *env, &envlen);
  if (ret != ERROR_SUCCESS) {
    HeapFree(GetProcessHeap(), 0, *env);
    *env = 0;
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_QUERYVALUE_FAILED, NSSM_REG_ENV, error_string(GetLastError()), 0);
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

int get_parameters(char *service_name, char *exe, unsigned long exelen, char *flags, unsigned long flagslen, char *dir, unsigned long dirlen, char **env, unsigned long *throttle_delay, unsigned long *stop_method, STARTUPINFO *si) {
  unsigned long ret;

  /* Get registry */
  char registry[KEY_LENGTH];
  if (_snprintf_s(registry, sizeof(registry), _TRUNCATE, NSSM_REGISTRY, service_name) < 0) {
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
  if (expand_parameter(key, NSSM_REG_EXE, exe, exelen, false)) {
    RegCloseKey(key);
    return 3;
  }

  /* Try to get flags - may fail and we don't care */
  if (expand_parameter(key, NSSM_REG_FLAGS, flags, flagslen, false)) {
    log_event(EVENTLOG_WARNING_TYPE, NSSM_EVENT_NO_FLAGS, NSSM_REG_FLAGS, service_name, exe, 0);
    ZeroMemory(flags, flagslen);
  }

  /* Try to get startup directory - may fail and we fall back to a default */
  if (expand_parameter(key, NSSM_REG_DIR, dir, dirlen, true) || ! dir[0]) {
    /* Our buffers are defined to be long enough for this to be safe */
    size_t i;
    for (i = strlen(exe); i && exe[i] != '\\' && exe[i] != '/'; i--);
    if (i) {
      memmove(dir, exe, i);
      dir[i] = '\0';
    }
    else {
      /* Help! */
      ret = GetWindowsDirectory(dir, dirlen);
      if (! ret || ret > dirlen) {
        log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_NO_DIR_AND_NO_FALLBACK, NSSM_REG_DIR, service_name, 0);
        RegCloseKey(key);
        return 4;
      }
    }
    log_event(EVENTLOG_WARNING_TYPE, NSSM_EVENT_NO_DIR, NSSM_REG_DIR, service_name, dir, 0);
  }

  /* Try to get environment variables - may fail */
  set_environment(service_name, key, env);

  /* Try to get stdout and stderr */
  if (get_output_handles(key, si)) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_GET_OUTPUT_HANDLES_FAILED, service_name, 0);
    RegCloseKey(key);
    return 5;
  }

  /* Try to get throttle restart delay */
  unsigned long type = REG_DWORD;
  unsigned long buflen = sizeof(*throttle_delay);
  bool throttle_ok = false;
  ret = RegQueryValueEx(key, NSSM_REG_THROTTLE, 0, &type, (unsigned char *) throttle_delay, &buflen);
  if (ret != ERROR_SUCCESS) {
    if (ret != ERROR_FILE_NOT_FOUND) {
      if (type != REG_DWORD) {
        char milliseconds[16];
        _snprintf_s(milliseconds, sizeof(milliseconds), _TRUNCATE, "%lu", NSSM_RESET_THROTTLE_RESTART);
        log_event(EVENTLOG_WARNING_TYPE, NSSM_EVENT_BOGUS_THROTTLE, service_name, NSSM_REG_THROTTLE, milliseconds, 0);
      }
      else log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_QUERYVALUE_FAILED, NSSM_REG_THROTTLE, error_string(GetLastError()), 0);
    }
  }
  else throttle_ok = true;

  if (! throttle_ok) *throttle_delay = NSSM_RESET_THROTTLE_RESTART;

  /* Try to get service stop flags. */
  type = REG_DWORD;
  unsigned long stop_method_skip;
  buflen = sizeof(stop_method_skip);
  bool stop_ok = false;
  ret = RegQueryValueEx(key, NSSM_REG_STOP_METHOD_SKIP, 0, &type, (unsigned char *) &stop_method_skip, &buflen);
  if (ret != ERROR_SUCCESS) {
    if (ret != ERROR_FILE_NOT_FOUND) {
      if (type != REG_DWORD) {
        log_event(EVENTLOG_WARNING_TYPE, NSSM_EVENT_BOGUS_STOP_METHOD_SKIP, service_name, NSSM_REG_STOP_METHOD_SKIP, NSSM, 0);
      }
      else log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_QUERYVALUE_FAILED, NSSM_REG_STOP_METHOD_SKIP, error_string(GetLastError()), 0);
    }
  }
  else stop_ok = true;

  /* Try all methods except those requested to be skipped. */
  *stop_method = ~0;
  if (stop_ok) *stop_method &= ~stop_method_skip;

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
