#include "nssm.h"

extern const TCHAR *hook_event_strings[];
extern const TCHAR *hook_action_strings[];

static enum { NSSM_TAB_APPLICATION, NSSM_TAB_DETAILS, NSSM_TAB_LOGON, NSSM_TAB_DEPENDENCIES, NSSM_TAB_PROCESS, NSSM_TAB_SHUTDOWN, NSSM_TAB_EXIT, NSSM_TAB_IO, NSSM_TAB_ROTATION, NSSM_TAB_ENVIRONMENT, NSSM_TAB_HOOKS, NSSM_NUM_TABS } nssm_tabs;
static HWND tablist[NSSM_NUM_TABS];
static int selected_tab;

static HWND dialog(const TCHAR *templ, HWND parent, DLGPROC function, LPARAM l) {
  /* The caller will deal with GetLastError()... */
  HRSRC resource = FindResourceEx(0, RT_DIALOG, templ, GetUserDefaultLangID());
  if (! resource) {
    if (GetLastError() != ERROR_RESOURCE_LANG_NOT_FOUND) return 0;
    resource = FindResourceEx(0, RT_DIALOG, templ, MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL));
    if (! resource) return 0;
  }

  HGLOBAL ret = LoadResource(0, resource);
  if (! ret) return 0;

  return CreateDialogIndirectParam(0, (DLGTEMPLATE *) ret, parent, function, l);
}

static HWND dialog(const TCHAR *templ, HWND parent, DLGPROC function) {
  return dialog(templ, parent, function, 0);
}

static inline void set_logon_enabled(unsigned char interact_enabled, unsigned char credentials_enabled) {
  EnableWindow(GetDlgItem(tablist[NSSM_TAB_LOGON], IDC_INTERACT), interact_enabled);
  EnableWindow(GetDlgItem(tablist[NSSM_TAB_LOGON], IDC_USERNAME), credentials_enabled);
  EnableWindow(GetDlgItem(tablist[NSSM_TAB_LOGON], IDC_PASSWORD1), credentials_enabled);
  EnableWindow(GetDlgItem(tablist[NSSM_TAB_LOGON], IDC_PASSWORD2), credentials_enabled);
}

