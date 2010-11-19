#ifndef NSSM_H
#define NSSM_H

#define _WIN32_WINNT 0x0500
#include <stdarg.h>
#include <stdio.h>
#include <windows.h>
#include "event.h"
#include "messages.h"
#include "registry.h"
#include "service.h"
#include "gui.h"

int str_equiv(const char *, const char *);

#define NSSM "nssm"
#define NSSM_VERSION "2.6"
#define NSSM_DATE "2010-11-19"
#define NSSM_RUN "run"

/*
  MSDN says the commandline in CreateProcess() is limited to 32768 characters
  and the application name to MAX_PATH.
  A registry key is limited to 255 characters.
  A registry value is limited to 16383 characters.
  Therefore we limit the service name to accommodate the path under HKLM.
*/
#define EXE_LENGTH MAX_PATH
#define CMD_LENGTH 32768
#define KEY_LENGTH 255
#define VALUE_LENGTH 16383
#define SERVICE_NAME_LENGTH KEY_LENGTH - 55

#endif
