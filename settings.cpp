#include "nssm.h"
/* XXX: (value && value->string) is probably bogus because value is probably never null */

/* Affinity. */
#define NSSM_AFFINITY_ALL _T("All")
/* Default value. */
#define NSSM_DEFAULT_STRING _T("Default")

extern const TCHAR *exit_action_strings[];
extern const TCHAR *startup_strings[];
extern const TCHAR *priority_strings[];
extern const TCHAR *hook_event_strings[];
extern const TCHAR *hook_action_strings[];

/* Does the parameter refer to the default value of the setting? */
static inline int is_default(const TCHAR *value) {
  return (str_equiv(value, NSSM_DEFAULT_STRING) || str_equiv(value, _T("*")) || ! value[0]);
}

/* What type of parameter is it parameter? */
static inline bool is_string_type(const unsigned long type) {
  return (type == REG_MULTI_SZ || type == REG_EXPAND_SZ || type == REG_SZ);
}
static inline bool is_numeric_type(const unsigned long type) {
  return (type == REG_DWORD);
}

static int value_from_string(const TCHAR *name, value_t *value, const TCHAR *string) {
  size_t len = _tcslen(string);
  if (! len++) {
    value->string = 0;
    return 0;
  }

  value->string = (TCHAR *) HeapAlloc(GetProcessHeap(), 0, len * sizeof(TCHAR));
  if (! value->string) {
    print_message(stderr, NSSM_MESSAGE_OUT_OF_MEMORY, name, _T("value_from_string()"));
    return -1;
  }

  if (_sntprintf_s(value->string, len, _TRUNCATE, _T("%s"), string) < 0) {
    HeapFree(GetProcessHeap(), 0, value->string);
    print_message(stderr, NSSM_MESSAGE_OUT_OF_MEMORY, name, _T("value_from_string()"));
    return -1;
  }

  return 1;
}

/* Functions to manage NSSM-specific settings in the registry. */
static int setting_set_number(const TCHAR *service_name, void *param, const TCHAR *name, void *default_value, value_t *value, const TCHAR *additional) {
  HKEY key = (HKEY) param;
  if (! key) return -1;

  unsigned long number;
  long error;

  /* Resetting to default? */
  if (! value || ! value->string) {
    error = RegDeleteValue(key, name);
    if (error == ERROR_SUCCESS || error == ERROR_FILE_NOT_FOUND) return 0;
    print_message(stderr, NSSM_MESSAGE_REGDELETEVALUE_FAILED, name, service_name, error_string(error));
    return -1;
  }
  if (str_number(value->string, &number)) return -1;

  if (default_value && number == PtrToUlong(default_value)) {
    error = RegDeleteValue(key, name);
    if (error == ERROR_SUCCESS || error == ERROR_FILE_NOT_FOUND) return 0;
    print_message(stderr, NSSM_MESSAGE_REGDELETEVALUE_FAILED, name, service_name, error_string(error));
    return -1;
  }

  if (set_number(key, (TCHAR *) name, number)) return -1;

  return 1;
}

static int setting_get_number(const TCHAR *service_name, void *param, const TCHAR *name, void *default_value, value_t *value, const TCHAR *additional) {
  HKEY key = (HKEY) param;
  return get_number(key, (TCHAR *) name, &value->numeric, false);
}

static int setting_set_string(const TCHAR *service_name, void *param, const TCHAR *name, void *default_value, value_t *value, const TCHAR *additional) {
  HKEY key = (HKEY) param;
  if (! key) return -1;

  long error;

  /* Resetting to default? */
  if (! value || ! value->string) {
    if (default_value) value->string = (TCHAR *) default_value;
    else {
      error = RegDeleteValue(key, name);
      if (error == ERROR_SUCCESS || error == ERROR_FILE_NOT_FOUND) return 0;
      print_message(stderr, NSSM_MESSAGE_REGDELETEVALUE_FAILED, name, service_name, error_string(error));
      return -1;
    }
  }
  if (default_value && _tcslen((TCHAR *) default_value) && str_equiv(value->string, (TCHAR *) default_value)) {
    error = RegDeleteValue(key, name);
    if (error == ERROR_SUCCESS || error == ERROR_FILE_NOT_FOUND) return 0;
    print_message(stderr, NSSM_MESSAGE_REGDELETEVALUE_FAILED, name, service_name, error_string(error));
    return -1;
  }

  if (set_expand_string(key, (TCHAR *) name, value->string)) return -1;

  return 1;
}

static int setting_get_string(const TCHAR *service_name, void *param, const TCHAR *name, void *default_value, value_t *value, const TCHAR *additional) {
  HKEY key = (HKEY) param;
  TCHAR buffer[VALUE_LENGTH];

  if (get_string(key, (TCHAR *) name, (TCHAR *) buffer, (unsigned long) sizeof(buffer), false, false, false)) return -1;

  return value_from_string(name, value, buffer);
}

static int setting_not_dumpable(const TCHAR *service_name, void *param, const TCHAR *name, void *default_value, value_t *value, const TCHAR *additional) {
  return 0;
}

static int setting_dump_string(const TCHAR *service_name, void *param, const TCHAR *name, const value_t *value, const TCHAR *additional) {
  TCHAR quoted_service_name[SERVICE_NAME_LENGTH * 2];
  TCHAR quoted_value[VALUE_LENGTH * 2];
  TCHAR quoted_additional[VALUE_LENGTH * 2];
  TCHAR quoted_nssm[EXE_LENGTH * 2];

  if (quote(service_name, quoted_service_name, _countof(quoted_service_name))) return 1;

  if (additional) {
    if (_tcslen(additional)) {
      if (quote(additional, quoted_additional, _countof(quoted_additional))) return 3;
    }
  else _sntprintf_s(quoted_additional, _countof(quoted_additional), _TRUNCATE, _T("\"\""));
  }
  else quoted_additional[0] = _T('\0');

  unsigned long type = (unsigned long) param;
  if (is_string_type(type)) {
    if (_tcslen(value->string)) {
      if (quote(value->string, quoted_value, _countof(quoted_value))) return 2;
    }
    else _sntprintf_s(quoted_value, _countof(quoted_value), _TRUNCATE, _T("\"\""));
  }
  else if (is_numeric_type(type)) _sntprintf_s(quoted_value, _countof(quoted_value), _TRUNCATE, _T("%lu"), value->numeric);
  else return 2;

  if (quote(nssm_exe(), quoted_nssm, _countof(quoted_nssm))) return 3;
  if (_tcslen(quoted_additional)) _tprintf(_T("%s set %s %s %s %s\n"), quoted_nssm, quoted_service_name, name, quoted_additional, quoted_value);
  else _tprintf(_T("%s set %s %s %s\n"), quoted_nssm, quoted_service_name, name, quoted_value);
  return 0;
}

static int setting_set_exit_action(const TCHAR *service_name, void *param, const TCHAR *name, void *default_value, value_t *value, const TCHAR *additional) {
  unsigned long exitcode;
  TCHAR *code;
  TCHAR action_string[ACTION_LEN];

  if (additional) {
    /* Default action? */
    if (is_default(additional)) code = 0;
    else {
      if (str_number(additional, &exitcode)) return -1;
      code = (TCHAR *) additional;
    }
  }

  HKEY key = open_registry(service_name, name, KEY_WRITE);
  if (! key) return -1;

  long error;
  int ret = 1;

  /* Resetting to default? */
  if (value && value->string) _sntprintf_s(action_string, _countof(action_string), _TRUNCATE, _T("%s"), value->string);
  else {
    if (code) {
      /* Delete explicit action. */
      error = RegDeleteValue(key, code);
      RegCloseKey(key);
      if (error == ERROR_SUCCESS || error == ERROR_FILE_NOT_FOUND) return 0;
      print_message(stderr, NSSM_MESSAGE_REGDELETEVALUE_FAILED, code, service_name, error_string(error));
      return -1;
    }
    else {
      /* Explicitly keep the default action. */
      if (default_value) _sntprintf_s(action_string, _countof(action_string), _TRUNCATE, _T("%s"), (TCHAR *) default_value);
      ret = 0;
    }
  }

  /* Validate the string. */
  for (int i = 0; exit_action_strings[i]; i++) {
    if (! _tcsnicmp((const TCHAR *) action_string, exit_action_strings[i], ACTION_LEN)) {
      if (default_value && str_equiv(action_string, (TCHAR *) default_value)) ret = 0;
      if (RegSetValueEx(key, code, 0, REG_SZ, (const unsigned char *) exit_action_strings[i], (unsigned long) (_tcslen(action_string) + 1) * sizeof(TCHAR)) != ERROR_SUCCESS) {
        print_message(stderr, NSSM_MESSAGE_SETVALUE_FAILED, code, service_name, error_string(GetLastError()));
        RegCloseKey(key);
        return -1;
      }

      RegCloseKey(key);
      return ret;
    }
  }

  print_message(stderr, NSSM_MESSAGE_INVALID_EXIT_ACTION, action_string);
  for (int i = 0; exit_action_strings[i]; i++) _ftprintf(stderr, _T("%s\n"), exit_action_strings[i]);

  return -1;
}

