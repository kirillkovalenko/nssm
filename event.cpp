#include "nssm.h"

/* Log a message to the Event Log */
void log_event(unsigned short type, unsigned long id, ...) {
  va_list arg;
  int count;
  char *s;
  char *strings[6];

  /* Open event log */
  HANDLE handle = RegisterEventSource(0, TEXT(NSSM));
  if (! handle) return;

  /* Log it */
  count = 0;
  va_start(arg, id);
  while ((s = va_arg(arg, char *))) strings[count++] = s;
  va_end(arg);
  ReportEvent(handle, type, 0, id, 0, count, 0, (const char **) strings, 0);

  /* Close event log */
  DeregisterEventSource(handle);
}
