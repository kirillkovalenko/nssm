#ifndef NSSM_H
#define NSSM_H

/*
  MSDN says, basically, that the maximum length of a path is 260 characters,
  which is represented by the constant MAX_PATH.  Except when it isn't.

  The maximum length of a directory path is MAX_PATH - 12 because it must be
  possible to create a file in 8.3 format under any valid directory.

  Unicode versions of filesystem API functions accept paths up to 32767
  characters if the first four (wide) characters are L"\\?\" and each component
  of the path, separated by L"\", does not exceed the value of
  lpMaximumComponentLength returned by GetVolumeInformation(), which is
  probably 255.  But might not be.

  Relative paths are always limited to MAX_PATH because the L"\\?\" prefix
  is not valid for a relative path.

  Note that we don't care about the last two paragraphs because we're only
  concerned with allocating buffers big enough to store valid paths.  If the
  user tries to store invalid paths they will fit in the buffers but the
  application will fail.  The reason for the failure will end up in the
  event log and the user will realise the mistake.

  So that's that cleared up, then.
*/
#ifdef UNICODE
#define PATH_LENGTH 32767
#else
#define PATH_LENGTH MAX_PATH
#endif
#define DIR_LENGTH PATH_LENGTH - 12

#define _WIN32_WINNT 0x0500
#include <fcntl.h>
#include <io.h>
#include <shlwapi.h>
#include <stdarg.h>
#include <stdio.h>
#include <tchar.h>
#include <windows.h>
#include "service.h"
#include "account.h"
#include "console.h"
#include "env.h"
#include "event.h"
#include "imports.h"
#include "messages.h"
#include "process.h"
#include "registry.h"
#include "settings.h"
#include "io.h"
#include "gui.h"

int str_equiv(const TCHAR *, const TCHAR *);
void strip_basename(TCHAR *);
int str_number(const TCHAR *, unsigned long *, TCHAR **);
int str_number(const TCHAR *, unsigned long *);
int num_cpus();
int usage(int);

#define NSSM _T("NSSM")
#ifdef _WIN64
#define NSSM_ARCHITECTURE _T("64-bit")
#else
#define NSSM_ARCHITECTURE _T("32-bit")
#endif
#ifdef _DEBUG
#define NSSM_DEBUG _T(" debug")
#else
#define NSSM_DEBUG _T("")
#endif
#define NSSM_CONFIGURATION NSSM_ARCHITECTURE NSSM_DEBUG
#include "version.h"

/*
  Throttle the restart of the service if it stops before this many
  milliseconds have elapsed since startup.  Override in registry.
*/
#define NSSM_RESET_THROTTLE_RESTART 1500

/*
  How many milliseconds to wait for the application to die after sending
  a Control-C event to its console.  Override in registry.
*/
#define NSSM_KILL_CONSOLE_GRACE_PERIOD 1500
/*
  How many milliseconds to wait for the application to die after posting to
  its windows' message queues.  Override in registry.
*/
#define NSSM_KILL_WINDOW_GRACE_PERIOD 1500
/*
  How many milliseconds to wait for the application to die after posting to
  its threads' message queues.  Override in registry.
*/
#define NSSM_KILL_THREADS_GRACE_PERIOD 1500

/* Margin of error for service status wait hints in milliseconds. */
#define NSSM_WAITHINT_MARGIN 2000

/* Methods used to try to stop the application. */
#define NSSM_STOP_METHOD_CONSOLE (1 << 0)
#define NSSM_STOP_METHOD_WINDOW (1 << 1)
#define NSSM_STOP_METHOD_THREADS (1 << 2)
#define NSSM_STOP_METHOD_TERMINATE (1 << 3)

/* Startup types. */
#define NSSM_STARTUP_AUTOMATIC 0
#define NSSM_STARTUP_DELAYED 1
#define NSSM_STARTUP_MANUAL 2
#define NSSM_STARTUP_DISABLED 3

/* Exit actions. */
#define NSSM_EXIT_RESTART 0
#define NSSM_EXIT_IGNORE 1
#define NSSM_EXIT_REALLY 2
#define NSSM_EXIT_UNCLEAN 3
#define NSSM_NUM_EXIT_ACTIONS 4

/* Process priority. */
#define NSSM_REALTIME_PRIORITY 0
#define NSSM_HIGH_PRIORITY 1
#define NSSM_ABOVE_NORMAL_PRIORITY 2
#define NSSM_NORMAL_PRIORITY 3
#define NSSM_BELOW_NORMAL_PRIORITY 4
#define NSSM_IDLE_PRIORITY 5

/* How many milliseconds to wait before updating service status. */
#define NSSM_SERVICE_STATUS_DEADLINE 20000

/* User-defined service controls can be in the range 128-255. */
#define NSSM_SERVICE_CONTROL_START 0
#define NSSM_SERVICE_CONTROL_ROTATE 128

#endif