static int setting_get_exit_action(const TCHAR *service_name, void *param, const TCHAR *name, void *default_value, value_t *value, const TCHAR *additional) {
  unsigned long exitcode = 0;
  unsigned long *code = 0;

  if (additional) {
    if (! is_default(additional)) {
      if (str_number(additional, &exitcode)) return -1;
      code = &exitcode;
    }
  }

  TCHAR action_string[ACTION_LEN];
  bool default_action;
  if (get_exit_action(service_name, code, action_string, &default_action)) return -1;

  value_from_string(name, value, action_string);

  if (default_action && ! _tcsnicmp((const TCHAR *) action_string, (TCHAR *) default_value, ACTION_LEN)) return 0;
  return 1;
}

static int setting_dump_exit_action(const TCHAR *service_name, void *param, const TCHAR *name, void *default_value, value_t *value, const TCHAR *additional) {
  int errors = 0;
  HKEY key = open_registry(service_name, NSSM_REG_EXIT, KEY_READ);
  if (! key) return -1;

  TCHAR code[16];
  unsigned long index = 0;
  while (true) {
    int ret = enumerate_registry_values(key, &index, code, _countof(code));
    if (ret == ERROR_NO_MORE_ITEMS) break;
    if (ret != ERROR_SUCCESS) continue;
    bool valid = true;
    int i;
    for (i = 0; i < _countof(code); i++) {
      if (! code[i]) break;
      if (code[i] >= _T('0') || code[i] <= _T('9')) continue;
      valid = false;
      break;
    }
    if (! valid) continue;

    TCHAR *additional = (code[i]) ? code : NSSM_DEFAULT_STRING;

    ret = setting_get_exit_action(service_name, 0, name, default_value, value, additional);
    if (ret == 1) {
      if (setting_dump_string(service_name, (void *) REG_SZ, name, value, additional)) errors++;
    }
    else if (ret < 0) errors++;
  }

  if (errors) return -1;
  return 0;
}

static inline bool split_hook_name(const TCHAR *hook_name, TCHAR *hook_event, TCHAR *hook_action) {
  TCHAR *s;

  for (s = (TCHAR *) hook_name; *s; s++) {
    if (*s == _T('/')) {
      *s = _T('\0');
      _sntprintf_s(hook_event, HOOK_NAME_LENGTH, _TRUNCATE, _T("%s"), hook_name);
      *s++ = _T('/');
      _sntprintf_s(hook_action, HOOK_NAME_LENGTH, _TRUNCATE, _T("%s"), s);
      return valid_hook_name(hook_event, hook_action, false);
    }
  }

  print_message(stderr, NSSM_MESSAGE_INVALID_HOOK_NAME, hook_name);
  return false;
}

static int setting_set_hook(const TCHAR *service_name, void *param, const TCHAR *name, void *default_value, value_t *value, const TCHAR *additional) {
  TCHAR hook_event[HOOK_NAME_LENGTH];
  TCHAR hook_action[HOOK_NAME_LENGTH];
  if (! split_hook_name(additional, hook_event, hook_action)) return -1;

  TCHAR *cmd;
  if (value && value->string) cmd = value->string;
  else cmd = _T("");

  if (set_hook(service_name, hook_event, hook_action, cmd)) return -1;
  if (! _tcslen(cmd)) return 0;
  return 1;
}

static int setting_get_hook(const TCHAR *service_name, void *param, const TCHAR *name, void *default_value, value_t *value, const TCHAR *additional) {
  TCHAR hook_event[HOOK_NAME_LENGTH];
  TCHAR hook_action[HOOK_NAME_LENGTH];
  if (! split_hook_name(additional, hook_event, hook_action)) return -1;

  TCHAR cmd[CMD_LENGTH];
  if (get_hook(service_name, hook_event, hook_action, cmd, sizeof(cmd))) return -1;

  value_from_string(name, value, cmd);

  if (! _tcslen(cmd)) return 0;
  return 1;
}

static int setting_dump_hooks(const TCHAR *service_name, void *param, const TCHAR *name, void *default_value, value_t *value, const TCHAR *additional) {
  int i, j;

  int errors = 0;
  for (i = 0; hook_event_strings[i]; i++) {
    const TCHAR *hook_event = hook_event_strings[i];
    for (j = 0; hook_action_strings[j]; j++) {
      const TCHAR *hook_action = hook_action_strings[j];
      if (! valid_hook_name(hook_event, hook_action, true)) continue;

      TCHAR hook_name[HOOK_NAME_LENGTH];
      _sntprintf_s(hook_name, _countof(hook_name), _TRUNCATE, _T("%s/%s"), hook_event, hook_action);

      int ret = setting_get_hook(service_name, param, name, default_value, value, hook_name);
      if (ret != 1) {
        if (ret < 0) errors++;
        continue;
      }

      if (setting_dump_string(service_name, (void *) REG_SZ, name, value, hook_name)) errors++;
    }
  }

  if (errors) return -1;
  return 0;
}

static int setting_set_affinity(const TCHAR *service_name, void *param, const TCHAR *name, void *default_value, value_t *value, const TCHAR *additional) {
  HKEY key = (HKEY) param;
  if (! key) return -1;

  long error;
  __int64 mask;
  __int64 system_affinity = 0LL;

  if (value && value->string) {
    DWORD_PTR affinity;
    if (! GetProcessAffinityMask(GetCurrentProcess(), &affinity, (DWORD_PTR *) &system_affinity)) system_affinity = ~0;

    if (is_default(value->string) || str_equiv(value->string, NSSM_AFFINITY_ALL)) mask = 0LL;
    else if (affinity_string_to_mask(value->string, &mask)) {
      print_message(stderr, NSSM_MESSAGE_BOGUS_AFFINITY_MASK, value->string, num_cpus() - 1);
      return -1;
    }
  }
  else mask = 0LL;

  if (! mask) {
    error = RegDeleteValue(key, name);
    if (error == ERROR_SUCCESS || error == ERROR_FILE_NOT_FOUND) return 0;
    print_message(stderr, NSSM_MESSAGE_REGDELETEVALUE_FAILED, name, service_name, error_string(error));
    return -1;
  }

  /* Canonicalise. */
  TCHAR *canon = 0;
  if (affinity_mask_to_string(mask, &canon)) canon = value->string;

  __int64 effective_affinity = mask & system_affinity;
  if (effective_affinity != mask) {
    /* Requested CPUs did not intersect with available CPUs? */
    if (! effective_affinity) mask = effective_affinity = system_affinity;

    TCHAR *system = 0;
    if (! affinity_mask_to_string(system_affinity, &system)) {
      TCHAR *effective = 0;
      if (! affinity_mask_to_string(effective_affinity, &effective)) {
        print_message(stderr, NSSM_MESSAGE_EFFECTIVE_AFFINITY_MASK, value->string, system, effective);
        HeapFree(GetProcessHeap(), 0, effective);
      }
      HeapFree(GetProcessHeap(), 0, system);
    }
  }

  if (RegSetValueEx(key, name, 0, REG_SZ, (const unsigned char *) canon, (unsigned long) (_tcslen(canon) + 1) * sizeof(TCHAR)) != ERROR_SUCCESS) {
    if (canon != value->string) HeapFree(GetProcessHeap(), 0, canon);
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_SETVALUE_FAILED, name, error_string(GetLastError()), 0);
    return -1;
  }

  if (canon != value->string) HeapFree(GetProcessHeap(), 0, canon);
  return 1;
}

