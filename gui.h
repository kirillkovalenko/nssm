#ifndef GUI_H
#define GUI_H

#include <stdio.h>
#include <windows.h>
#include <commctrl.h>
#include "resource.h"

int nssm_gui(int, nssm_service_t *);
void centre_window(HWND);
int install(HWND);
int remove(HWND);
void browse(HWND);
INT_PTR CALLBACK nssm_dlg(HWND, UINT, WPARAM, LPARAM);

#endif