int nssm_gui(int resource, nssm_service_t *service) {
  /* Create window */
  HWND dlg = dialog(MAKEINTRESOURCE(resource), 0, nssm_dlg, (LPARAM) service);
  if (! dlg) {
    popup_message(0, MB_OK, NSSM_GUI_CREATEDIALOG_FAILED, error_string(GetLastError()));
    return 1;
  }

  /* Load the icon. */
  HANDLE icon = LoadImage(GetModuleHandle(0), MAKEINTRESOURCE(IDI_NSSM), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
  if (icon) SendMessage(dlg, WM_SETICON, ICON_SMALL, (LPARAM) icon);
  icon = LoadImage(GetModuleHandle(0), MAKEINTRESOURCE(IDI_NSSM), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0);
  if (icon) SendMessage(dlg, WM_SETICON, ICON_BIG, (LPARAM) icon);

  /* Remember what the window is for. */
  SetWindowLongPtr(dlg, GWLP_USERDATA, (LONG_PTR) resource);

  /* Display the window */
  centre_window(dlg);
  ShowWindow(dlg, SW_SHOW);

  /* Set service name if given */
  if (service->name[0]) {
    SetDlgItemText(dlg, IDC_NAME, service->name);
    /* No point making user click remove if the name is already entered */
    if (resource == IDD_REMOVE) {
      HWND button = GetDlgItem(dlg, IDC_REMOVE);
      if (button) {
        SendMessage(button, WM_LBUTTONDOWN, 0, 0);
        SendMessage(button, WM_LBUTTONUP, 0, 0);
      }
    }
  }

  if (resource == IDD_EDIT) {
    /* We'll need the service handle later. */
    SetWindowLongPtr(dlg, DWLP_USER, (LONG_PTR) service);

    /* Service name can't be edited. */
    EnableWindow(GetDlgItem(dlg, IDC_NAME), 0);
    SetFocus(GetDlgItem(dlg, IDOK));

    /* Set existing details. */
    HWND combo;
    HWND list;

    /* Application tab. */
    if (service->native) SetDlgItemText(tablist[NSSM_TAB_APPLICATION], IDC_PATH, service->image);
    else SetDlgItemText(tablist[NSSM_TAB_APPLICATION], IDC_PATH, service->exe);
    SetDlgItemText(tablist[NSSM_TAB_APPLICATION], IDC_DIR, service->dir);
    SetDlgItemText(tablist[NSSM_TAB_APPLICATION], IDC_FLAGS, service->flags);

    /* Details tab. */
    SetDlgItemText(tablist[NSSM_TAB_DETAILS], IDC_DISPLAYNAME, service->displayname);
    SetDlgItemText(tablist[NSSM_TAB_DETAILS], IDC_DESCRIPTION, service->description);
    combo = GetDlgItem(tablist[NSSM_TAB_DETAILS], IDC_STARTUP);
    SendMessage(combo, CB_SETCURSEL, service->startup, 0);

    /* Log on tab. */
    if (service->username) {
      if (is_virtual_account(service->name, service->username)) {
        CheckRadioButton(tablist[NSSM_TAB_LOGON], IDC_LOCALSYSTEM, IDC_VIRTUAL_SERVICE, IDC_VIRTUAL_SERVICE);
        set_logon_enabled(0, 0);
      }
      else {
        CheckRadioButton(tablist[NSSM_TAB_LOGON], IDC_LOCALSYSTEM, IDC_VIRTUAL_SERVICE, IDC_ACCOUNT);
        SetDlgItemText(tablist[NSSM_TAB_LOGON], IDC_USERNAME, service->username);
        set_logon_enabled(0, 1);
      }
    }
    else {
      CheckRadioButton(tablist[NSSM_TAB_LOGON], IDC_LOCALSYSTEM, IDC_VIRTUAL_SERVICE, IDC_LOCALSYSTEM);
      if (service->type & SERVICE_INTERACTIVE_PROCESS) SendDlgItemMessage(tablist[NSSM_TAB_LOGON], IDC_INTERACT, BM_SETCHECK, BST_CHECKED, 0);
    }

    /* Dependencies tab. */
    if (service->dependencieslen) {
      TCHAR *formatted;
      unsigned long newlen;
      if (format_double_null(service->dependencies, service->dependencieslen, &formatted, &newlen)) {
        popup_message(dlg, MB_OK | MB_ICONEXCLAMATION, NSSM_EVENT_OUT_OF_MEMORY, _T("dependencies"), _T("nssm_dlg()"));
      }
      else {
        SetDlgItemText(tablist[NSSM_TAB_DEPENDENCIES], IDC_DEPENDENCIES, formatted);
        HeapFree(GetProcessHeap(), 0, formatted);
      }
    }

    /* Process tab. */
    if (service->priority) {
      int priority = priority_constant_to_index(service->priority);
      combo = GetDlgItem(tablist[NSSM_TAB_PROCESS], IDC_PRIORITY);
      SendMessage(combo, CB_SETCURSEL, priority, 0);
    }

    if (service->affinity) {
      list = GetDlgItem(tablist[NSSM_TAB_PROCESS], IDC_AFFINITY);
      SendDlgItemMessage(tablist[NSSM_TAB_PROCESS], IDC_AFFINITY_ALL, BM_SETCHECK, BST_UNCHECKED, 0);
      EnableWindow(GetDlgItem(tablist[NSSM_TAB_PROCESS], IDC_AFFINITY), 1);

      DWORD_PTR affinity, system_affinity;
      if (GetProcessAffinityMask(GetCurrentProcess(), &affinity, &system_affinity)) {
        if ((service->affinity & (__int64) system_affinity) != service->affinity) popup_message(dlg, MB_OK | MB_ICONWARNING, NSSM_GUI_WARN_AFFINITY);
      }

      for (int i = 0; i < num_cpus(); i++) {
        if (! (service->affinity & (1LL << (__int64) i))) SendMessage(list, LB_SETSEL, 0, i);
      }
    }

    if (service->no_console) {
      SendDlgItemMessage(tablist[NSSM_TAB_PROCESS], IDC_CONSOLE, BM_SETCHECK, BST_UNCHECKED, 0);
    }

    /* Shutdown tab. */
    if (! (service->stop_method & NSSM_STOP_METHOD_CONSOLE)) {
      SendDlgItemMessage(tablist[NSSM_TAB_SHUTDOWN], IDC_METHOD_CONSOLE, BM_SETCHECK, BST_UNCHECKED, 0);
      EnableWindow(GetDlgItem(tablist[NSSM_TAB_SHUTDOWN], IDC_KILL_CONSOLE), 0);
    }
    SetDlgItemInt(tablist[NSSM_TAB_SHUTDOWN], IDC_KILL_CONSOLE, service->kill_console_delay, 0);
    if (! (service->stop_method & NSSM_STOP_METHOD_WINDOW)) {
      SendDlgItemMessage(tablist[NSSM_TAB_SHUTDOWN], IDC_METHOD_WINDOW, BM_SETCHECK, BST_UNCHECKED, 0);
      EnableWindow(GetDlgItem(tablist[NSSM_TAB_SHUTDOWN], IDC_KILL_WINDOW), 0);
    }
    SetDlgItemInt(tablist[NSSM_TAB_SHUTDOWN], IDC_KILL_WINDOW, service->kill_window_delay, 0);
    if (! (service->stop_method & NSSM_STOP_METHOD_THREADS)) {
      SendDlgItemMessage(tablist[NSSM_TAB_SHUTDOWN], IDC_METHOD_THREADS, BM_SETCHECK, BST_UNCHECKED, 0);
      EnableWindow(GetDlgItem(tablist[NSSM_TAB_SHUTDOWN], IDC_KILL_THREADS), 0);
    }
    SetDlgItemInt(tablist[NSSM_TAB_SHUTDOWN], IDC_KILL_THREADS, service->kill_threads_delay, 0);
    if (! (service->stop_method & NSSM_STOP_METHOD_TERMINATE)) {
      SendDlgItemMessage(tablist[NSSM_TAB_SHUTDOWN], IDC_METHOD_TERMINATE, BM_SETCHECK, BST_UNCHECKED, 0);
    }
    if (! service->kill_process_tree) {
      SendDlgItemMessage(tablist[NSSM_TAB_SHUTDOWN], IDC_KILL_PROCESS_TREE, BM_SETCHECK, BST_UNCHECKED, 0);
    }

    /* Restart tab. */
    SetDlgItemInt(tablist[NSSM_TAB_EXIT], IDC_THROTTLE, service->throttle_delay, 0);
    combo = GetDlgItem(tablist[NSSM_TAB_EXIT], IDC_APPEXIT);
    SendMessage(combo, CB_SETCURSEL, service->default_exit_action, 0);
    SetDlgItemInt(tablist[NSSM_TAB_EXIT], IDC_RESTART_DELAY, service->restart_delay, 0);

    /* I/O tab. */
    SetDlgItemText(tablist[NSSM_TAB_IO], IDC_STDIN, service->stdin_path);
    SetDlgItemText(tablist[NSSM_TAB_IO], IDC_STDOUT, service->stdout_path);
    SetDlgItemText(tablist[NSSM_TAB_IO], IDC_STDERR, service->stderr_path);
    if (service->timestamp_log) SendDlgItemMessage(tablist[NSSM_TAB_IO], IDC_TIMESTAMP, BM_SETCHECK, BST_CHECKED, 0);

    /* Rotation tab. */
    if (service->stdout_disposition == CREATE_ALWAYS) SendDlgItemMessage(tablist[NSSM_TAB_ROTATION], IDC_TRUNCATE, BM_SETCHECK, BST_CHECKED, 0);
    if (service->rotate_files) {
      SendDlgItemMessage(tablist[NSSM_TAB_ROTATION], IDC_ROTATE, BM_SETCHECK, BST_CHECKED, 0);
      EnableWindow(GetDlgItem(tablist[NSSM_TAB_ROTATION], IDC_ROTATE_ONLINE), 1);
      EnableWindow(GetDlgItem(tablist[NSSM_TAB_ROTATION], IDC_ROTATE_SECONDS), 1);
      EnableWindow(GetDlgItem(tablist[NSSM_TAB_ROTATION], IDC_ROTATE_BYTES_LOW), 1);
    }
    if (service->rotate_stdout_online || service->rotate_stderr_online) SendDlgItemMessage(tablist[NSSM_TAB_ROTATION], IDC_ROTATE_ONLINE, BM_SETCHECK, BST_CHECKED, 0);
    SetDlgItemInt(tablist[NSSM_TAB_ROTATION], IDC_ROTATE_SECONDS, service->rotate_seconds, 0);
    if (! service->rotate_bytes_high) SetDlgItemInt(tablist[NSSM_TAB_ROTATION], IDC_ROTATE_BYTES_LOW, service->rotate_bytes_low, 0);

    /* Hooks tab. */
    if (service->hook_share_output_handles) SendDlgItemMessage(tablist[NSSM_TAB_HOOKS], IDC_REDIRECT_HOOK, BM_SETCHECK, BST_CHECKED, 0);

    /* Check if advanced settings are in use. */
    if (service->stdout_disposition != service->stderr_disposition || (service->stdout_disposition && service->stdout_disposition != NSSM_STDOUT_DISPOSITION && service->stdout_disposition != CREATE_ALWAYS) || (service->stderr_disposition && service->stderr_disposition != NSSM_STDERR_DISPOSITION && service->stderr_disposition != CREATE_ALWAYS)) popup_message(dlg, MB_OK | MB_ICONWARNING, NSSM_GUI_WARN_STDIO);
    if (service->rotate_bytes_high) popup_message(dlg, MB_OK | MB_ICONWARNING, NSSM_GUI_WARN_ROTATE_BYTES);

    /* Environment tab. */
    TCHAR *env;
    unsigned long envlen;
    if (service->env_extralen) {
      env = service->env_extra;
      envlen = service->env_extralen;
    }
    else {
      env = service->env;
      envlen = service->envlen;
      if (envlen) SendDlgItemMessage(tablist[NSSM_TAB_ENVIRONMENT], IDC_ENVIRONMENT_REPLACE, BM_SETCHECK, BST_CHECKED, 0);
    }

    if (envlen) {
      TCHAR *formatted;
      unsigned long newlen;
      if (format_double_null(env, envlen, &formatted, &newlen)) {
        popup_message(dlg, MB_OK | MB_ICONEXCLAMATION, NSSM_EVENT_OUT_OF_MEMORY, _T("environment"), _T("nssm_dlg()"));
      }
      else {
        SetDlgItemText(tablist[NSSM_TAB_ENVIRONMENT], IDC_ENVIRONMENT, formatted);
        HeapFree(GetProcessHeap(), 0, formatted);
      }
    }
    if (service->envlen && service->env_extralen) popup_message(dlg, MB_OK | MB_ICONWARNING, NSSM_GUI_WARN_ENVIRONMENT);
  }

  /* Go! */
  MSG message;
  while (GetMessage(&message, 0, 0, 0)) {
    if (IsDialogMessage(dlg, &message)) continue;
    TranslateMessage(&message);
    DispatchMessage(&message);
  }

  return (int) message.wParam;
}

void centre_window(HWND window) {
  HWND desktop;
  RECT size, desktop_size;
  unsigned long x, y;

  if (! window) return;

  /* Find window size */
  if (! GetWindowRect(window, &size)) return;

  /* Find desktop window */
  desktop = GetDesktopWindow();
  if (! desktop) return;

  /* Find desktop window size */
  if (! GetWindowRect(desktop, &desktop_size)) return;

  /* Centre window */
  x = (desktop_size.right - size.right) / 2;
  y = (desktop_size.bottom - size.bottom) / 2;
  MoveWindow(window, x, y, size.right - size.left, size.bottom - size.top, 0);
}

static inline void check_stop_method(nssm_service_t *service, unsigned long method, unsigned long control) {
  if (SendDlgItemMessage(tablist[NSSM_TAB_SHUTDOWN], control, BM_GETCHECK, 0, 0) & BST_CHECKED) return;
  service->stop_method &= ~method;
}

static inline void check_number(HWND tab, unsigned long control, unsigned long *timeout) {
  BOOL translated;
  unsigned long configured = GetDlgItemInt(tab, control, &translated, 0);
  if (translated) *timeout = configured;
}

static inline void set_timeout_enabled(unsigned long control, unsigned long dependent) {
  unsigned char enabled = 0;
  if (SendDlgItemMessage(tablist[NSSM_TAB_SHUTDOWN], control, BM_GETCHECK, 0, 0) & BST_CHECKED) enabled = 1;
  EnableWindow(GetDlgItem(tablist[NSSM_TAB_SHUTDOWN], dependent), enabled);
}

static inline void set_affinity_enabled(unsigned char enabled) {
  EnableWindow(GetDlgItem(tablist[NSSM_TAB_PROCESS], IDC_AFFINITY), enabled);
}

static inline void set_rotation_enabled(unsigned char enabled) {
  EnableWindow(GetDlgItem(tablist[NSSM_TAB_ROTATION], IDC_ROTATE_ONLINE), enabled);
  EnableWindow(GetDlgItem(tablist[NSSM_TAB_ROTATION], IDC_ROTATE_SECONDS), enabled);
  EnableWindow(GetDlgItem(tablist[NSSM_TAB_ROTATION], IDC_ROTATE_BYTES_LOW), enabled);
}

static inline int hook_env(const TCHAR *hook_event, const TCHAR *hook_action, TCHAR *buffer, unsigned long buflen) {
  return _sntprintf_s(buffer, buflen, _TRUNCATE, _T("NSSM_HOOK_%s_%s"), hook_event, hook_action);
}

static inline void set_hook_tab(int event_index, int action_index, bool changed) {
  int first_event = NSSM_GUI_HOOK_EVENT_START;
  HWND combo;
  combo = GetDlgItem(tablist[NSSM_TAB_HOOKS], IDC_HOOK_EVENT);
  SendMessage(combo, CB_SETCURSEL, event_index, 0);
  combo = GetDlgItem(tablist[NSSM_TAB_HOOKS], IDC_HOOK_ACTION);
  SendMessage(combo, CB_RESETCONTENT, 0, 0);

  const TCHAR *hook_event = hook_event_strings[event_index];
  TCHAR *hook_action;
  int i;
  switch (event_index + first_event) {
    case NSSM_GUI_HOOK_EVENT_ROTATE:
      i = 0;
      SendMessage(combo, CB_INSERTSTRING, i, (LPARAM) message_string(NSSM_GUI_HOOK_ACTION_ROTATE_PRE));
      if (action_index == i++) hook_action = NSSM_HOOK_ACTION_PRE;
      SendMessage(combo, CB_INSERTSTRING, i, (LPARAM) message_string(NSSM_GUI_HOOK_ACTION_ROTATE_POST));
      if (action_index == i++) hook_action = NSSM_HOOK_ACTION_POST;
      break;

    case NSSM_GUI_HOOK_EVENT_START:
      i = 0;
      SendMessage(combo, CB_INSERTSTRING, i, (LPARAM) message_string(NSSM_GUI_HOOK_ACTION_START_PRE));
      if (action_index == i++) hook_action = NSSM_HOOK_ACTION_PRE;
      SendMessage(combo, CB_INSERTSTRING, i, (LPARAM) message_string(NSSM_GUI_HOOK_ACTION_START_POST));
      if (action_index == i++) hook_action = NSSM_HOOK_ACTION_POST;
      break;

    case NSSM_GUI_HOOK_EVENT_STOP:
      i = 0;
      SendMessage(combo, CB_INSERTSTRING, i, (LPARAM) message_string(NSSM_GUI_HOOK_ACTION_STOP_PRE));
      if (action_index == i++) hook_action = NSSM_HOOK_ACTION_PRE;
      break;

    case NSSM_GUI_HOOK_EVENT_EXIT:
      i = 0;
      SendMessage(combo, CB_INSERTSTRING, i, (LPARAM) message_string(NSSM_GUI_HOOK_ACTION_EXIT_POST));
      if (action_index == i++) hook_action = NSSM_HOOK_ACTION_POST;
      break;

    case NSSM_GUI_HOOK_EVENT_POWER:
      i = 0;
      SendMessage(combo, CB_INSERTSTRING, i, (LPARAM) message_string(NSSM_GUI_HOOK_ACTION_POWER_CHANGE));
      if (action_index == i++) hook_action = NSSM_HOOK_ACTION_CHANGE;
      SendMessage(combo, CB_INSERTSTRING, i, (LPARAM) message_string(NSSM_GUI_HOOK_ACTION_POWER_RESUME));
      if (action_index == i++) hook_action = NSSM_HOOK_ACTION_RESUME;
      break;
  }

  SendMessage(combo, CB_SETCURSEL, action_index, 0);

  TCHAR hook_name[HOOK_NAME_LENGTH];
  hook_env(hook_event, hook_action, hook_name, _countof(hook_name));

  if (! *hook_name) return;

  TCHAR cmd[CMD_LENGTH];
  if (changed) {
    GetDlgItemText(tablist[NSSM_TAB_HOOKS], IDC_HOOK, cmd, _countof(cmd));
    SetEnvironmentVariable(hook_name, cmd);
  }
  else {
    if (! GetEnvironmentVariable(hook_name, cmd, _countof(cmd))) cmd[0] = _T('\0');
    SetDlgItemText(tablist[NSSM_TAB_HOOKS], IDC_HOOK, cmd);
  }
}

static inline int update_hook(TCHAR *service_name, const TCHAR *hook_event, const TCHAR *hook_action) {
  TCHAR hook_name[HOOK_NAME_LENGTH];
  if (hook_env(hook_event, hook_action, hook_name, _countof(hook_name)) < 0) return 1;
  TCHAR cmd[CMD_LENGTH];
  ZeroMemory(cmd, sizeof(cmd));
  GetEnvironmentVariable(hook_name, cmd, _countof(cmd));
  if (set_hook(service_name, hook_event, hook_action, cmd)) return 2;
  return 0;
}

static inline int update_hooks(TCHAR *service_name) {
  int ret = 0;
  ret += update_hook(service_name, NSSM_HOOK_EVENT_START, NSSM_HOOK_ACTION_PRE);
  ret += update_hook(service_name, NSSM_HOOK_EVENT_START, NSSM_HOOK_ACTION_POST);
  ret += update_hook(service_name, NSSM_HOOK_EVENT_STOP, NSSM_HOOK_ACTION_PRE);
  ret += update_hook(service_name, NSSM_HOOK_EVENT_EXIT, NSSM_HOOK_ACTION_POST);
  ret += update_hook(service_name, NSSM_HOOK_EVENT_POWER, NSSM_HOOK_ACTION_CHANGE);
  ret += update_hook(service_name, NSSM_HOOK_EVENT_POWER, NSSM_HOOK_ACTION_RESUME);
  ret += update_hook(service_name, NSSM_HOOK_EVENT_ROTATE, NSSM_HOOK_ACTION_PRE);
  ret += update_hook(service_name, NSSM_HOOK_EVENT_ROTATE, NSSM_HOOK_ACTION_POST);
  return ret;
}

static inline void check_io(HWND owner, TCHAR *name, TCHAR *buffer, unsigned long len, unsigned long control) {
  if (! SendMessage(GetDlgItem(tablist[NSSM_TAB_IO], control), WM_GETTEXTLENGTH, 0, 0)) return;
  if (GetDlgItemText(tablist[NSSM_TAB_IO], control, buffer, (int) len)) return;
  popup_message(owner, MB_OK | MB_ICONEXCLAMATION, NSSM_MESSAGE_PATH_TOO_LONG, name);
  ZeroMemory(buffer, len * sizeof(TCHAR));
}

/* Set service parameters. */
int configure(HWND window, nssm_service_t *service, nssm_service_t *orig_service) {
  if (! service) return 1;

  set_nssm_service_defaults(service);

  if (orig_service) {
    service->native = orig_service->native;
    service->handle = orig_service->handle;
  }

  /* Get service name. */
  if (! GetDlgItemText(window, IDC_NAME, service->name, _countof(service->name))) {
    popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_GUI_MISSING_SERVICE_NAME);
    cleanup_nssm_service(service);
    return 2;
  }

  /* Get executable name */
  if (! service->native) {
    if (! GetDlgItemText(tablist[NSSM_TAB_APPLICATION], IDC_PATH, service->exe, _countof(service->exe))) {
      popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_GUI_MISSING_PATH);
      return 3;
    }

    /* Get startup directory. */
    if (! GetDlgItemText(tablist[NSSM_TAB_APPLICATION], IDC_DIR, service->dir, _countof(service->dir))) {
      _sntprintf_s(service->dir, _countof(service->dir), _TRUNCATE, _T("%s"), service->exe);
      strip_basename(service->dir);
    }

    /* Get flags. */
    if (SendMessage(GetDlgItem(tablist[NSSM_TAB_APPLICATION], IDC_FLAGS), WM_GETTEXTLENGTH, 0, 0)) {
      if (! GetDlgItemText(tablist[NSSM_TAB_APPLICATION], IDC_FLAGS, service->flags, _countof(service->flags))) {
        popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_GUI_INVALID_OPTIONS);
        return 4;
      }
    }
  }

  /* Get details. */
  if (SendMessage(GetDlgItem(tablist[NSSM_TAB_DETAILS], IDC_DISPLAYNAME), WM_GETTEXTLENGTH, 0, 0)) {
    if (! GetDlgItemText(tablist[NSSM_TAB_DETAILS], IDC_DISPLAYNAME, service->displayname, _countof(service->displayname))) {
      popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_GUI_INVALID_DISPLAYNAME);
      return 5;
    }
  }

  if (SendMessage(GetDlgItem(tablist[NSSM_TAB_DETAILS], IDC_DESCRIPTION), WM_GETTEXTLENGTH, 0, 0)) {
    if (! GetDlgItemText(tablist[NSSM_TAB_DETAILS], IDC_DESCRIPTION, service->description, _countof(service->description))) {
      popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_GUI_INVALID_DESCRIPTION);
      return 5;
    }
  }

  HWND combo = GetDlgItem(tablist[NSSM_TAB_DETAILS], IDC_STARTUP);
  service->startup = (unsigned long) SendMessage(combo, CB_GETCURSEL, 0, 0);
  if (service->startup == CB_ERR) service->startup = 0;

  /* Get logon stuff. */
  if (SendDlgItemMessage(tablist[NSSM_TAB_LOGON], IDC_LOCALSYSTEM, BM_GETCHECK, 0, 0) & BST_CHECKED) {
    if (SendDlgItemMessage(tablist[NSSM_TAB_LOGON], IDC_INTERACT, BM_GETCHECK, 0, 0) & BST_CHECKED) {
      service->type |= SERVICE_INTERACTIVE_PROCESS;
    }
    if (service->username) HeapFree(GetProcessHeap(), 0, service->username);
    service->username = 0;
    service->usernamelen = 0;
    if (service->password) {
      SecureZeroMemory(service->password, service->passwordlen * sizeof(TCHAR));
      HeapFree(GetProcessHeap(), 0, service->password);
    }
    service->password = 0;
    service->passwordlen = 0;
  }
  else if (SendDlgItemMessage(tablist[NSSM_TAB_LOGON], IDC_VIRTUAL_SERVICE, BM_GETCHECK, 0, 0) & BST_CHECKED) {
    if (service->username) HeapFree(GetProcessHeap(), 0, service->username);
    service->username = virtual_account(service->name);
    if (! service->username) {
      popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_EVENT_OUT_OF_MEMORY, _T("account name"), _T("install()"));
      return 6;
    }
    service->usernamelen = _tcslen(service->username) + 1;
    service->password = 0;
    service->passwordlen = 0;
  }
  else {
    /* Username. */
    service->usernamelen = SendMessage(GetDlgItem(tablist[NSSM_TAB_LOGON], IDC_USERNAME), WM_GETTEXTLENGTH, 0, 0);
    if (! service->usernamelen) {
      popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_GUI_MISSING_USERNAME);
      return 6;
    }
    service->usernamelen++;

    service->username = (TCHAR *) HeapAlloc(GetProcessHeap(), 0, service->usernamelen * sizeof(TCHAR));
    if (! service->username) {
      popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_EVENT_OUT_OF_MEMORY, _T("account name"), _T("install()"));
      return 6;
    }
    if (! GetDlgItemText(tablist[NSSM_TAB_LOGON], IDC_USERNAME, service->username, (int) service->usernamelen)) {
      HeapFree(GetProcessHeap(), 0, service->username);
      service->username = 0;
      service->usernamelen = 0;
      popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_GUI_INVALID_USERNAME);
      return 6;
    }

    /*
      Special case for well-known accounts.
      Ignore the password if we're editing and the username hasn't changed.
    */
    const TCHAR *well_known = well_known_username(service->username);
    if (well_known) {
      if (str_equiv(well_known, NSSM_LOCALSYSTEM_ACCOUNT)) {
        HeapFree(GetProcessHeap(), 0, service->username);
        service->username = 0;
        service->usernamelen = 0;
      }
      else {
        service->usernamelen = _tcslen(well_known) + 1;
        service->username = (TCHAR *) HeapAlloc(GetProcessHeap(), 0, service->usernamelen * sizeof(TCHAR));
        if (! service->username) {
          print_message(stderr, NSSM_MESSAGE_OUT_OF_MEMORY, _T("canon"), _T("install()"));
          return 6;
        }
        memmove(service->username, well_known, service->usernamelen * sizeof(TCHAR));
      }
    }
    else {
      /* Password. */
      service->passwordlen = SendMessage(GetDlgItem(tablist[NSSM_TAB_LOGON], IDC_PASSWORD1), WM_GETTEXTLENGTH, 0, 0);
      size_t passwordlen = SendMessage(GetDlgItem(tablist[NSSM_TAB_LOGON], IDC_PASSWORD2), WM_GETTEXTLENGTH, 0, 0);

      if (! orig_service || ! orig_service->username || ! str_equiv(service->username, orig_service->username) || service->passwordlen || passwordlen) {
        if (! service->passwordlen) {
          popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_GUI_MISSING_PASSWORD);
          return 6;
        }
        if (passwordlen != service->passwordlen) {
          popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_GUI_MISSING_PASSWORD);
          return 6;
        }
        service->passwordlen++;

        /* Temporary buffer for password validation. */
        TCHAR *password = (TCHAR *) HeapAlloc(GetProcessHeap(), 0, service->passwordlen * sizeof(TCHAR));
        if (! password) {
          HeapFree(GetProcessHeap(), 0, service->username);
          service->username = 0;
          service->usernamelen = 0;
          popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_EVENT_OUT_OF_MEMORY, _T("password confirmation"), _T("install()"));
          return 6;
        }

        /* Actual password buffer. */
        service->password = (TCHAR *) HeapAlloc(GetProcessHeap(), 0, service->passwordlen * sizeof(TCHAR));
        if (! service->password) {
          HeapFree(GetProcessHeap(), 0, password);
          HeapFree(GetProcessHeap(), 0, service->username);
          service->username = 0;
          service->usernamelen = 0;
          popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_EVENT_OUT_OF_MEMORY, _T("password"), _T("install()"));
          return 6;
        }

        /* Get first password. */
        if (! GetDlgItemText(tablist[NSSM_TAB_LOGON], IDC_PASSWORD1, service->password, (int) service->passwordlen)) {
          HeapFree(GetProcessHeap(), 0, password);
          SecureZeroMemory(service->password, service->passwordlen * sizeof(TCHAR));
          HeapFree(GetProcessHeap(), 0, service->password);
          service->password = 0;
          service->passwordlen = 0;
          HeapFree(GetProcessHeap(), 0, service->username);
          service->username = 0;
          service->usernamelen = 0;
          popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_GUI_INVALID_PASSWORD);
          return 6;
        }

        /* Get confirmation. */
        if (! GetDlgItemText(tablist[NSSM_TAB_LOGON], IDC_PASSWORD2, password, (int) service->passwordlen)) {
          SecureZeroMemory(password, service->passwordlen * sizeof(TCHAR));
          HeapFree(GetProcessHeap(), 0, password);
          SecureZeroMemory(service->password, service->passwordlen * sizeof(TCHAR));
          HeapFree(GetProcessHeap(), 0, service->password);
          service->password = 0;
          service->passwordlen = 0;
          HeapFree(GetProcessHeap(), 0, service->username);
          service->username = 0;
          service->usernamelen = 0;
          popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_GUI_INVALID_PASSWORD);
          return 6;
        }

        /* Compare. */
        if (_tcsncmp(password, service->password, service->passwordlen)) {
          popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_GUI_MISSING_PASSWORD);
          SecureZeroMemory(password, service->passwordlen * sizeof(TCHAR));
          HeapFree(GetProcessHeap(), 0, password);
          SecureZeroMemory(service->password, service->passwordlen * sizeof(TCHAR));
          HeapFree(GetProcessHeap(), 0, service->password);
          service->password = 0;
          service->passwordlen = 0;
          HeapFree(GetProcessHeap(), 0, service->username);
          service->username = 0;
          service->usernamelen = 0;
          return 6;
        }
      }
    }
  }

  /* Get dependencies. */
  unsigned long dependencieslen = (unsigned long) SendMessage(GetDlgItem(tablist[NSSM_TAB_DEPENDENCIES], IDC_DEPENDENCIES), WM_GETTEXTLENGTH, 0, 0);
  if (dependencieslen) {
    TCHAR *dependencies = (TCHAR *) HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (dependencieslen + 2) * sizeof(TCHAR));
    if (! dependencies) {
      popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_EVENT_OUT_OF_MEMORY, _T("dependencies"), _T("install()"));
      cleanup_nssm_service(service);
      return 6;
    }

    if (! GetDlgItemText(tablist[NSSM_TAB_DEPENDENCIES], IDC_DEPENDENCIES, dependencies, dependencieslen + 1)) {
      popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_GUI_INVALID_DEPENDENCIES);
      HeapFree(GetProcessHeap(), 0, dependencies);
      cleanup_nssm_service(service);
      return 6;
    }

    if (unformat_double_null(dependencies, dependencieslen, &service->dependencies, &service->dependencieslen)) {
      HeapFree(GetProcessHeap(), 0, dependencies);
      popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_EVENT_OUT_OF_MEMORY, _T("dependencies"), _T("install()"));
      cleanup_nssm_service(service);
      return 6;
    }

    HeapFree(GetProcessHeap(), 0, dependencies);
  }

  /* Remaining tabs are only for services we manage. */
  if (service->native) return 0;

  /* Get process stuff. */
  combo = GetDlgItem(tablist[NSSM_TAB_PROCESS], IDC_PRIORITY);
  service->priority = priority_index_to_constant((unsigned long) SendMessage(combo, CB_GETCURSEL, 0, 0));

  service->affinity = 0LL;
  if (! (SendDlgItemMessage(tablist[NSSM_TAB_PROCESS], IDC_AFFINITY_ALL, BM_GETCHECK, 0, 0) & BST_CHECKED)) {
    HWND list = GetDlgItem(tablist[NSSM_TAB_PROCESS], IDC_AFFINITY);
    int selected = (int) SendMessage(list, LB_GETSELCOUNT, 0, 0);
    int count = (int) SendMessage(list, LB_GETCOUNT, 0, 0);
    if (! selected) {
      popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_GUI_WARN_AFFINITY_NONE);
      return 5;
    }
    else if (selected < count) {
      for (int i = 0; i < count; i++) {
        if (SendMessage(list, LB_GETSEL, i, 0)) service->affinity |= (1LL << (__int64) i);
      }
    }
  }

  if (SendDlgItemMessage(tablist[NSSM_TAB_PROCESS], IDC_CONSOLE, BM_GETCHECK, 0, 0) & BST_CHECKED) service->no_console = 0;
  else service->no_console = 1;

  /* Get stop method stuff. */
  check_stop_method(service, NSSM_STOP_METHOD_CONSOLE, IDC_METHOD_CONSOLE);
  check_stop_method(service, NSSM_STOP_METHOD_WINDOW, IDC_METHOD_WINDOW);
  check_stop_method(service, NSSM_STOP_METHOD_THREADS, IDC_METHOD_THREADS);
  check_stop_method(service, NSSM_STOP_METHOD_TERMINATE, IDC_METHOD_TERMINATE);
  check_number(tablist[NSSM_TAB_SHUTDOWN], IDC_KILL_CONSOLE, &service->kill_console_delay);
  check_number(tablist[NSSM_TAB_SHUTDOWN], IDC_KILL_WINDOW, &service->kill_window_delay);
  check_number(tablist[NSSM_TAB_SHUTDOWN], IDC_KILL_THREADS, &service->kill_threads_delay);
  if (SendDlgItemMessage(tablist[NSSM_TAB_SHUTDOWN], IDC_KILL_PROCESS_TREE, BM_GETCHECK, 0, 0) & BST_CHECKED) service->kill_process_tree = 1;
  else service->kill_process_tree = 0;

  /* Get exit action stuff. */
  check_number(tablist[NSSM_TAB_EXIT], IDC_THROTTLE, &service->throttle_delay);
  combo = GetDlgItem(tablist[NSSM_TAB_EXIT], IDC_APPEXIT);
  service->default_exit_action = (unsigned long) SendMessage(combo, CB_GETCURSEL, 0, 0);
  if (service->default_exit_action == CB_ERR) service->default_exit_action = 0;
  check_number(tablist[NSSM_TAB_EXIT], IDC_RESTART_DELAY, &service->restart_delay);

  /* Get I/O stuff. */
  check_io(window, _T("stdin"), service->stdin_path, _countof(service->stdin_path), IDC_STDIN);
  check_io(window, _T("stdout"), service->stdout_path, _countof(service->stdout_path), IDC_STDOUT);
  check_io(window, _T("stderr"), service->stderr_path, _countof(service->stderr_path), IDC_STDERR);
  if (SendDlgItemMessage(tablist[NSSM_TAB_IO], IDC_TIMESTAMP, BM_GETCHECK, 0, 0) & BST_CHECKED) service->timestamp_log = true;
  else service->timestamp_log = false;

  /* Override stdout and/or stderr. */
  if (SendDlgItemMessage(tablist[NSSM_TAB_ROTATION], IDC_TRUNCATE, BM_GETCHECK, 0, 0) & BST_CHECKED) {
    if (service->stdout_path[0]) service->stdout_disposition = CREATE_ALWAYS;
    if (service->stderr_path[0]) service->stderr_disposition = CREATE_ALWAYS;
  }

  /* Get rotation stuff. */
  if (SendDlgItemMessage(tablist[NSSM_TAB_ROTATION], IDC_ROTATE, BM_GETCHECK, 0, 0) & BST_CHECKED) {
    service->rotate_files = true;
    if (SendDlgItemMessage(tablist[NSSM_TAB_ROTATION], IDC_ROTATE_ONLINE, BM_GETCHECK, 0, 0) & BST_CHECKED) service->rotate_stdout_online = service->rotate_stderr_online = NSSM_ROTATE_ONLINE;
    check_number(tablist[NSSM_TAB_ROTATION], IDC_ROTATE_SECONDS, &service->rotate_seconds);
    check_number(tablist[NSSM_TAB_ROTATION], IDC_ROTATE_BYTES_LOW, &service->rotate_bytes_low);
  }

  /* Get hook stuff. */
  if (SendDlgItemMessage(tablist[NSSM_TAB_HOOKS], IDC_REDIRECT_HOOK, BM_GETCHECK, 0, 0) & BST_CHECKED) service->hook_share_output_handles = true;

  /* Get environment. */
  unsigned long envlen = (unsigned long) SendMessage(GetDlgItem(tablist[NSSM_TAB_ENVIRONMENT], IDC_ENVIRONMENT), WM_GETTEXTLENGTH, 0, 0);
  if (envlen) {
    TCHAR *env = (TCHAR *) HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (envlen + 2) * sizeof(TCHAR));
    if (! env) {
      popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_EVENT_OUT_OF_MEMORY, _T("environment"), _T("install()"));
      cleanup_nssm_service(service);
      return 5;
    }

    if (! GetDlgItemText(tablist[NSSM_TAB_ENVIRONMENT], IDC_ENVIRONMENT, env, envlen + 1)) {
      popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_GUI_INVALID_ENVIRONMENT);
      HeapFree(GetProcessHeap(), 0, env);
      cleanup_nssm_service(service);
      return 5;
    }

    TCHAR *newenv;
    unsigned long newlen;
    if (unformat_double_null(env, envlen, &newenv, &newlen)) {
      HeapFree(GetProcessHeap(), 0, env);
      popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_EVENT_OUT_OF_MEMORY, _T("environment"), _T("install()"));
      cleanup_nssm_service(service);
      return 5;
    }

    HeapFree(GetProcessHeap(), 0, env);
    env = newenv;
    envlen = newlen;

    /* Test the environment is valid. */
    if (test_environment(env)) {
      popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_GUI_INVALID_ENVIRONMENT);
      HeapFree(GetProcessHeap(), 0, env);
      cleanup_nssm_service(service);
      return 5;
    }

    if (SendDlgItemMessage(tablist[NSSM_TAB_ENVIRONMENT], IDC_ENVIRONMENT_REPLACE, BM_GETCHECK, 0, 0) & BST_CHECKED) {
      service->env = env;
      service->envlen = envlen;
    }
    else {
      service->env_extra = env;
      service->env_extralen = envlen;
    }
  }

  return 0;
}