static int setting_get_affinity(const TCHAR *service_name, void *param, const TCHAR *name, void *default_value, value_t *value, const TCHAR *additional) {
  HKEY key = (HKEY) param;
  if (! key) return -1;

  unsigned long type;
  TCHAR *buffer = 0;
  unsigned long buflen = 0;

  int ret = RegQueryValueEx(key, name, 0, &type, 0, &buflen);
  if (ret == ERROR_FILE_NOT_FOUND) {
    if (value_from_string(name, value, NSSM_AFFINITY_ALL) == 1) return 0;
    return -1;
  }
  if (ret != ERROR_SUCCESS) return -1;

  if (type != REG_SZ) return -1;

  buffer = (TCHAR *) HeapAlloc(GetProcessHeap(), 0, buflen);
  if (! buffer) {
    print_message(stderr, NSSM_MESSAGE_OUT_OF_MEMORY, _T("affinity"), _T("setting_get_affinity"));
    return -1;
  }

  if (get_string(key, (TCHAR *) name, buffer, buflen, false, false, true)) {
    HeapFree(GetProcessHeap(), 0, buffer);
    return -1;
  }

  __int64 affinity;
  if (affinity_string_to_mask(buffer, &affinity)) {
    print_message(stderr, NSSM_MESSAGE_BOGUS_AFFINITY_MASK, buffer, num_cpus() - 1);
    HeapFree(GetProcessHeap(), 0, buffer);
    return -1;
  }

  HeapFree(GetProcessHeap(), 0, buffer);

  /* Canonicalise. */
  if (affinity_mask_to_string(affinity, &buffer)) {
    if (buffer) HeapFree(GetProcessHeap(), 0, buffer);
    return -1;
  }

  ret = value_from_string(name, value, buffer);
  HeapFree(GetProcessHeap(), 0, buffer);
  return ret;
}

static int setting_set_environment(const TCHAR *service_name, void *param, const TCHAR *name, void *default_value, value_t *value, const TCHAR *additional) {
  HKEY key = (HKEY) param;
  if (! param) return -1;

  TCHAR *string = 0;
  TCHAR *unformatted = 0;
  unsigned long envlen;
  unsigned long newlen = 0;
  int op = 0;
  if (value && value->string && value->string[0]) {
    string = value->string;
    switch (string[0]) {
      case _T('+'): op = 1; break;
      case _T('-'): op = -1; break;
      case _T(':'): string++; break;
    }
  }

  if (op) {
    string++;
    TCHAR *env = 0;
    if (get_environment((TCHAR *) service_name, key, (TCHAR *) name, &env, &envlen)) return -1;
    if (env) {
      int ret;
      if (op > 0) ret = append_to_environment_block(env, envlen, string, &unformatted, &newlen);
      else ret = remove_from_environment_block(env, envlen, string, &unformatted, &newlen);
      if (envlen) HeapFree(GetProcessHeap(), 0, env);
      if (ret) return -1;

      string = unformatted;
    }
    else {
      /*
        No existing environment.
        We can't remove from an empty environment so just treat an add
        operation as setting a new string.
      */
      if (op < 0) return 0;
      op = 0;
    }
  }

  if (! string || ! string[0]) {
    long error = RegDeleteValue(key, name);
    if (error == ERROR_SUCCESS || error == ERROR_FILE_NOT_FOUND) return 0;
    print_message(stderr, NSSM_MESSAGE_REGDELETEVALUE_FAILED, name, service_name, error_string(error));
    return -1;
  }

  if (! op) {
    if (unformat_double_null(string, (unsigned long) _tcslen(string), &unformatted, &newlen)) return -1;
  }

  if (test_environment(unformatted)) {
    HeapFree(GetProcessHeap(), 0, unformatted);
    print_message(stderr, NSSM_GUI_INVALID_ENVIRONMENT);
    return -1;
  }

  if (RegSetValueEx(key, name, 0, REG_MULTI_SZ, (const unsigned char *) unformatted, (unsigned long) newlen * sizeof(TCHAR)) != ERROR_SUCCESS) {
    if (newlen) HeapFree(GetProcessHeap(), 0, unformatted);
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_SETVALUE_FAILED, NSSM_REG_ENV, error_string(GetLastError()), 0);
    return -1;
  }

  if (newlen) HeapFree(GetProcessHeap(), 0, unformatted);
  return 1;
}

static int setting_get_environment(const TCHAR *service_name, void *param, const TCHAR *name, void *default_value, value_t *value, const TCHAR *additional) {
  HKEY key = (HKEY) param;
  if (! param) return -1;

  TCHAR *env = 0;
  unsigned long envlen;
  if (get_environment((TCHAR *) service_name, key, (TCHAR *) name, &env, &envlen)) return -1;
  if (! envlen) return 0;

  TCHAR *formatted;
  unsigned long newlen;
  if (format_double_null(env, envlen, &formatted, &newlen)) return -1;

  int ret;
  if (additional) {
    /* Find named environment variable. */
    TCHAR *s;
    size_t len = _tcslen(additional);
    for (s = env; *s; s++) {
      /* Look for <additional>=<string> NULL NULL */
      if (! _tcsnicmp(s, additional, len) && s[len] == _T('=')) {
        /* Strip <key>= */
        s += len + 1;
        ret = value_from_string(name, value, s);
        HeapFree(GetProcessHeap(), 0, env);
        return ret;
      }

      /* Skip this string. */
      for ( ; *s; s++);
    }
    HeapFree(GetProcessHeap(), 0, env);
    return 0;
  }

  HeapFree(GetProcessHeap(), 0, env);

  ret = value_from_string(name, value, formatted);
  if (newlen) HeapFree(GetProcessHeap(), 0, formatted);
  return ret;
}

static int setting_dump_environment(const TCHAR *service_name, void *param, const TCHAR *name, void *default_value, value_t *value, const TCHAR *additional) {
  int errors = 0;
  HKEY key = (HKEY) param;
  if (! param) return -1;

  TCHAR *env = 0;
  unsigned long envlen;
  if (get_environment((TCHAR *) service_name, key, (TCHAR *) name, &env, &envlen)) return -1;
  if (! envlen) return 0;

  TCHAR *s;
  for (s = env; *s; s++) {
    size_t len = _tcslen(s) + 2;
    value->string = (TCHAR *) HeapAlloc(GetProcessHeap(), 0, len * sizeof(TCHAR));
    if (! value->string) {
      print_message(stderr, NSSM_MESSAGE_OUT_OF_MEMORY, _T("dump"), _T("setting_dump_environment"));
      break;
    }

    _sntprintf_s(value->string, len, _TRUNCATE, _T("%c%s"), (s > env) ? _T('+') : _T(':'), s);
    if (setting_dump_string(service_name, (void *) REG_SZ, name, value, 0)) errors++;
    HeapFree(GetProcessHeap(), 0, value->string);
    value->string = 0;

    for ( ; *s; s++);
  }

  HeapFree(GetProcessHeap(), 0, env);

  if (errors) return 1;
  return 0;
}

static int setting_set_priority(const TCHAR *service_name, void *param, const TCHAR *name, void *default_value, value_t *value, const TCHAR *additional) {
  HKEY key = (HKEY) param;
  if (! param) return -1;

  TCHAR *priority_string;
  int i;
  long error;

  if (value && value->string) priority_string = value->string;
  else if (default_value) priority_string = (TCHAR *) default_value;
  else {
    error = RegDeleteValue(key, name);
    if (error == ERROR_SUCCESS || error == ERROR_FILE_NOT_FOUND) return 0;
    print_message(stderr, NSSM_MESSAGE_REGDELETEVALUE_FAILED, name, service_name, error_string(error));
    return -1;
  }

  for (i = 0; priority_strings[i]; i++) {
    if (! str_equiv(priority_strings[i], priority_string)) continue;

    if (default_value && str_equiv(priority_string, (TCHAR *) default_value)) {
      error = RegDeleteValue(key, name);
      if (error == ERROR_SUCCESS || error == ERROR_FILE_NOT_FOUND) return 0;
      print_message(stderr, NSSM_MESSAGE_REGDELETEVALUE_FAILED, name, service_name, error_string(error));
      return -1;
    }

    if (set_number(key, (TCHAR *) name, priority_index_to_constant(i))) return -1;
    return 1;
  }

  print_message(stderr, NSSM_MESSAGE_INVALID_PRIORITY, priority_string);
  for (i = 0; priority_strings[i]; i++) _ftprintf(stderr, _T("%s\n"), priority_strings[i]);

  return -1;
}

