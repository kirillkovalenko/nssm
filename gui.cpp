#include "nssm.h"

int nssm_gui(int resource, char *name) {
  char blurb[256];

  /* Create window */
  HWND dlg = CreateDialog(0, MAKEINTRESOURCE(resource), 0, install_dlg);
  if (! dlg) {
    _snprintf(blurb, sizeof(blurb), "CreateDialog() failed with error code %d", GetLastError());
    MessageBox(0, blurb, NSSM, MB_OK);
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
    MessageBox(0, "No valid service name was specified!", NSSM, MB_OK);
    return 2;
  }

  /* Get executable name */
  if (! GetDlgItemText(window, IDC_PATH, exe, sizeof(exe))) {
    MessageBox(0, "No valid executable path was specified!", NSSM, MB_OK);
    return 3;
  }

  /* Get flags */
  if (SendMessage(GetDlgItem(window, IDC_FLAGS), WM_GETTEXTLENGTH, 0, 0)) {
    if (! GetDlgItemText(window, IDC_FLAGS, flags, sizeof(flags))) {
      MessageBox(0, "No valid options were specified!", NSSM, MB_OK);
      return 4;
    }
  }
  else ZeroMemory(&flags, sizeof(flags));

  /* See if it works */
  switch (install_service(name, exe, flags)) {
    case 2:
      MessageBox(0, "Can't open service manager!\nPerhaps you need to be an administrator...", NSSM, MB_OK);
      return 2;

    case 3:
      MessageBox(0, "Path too long!\nThe full path to " NSSM " is too long.\nPlease install " NSSM " somewhere else...\n", NSSM, MB_OK);
      return 3;

    case 4:
      MessageBox(0, "Error constructing ImagePath!\nThis really shouldn't happen.  You could be out of memory\nor the world may be about to end or something equally bad.", NSSM, MB_OK);
      return 4;

    case 5:
      MessageBox(0, "Couldn't create service!\nPerhaps it is already installed...", NSSM, MB_OK);
      return 5;

    case 6:
      MessageBox(0, "Couldn't set startup parameters for the service!\nDeleting the service...", NSSM, MB_OK);
      return 6;
  }

  MessageBox(0, "Service successfully installed!", NSSM, MB_OK);
  return 0;
}

/* Remove the service */
int remove(HWND window) {
  if (! window) return 1;

  /* Check parameters in the window */
  char name[STRING_SIZE];

  /* Get service name */
  if (! GetDlgItemText(window, IDC_NAME, name, sizeof(name))) {
    MessageBox(0, "No valid service name was specified!", NSSM, MB_OK);
    return 2;
  }

  /* Confirm */
  char blurb[MAX_PATH];
  if (_snprintf(blurb, sizeof(blurb), "Remove the \"%s\" service?", name) < 0) {
    if (MessageBox(0, "Remove the service?", NSSM, MB_YESNO) != IDYES) return 0;
  }
  else if (MessageBox(0, blurb, NSSM, MB_YESNO) != IDYES) return 0;

  /* See if it works */
  switch (remove_service(name)) {
    case 2:
      MessageBox(0, "Can't open service manager!\nPerhaps you need to be an administrator...", NSSM, MB_OK);
      return 2;

    case 3:
      MessageBox(0, "Can't open service!\nPerhaps it isn't installed...", NSSM, MB_OK);
      return 3;

    case 4:
      MessageBox(0, "Can't delete service!  Make sure the service is stopped and try again.\nIf this error persists, you may need to set the service NOT to start\nautomatically, reboot your computer and try removing it again.", NSSM, MB_OK);
      return 4;
  }

  MessageBox(0, "Service successfully removed!", NSSM, MB_OK);
  return 0;
}

/* Browse for game */
void browse(HWND window) {
  if (! window) return;

  OPENFILENAME ofn;
  ZeroMemory(&ofn, sizeof(ofn));
  ofn.lStructSize = sizeof(ofn);
  ofn.lpstrFilter = "Applications\0*.exe\0All files\0*.*\0\0";
  ofn.lpstrFile = new char[MAX_PATH];
  ofn.lpstrFile[0] = '\0';
  ofn.lpstrTitle = "Locate application file";
  ofn.nMaxFile = MAX_PATH;
  ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;

  if (GetOpenFileName(&ofn)) {
    SendMessage(window, WM_SETTEXT, 0, (LPARAM) ofn.lpstrFile);
  }

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