/* Install the service. */
int install(HWND window) {
  if (! window) return 1;

  nssm_service_t *service = alloc_nssm_service();
  if (service) {
    int ret = configure(window, service, 0);
    if (ret) return ret;
  }

  /* See if it works. */
  switch (install_service(service)) {
    case 1:
      popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_EVENT_OUT_OF_MEMORY, _T("service"), _T("install()"));
      cleanup_nssm_service(service);
      return 1;

    case 2:
      popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_MESSAGE_OPEN_SERVICE_MANAGER_FAILED);
      cleanup_nssm_service(service);
      return 2;

    case 3:
      popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_MESSAGE_PATH_TOO_LONG, NSSM);
      cleanup_nssm_service(service);
      return 3;

    case 4:
      popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_GUI_OUT_OF_MEMORY_FOR_IMAGEPATH);
      cleanup_nssm_service(service);
      return 4;

    case 5:
      popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_GUI_INSTALL_SERVICE_FAILED);
      cleanup_nssm_service(service);
      return 5;

    case 6:
      popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_GUI_CREATE_PARAMETERS_FAILED);
      cleanup_nssm_service(service);
      return 6;
  }

  update_hooks(service->name);

  popup_message(window, MB_OK, NSSM_MESSAGE_SERVICE_INSTALLED, service->name);
  cleanup_nssm_service(service);
  return 0;
}