static int setting_get_priority(const TCHAR *service_name, void *param, const TCHAR *name, void *default_value, value_t *value, const TCHAR *additional) {
  HKEY key = (HKEY) param;
  if (! param) return -1;

  unsigned long constant;
  switch (get_number(key, (TCHAR *) name, &constant, false)) {
    case 0:
      if (value_from_string(name, value, (const TCHAR *) default_value) == -1) return -1;
      return 0;
    case -1: return -1;
  }

  return value_from_string(name, value, priority_strings[priority_constant_to_index(constant)]);
}

static int setting_dump_priority(const TCHAR *service_name, void *key_ptr, const TCHAR *name, void *setting_ptr, value_t *value, const TCHAR *additional) {
  settings_t *setting = (settings_t *) setting_ptr;
  int ret = setting_get_priority(service_name, key_ptr, name, (void *) setting->default_value, value, 0);
  if (ret != 1) return ret;
  return setting_dump_string(service_name, (void *) REG_SZ, name, value, 0);
}

/* Functions to manage native service settings. */
static int native_set_dependon(const TCHAR *service_name, SC_HANDLE service_handle, TCHAR **dependencies, unsigned long *dependencieslen, value_t *value, int type) {
  *dependencieslen = 0;
  if (! value || ! value->string || ! value->string[0]) return 0;

  TCHAR *string = value->string;
  unsigned long buflen;
  int op = 0;
  switch (string[0]) {
    case _T('+'): op = 1; break;
    case _T('-'): op = -1; break;
    case _T(':'): string++; break;
  }

  if (op) {
    string++;
    TCHAR *buffer = 0;
    if (get_service_dependencies(service_name, service_handle, &buffer, &buflen, type)) return -1;
    if (buffer) {
      int ret;
      if (op > 0) ret = append_to_dependencies(buffer, buflen, string, dependencies, dependencieslen, type);
      else ret = remove_from_dependencies(buffer, buflen, string, dependencies, dependencieslen, type);
      if (buflen) HeapFree(GetProcessHeap(), 0, buffer);
      return ret;
    }
    else {
      /*
        No existing list.
        We can't remove from an empty list so just treat an add
        operation as setting a new string.
      */
      if (op < 0) return 0;
      op = 0;
    }
  }

  if (! op) {
    TCHAR *unformatted = 0;
    unsigned long newlen;
    if (unformat_double_null(string, (unsigned long) _tcslen(string), &unformatted, &newlen)) return -1;

    if (type == DEPENDENCY_GROUPS) {
      /* Prepend group identifier. */
      unsigned long missing = 0;
      TCHAR *canon = unformatted;
      size_t canonlen = 0;
      TCHAR *s;
      for (s = unformatted; *s; s++) {
        if (*s != SC_GROUP_IDENTIFIER) missing++;
        size_t len = _tcslen(s);
        canonlen += len + 1;
        s += len;
      }

      if (missing) {
        /* Missing identifiers plus double NULL terminator. */
        canonlen += missing + 1;

        canon = (TCHAR *) HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, canonlen * sizeof(TCHAR));
        if (! canon) {
          print_message(stderr, NSSM_MESSAGE_OUT_OF_MEMORY, _T("canon"), _T("native_set_dependon"));
          if (unformatted) HeapFree(GetProcessHeap(), 0, unformatted);
          return -1;
        }

        size_t i = 0;
        for (s = unformatted; *s; s++) {
          if (*s != SC_GROUP_IDENTIFIER) canon[i++] = SC_GROUP_IDENTIFIER;
          size_t len = _tcslen(s);
          memmove(canon + i, s, (len + 1) * sizeof(TCHAR));
          i += len + 1;
          s += len;
        }

        HeapFree(GetProcessHeap(), 0, unformatted);
        unformatted = canon;
        newlen = (unsigned long) canonlen;
      }
    }

    *dependencies = unformatted;
    *dependencieslen = newlen;
  }

  return 0;
}

static int native_set_dependongroup(const TCHAR *service_name, void *param, const TCHAR *name, void *default_value, value_t *value, const TCHAR *additional) {
  SC_HANDLE service_handle = (SC_HANDLE) param;
  if (! service_handle) return -1;

  /*
    Get existing service dependencies because we must set both types together.
  */
  TCHAR *services_buffer;
  unsigned long services_buflen;
  if (get_service_dependencies(service_name, service_handle, &services_buffer, &services_buflen, DEPENDENCY_SERVICES)) return -1;

  if (! value || ! value->string || ! value->string[0]) {
    if (! ChangeServiceConfig(service_handle, SERVICE_NO_CHANGE, SERVICE_NO_CHANGE, SERVICE_NO_CHANGE, 0, 0, 0, services_buffer, 0, 0, 0)) {
      print_message(stderr, NSSM_MESSAGE_CHANGESERVICECONFIG_FAILED, error_string(GetLastError()));
      if (services_buffer) HeapFree(GetProcessHeap(), 0, services_buffer);
      return -1;
    }

    if (services_buffer) HeapFree(GetProcessHeap(), 0, services_buffer);
    return 0;
  }

  /* Update the group list. */
  TCHAR *groups_buffer;
  unsigned long groups_buflen;
  if (native_set_dependon(service_name, service_handle, &groups_buffer, &groups_buflen, value, DEPENDENCY_GROUPS)) return -1;

  TCHAR *dependencies;
  if (services_buflen > 2) {
    dependencies = (TCHAR *) HeapAlloc(GetProcessHeap(), 0, (groups_buflen + services_buflen) * sizeof(TCHAR));
    if (! dependencies) {
      print_message(stderr, NSSM_MESSAGE_OUT_OF_MEMORY, _T("dependencies"), _T("native_set_dependongroup"));
      if (groups_buffer) HeapFree(GetProcessHeap(), 0, groups_buffer);
      if (services_buffer) HeapFree(GetProcessHeap(), 0, services_buffer);
      return -1;
    }

    memmove(dependencies, services_buffer, services_buflen * sizeof(TCHAR));
    memmove(dependencies + services_buflen - 1, groups_buffer, groups_buflen * sizeof(TCHAR));
  }
  else dependencies = groups_buffer;

  int ret = 1;
  if (set_service_dependencies(service_name, service_handle, dependencies)) ret = -1;
  if (dependencies != groups_buffer) HeapFree(GetProcessHeap(), 0, dependencies);
  if (groups_buffer) HeapFree(GetProcessHeap(), 0, groups_buffer);
  if (services_buffer) HeapFree(GetProcessHeap(), 0, services_buffer);

  return ret;
}

static int native_get_dependongroup(const TCHAR *service_name, void *param, const TCHAR *name, void *default_value, value_t *value, const TCHAR *additional) {
  SC_HANDLE service_handle = (SC_HANDLE) param;
  if (! service_handle) return -1;

  TCHAR *buffer;
  unsigned long buflen;
  if (get_service_dependencies(service_name, service_handle, &buffer, &buflen, DEPENDENCY_GROUPS)) return -1;

  int ret;
  if (buflen) {
    TCHAR *formatted;
    unsigned long newlen;
    if (format_double_null(buffer, buflen, &formatted, &newlen)) {
      HeapFree(GetProcessHeap(), 0, buffer);
      return -1;
    }

    ret = value_from_string(name, value, formatted);
    HeapFree(GetProcessHeap(), 0, formatted);
    HeapFree(GetProcessHeap(), 0, buffer);
  }
  else {
    value->string = 0;
    ret = 0;
  }

  return ret;
}

static int setting_dump_dependon(const TCHAR *service_name, SC_HANDLE service_handle, const TCHAR *name, int type, value_t *value) {
  int errors = 0;

  TCHAR *dependencies = 0;
  unsigned long dependencieslen;
  if (get_service_dependencies(service_name, service_handle, &dependencies, &dependencieslen, type)) return -1;
  if (! dependencieslen) return 0;

  TCHAR *s;
  for (s = dependencies; *s; s++) {
    size_t len = _tcslen(s) + 2;
    value->string = (TCHAR *) HeapAlloc(GetProcessHeap(), 0, len * sizeof(TCHAR));
    if (! value->string) {
      print_message(stderr, NSSM_MESSAGE_OUT_OF_MEMORY, _T("dump"), _T("setting_dump_dependon"));
      break;
    }

    _sntprintf_s(value->string, len, _TRUNCATE, _T("%c%s"), (s > dependencies) ? _T('+') : _T(':'), s);
    if (setting_dump_string(service_name, (void *) REG_SZ, name, value, 0)) errors++;
    HeapFree(GetProcessHeap(), 0, value->string);
    value->string = 0;

    for ( ; *s; s++);
  }

  HeapFree(GetProcessHeap(), 0, dependencies);

  if (errors) return 1;
  return 0;
}

