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