/* Remove the service */
int remove(HWND window) {
  if (! window) return 1;

  /* See if it works */
  nssm_service_t *service = alloc_nssm_service();
  if (service) {
    /* Get service name */
    if (! GetDlgItemText(window, IDC_NAME, service->name, _countof(service->name))) {
      popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_GUI_MISSING_SERVICE_NAME);
      cleanup_nssm_service(service);
      return 2;
    }

    /* Confirm */
    if (popup_message(window, MB_YESNO, NSSM_GUI_ASK_REMOVE_SERVICE, service->name) != IDYES) {
      cleanup_nssm_service(service);
      return 0;
    }
  }

  switch (remove_service(service)) {
    case 1:
      popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_EVENT_OUT_OF_MEMORY, _T("service"), _T("remove()"));
      cleanup_nssm_service(service);
      return 1;

    case 2:
      popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_MESSAGE_OPEN_SERVICE_MANAGER_FAILED);
      cleanup_nssm_service(service);
      return 2;

    case 3:
      popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_GUI_SERVICE_NOT_INSTALLED);
      cleanup_nssm_service(service);
      return 3;

    case 4:
      popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_GUI_REMOVE_SERVICE_FAILED);
      cleanup_nssm_service(service);
      return 4;
  }

  popup_message(window, MB_OK, NSSM_MESSAGE_SERVICE_REMOVED, service->name);
  cleanup_nssm_service(service);
  return 0;
}