static int native_dump_dependongroup(const TCHAR *service_name, void *param, const TCHAR *name, void *default_value, value_t *value, const TCHAR *additional) {
  return setting_dump_dependon(service_name, (SC_HANDLE) param, name, DEPENDENCY_GROUPS, value);
}

static int native_set_dependonservice(const TCHAR *service_name, void *param, const TCHAR *name, void *default_value, value_t *value, const TCHAR *additional) {
  SC_HANDLE service_handle = (SC_HANDLE) param;
  if (! service_handle) return -1;

  /*
    Get existing group dependencies because we must set both types together.
  */
  TCHAR *groups_buffer;
  unsigned long groups_buflen;
  if (get_service_dependencies(service_name, service_handle, &groups_buffer, &groups_buflen, DEPENDENCY_GROUPS)) return -1;

  if (! value || ! value->string || ! value->string[0]) {
    if (! ChangeServiceConfig(service_handle, SERVICE_NO_CHANGE, SERVICE_NO_CHANGE, SERVICE_NO_CHANGE, 0, 0, 0, groups_buffer, 0, 0, 0)) {
      print_message(stderr, NSSM_MESSAGE_CHANGESERVICECONFIG_FAILED, error_string(GetLastError()));
      if (groups_buffer) HeapFree(GetProcessHeap(), 0, groups_buffer);
      return -1;
    }

    if (groups_buffer) HeapFree(GetProcessHeap(), 0, groups_buffer);
    return 0;
  }

  /* Update the service list. */
  TCHAR *services_buffer;
  unsigned long services_buflen;
  if (native_set_dependon(service_name, service_handle, &services_buffer, &services_buflen, value, DEPENDENCY_SERVICES)) return -1;

  TCHAR *dependencies;
  if (groups_buflen > 2) {
    dependencies = (TCHAR *) HeapAlloc(GetProcessHeap(), 0, (services_buflen + groups_buflen) * sizeof(TCHAR));
    if (! dependencies) {
      print_message(stderr, NSSM_MESSAGE_OUT_OF_MEMORY, _T("dependencies"), _T("native_set_dependonservice"));
      if (groups_buffer) HeapFree(GetProcessHeap(), 0, groups_buffer);
      if (services_buffer) HeapFree(GetProcessHeap(), 0, services_buffer);
      return -1;
    }

    memmove(dependencies, services_buffer, services_buflen * sizeof(TCHAR));
    memmove(dependencies + services_buflen - 1, groups_buffer, groups_buflen * sizeof(TCHAR));
  }
  else dependencies = services_buffer;

  int ret = 1;
  if (set_service_dependencies(service_name, service_handle, dependencies)) ret = -1;
  if (dependencies != services_buffer) HeapFree(GetProcessHeap(), 0, dependencies);
  if (groups_buffer) HeapFree(GetProcessHeap(), 0, groups_buffer);
  if (services_buffer) HeapFree(GetProcessHeap(), 0, services_buffer);

  return ret;
}

static int native_get_dependonservice(const TCHAR *service_name, void *param, const TCHAR *name, void *default_value, value_t *value, const TCHAR *additional) {
  SC_HANDLE service_handle = (SC_HANDLE) param;
  if (! service_handle) return -1;

  TCHAR *buffer;
  unsigned long buflen;
  if (get_service_dependencies(service_name, service_handle, &buffer, &buflen, DEPENDENCY_SERVICES)) return -1;

  int ret;
  if (buflen) {
    TCHAR *formatted;
    unsigned long newlen;
    if (format_double_null(buffer, buflen, &formatted, &newlen)) {
      HeapFree(GetProcessHeap(), 0, buffer);
      return -1;
    }

    ret = value_from_string(name, value, formatted);
    HeapFree(GetProcessHeap(), 0, formatted);
    HeapFree(GetProcessHeap(), 0, buffer);
  }
  else {
    value->string = 0;
    ret = 0;
  }

  return ret;
}

static int native_dump_dependonservice(const TCHAR *service_name, void *param, const TCHAR *name, void *default_value, value_t *value, const TCHAR *additional) {
  return setting_dump_dependon(service_name, (SC_HANDLE) param, name, DEPENDENCY_SERVICES, value);
}

int native_set_description(const TCHAR *service_name, void *param, const TCHAR *name, void *default_value, value_t *value, const TCHAR *additional) {
  SC_HANDLE service_handle = (SC_HANDLE) param;
  if (! service_handle) return -1;

  TCHAR *description = 0;
  if (value) description = value->string;
  if (set_service_description(service_name, service_handle, description)) return -1;

  if (description && description[0]) return 1;

  return 0;
}

int native_get_description(const TCHAR *service_name, void *param, const TCHAR *name, void *default_value, value_t *value, const TCHAR *additional) {
  SC_HANDLE service_handle = (SC_HANDLE) param;
  if (! service_handle) return -1;

  TCHAR buffer[VALUE_LENGTH];
  if (get_service_description(service_name, service_handle, _countof(buffer), buffer)) return -1;

  if (buffer[0]) return value_from_string(name, value, buffer);
  value->string = 0;

  return 0;
}

int native_set_displayname(const TCHAR *service_name, void *param, const TCHAR *name, void *default_value, value_t *value, const TCHAR *additional) {
  SC_HANDLE service_handle = (SC_HANDLE) param;
  if (! service_handle) return -1;

  TCHAR *displayname = 0;
  if (value && value->string) displayname = value->string;
  else displayname = (TCHAR *) service_name;

  if (! ChangeServiceConfig(service_handle, SERVICE_NO_CHANGE, SERVICE_NO_CHANGE, SERVICE_NO_CHANGE, 0, 0, 0, 0, 0, 0, displayname)) {
    print_message(stderr, NSSM_MESSAGE_CHANGESERVICECONFIG_FAILED, error_string(GetLastError()));
    return -1;
  }

  /*
    If the display name and service name differ only in case,
    ChangeServiceConfig() will return success but the display name will be
    set to the service name, NOT the value passed to the function.
    This appears to be a quirk of Windows rather than a bug here.
  */
  if (displayname != service_name && ! str_equiv(displayname, service_name)) return 1;

  return 0;
}

int native_get_displayname(const TCHAR *service_name, void *param, const TCHAR *name, void *default_value, value_t *value, const TCHAR *additional) {
  SC_HANDLE service_handle = (SC_HANDLE) param;
  if (! service_handle) return -1;

  QUERY_SERVICE_CONFIG *qsc = query_service_config(service_name, service_handle);
  if (! qsc) return -1;

  int ret = value_from_string(name, value, qsc->lpDisplayName);
  HeapFree(GetProcessHeap(), 0, qsc);

  return ret;
}

int native_set_environment(const TCHAR *service_name, void *param, const TCHAR *name, void *default_value, value_t *value, const TCHAR *additional) {
  HKEY key = open_service_registry(service_name, KEY_SET_VALUE, true);
  if (! key) return -1;

  int ret = setting_set_environment(service_name, (void *) key, name, default_value, value, additional);
  RegCloseKey(key);
  return ret;
}

int native_get_environment(const TCHAR *service_name, void *param, const TCHAR *name, void *default_value, value_t *value, const TCHAR *additional) {
  HKEY key = open_service_registry(service_name, KEY_READ, true);
  if (! key) return -1;

  ZeroMemory(value, sizeof(value_t));
  int ret = setting_get_environment(service_name, (void *) key, name, default_value, value, additional);
  RegCloseKey(key);
  return ret;
}

static int native_dump_environment(const TCHAR *service_name, void *param, const TCHAR *name, void *default_value, value_t *value, const TCHAR *additional) {
  HKEY key = open_service_registry(service_name, KEY_READ, true);
  if (! key) return -1;

  int ret = setting_dump_environment(service_name, (void *) key, name, default_value, value, additional);
  RegCloseKey(key);
  return ret;
}

