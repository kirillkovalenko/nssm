#ifndef NSSM_H
#define NSSM_H

#define _WIN32_WINNT 0x0500
#include <shlwapi.h>
#include <stdarg.h>
#include <stdio.h>
#include <windows.h>
#include "service.h"
#include "event.h"
#include "imports.h"
#include "messages.h"
#include "process.h"
#include "registry.h"
#include "io.h"
#include "gui.h"

int str_equiv(const char *, const char *);
void strip_basename(char *);

#define NSSM "nssm"
#define NSSM_VERSION "2.21"
#define NSSM_VERSIONINFO 2,21,0,0
#define NSSM_DATE "2013-11-24"

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

/* Exit actions. */
#define NSSM_EXIT_RESTART 0
#define NSSM_EXIT_IGNORE 1
#define NSSM_EXIT_REALLY 2
#define NSSM_EXIT_UNCLEAN 3
#define NSSM_NUM_EXIT_ACTIONS 4

/* How many milliseconds to wait before updating service status. */
#define NSSM_SERVICE_STATUS_DEADLINE 20000

#endif