int edit(HWND window, nssm_service_t *orig_service) {
  if (! window) return 1;

  nssm_service_t *service = alloc_nssm_service();
  if (service) {
    int ret = configure(window, service, orig_service);
    if (ret) return ret;
  }

  switch (edit_service(service, true)) {
    case 1:
      popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_EVENT_OUT_OF_MEMORY, _T("service"), _T("edit()"));
      cleanup_nssm_service(service);
      return 1;

    case 3:
      popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_MESSAGE_PATH_TOO_LONG, NSSM);
      cleanup_nssm_service(service);
      return 3;

    case 4:
      popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_GUI_OUT_OF_MEMORY_FOR_IMAGEPATH);
      cleanup_nssm_service(service);
      return 4;

    case 5:
    case 6:
      popup_message(window, MB_OK | MB_ICONEXCLAMATION, NSSM_GUI_EDIT_PARAMETERS_FAILED);
      cleanup_nssm_service(service);
      return 6;
  }

  update_hooks(service->name);

  popup_message(window, MB_OK, NSSM_MESSAGE_SERVICE_EDITED, service->name);
  cleanup_nssm_service(service);
  return 0;
}

static TCHAR *browse_filter(int message) {
  switch (message) {
    case NSSM_GUI_BROWSE_FILTER_APPLICATIONS: return _T("*.exe;*.bat;*.cmd");
    case NSSM_GUI_BROWSE_FILTER_DIRECTORIES: return _T(".");
    case NSSM_GUI_BROWSE_FILTER_ALL_FILES: /* Fall through. */
    default: return _T("*.*");
  }
}