int native_set_imagepath(const TCHAR *service_name, void *param, const TCHAR *name, void *default_value, value_t *value, const TCHAR *additional) {
  SC_HANDLE service_handle = (SC_HANDLE) param;
  if (! service_handle) return -1;

  /* It makes no sense to try to reset the image path. */
  if (! value || ! value->string) {
    print_message(stderr, NSSM_MESSAGE_NO_DEFAULT_VALUE, name);
    return -1;
  }

  if (! ChangeServiceConfig(service_handle, SERVICE_NO_CHANGE, SERVICE_NO_CHANGE, SERVICE_NO_CHANGE, value->string, 0, 0, 0, 0, 0, 0)) {
    print_message(stderr, NSSM_MESSAGE_CHANGESERVICECONFIG_FAILED, error_string(GetLastError()));
    return -1;
  }

  return 1;
}

int native_get_imagepath(const TCHAR *service_name, void *param, const TCHAR *name, void *default_value, value_t *value, const TCHAR *additional) {
  SC_HANDLE service_handle = (SC_HANDLE) param;
  if (! service_handle) return -1;

  QUERY_SERVICE_CONFIG *qsc = query_service_config(service_name, service_handle);
  if (! qsc) return -1;

  int ret = value_from_string(name, value, qsc->lpBinaryPathName);
  HeapFree(GetProcessHeap(), 0, qsc);

  return ret;
}

int native_set_name(const TCHAR *service_name, void *param, const TCHAR *name, void *default_value, value_t *value, const TCHAR *additional) {
  print_message(stderr, NSSM_MESSAGE_CANNOT_RENAME_SERVICE);
  return -1;
}

int native_get_name(const TCHAR *service_name, void *param, const TCHAR *name, void *default_value, value_t *value, const TCHAR *additional) {
  return value_from_string(name, value, service_name);
}

int native_set_objectname(const TCHAR *service_name, void *param, const TCHAR *name, void *default_value, value_t *value, const TCHAR *additional) {
  SC_HANDLE service_handle = (SC_HANDLE) param;
  if (! service_handle) return -1;

  /*
    Logical syntax is: nssm set <service> ObjectName <username> <password>
    That means the username is actually passed in the additional parameter.
  */
  bool localsystem = false;
  bool virtual_account = false;
  TCHAR *username = NSSM_LOCALSYSTEM_ACCOUNT;
  TCHAR *password = 0;
  if (additional) {
    username = (TCHAR *) additional;
    if (value && value->string) password = value->string;
  }
  else if (value && value->string) username = value->string;

  const TCHAR *well_known = well_known_username(username);
  size_t passwordsize = 0;
  if (well_known) {
    if (str_equiv(well_known, NSSM_LOCALSYSTEM_ACCOUNT)) localsystem = true;
    username = (TCHAR *) well_known;
    password = _T("");
  }
  else if (is_virtual_account(service_name, username)) virtual_account = true;
  else if (! password) {
    /* We need a password if the account requires it. */
    print_message(stderr, NSSM_MESSAGE_MISSING_PASSWORD, name);
    return -1;
  }
  else passwordsize = _tcslen(password) * sizeof(TCHAR);

  /*
    ChangeServiceConfig() will fail to set the username if the service is set
    to interact with the desktop.
  */
  unsigned long type = SERVICE_NO_CHANGE;
  if (! localsystem) {
    QUERY_SERVICE_CONFIG *qsc = query_service_config(service_name, service_handle);
    if (! qsc) {
      if (passwordsize) SecureZeroMemory(password, passwordsize);
      return -1;
    }

    type = qsc->dwServiceType & ~SERVICE_INTERACTIVE_PROCESS;
    HeapFree(GetProcessHeap(), 0, qsc);
  }

  if (! well_known && ! virtual_account) {
    if (grant_logon_as_service(username)) {
      if (passwordsize) SecureZeroMemory(password, passwordsize);
      print_message(stderr, NSSM_MESSAGE_GRANT_LOGON_AS_SERVICE_FAILED, username);
      return -1;
    }
  }

  if (! ChangeServiceConfig(service_handle, type, SERVICE_NO_CHANGE, SERVICE_NO_CHANGE, 0, 0, 0, 0, username, password, 0)) {
    if (passwordsize) SecureZeroMemory(password, passwordsize);
    print_message(stderr, NSSM_MESSAGE_CHANGESERVICECONFIG_FAILED, error_string(GetLastError()));
    return -1;
  }

  if (passwordsize) SecureZeroMemory(password, passwordsize);

  if (localsystem) return 0;

  return 1;
}

int native_get_objectname(const TCHAR *service_name, void *param, const TCHAR *name, void *default_value, value_t *value, const TCHAR *additional) {
  SC_HANDLE service_handle = (SC_HANDLE) param;
  if (! service_handle) return -1;

  QUERY_SERVICE_CONFIG *qsc = query_service_config(service_name, service_handle);
  if (! qsc) return -1;

  int ret = value_from_string(name, value, qsc->lpServiceStartName);
  HeapFree(GetProcessHeap(), 0, qsc);

  return ret;
}

int native_dump_objectname(const TCHAR *service_name, void *param, const TCHAR *name, void *default_value, value_t *value, const TCHAR *additional) {
  int ret = native_get_objectname(service_name, param, name, default_value, value, additional);
  if (ret != 1) return ret;

  /* Properly checking for a virtual account requires the actual service name. */
  if (! _tcsnicmp(NSSM_VIRTUAL_SERVICE_ACCOUNT_DOMAIN, value->string, _tcslen(NSSM_VIRTUAL_SERVICE_ACCOUNT_DOMAIN))) {
    TCHAR *name = virtual_account(service_name);
    if (! name) return -1;
    HeapFree(GetProcessHeap(), 0, value->string);
    value->string = name;
  }
  else {
    /* Do we need to dump a dummy password? */
    if (! well_known_username(value->string)) {
      /* Parameters are the other way round. */
      value_t inverted;
      inverted.string = _T("****");
      return setting_dump_string(service_name, (void *) REG_SZ, name, &inverted, value->string);
    }
  }
  return setting_dump_string(service_name, (void *) REG_SZ, name, value, 0);
}

int native_set_startup(const TCHAR *service_name, void *param, const TCHAR *name, void *default_value, value_t *value, const TCHAR *additional) {
  SC_HANDLE service_handle = (SC_HANDLE) param;
  if (! service_handle) return -1;

  /* It makes no sense to try to reset the startup type. */
  if (! value || ! value->string) {
    print_message(stderr, NSSM_MESSAGE_NO_DEFAULT_VALUE, name);
    return -1;
  }

  /* Map NSSM_STARTUP_* constant to Windows SERVICE_*_START constant. */
  int service_startup = -1;
  int i;
  for (i = 0; startup_strings[i]; i++) {
    if (str_equiv(value->string, startup_strings[i])) {
      service_startup = i;
      break;
    }
  }

  if (service_startup < 0) {
    print_message(stderr, NSSM_MESSAGE_INVALID_SERVICE_STARTUP, value->string);
    for (i = 0; startup_strings[i]; i++) _ftprintf(stderr, _T("%s\n"), startup_strings[i]);
    return -1;
  }

  unsigned long startup;
  switch (service_startup) {
    case NSSM_STARTUP_MANUAL: startup = SERVICE_DEMAND_START; break;
    case NSSM_STARTUP_DISABLED: startup = SERVICE_DISABLED; break;
    default: startup = SERVICE_AUTO_START;
  }

  if (! ChangeServiceConfig(service_handle, SERVICE_NO_CHANGE, startup, SERVICE_NO_CHANGE, 0, 0, 0, 0, 0, 0, 0)) {
    print_message(stderr, NSSM_MESSAGE_CHANGESERVICECONFIG_FAILED, error_string(GetLastError()));
    return -1;
  }

  SERVICE_DELAYED_AUTO_START_INFO delayed;
  ZeroMemory(&delayed, sizeof(delayed));
  if (service_startup == NSSM_STARTUP_DELAYED) delayed.fDelayedAutostart = 1;
  else delayed.fDelayedAutostart = 0;
  if (! ChangeServiceConfig2(service_handle, SERVICE_CONFIG_DELAYED_AUTO_START_INFO, &delayed)) {
    unsigned long error = GetLastError();
    /* Pre-Vista we expect to fail with ERROR_INVALID_LEVEL */
    if (error != ERROR_INVALID_LEVEL) {
      log_event(EVENTLOG_ERROR_TYPE, NSSM_MESSAGE_SERVICE_CONFIG_DELAYED_AUTO_START_INFO_FAILED, service_name, error_string(error), 0);
    }
  }

  return 1;
}

