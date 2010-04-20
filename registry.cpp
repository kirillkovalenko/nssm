#include "nssm.h"

int create_messages() {
  HKEY key;

  char registry[MAX_PATH];
  if (_snprintf(registry, sizeof(registry), "SYSTEM\\CurrentControlSet\\Services\\EventLog\\Application\\%s", NSSM) < 0) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, "eventlog registry", "create_messages()", 0);
    return 1;
  }

  if (RegCreateKeyEx(HKEY_LOCAL_MACHINE, registry, 0, 0, REG_OPTION_NON_VOLATILE, KEY_WRITE, 0, &key, 0) != ERROR_SUCCESS) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OPENKEY_FAILED, registry, GetLastError(), 0);
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
  char registry[MAX_PATH];
  if (_snprintf(registry, sizeof(registry), NSSM_REGISTRY, service_name) < 0) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, "NSSM_REGISTRY", "create_parameters()", 0);
    return 1;
  }

  /* Try to open the registry */
  HKEY key;
  if (RegCreateKeyEx(HKEY_LOCAL_MACHINE, registry, 0, 0, REG_OPTION_NON_VOLATILE, KEY_WRITE, 0, &key, 0) != ERROR_SUCCESS) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OPENKEY_FAILED, registry, GetLastError(), 0);
    return 2;
  }

  /* Try to create the parameters */
  if (RegSetValueEx(key, NSSM_REG_EXE, 0, REG_SZ, (const unsigned char *) exe, strlen(exe) + 1) != ERROR_SUCCESS) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_SETVALUE_FAILED, NSSM_REG_EXE, GetLastError(), 0);
    RegDeleteKey(HKEY_LOCAL_MACHINE, NSSM_REGISTRY);
    RegCloseKey(key);
    return 3;
  }
  if (RegSetValueEx(key, NSSM_REG_FLAGS, 0, REG_SZ, (const unsigned char *) flags, strlen(flags) + 1) != ERROR_SUCCESS) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_SETVALUE_FAILED, NSSM_REG_FLAGS, GetLastError(), 0);
    RegDeleteKey(HKEY_LOCAL_MACHINE, NSSM_REGISTRY);
    RegCloseKey(key);
    return 4;
  }
  if (RegSetValueEx(key, NSSM_REG_DIR, 0, REG_SZ, (const unsigned char *) dir, strlen(dir) + 1) != ERROR_SUCCESS) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_SETVALUE_FAILED, NSSM_REG_DIR, GetLastError(), 0);
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
  char registry[MAX_PATH];
  if (_snprintf(registry, sizeof(registry), NSSM_REGISTRY "\\%s", service_name, NSSM_REG_EXIT) < 0) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, "NSSM_REG_EXIT", "create_exit_action()", 0);
    return 1;
  }

  /* Try to open the registry */
  HKEY key;
  unsigned long disposition;
  if (RegCreateKeyEx(HKEY_LOCAL_MACHINE, registry, 0, 0, REG_OPTION_NON_VOLATILE, KEY_WRITE, 0, &key, &disposition) != ERROR_SUCCESS) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OPENKEY_FAILED, registry, GetLastError(), 0);
    return 2;
  }

  /* Do nothing if the key already existed */
  if (disposition == REG_OPENED_EXISTING_KEY) {
    RegCloseKey(key);
    return 0;
  }

  /* Create the default value */
  if (RegSetValueEx(key, 0, 0, REG_SZ, (const unsigned char *) action_string, strlen(action_string) + 1) != ERROR_SUCCESS) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_SETVALUE_FAILED, NSSM_REG_EXIT, GetLastError(), 0);
    RegCloseKey(key);
    return 3;
  }

  /* Close registry */
  RegCloseKey(key);

  return 0;
}

int get_parameters(char *service_name, char *exe, int exelen, char *flags, int flagslen, char *dir, int dirlen) {
  /* Get registry */
  char registry[MAX_PATH];
  if (_snprintf(registry, sizeof(registry), NSSM_REGISTRY, service_name) < 0) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, "NSSM_REGISTRY", "get_parameters()", 0);
    return 1;
  }

  /* Try to open the registry */
  HKEY key;
  if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, registry, 0, KEY_READ, &key) != ERROR_SUCCESS) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OPENKEY_FAILED, registry, GetLastError(), 0);
    return 2;
  }

  unsigned long type = REG_SZ;

  /* Try to get executable file - MUST succeed */
  if (RegQueryValueEx(key, NSSM_REG_EXE, 0, &type, (unsigned char *) exe, (unsigned long *) &exelen) != ERROR_SUCCESS) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_QUERYVALUE_FAILED, NSSM_REG_EXE, GetLastError(), 0);
    RegCloseKey(key);
    return 3;
  }

  /* Try to get flags - may fail */
  if (RegQueryValueEx(key, NSSM_REG_FLAGS, 0, &type, (unsigned char *) flags, (unsigned long *) &flagslen) != ERROR_SUCCESS) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_QUERYVALUE_FAILED, NSSM_REG_FLAGS, GetLastError(), 0);
    RegCloseKey(key);
    return 4;
  }

  /* Try to get startup directory - may fail */
  if (RegQueryValueEx(key, NSSM_REG_DIR, 0, &type, (unsigned char *) dir, (unsigned long *) &dirlen) != ERROR_SUCCESS) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_QUERYVALUE_FAILED, NSSM_REG_DIR, GetLastError(), 0);
    RegCloseKey(key);
    return 5;
  }

  /* Close registry */
  RegCloseKey(key);

  return 0;
}

int get_exit_action(char *service_name, unsigned long *ret, unsigned char *action) {
  /* Get registry */
  char registry[MAX_PATH];
  if (_snprintf(registry, sizeof(registry), NSSM_REGISTRY "\\%s", service_name, NSSM_REG_EXIT) < 0) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, "NSSM_REG_EXIT", "get_exit_action()", 0);
    return 1;
  }

  /* Try to open the registry */
  HKEY key;
  long error = RegOpenKeyEx(HKEY_LOCAL_MACHINE, registry, 0, KEY_READ, &key);
  if (error != ERROR_SUCCESS && error != ERROR_FILE_NOT_FOUND) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OPENKEY_FAILED, registry, GetLastError(), 0);
    return 2;
  }

  unsigned long type = REG_SZ;
  unsigned long action_len = ACTION_LEN;

  char code[64];
  if (! ret) code[0] = '\0';
  else if (_snprintf(code, sizeof(code), "%lu", *ret) < 0) {
    RegCloseKey(key);
    return get_exit_action(service_name, 0, action);
  }
  if (RegQueryValueEx(key, code, 0, &type, action, &action_len) != ERROR_SUCCESS) {
    RegCloseKey(key);
    /* Try again with * as the key if an exit code was defined */
    if (ret) return get_exit_action(service_name, 0, action);
    return 0;
  }

  /* Close registry */
  RegCloseKey(key);

  return 0;
}