UINT_PTR CALLBACK browse_hook(HWND dlg, UINT message, WPARAM w, LPARAM l) {
  switch (message) {
    case WM_INITDIALOG:
      return 1;
  }

  return 0;
}

/* Browse for application */
void browse(HWND window, TCHAR *current, unsigned long flags, ...) {
  if (! window) return;

  va_list arg;
  size_t bufsize = 256;
  size_t len = bufsize;
  int i;

  OPENFILENAME ofn;
  ZeroMemory(&ofn, sizeof(ofn));
  ofn.lStructSize = sizeof(ofn);
  ofn.lpstrFilter = (TCHAR *) HeapAlloc(GetProcessHeap(), 0, bufsize * sizeof(TCHAR));
  /* XXX: Escaping nulls with FormatMessage is tricky */
  if (ofn.lpstrFilter) {
    ZeroMemory((void *) ofn.lpstrFilter, bufsize);
    len = 0;
    /* "Applications" + NULL + "*.exe" + NULL */
    va_start(arg, flags);
    while (i = va_arg(arg, int)) {
      TCHAR *localised = message_string(i);
      _sntprintf_s((TCHAR *) ofn.lpstrFilter + len, bufsize - len, _TRUNCATE, localised);
      len += _tcslen(localised) + 1;
      LocalFree(localised);
      TCHAR *filter = browse_filter(i);
      _sntprintf_s((TCHAR *) ofn.lpstrFilter + len, bufsize - len, _TRUNCATE, _T("%s"), filter);
      len += _tcslen(filter) + 1;
    }
    va_end(arg);
    /* Remainder of the buffer is already zeroed */
  }
  ofn.lpstrFile = (TCHAR *) HeapAlloc(GetProcessHeap(), 0, PATH_LENGTH * sizeof(TCHAR));
  if (ofn.lpstrFile) {
    if (flags & OFN_NOVALIDATE) {
      /* Directory hack. */
      _sntprintf_s(ofn.lpstrFile, PATH_LENGTH, _TRUNCATE, _T(":%s:"), message_string(NSSM_GUI_BROWSE_FILTER_DIRECTORIES));
      ofn.nMaxFile = DIR_LENGTH;
    }
    else {
      _sntprintf_s(ofn.lpstrFile, PATH_LENGTH, _TRUNCATE, _T("%s"), current);
      ofn.nMaxFile = PATH_LENGTH;
    }
  }
  ofn.lpstrTitle = message_string(NSSM_GUI_BROWSE_TITLE);
  ofn.Flags = OFN_EXPLORER | OFN_HIDEREADONLY | OFN_PATHMUSTEXIST | flags;

  if (GetOpenFileName(&ofn)) {
    /* Directory hack. */
    if (flags & OFN_NOVALIDATE) strip_basename(ofn.lpstrFile);
    SendMessage(window, WM_SETTEXT, 0, (LPARAM) ofn.lpstrFile);
  }
  if (ofn.lpstrFilter) HeapFree(GetProcessHeap(), 0, (void *) ofn.lpstrFilter);
  if (ofn.lpstrFile) HeapFree(GetProcessHeap(), 0, ofn.lpstrFile);
}

INT_PTR CALLBACK tab_dlg(HWND tab, UINT message, WPARAM w, LPARAM l) {
  switch (message) {
    case WM_INITDIALOG:
      return 1;

    /* Button was pressed or control was controlled. */
    case WM_COMMAND:
      HWND dlg;
      TCHAR buffer[PATH_LENGTH];
      unsigned char enabled;

      switch (LOWORD(w)) {
        /* Browse for application. */
        case IDC_BROWSE:
          dlg = GetDlgItem(tab, IDC_PATH);
          GetDlgItemText(tab, IDC_PATH, buffer, _countof(buffer));
          browse(dlg, buffer, OFN_FILEMUSTEXIST, NSSM_GUI_BROWSE_FILTER_APPLICATIONS, NSSM_GUI_BROWSE_FILTER_ALL_FILES, 0);
          /* Fill in startup directory if it wasn't already specified. */
          GetDlgItemText(tab, IDC_DIR, buffer, _countof(buffer));
          if (! buffer[0]) {
            GetDlgItemText(tab, IDC_PATH, buffer, _countof(buffer));
            strip_basename(buffer);
            SetDlgItemText(tab, IDC_DIR, buffer);
          }
          break;

        /* Browse for startup directory. */
        case IDC_BROWSE_DIR:
          dlg = GetDlgItem(tab, IDC_DIR);
          GetDlgItemText(tab, IDC_DIR, buffer, _countof(buffer));
          browse(dlg, buffer, OFN_NOVALIDATE, NSSM_GUI_BROWSE_FILTER_DIRECTORIES, 0);
          break;

        /* Log on. */
        case IDC_LOCALSYSTEM:
          set_logon_enabled(1, 0);
          break;

        case IDC_VIRTUAL_SERVICE:
          set_logon_enabled(0, 0);
          break;

        case IDC_ACCOUNT:
          set_logon_enabled(0, 1);
          break;

        /* Affinity. */
        case IDC_AFFINITY_ALL:
          if (SendDlgItemMessage(tab, LOWORD(w), BM_GETCHECK, 0, 0) & BST_CHECKED) enabled = 0;
          else enabled = 1;
          set_affinity_enabled(enabled);
          break;

        /* Shutdown methods. */
        case IDC_METHOD_CONSOLE:
          set_timeout_enabled(LOWORD(w), IDC_KILL_CONSOLE);
          break;

        case IDC_METHOD_WINDOW:
          set_timeout_enabled(LOWORD(w), IDC_KILL_WINDOW);
          break;

        case IDC_METHOD_THREADS:
          set_timeout_enabled(LOWORD(w), IDC_KILL_THREADS);
          break;

        /* Browse for stdin. */
        case IDC_BROWSE_STDIN:
          dlg = GetDlgItem(tab, IDC_STDIN);
          GetDlgItemText(tab, IDC_STDIN, buffer, _countof(buffer));
          browse(dlg, buffer, 0, NSSM_GUI_BROWSE_FILTER_ALL_FILES, 0);
          break;

        /* Browse for stdout. */
        case IDC_BROWSE_STDOUT:
          dlg = GetDlgItem(tab, IDC_STDOUT);
          GetDlgItemText(tab, IDC_STDOUT, buffer, _countof(buffer));
          browse(dlg, buffer, 0, NSSM_GUI_BROWSE_FILTER_ALL_FILES, 0);
          /* Fill in stderr if it wasn't already specified. */
          GetDlgItemText(tab, IDC_STDERR, buffer, _countof(buffer));
          if (! buffer[0]) {
            GetDlgItemText(tab, IDC_STDOUT, buffer, _countof(buffer));
            SetDlgItemText(tab, IDC_STDERR, buffer);
          }
          break;

        /* Browse for stderr. */
        case IDC_BROWSE_STDERR:
          dlg = GetDlgItem(tab, IDC_STDERR);
          GetDlgItemText(tab, IDC_STDERR, buffer, _countof(buffer));
          browse(dlg, buffer, 0, NSSM_GUI_BROWSE_FILTER_ALL_FILES, 0);
          break;

        /* Rotation. */
        case IDC_ROTATE:
          if (SendDlgItemMessage(tab, LOWORD(w), BM_GETCHECK, 0, 0) & BST_CHECKED) enabled = 1;
          else enabled = 0;
          set_rotation_enabled(enabled);
          break;

        /* Hook event. */
        case IDC_HOOK_EVENT:
          if (HIWORD(w) == CBN_SELCHANGE) set_hook_tab((int) SendMessage(GetDlgItem(tab, IDC_HOOK_EVENT), CB_GETCURSEL, 0, 0), 0, false);
          break;

        /* Hook action. */
        case IDC_HOOK_ACTION:
          if (HIWORD(w) == CBN_SELCHANGE) set_hook_tab((int) SendMessage(GetDlgItem(tab, IDC_HOOK_EVENT), CB_GETCURSEL, 0, 0), (int) SendMessage(GetDlgItem(tab, IDC_HOOK_ACTION), CB_GETCURSEL, 0, 0), false);
          break;

        /* Browse for hook. */
        case IDC_BROWSE_HOOK:
          dlg = GetDlgItem(tab, IDC_HOOK);
          GetDlgItemText(tab, IDC_HOOK, buffer, _countof(buffer));
          browse(dlg, _T(""), OFN_FILEMUSTEXIST, NSSM_GUI_BROWSE_FILTER_ALL_FILES, 0);
          break;

        /* Hook. */
        case IDC_HOOK:
          set_hook_tab((int) SendMessage(GetDlgItem(tab, IDC_HOOK_EVENT), CB_GETCURSEL, 0, 0), (int) SendMessage(GetDlgItem(tab, IDC_HOOK_ACTION), CB_GETCURSEL, 0, 0), true);
          break;
      }
      return 1;
  }

  return 0;
}

