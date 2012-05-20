#include "nssm.h"

int nssm_gui(int resource, char *name) {
  /* Create window */
  HWND dlg = CreateDialog(0, MAKEINTRESOURCE(resource), 0, install_dlg);
  if (! dlg) {
    popup_message(MB_OK, NSSM_GUI_CREATEDIALOG_FAILED, error_string(GetLastError()));
    return 1;
  }

  /* Display the window */
  centre_window(dlg);
  ShowWindow(dlg, SW_SHOW);

  /* Set service name if given */
  if (name) {
    SetDlgItemText(dlg, IDC_NAME, name);
    /* No point making user click remove if the name is already entered */
    if (resource == IDD_REMOVE) {
      HWND button = GetDlgItem(dlg, IDC_REMOVE);
      if (button) {
        SendMessage(button, WM_LBUTTONDOWN, 0, 0);
        SendMessage(button, WM_LBUTTONUP, 0, 0);
      }
    }
  }

  /* Go! */
  MSG message;
  while (GetMessage(&message, 0, 0, 0)) {
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

/* Install the service */
int install(HWND window) {
  if (! window) return 1;

  /* Check parameters in the window */
  char name[STRING_SIZE];
  char exe[EXE_LENGTH];
  char flags[STRING_SIZE];

  /* Get service name */
  if (! GetDlgItemText(window, IDC_NAME, name, sizeof(name))) {
    popup_message(MB_OK | MB_ICONEXCLAMATION, NSSM_GUI_MISSING_SERVICE_NAME);
    return 2;
  }

  /* Get executable name */
  if (! GetDlgItemText(window, IDC_PATH, exe, sizeof(exe))) {
    popup_message(MB_OK | MB_ICONEXCLAMATION, NSSM_GUI_MISSING_PATH);
    return 3;
  }

  /* Get flags */
  if (SendMessage(GetDlgItem(window, IDC_FLAGS), WM_GETTEXTLENGTH, 0, 0)) {
    if (! GetDlgItemText(window, IDC_FLAGS, flags, sizeof(flags))) {
      popup_message(MB_OK | MB_ICONEXCLAMATION, NSSM_GUI_INVALID_OPTIONS);
      return 4;
    }
  }
  else ZeroMemory(&flags, sizeof(flags));

  /* See if it works */
  switch (install_service(name, exe, flags)) {
    case 2:
      popup_message(MB_OK | MB_ICONEXCLAMATION, NSSM_MESSAGE_OPEN_SERVICE_MANAGER_FAILED);
      return 2;

    case 3:
      popup_message(MB_OK | MB_ICONEXCLAMATION, NSSM_MESSAGE_PATH_TOO_LONG, NSSM);
      return 3;

    case 4:
      popup_message(MB_OK | MB_ICONEXCLAMATION, NSSM_GUI_OUT_OF_MEMORY_FOR_IMAGEPATH);
      return 4;

    case 5:
      popup_message(MB_OK | MB_ICONEXCLAMATION, NSSM_GUI_INSTALL_SERVICE_FAILED);
      return 5;

    case 6:
      popup_message(MB_OK | MB_ICONEXCLAMATION, NSSM_GUI_CREATE_PARAMETERS_FAILED);
      return 6;
  }

  popup_message(MB_OK, NSSM_MESSAGE_SERVICE_INSTALLED, name);
  return 0;
}

/* Remove the service */
int remove(HWND window) {
  if (! window) return 1;

  /* Check parameters in the window */
  char name[STRING_SIZE];

  /* Get service name */
  if (! GetDlgItemText(window, IDC_NAME, name, sizeof(name))) {
    popup_message(MB_OK | MB_ICONEXCLAMATION, NSSM_GUI_MISSING_SERVICE_NAME);
    return 2;
  }

  /* Confirm */
  if (popup_message(MB_YESNO, NSSM_GUI_ASK_REMOVE_SERVICE, name) != IDYES) return 0;

  /* See if it works */
  switch (remove_service(name)) {
    case 2:
      popup_message(MB_OK | MB_ICONEXCLAMATION, NSSM_MESSAGE_OPEN_SERVICE_MANAGER_FAILED);
      return 2;

    case 3:
      popup_message(MB_OK | MB_ICONEXCLAMATION, NSSM_GUI_SERVICE_NOT_INSTALLED);
      return 3;

    case 4:
      popup_message(MB_OK | MB_ICONEXCLAMATION, NSSM_GUI_REMOVE_SERVICE_FAILED);
      return 4;
  }

  popup_message(MB_OK, NSSM_MESSAGE_SERVICE_REMOVED, name);
  return 0;
}

/* Browse for application */
void browse(HWND window) {
  if (! window) return;

  unsigned long bufsize = 256;
  unsigned long len = bufsize;
  OPENFILENAME ofn;
  ZeroMemory(&ofn, sizeof(ofn));
  ofn.lStructSize = sizeof(ofn);
  ofn.lpstrFilter = (char *) HeapAlloc(GetProcessHeap(), 0, bufsize);
  /* XXX: Escaping nulls with FormatMessage is tricky */
  if (ofn.lpstrFilter) {
    ZeroMemory((void *) ofn.lpstrFilter, bufsize);
    char *localised = message_string(NSSM_GUI_BROWSE_FILTER_APPLICATIONS);
    _snprintf((char *) ofn.lpstrFilter, bufsize, localised);
    /* "Applications" + NULL + "*.exe" + NULL */
    len = strlen(localised) + 1;
    LocalFree(localised);
    _snprintf((char *) ofn.lpstrFilter + len, bufsize - len, "*.exe");
    /* "All files" + NULL + "*.*" + NULL */
    len += 6;
    localised = message_string(NSSM_GUI_BROWSE_FILTER_ALL_FILES);
    _snprintf((char *) ofn.lpstrFilter + len, bufsize - len, localised);
    len += strlen(localised) + 1;
    LocalFree(localised);
    _snprintf((char *) ofn.lpstrFilter + len, bufsize - len, "*.*");
    /* Remainder of the buffer is already zeroed */
  }
  ofn.lpstrFile = new char[MAX_PATH];
  ofn.lpstrFile[0] = '\0';
  ofn.lpstrTitle = message_string(NSSM_GUI_BROWSE_TITLE);
  ofn.nMaxFile = MAX_PATH;
  ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;

  if (GetOpenFileName(&ofn)) {
    SendMessage(window, WM_SETTEXT, 0, (LPARAM) ofn.lpstrFile);
  }
  if (ofn.lpstrFilter) HeapFree(GetProcessHeap(), 0, (void *) ofn.lpstrFilter);

  delete[] ofn.lpstrFile;
}

/* Install/remove dialogue callback */
INT_PTR CALLBACK install_dlg(HWND window, UINT message, WPARAM w, LPARAM l) {
  switch (message) {
    /* Creating the dialogue */
    case WM_INITDIALOG:
      return 1;

    /* Button was pressed or control was controlled */
    case WM_COMMAND:
      switch (LOWORD(w)) {
        /* OK button */
        case IDC_OK:
          PostQuitMessage(install(window));
          break;

        /* Cancel button */
        case IDC_CANCEL:
          DestroyWindow(window);
          break;

        /* Browse button */
        case IDC_BROWSE:
          browse(GetDlgItem(window, IDC_PATH));
          break;

        /* Remove button */
        case IDC_REMOVE:
          PostQuitMessage(remove(window));
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
