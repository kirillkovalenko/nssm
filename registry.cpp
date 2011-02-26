#include "nssm.h"

int create_messages() {
  HKEY key;

  char registry[KEY_LENGTH];
  if (_snprintf(registry, sizeof(registry), "SYSTEM\\CurrentControlSet\\Services\\EventLog\\Application\\%s", NSSM) < 0) {
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
  RegSetValueEx(key, "EventMessageFile", 0, REG_SZ, (const unsigned char *) path, strlen(path) + 1);
  unsigned long types = EVENTLOG_INFORMATION_TYPE | EVENTLOG_WARNING_TYPE | EVENTLOG_ERROR_TYPE;
  RegSetValueEx(key, "TypesSupported", 0, REG_DWORD, /*XXX*/(PBYTE) &types, sizeof(types));

  return 0;
}

int create_parameters(char *service_name, char *exe, char *flags, char *dir) {
  /* Get registry */
  char registry[KEY_LENGTH];
  if (_snprintf(registry, sizeof(registry), NSSM_REGISTRY, service_name) < 0) {
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
  if (RegSetValueEx(key, NSSM_REG_EXE, 0, REG_EXPAND_SZ, (const unsigned char *) exe, strlen(exe) + 1) != ERROR_SUCCESS) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_SETVALUE_FAILED, NSSM_REG_EXE, error_string(GetLastError()), 0);
    RegDeleteKey(HKEY_LOCAL_MACHINE, NSSM_REGISTRY);
    RegCloseKey(key);
    return 3;
  }
  if (RegSetValueEx(key, NSSM_REG_FLAGS, 0, REG_EXPAND_SZ, (const unsigned char *) flags, strlen(flags) + 1) != ERROR_SUCCESS) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_SETVALUE_FAILED, NSSM_REG_FLAGS, error_string(GetLastError()), 0);
    RegDeleteKey(HKEY_LOCAL_MACHINE, NSSM_REGISTRY);
    RegCloseKey(key);
    return 4;
  }
  if (RegSetValueEx(key, NSSM_REG_DIR, 0, REG_EXPAND_SZ, (const unsigned char *) dir, strlen(dir) + 1) != ERROR_SUCCESS) {
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
  if (_snprintf(registry, sizeof(registry), NSSM_REGISTRY "\\%s", service_name, NSSM_REG_EXIT) < 0) {
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
  if (RegSetValueEx(key, 0, 0, REG_SZ, (const unsigned char *) action_string, strlen(action_string) + 1) != ERROR_SUCCESS) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_SETVALUE_FAILED, NSSM_REG_EXIT, error_string(GetLastError()), 0);
    RegCloseKey(key);
    return 3;
  }

  /* Close registry */
  RegCloseKey(key);

  return 0;
}

int expand_parameter(HKEY key, char *value, char *data, unsigned long datalen, bool sanitise) {
  unsigned char *buffer = (unsigned char *) HeapAlloc(GetProcessHeap(), 0, datalen);
  if (! buffer) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, value, "expand_parameter()", 0);
    return 1;
  }

  unsigned long type = REG_EXPAND_SZ;
  unsigned long buflen = datalen;

  unsigned long ret = RegQueryValueEx(key, value, 0, &type, buffer, &buflen);
  if (ret != ERROR_SUCCESS) {
    if (ret != ERROR_FILE_NOT_FOUND) log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_QUERYVALUE_FAILED, value, error_string(GetLastError()), 0);
    HeapFree(GetProcessHeap(), 0, buffer);
    return 2;
  }

  /* Paths aren't allowed to contain quotes. */
  if (sanitise) PathUnquoteSpaces((LPSTR) buffer);

  ZeroMemory(data, datalen);

  /* Technically we shouldn't expand environment strings from REG_SZ values */
  if (type != REG_EXPAND_SZ) {
    memmove(data, buffer, buflen);
    HeapFree(GetProcessHeap(), 0, buffer);
    return 0;
  }

  ret = ExpandEnvironmentStrings((char *) buffer, data, datalen);
  if (! ret || ret > datalen) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_EXPANDENVIRONMENTSTRINGS_FAILED, value, buffer, error_string(GetLastError()), 0);
    HeapFree(GetProcessHeap(), 0, buffer);
    return 3;
  }

  HeapFree(GetProcessHeap(), 0, buffer);
  return 0;
}

int get_parameters(char *service_name, char *exe, int exelen, char *flags, int flagslen, char *dir, int dirlen, unsigned long *throttle_delay) {
  unsigned long ret;

  /* Get registry */
  char registry[KEY_LENGTH];
  if (_snprintf(registry, sizeof(registry), NSSM_REGISTRY, service_name) < 0) {
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

  /* Try to get throttle restart delay */
  unsigned long type = REG_DWORD;
  unsigned long buflen = sizeof(*throttle_delay);
  bool throttle_ok = false;
  ret = RegQueryValueEx(key, NSSM_REG_THROTTLE, 0, &type, (unsigned char *) throttle_delay, &buflen);
  if (ret != ERROR_SUCCESS) {
    if (ret != ERROR_FILE_NOT_FOUND) {
      if (type != REG_DWORD) {
        char milliseconds[16];
        _snprintf(milliseconds, sizeof(milliseconds), "%lu", NSSM_RESET_THROTTLE_RESTART);
        log_event(EVENTLOG_WARNING_TYPE, NSSM_EVENT_BOGUS_THROTTLE, service_name, NSSM_REG_THROTTLE, milliseconds, 0);
      }
      else log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_QUERYVALUE_FAILED, NSSM_REG_THROTTLE, error_string(GetLastError()), 0);
    }
  }
  else throttle_ok = true;

  if (! throttle_ok) *throttle_delay = NSSM_RESET_THROTTLE_RESTART;

  /* Close registry */
  RegCloseKey(key);

  return 0;
}

int get_exit_action(char *service_name, unsigned long *ret, unsigned char *action, bool *default_action) {
  /* Are we returning the default action or a status-specific one? */
  *default_action = ! ret;

  /* Get registry */
  char registry[KEY_LENGTH];
  if (_snprintf(registry, sizeof(registry), NSSM_REGISTRY "\\%s", service_name, NSSM_REG_EXIT) < 0) {
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
  else if (_snprintf(code, sizeof(code), "%lu", *ret) < 0) {
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