/* Install/remove dialogue callback */
INT_PTR CALLBACK nssm_dlg(HWND window, UINT message, WPARAM w, LPARAM l) {
  nssm_service_t *service;

  switch (message) {
    /* Creating the dialogue */
    case WM_INITDIALOG:
      service = (nssm_service_t *) l;

      SetFocus(GetDlgItem(window, IDC_NAME));

      HWND tabs;
      HWND combo;
      HWND list;
      int i, n;
      tabs = GetDlgItem(window, IDC_TAB1);
      if (! tabs) return 0;

      /* Set up tabs. */
      TCITEM tab;
      ZeroMemory(&tab, sizeof(tab));
      tab.mask = TCIF_TEXT;

      selected_tab = 0;

      /* Application tab. */
      if (service->native) tab.pszText = message_string(NSSM_GUI_TAB_NATIVE);
      else tab.pszText = message_string(NSSM_GUI_TAB_APPLICATION);
      tab.cchTextMax = (int) _tcslen(tab.pszText);
      SendMessage(tabs, TCM_INSERTITEM, NSSM_TAB_APPLICATION, (LPARAM) &tab);
      if (service->native) {
        tablist[NSSM_TAB_APPLICATION] = dialog(MAKEINTRESOURCE(IDD_NATIVE), window, tab_dlg);
        EnableWindow(tablist[NSSM_TAB_APPLICATION], 0);
        EnableWindow(GetDlgItem(tablist[NSSM_TAB_APPLICATION], IDC_PATH), 0);
      }
      else tablist[NSSM_TAB_APPLICATION] = dialog(MAKEINTRESOURCE(IDD_APPLICATION), window, tab_dlg);
      ShowWindow(tablist[NSSM_TAB_APPLICATION], SW_SHOW);

      /* Details tab. */
      tab.pszText = message_string(NSSM_GUI_TAB_DETAILS);
      tab.cchTextMax = (int) _tcslen(tab.pszText);
      SendMessage(tabs, TCM_INSERTITEM, NSSM_TAB_DETAILS, (LPARAM) &tab);
      tablist[NSSM_TAB_DETAILS] = dialog(MAKEINTRESOURCE(IDD_DETAILS), window, tab_dlg);
      ShowWindow(tablist[NSSM_TAB_DETAILS], SW_HIDE);

      /* Set defaults. */
      combo = GetDlgItem(tablist[NSSM_TAB_DETAILS], IDC_STARTUP);
      SendMessage(combo, CB_INSERTSTRING, NSSM_STARTUP_AUTOMATIC, (LPARAM) message_string(NSSM_GUI_STARTUP_AUTOMATIC));
      SendMessage(combo, CB_INSERTSTRING, NSSM_STARTUP_DELAYED, (LPARAM) message_string(NSSM_GUI_STARTUP_DELAYED));
      SendMessage(combo, CB_INSERTSTRING, NSSM_STARTUP_MANUAL, (LPARAM) message_string(NSSM_GUI_STARTUP_MANUAL));
      SendMessage(combo, CB_INSERTSTRING, NSSM_STARTUP_DISABLED, (LPARAM) message_string(NSSM_GUI_STARTUP_DISABLED));
      SendMessage(combo, CB_SETCURSEL, NSSM_STARTUP_AUTOMATIC, 0);

      /* Logon tab. */
      tab.pszText = message_string(NSSM_GUI_TAB_LOGON);
      tab.cchTextMax = (int) _tcslen(tab.pszText);
      SendMessage(tabs, TCM_INSERTITEM, NSSM_TAB_LOGON, (LPARAM) &tab);
      tablist[NSSM_TAB_LOGON] = dialog(MAKEINTRESOURCE(IDD_LOGON), window, tab_dlg);
      ShowWindow(tablist[NSSM_TAB_LOGON], SW_HIDE);

      /* Set defaults. */
      CheckRadioButton(tablist[NSSM_TAB_LOGON], IDC_LOCALSYSTEM, IDC_ACCOUNT, IDC_LOCALSYSTEM);
      set_logon_enabled(1, 0);

      /* Dependencies tab. */
      tab.pszText = message_string(NSSM_GUI_TAB_DEPENDENCIES);
      tab.cchTextMax = (int) _tcslen(tab.pszText);
      SendMessage(tabs, TCM_INSERTITEM, NSSM_TAB_DEPENDENCIES, (LPARAM) &tab);
      tablist[NSSM_TAB_DEPENDENCIES] = dialog(MAKEINTRESOURCE(IDD_DEPENDENCIES), window, tab_dlg);
      ShowWindow(tablist[NSSM_TAB_DEPENDENCIES], SW_HIDE);

      /* Remaining tabs are only for services we manage. */
      if (service->native) return 1;

      /* Process tab. */
      tab.pszText = message_string(NSSM_GUI_TAB_PROCESS);
      tab.cchTextMax = (int) _tcslen(tab.pszText);
      SendMessage(tabs, TCM_INSERTITEM, NSSM_TAB_PROCESS, (LPARAM) &tab);
      tablist[NSSM_TAB_PROCESS] = dialog(MAKEINTRESOURCE(IDD_PROCESS), window, tab_dlg);
      ShowWindow(tablist[NSSM_TAB_PROCESS], SW_HIDE);

      /* Set defaults. */
      combo = GetDlgItem(tablist[NSSM_TAB_PROCESS], IDC_PRIORITY);
      SendMessage(combo, CB_INSERTSTRING, NSSM_REALTIME_PRIORITY, (LPARAM) message_string(NSSM_GUI_REALTIME_PRIORITY_CLASS));
      SendMessage(combo, CB_INSERTSTRING, NSSM_HIGH_PRIORITY, (LPARAM) message_string(NSSM_GUI_HIGH_PRIORITY_CLASS));
      SendMessage(combo, CB_INSERTSTRING, NSSM_ABOVE_NORMAL_PRIORITY, (LPARAM) message_string(NSSM_GUI_ABOVE_NORMAL_PRIORITY_CLASS));
      SendMessage(combo, CB_INSERTSTRING, NSSM_NORMAL_PRIORITY, (LPARAM) message_string(NSSM_GUI_NORMAL_PRIORITY_CLASS));
      SendMessage(combo, CB_INSERTSTRING, NSSM_BELOW_NORMAL_PRIORITY, (LPARAM) message_string(NSSM_GUI_BELOW_NORMAL_PRIORITY_CLASS));
      SendMessage(combo, CB_INSERTSTRING, NSSM_IDLE_PRIORITY, (LPARAM) message_string(NSSM_GUI_IDLE_PRIORITY_CLASS));
      SendMessage(combo, CB_SETCURSEL, NSSM_NORMAL_PRIORITY, 0);

      SendDlgItemMessage(tablist[NSSM_TAB_PROCESS], IDC_CONSOLE, BM_SETCHECK, BST_CHECKED, 0);

      list = GetDlgItem(tablist[NSSM_TAB_PROCESS], IDC_AFFINITY);
      n = num_cpus();
      SendMessage(list, LB_SETCOLUMNWIDTH, 16, 0);
      for (i = 0; i < n; i++) {
        TCHAR buffer[3];
        _sntprintf_s(buffer, _countof(buffer), _TRUNCATE, _T("%d"), i);
        SendMessage(list, LB_ADDSTRING, 0, (LPARAM) buffer);
      }

      /*
        Size to fit.
        The box is high enough for four rows.  It is wide enough for eight
        columns without scrolling.  With scrollbars it shrinks to two rows.
        Note that the above only holds if we set the column width BEFORE
        adding the strings.
      */
      if (n < 32) {
        int columns = (n - 1) / 4;
        RECT rect;
        GetWindowRect(list, &rect);
        int width = rect.right - rect.left;
        width -= (7 - columns) * 16;
        int height = rect.bottom - rect.top;
        if (n < 4) height -= (int) SendMessage(list, LB_GETITEMHEIGHT, 0, 0) * (4 - n);
        SetWindowPos(list, 0, 0, 0, width, height, SWP_NOMOVE | SWP_NOOWNERZORDER);
      }
      SendMessage(list, LB_SELITEMRANGE, 1, MAKELPARAM(0, n));

      SendDlgItemMessage(tablist[NSSM_TAB_PROCESS], IDC_AFFINITY_ALL, BM_SETCHECK, BST_CHECKED, 0);
      set_affinity_enabled(0);

      /* Shutdown tab. */
      tab.pszText = message_string(NSSM_GUI_TAB_SHUTDOWN);
      tab.cchTextMax = (int) _tcslen(tab.pszText);
      SendMessage(tabs, TCM_INSERTITEM, NSSM_TAB_SHUTDOWN, (LPARAM) &tab);
      tablist[NSSM_TAB_SHUTDOWN] = dialog(MAKEINTRESOURCE(IDD_SHUTDOWN), window, tab_dlg);
      ShowWindow(tablist[NSSM_TAB_SHUTDOWN], SW_HIDE);

      /* Set defaults. */
      SendDlgItemMessage(tablist[NSSM_TAB_SHUTDOWN], IDC_METHOD_CONSOLE, BM_SETCHECK, BST_CHECKED, 0);
      SetDlgItemInt(tablist[NSSM_TAB_SHUTDOWN], IDC_KILL_CONSOLE, NSSM_KILL_CONSOLE_GRACE_PERIOD, 0);
      SendDlgItemMessage(tablist[NSSM_TAB_SHUTDOWN], IDC_METHOD_WINDOW, BM_SETCHECK, BST_CHECKED, 0);
      SetDlgItemInt(tablist[NSSM_TAB_SHUTDOWN], IDC_KILL_WINDOW, NSSM_KILL_WINDOW_GRACE_PERIOD, 0);
      SendDlgItemMessage(tablist[NSSM_TAB_SHUTDOWN], IDC_METHOD_THREADS, BM_SETCHECK, BST_CHECKED, 0);
      SetDlgItemInt(tablist[NSSM_TAB_SHUTDOWN], IDC_KILL_THREADS, NSSM_KILL_THREADS_GRACE_PERIOD, 0);
      SendDlgItemMessage(tablist[NSSM_TAB_SHUTDOWN], IDC_METHOD_TERMINATE, BM_SETCHECK, BST_CHECKED, 0);
      SendDlgItemMessage(tablist[NSSM_TAB_SHUTDOWN], IDC_KILL_PROCESS_TREE, BM_SETCHECK, BST_CHECKED, 1);

      /* Restart tab. */
      tab.pszText = message_string(NSSM_GUI_TAB_EXIT);
      tab.cchTextMax = (int) _tcslen(tab.pszText);
      SendMessage(tabs, TCM_INSERTITEM, NSSM_TAB_EXIT, (LPARAM) &tab);
      tablist[NSSM_TAB_EXIT] = dialog(MAKEINTRESOURCE(IDD_APPEXIT), window, tab_dlg);
      ShowWindow(tablist[NSSM_TAB_EXIT], SW_HIDE);

      /* Set defaults. */
      SetDlgItemInt(tablist[NSSM_TAB_EXIT], IDC_THROTTLE, NSSM_RESET_THROTTLE_RESTART, 0);
      combo = GetDlgItem(tablist[NSSM_TAB_EXIT], IDC_APPEXIT);
      SendMessage(combo, CB_INSERTSTRING, NSSM_EXIT_RESTART, (LPARAM) message_string(NSSM_GUI_EXIT_RESTART));
      SendMessage(combo, CB_INSERTSTRING, NSSM_EXIT_IGNORE, (LPARAM) message_string(NSSM_GUI_EXIT_IGNORE));
      SendMessage(combo, CB_INSERTSTRING, NSSM_EXIT_REALLY, (LPARAM) message_string(NSSM_GUI_EXIT_REALLY));
      SendMessage(combo, CB_INSERTSTRING, NSSM_EXIT_UNCLEAN, (LPARAM) message_string(NSSM_GUI_EXIT_UNCLEAN));
      SendMessage(combo, CB_SETCURSEL, NSSM_EXIT_RESTART, 0);
      SetDlgItemInt(tablist[NSSM_TAB_EXIT], IDC_RESTART_DELAY, 0, 0);

      /* I/O tab. */
      tab.pszText = message_string(NSSM_GUI_TAB_IO);
      tab.cchTextMax = (int) _tcslen(tab.pszText) + 1;
      SendMessage(tabs, TCM_INSERTITEM, NSSM_TAB_IO, (LPARAM) &tab);
      tablist[NSSM_TAB_IO] = dialog(MAKEINTRESOURCE(IDD_IO), window, tab_dlg);
      ShowWindow(tablist[NSSM_TAB_IO], SW_HIDE);

      /* Set defaults. */
      SendDlgItemMessage(tablist[NSSM_TAB_IO], IDC_TIMESTAMP, BM_SETCHECK, BST_UNCHECKED, 0);

      /* Rotation tab. */
      tab.pszText = message_string(NSSM_GUI_TAB_ROTATION);
      tab.cchTextMax = (int) _tcslen(tab.pszText) + 1;
      SendMessage(tabs, TCM_INSERTITEM, NSSM_TAB_ROTATION, (LPARAM) &tab);
      tablist[NSSM_TAB_ROTATION] = dialog(MAKEINTRESOURCE(IDD_ROTATION), window, tab_dlg);
      ShowWindow(tablist[NSSM_TAB_ROTATION], SW_HIDE);

      /* Set defaults. */
      SendDlgItemMessage(tablist[NSSM_TAB_ROTATION], IDC_ROTATE_ONLINE, BM_SETCHECK, BST_UNCHECKED, 0);
      SetDlgItemInt(tablist[NSSM_TAB_ROTATION], IDC_ROTATE_SECONDS, 0, 0);
      SetDlgItemInt(tablist[NSSM_TAB_ROTATION], IDC_ROTATE_BYTES_LOW, 0, 0);
      set_rotation_enabled(0);

      /* Environment tab. */
      tab.pszText = message_string(NSSM_GUI_TAB_ENVIRONMENT);
      tab.cchTextMax = (int) _tcslen(tab.pszText) + 1;
      SendMessage(tabs, TCM_INSERTITEM, NSSM_TAB_ENVIRONMENT, (LPARAM) &tab);
      tablist[NSSM_TAB_ENVIRONMENT] = dialog(MAKEINTRESOURCE(IDD_ENVIRONMENT), window, tab_dlg);
      ShowWindow(tablist[NSSM_TAB_ENVIRONMENT], SW_HIDE);

      /* Hooks tab. */
      tab.pszText = message_string(NSSM_GUI_TAB_HOOKS);
      tab.cchTextMax = (int) _tcslen(tab.pszText) + 1;
      SendMessage(tabs, TCM_INSERTITEM, NSSM_TAB_HOOKS, (LPARAM) &tab);
      tablist[NSSM_TAB_HOOKS] = dialog(MAKEINTRESOURCE(IDD_HOOKS), window, tab_dlg);
      ShowWindow(tablist[NSSM_TAB_HOOKS], SW_HIDE);

      /* Set defaults. */
      combo = GetDlgItem(tablist[NSSM_TAB_HOOKS], IDC_HOOK_EVENT);
      SendMessage(combo, CB_INSERTSTRING, -1, (LPARAM) message_string(NSSM_GUI_HOOK_EVENT_START));
      SendMessage(combo, CB_INSERTSTRING, -1, (LPARAM) message_string(NSSM_GUI_HOOK_EVENT_STOP));
      SendMessage(combo, CB_INSERTSTRING, -1, (LPARAM) message_string(NSSM_GUI_HOOK_EVENT_EXIT));
      SendMessage(combo, CB_INSERTSTRING, -1, (LPARAM) message_string(NSSM_GUI_HOOK_EVENT_POWER));
      SendMessage(combo, CB_INSERTSTRING, -1, (LPARAM) message_string(NSSM_GUI_HOOK_EVENT_ROTATE));
      SendDlgItemMessage(tablist[NSSM_TAB_HOOKS], IDC_REDIRECT_HOOK, BM_SETCHECK, BST_UNCHECKED, 0);
      if (_tcslen(service->name)) {
        TCHAR hook_name[HOOK_NAME_LENGTH];
        TCHAR cmd[CMD_LENGTH];
        for (i = 0; hook_event_strings[i]; i++) {
          const TCHAR *hook_event = hook_event_strings[i];
          int j;
          for (j = 0; hook_action_strings[j]; j++) {
            const TCHAR *hook_action = hook_action_strings[j];
            if (! valid_hook_name(hook_event, hook_action, true)) continue;
            if (get_hook(service->name, hook_event, hook_action, cmd, sizeof(cmd))) continue;
            if (hook_env(hook_event, hook_action, hook_name, _countof(hook_name)) < 0) continue;
            SetEnvironmentVariable(hook_name, cmd);
          }
        }
      }
      set_hook_tab(0, 0, false);

      return 1;

    /* Tab change. */
    case WM_NOTIFY:
      NMHDR *notification;

      notification = (NMHDR *) l;
      switch (notification->code) {
        case TCN_SELCHANGE:
          HWND tabs;
          int selection;

          tabs = GetDlgItem(window, IDC_TAB1);
          if (! tabs) return 0;

          selection = (int) SendMessage(tabs, TCM_GETCURSEL, 0, 0);
          if (selection != selected_tab) {
            ShowWindow(tablist[selected_tab], SW_HIDE);
            ShowWindow(tablist[selection], SW_SHOWDEFAULT);
            SetFocus(GetDlgItem(window, IDOK));
            selected_tab = selection;
          }
          return 1;
      }

      return 0;

    /* Button was pressed or control was controlled */
    case WM_COMMAND:
      switch (LOWORD(w)) {
        /* OK button */
        case IDOK:
          if ((int) GetWindowLongPtr(window, GWLP_USERDATA) == IDD_EDIT) {
            if (! edit(window, (nssm_service_t *) GetWindowLongPtr(window, DWLP_USER))) PostQuitMessage(0);
          }
          else if (! install(window)) PostQuitMessage(0);
          break;

        /* Cancel button */
        case IDCANCEL:
          DestroyWindow(window);
          break;

        /* Remove button */
        case IDC_REMOVE:
          if (! remove(window)) PostQuitMessage(0);
          break;
      }
      return 1;

    /* Window closing */
    case WM_CLOSE:
      DestroyWindow(window);
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
  }
  return 0;
}