int native_get_startup(const TCHAR *service_name, void *param, const TCHAR *name, void *default_value, value_t *value, const TCHAR *additional) {
  SC_HANDLE service_handle = (SC_HANDLE) param;
  if (! service_handle) return -1;

  QUERY_SERVICE_CONFIG *qsc = query_service_config(service_name, service_handle);
  if (! qsc) return -1;

  unsigned long startup;
  int ret = get_service_startup(service_name, service_handle, qsc, &startup);
  HeapFree(GetProcessHeap(), 0, qsc);

  if (ret) return -1;

  unsigned long i;
  for (i = 0; startup_strings[i]; i++);
  if (startup >= i) return -1;

  return value_from_string(name, value, startup_strings[startup]);
}

int native_set_type(const TCHAR *service_name, void *param, const TCHAR *name, void *default_value, value_t *value, const TCHAR *additional) {
  SC_HANDLE service_handle = (SC_HANDLE) param;
  if (! service_handle) return -1;

  /* It makes no sense to try to reset the service type. */
  if (! value || ! value->string) {
    print_message(stderr, NSSM_MESSAGE_NO_DEFAULT_VALUE, name);
    return -1;
  }

  /*
    We can only manage services of type SERVICE_WIN32_OWN_PROCESS
    and SERVICE_INTERACTIVE_PROCESS.
  */
  unsigned long type = SERVICE_WIN32_OWN_PROCESS;
  if (str_equiv(value->string, NSSM_INTERACTIVE_PROCESS)) type |= SERVICE_INTERACTIVE_PROCESS;
  else if (! str_equiv(value->string, NSSM_WIN32_OWN_PROCESS)) {
    print_message(stderr, NSSM_MESSAGE_INVALID_SERVICE_TYPE, value->string);
    _ftprintf(stderr, _T("%s\n"), NSSM_WIN32_OWN_PROCESS);
    _ftprintf(stderr, _T("%s\n"), NSSM_INTERACTIVE_PROCESS);
    return -1;
  }

  /*
    ChangeServiceConfig() will fail if the service runs under an account
    other than LOCALSYSTEM and we try to make it interactive.
  */
  if (type & SERVICE_INTERACTIVE_PROCESS) {
    QUERY_SERVICE_CONFIG *qsc = query_service_config(service_name, service_handle);
    if (! qsc) return -1;

    if (! str_equiv(qsc->lpServiceStartName, NSSM_LOCALSYSTEM_ACCOUNT)) {
      HeapFree(GetProcessHeap(), 0, qsc);
      print_message(stderr, NSSM_MESSAGE_INTERACTIVE_NOT_LOCALSYSTEM, value->string, service_name, NSSM_LOCALSYSTEM_ACCOUNT);
      return -1;
    }

    HeapFree(GetProcessHeap(), 0, qsc);
  }

  if (! ChangeServiceConfig(service_handle, type, SERVICE_NO_CHANGE, SERVICE_NO_CHANGE, 0, 0, 0, 0, 0, 0, 0)) {
    print_message(stderr, NSSM_MESSAGE_CHANGESERVICECONFIG_FAILED, error_string(GetLastError()));
    return -1;
  }

  return 1;
}

int native_get_type(const TCHAR *service_name, void *param, const TCHAR *name, void *default_value, value_t *value, const TCHAR *additional) {
  SC_HANDLE service_handle = (SC_HANDLE) param;
  if (! service_handle) return -1;

  QUERY_SERVICE_CONFIG *qsc = query_service_config(service_name, service_handle);
  if (! qsc) return -1;

  value->numeric = qsc->dwServiceType;
  HeapFree(GetProcessHeap(), 0, qsc);

  const TCHAR *string;
  switch (value->numeric) {
    case SERVICE_KERNEL_DRIVER: string = NSSM_KERNEL_DRIVER; break;
    case SERVICE_FILE_SYSTEM_DRIVER: string = NSSM_FILE_SYSTEM_DRIVER; break;
    case SERVICE_WIN32_OWN_PROCESS: string = NSSM_WIN32_OWN_PROCESS; break;
    case SERVICE_WIN32_SHARE_PROCESS: string = NSSM_WIN32_SHARE_PROCESS; break;
    case SERVICE_WIN32_OWN_PROCESS|SERVICE_INTERACTIVE_PROCESS: string = NSSM_INTERACTIVE_PROCESS; break;
    case SERVICE_WIN32_SHARE_PROCESS|SERVICE_INTERACTIVE_PROCESS: string = NSSM_SHARE_INTERACTIVE_PROCESS; break;
    default: string = NSSM_UNKNOWN;
  }

  return value_from_string(name, value, string);
}

int set_setting(const TCHAR *service_name, HKEY key, settings_t *setting, value_t *value, const TCHAR *additional) {
  if (! key) return -1;
  int ret;

  if (setting->set) ret = setting->set(service_name, (void *) key, setting->name, setting->default_value, value, additional);
  else ret = -1;

  if (! ret) print_message(stdout, NSSM_MESSAGE_RESET_SETTING, setting->name, service_name);
  else if (ret > 0) print_message(stdout, NSSM_MESSAGE_SET_SETTING, setting->name, service_name);
  else print_message(stderr, NSSM_MESSAGE_SET_SETTING_FAILED, setting->name, service_name);

  return ret;
}

int set_setting(const TCHAR *service_name, SC_HANDLE service_handle, settings_t *setting, value_t *value, const TCHAR *additional) {
  if (! service_handle) return -1;

  int ret;
  if (setting->set) ret = setting->set(service_name, service_handle, setting->name, setting->default_value, value, additional);
  else ret = -1;

  if (! ret) print_message(stdout, NSSM_MESSAGE_RESET_SETTING, setting->name, service_name);
  else if (ret > 0) print_message(stdout, NSSM_MESSAGE_SET_SETTING, setting->name, service_name);
  else print_message(stderr, NSSM_MESSAGE_SET_SETTING_FAILED, setting->name, service_name);

  return ret;
}

/*
  Returns:  1 if the value was retrieved.
            0 if the default value was retrieved.
           -1 on error.
*/
int get_setting(const TCHAR *service_name, HKEY key, settings_t *setting, value_t *value, const TCHAR *additional) {
  if (! key) return -1;
  int ret;

  if (is_string_type(setting->type)) {
    value->string = (TCHAR *) setting->default_value;
    if (setting->get) ret = setting->get(service_name, (void *) key, setting->name, setting->default_value, value, additional);
    else ret = -1;
  }
  else if (is_numeric_type(setting->type)) {
    value->numeric = PtrToUlong(setting->default_value);
    if (setting->get) ret = setting->get(service_name, (void *) key, setting->name, setting->default_value, value, additional);
    else ret = -1;
  }
  else ret = -1;

  if (ret < 0) print_message(stderr, NSSM_MESSAGE_GET_SETTING_FAILED, setting->name, service_name);

  return ret;
}

int get_setting(const TCHAR *service_name, SC_HANDLE service_handle, settings_t *setting, value_t *value, const TCHAR *additional) {
  if (! service_handle) return -1;
  return setting->get(service_name, service_handle, setting->name, 0, value, additional);
}

int dump_setting(const TCHAR *service_name, HKEY key, SC_HANDLE service_handle, settings_t *setting) {
  void *param;
  if (setting->native) {
    if (! service_handle) return -1;
    param = (void *) service_handle;
  }
  else {
    /* Will be null for native services. */
    param = (void *) key;
  }

  value_t value = { 0 };
  int ret;

  if (setting->dump) return setting->dump(service_name, param, setting->name, (void *) setting, &value, 0);
  if (setting->native) ret = get_setting(service_name, service_handle, setting, &value, 0);
  else ret = get_setting(service_name, key, setting, &value, 0);
  if (ret != 1) return ret;
  return setting_dump_string(service_name, (void *) setting->type, setting->name, &value, 0);
}

