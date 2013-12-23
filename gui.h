#ifndef GUI_H
#define GUI_H

#include <stdio.h>
#include <windows.h>
#include <commctrl.h>
#include "resource.h"

int nssm_gui(int, nssm_service_t *);
void centre_window(HWND);
int configure(HWND, nssm_service_t *, nssm_service_t *);
int install(HWND);
int remove(HWND);
int edit(HWND, nssm_service_t *);
void browse(HWND);
INT_PTR CALLBACK nssm_dlg(HWND, UINT, WPARAM, LPARAM);

#endif
