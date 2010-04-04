#include "nssm.h"

int create_parameters(char *service_name, char *exe, char *flags, char *dir) {
  /* Get registry */
  char registry[MAX_PATH];
  if (_snprintf(registry, sizeof(registry), NSSM_REGISTRY, service_name) < 0) {
    eventprintf(EVENTLOG_ERROR_TYPE, "Out of memory for NSSM_REGISTRY in create_parameters()!");
    return 1;
  }

  /* Try to open the registry */
  HKEY key;
  if (RegCreateKeyEx(HKEY_LOCAL_MACHINE, registry, 0, 0, REG_OPTION_NON_VOLATILE, KEY_WRITE, 0, &key, 0) != ERROR_SUCCESS) {
    eventprintf(EVENTLOG_ERROR_TYPE, "Can't open service registry settings!", NSSM_REGISTRY);
    return 2;
  }

  /* Try to create the parameters */
  if (RegSetValueEx(key, NSSM_REG_EXE, 0, REG_SZ, (const unsigned char *) exe, strlen(exe) + 1) != ERROR_SUCCESS) {
    eventprintf(EVENTLOG_ERROR_TYPE, "Can't add registry value %s: %s", NSSM_REG_EXE, error_string(GetLastError()));
    RegDeleteKey(HKEY_LOCAL_MACHINE, NSSM_REGISTRY);
    RegCloseKey(key);
    return 3;
  }
  if (RegSetValueEx(key, NSSM_REG_FLAGS, 0, REG_SZ, (const unsigned char *) flags, strlen(flags) + 1) != ERROR_SUCCESS) {
    eventprintf(EVENTLOG_ERROR_TYPE, "Can't add registry value %s: %s", NSSM_REG_FLAGS, error_string(GetLastError()));
    RegDeleteKey(HKEY_LOCAL_MACHINE, NSSM_REGISTRY);
    RegCloseKey(key);
    return 4;
  }
  if (RegSetValueEx(key, NSSM_REG_DIR, 0, REG_SZ, (const unsigned char *) dir, strlen(dir) + 1) != ERROR_SUCCESS) {
    eventprintf(EVENTLOG_ERROR_TYPE, "Can't add registry value %s: %s", NSSM_REG_DIR, error_string(GetLastError()));
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
    eventprintf(EVENTLOG_ERROR_TYPE, "Out of memory for NSSM_REG_EXIT in create_exit_action()!");
    return 1;
  }

  /* Try to open the registry */
  HKEY key;
  unsigned long disposition;
  if (RegCreateKeyEx(HKEY_LOCAL_MACHINE, registry, 0, 0, REG_OPTION_NON_VOLATILE, KEY_WRITE, 0, &key, &disposition) != ERROR_SUCCESS) {
    eventprintf(EVENTLOG_ERROR_TYPE, "Can't open service exit action registry settings!");
    return 2;
  }

  /* Do nothing if the key already existed */
  if (disposition == REG_OPENED_EXISTING_KEY) {
    RegCloseKey(key);
    return 0;
  }

  /* Create the default value */
  if (RegSetValueEx(key, 0, 0, REG_SZ, (const unsigned char *) action_string, strlen(action_string) + 1) != ERROR_SUCCESS) {
    eventprintf(EVENTLOG_ERROR_TYPE, "Can't add default registry value %s: %s", NSSM_REG_EXIT, error_string(GetLastError()));
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
    eventprintf(EVENTLOG_ERROR_TYPE, "Out of memory for NSSM_REGISTRY in get_parameters()!");
    return 1;
  }

  /* Try to open the registry */
  HKEY key;
  if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, registry, 0, KEY_READ, &key) != ERROR_SUCCESS) {
    eventprintf(EVENTLOG_ERROR_TYPE, "Can't open service registry settings!", NSSM_REGISTRY);
    return 2;
  }

  unsigned long type = REG_SZ;

  /* Try to get executable file - MUST succeed */
  if (RegQueryValueEx(key, NSSM_REG_EXE, 0, &type, (unsigned char *) exe, (unsigned long *) &exelen) != ERROR_SUCCESS) {
    eventprintf(EVENTLOG_ERROR_TYPE, "Can't get application path (registry value %s): %s", NSSM_REG_EXE, error_string(GetLastError()));
    RegCloseKey(key);
    return 3;
  }

  /* Try to get flags - may fail */
  if (RegQueryValueEx(key, NSSM_REG_FLAGS, 0, &type, (unsigned char *) flags, (unsigned long *) &flagslen) != ERROR_SUCCESS) {
    eventprintf(EVENTLOG_WARNING_TYPE, "Can't get application flags (registry value %s): %s", NSSM_REG_FLAGS, error_string(GetLastError()));
    RegCloseKey(key);
    return 4;
  }

  /* Try to get startup directory - may fail */
  if (RegQueryValueEx(key, NSSM_REG_DIR, 0, &type, (unsigned char *) dir, (unsigned long *) &dirlen) != ERROR_SUCCESS) {
    eventprintf(EVENTLOG_WARNING_TYPE, "Can't get application startup directory (registry value %s): %s", NSSM_REG_DIR, error_string(GetLastError()));
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
    eventprintf(EVENTLOG_ERROR_TYPE, "Out of memory for NSSM_REG_EXIT in get_exit_action()!");
    return 1;
  }

  /* Try to open the registry */
  HKEY key;
  long error = RegOpenKeyEx(HKEY_LOCAL_MACHINE, registry, 0, KEY_READ, &key);
  if (error != ERROR_SUCCESS && error != ERROR_FILE_NOT_FOUND) {
    eventprintf(EVENTLOG_ERROR_TYPE, "Can't open registry %s!", registry);
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