settings_t settings[] = {
  { NSSM_REG_EXE, REG_EXPAND_SZ, (void *) _T(""), false, 0, setting_set_string, setting_get_string, setting_not_dumpable },
  { NSSM_REG_FLAGS, REG_EXPAND_SZ, (void *) _T(""), false, 0, setting_set_string, setting_get_string, 0 },
  { NSSM_REG_DIR, REG_EXPAND_SZ, (void *) _T(""), false, 0, setting_set_string, setting_get_string, 0 },
  { NSSM_REG_EXIT, REG_SZ, (void *) exit_action_strings[NSSM_EXIT_RESTART], false, ADDITIONAL_MANDATORY, setting_set_exit_action, setting_get_exit_action, setting_dump_exit_action },
  { NSSM_REG_HOOK, REG_SZ, (void *) _T(""), false, ADDITIONAL_MANDATORY, setting_set_hook, setting_get_hook, setting_dump_hooks },
  { NSSM_REG_AFFINITY, REG_SZ, 0, false, 0, setting_set_affinity, setting_get_affinity, 0 },
  { NSSM_REG_ENV, REG_MULTI_SZ, NULL, false, ADDITIONAL_CRLF, setting_set_environment, setting_get_environment, setting_dump_environment },
  { NSSM_REG_ENV_EXTRA, REG_MULTI_SZ, NULL, false, ADDITIONAL_CRLF, setting_set_environment, setting_get_environment, setting_dump_environment },
  { NSSM_REG_NO_CONSOLE, REG_DWORD, 0, false, 0, setting_set_number, setting_get_number, 0 },
  { NSSM_REG_PRIORITY, REG_SZ, (void *) priority_strings[NSSM_NORMAL_PRIORITY], false, 0, setting_set_priority, setting_get_priority, setting_dump_priority },
  { NSSM_REG_RESTART_DELAY, REG_DWORD, 0, false, 0, setting_set_number, setting_get_number, 0 },
  { NSSM_REG_STDIN, REG_EXPAND_SZ, NULL, false, 0, setting_set_string, setting_get_string, 0 },
  { NSSM_REG_STDIN NSSM_REG_STDIO_SHARING, REG_DWORD, (void *) NSSM_STDIN_SHARING, false, 0, setting_set_number, setting_get_number, 0 },
  { NSSM_REG_STDIN NSSM_REG_STDIO_DISPOSITION, REG_DWORD, (void *) NSSM_STDIN_DISPOSITION, false, 0, setting_set_number, setting_get_number, 0 },
  { NSSM_REG_STDIN NSSM_REG_STDIO_FLAGS, REG_DWORD, (void *) NSSM_STDIN_FLAGS, false, 0, setting_set_number, setting_get_number, 0 },
  { NSSM_REG_STDOUT, REG_EXPAND_SZ, NULL, false, 0, setting_set_string, setting_get_string, 0 },
  { NSSM_REG_STDOUT NSSM_REG_STDIO_SHARING, REG_DWORD, (void *) NSSM_STDOUT_SHARING, false, 0, setting_set_number, setting_get_number, 0 },
  { NSSM_REG_STDOUT NSSM_REG_STDIO_DISPOSITION, REG_DWORD, (void *) NSSM_STDOUT_DISPOSITION, false, 0, setting_set_number, setting_get_number, 0 },
  { NSSM_REG_STDOUT NSSM_REG_STDIO_FLAGS, REG_DWORD, (void *) NSSM_STDOUT_FLAGS, false, 0, setting_set_number, setting_get_number, 0 },
  { NSSM_REG_STDOUT NSSM_REG_STDIO_COPY_AND_TRUNCATE, REG_DWORD, 0, false, 0, setting_set_number, setting_get_number, 0 },
  { NSSM_REG_STDERR, REG_EXPAND_SZ, NULL, false, 0, setting_set_string, setting_get_string, 0 },
  { NSSM_REG_STDERR NSSM_REG_STDIO_SHARING, REG_DWORD, (void *) NSSM_STDERR_SHARING, false, 0, setting_set_number, setting_get_number, 0 },
  { NSSM_REG_STDERR NSSM_REG_STDIO_DISPOSITION, REG_DWORD, (void *) NSSM_STDERR_DISPOSITION, false, 0, setting_set_number, setting_get_number, 0 },
  { NSSM_REG_STDERR NSSM_REG_STDIO_FLAGS, REG_DWORD, (void *) NSSM_STDERR_FLAGS, false, 0, setting_set_number, setting_get_number, 0 },
  { NSSM_REG_STDERR NSSM_REG_STDIO_COPY_AND_TRUNCATE, REG_DWORD, 0, false, 0, setting_set_number, setting_get_number, 0 },
  { NSSM_REG_STOP_METHOD_SKIP, REG_DWORD, 0, false, 0, setting_set_number, setting_get_number, 0 },
  { NSSM_REG_KILL_CONSOLE_GRACE_PERIOD, REG_DWORD, (void *) NSSM_KILL_CONSOLE_GRACE_PERIOD, false, 0, setting_set_number, setting_get_number, 0 },
  { NSSM_REG_KILL_WINDOW_GRACE_PERIOD, REG_DWORD, (void *) NSSM_KILL_WINDOW_GRACE_PERIOD, false, 0, setting_set_number, setting_get_number, 0 },
  { NSSM_REG_KILL_THREADS_GRACE_PERIOD, REG_DWORD, (void *) NSSM_KILL_THREADS_GRACE_PERIOD, false, 0, setting_set_number, setting_get_number, 0 },
  { NSSM_REG_KILL_PROCESS_TREE, REG_DWORD, (void *) 1, false, 0, setting_set_number, setting_get_number, 0 },
  { NSSM_REG_THROTTLE, REG_DWORD, (void *) NSSM_RESET_THROTTLE_RESTART, false, 0, setting_set_number, setting_get_number, 0 },
  { NSSM_REG_HOOK_SHARE_OUTPUT_HANDLES, REG_DWORD, 0, false, 0, setting_set_number, setting_get_number, 0 },
  { NSSM_REG_ROTATE, REG_DWORD, 0, false, 0, setting_set_number, setting_get_number, 0 },
  { NSSM_REG_ROTATE_ONLINE, REG_DWORD, 0, false, 0, setting_set_number, setting_get_number, 0 },
  { NSSM_REG_ROTATE_SECONDS, REG_DWORD, 0, false, 0, setting_set_number, setting_get_number, 0 },
  { NSSM_REG_ROTATE_BYTES_LOW, REG_DWORD, 0, false, 0, setting_set_number, setting_get_number, 0 },
  { NSSM_REG_ROTATE_BYTES_HIGH, REG_DWORD, 0, false, 0, setting_set_number, setting_get_number, 0 },
  { NSSM_REG_ROTATE_DELAY, REG_DWORD, (void *) NSSM_ROTATE_DELAY, false, 0, setting_set_number, setting_get_number, 0 },
  { NSSM_REG_TIMESTAMP_LOG, REG_DWORD, 0, false, 0, setting_set_number, setting_get_number, 0 },
  { NSSM_NATIVE_DEPENDONGROUP, REG_MULTI_SZ, NULL, true, ADDITIONAL_CRLF, native_set_dependongroup, native_get_dependongroup, native_dump_dependongroup },
  { NSSM_NATIVE_DEPENDONSERVICE, REG_MULTI_SZ, NULL, true, ADDITIONAL_CRLF, native_set_dependonservice, native_get_dependonservice, native_dump_dependonservice },
  { NSSM_NATIVE_DESCRIPTION, REG_SZ, _T(""), true, 0, native_set_description, native_get_description, 0 },
  { NSSM_NATIVE_DISPLAYNAME, REG_SZ, NULL, true, 0, native_set_displayname, native_get_displayname, 0 },
  { NSSM_NATIVE_ENVIRONMENT, REG_MULTI_SZ, NULL, true, ADDITIONAL_CRLF, native_set_environment, native_get_environment, native_dump_environment },
  { NSSM_NATIVE_IMAGEPATH, REG_EXPAND_SZ, NULL, true, 0, native_set_imagepath, native_get_imagepath, setting_not_dumpable },
  { NSSM_NATIVE_OBJECTNAME, REG_SZ, NSSM_LOCALSYSTEM_ACCOUNT, true, 0, native_set_objectname, native_get_objectname, native_dump_objectname },
  { NSSM_NATIVE_NAME, REG_SZ, NULL, true, 0, native_set_name, native_get_name, setting_not_dumpable },
  { NSSM_NATIVE_STARTUP, REG_SZ, NULL, true, 0, native_set_startup, native_get_startup, 0 },
  { NSSM_NATIVE_TYPE, REG_SZ, NULL, true, 0, native_set_type, native_get_type, 0 },
  { NULL, NULL, NULL, NULL, NULL }
};
