#include "nssm.h"

/* Convert error code to error string - must call LocalFree() on return value */
char *error_string(unsigned long error) {
  char *message;
  if (! FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, 0, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (char *) &message, 0, 0)) return 0;
  return message;
}

/* Log a message to the Event Log */
void eventprintf(unsigned short type, char *format, ...) {
  char message[4096];
  char *strings[2];
  int n, size;
  va_list arg;

  /* Construct the message */
  size = sizeof(message);
  va_start(arg, format);
  n = _vsnprintf(message, size, format, arg);
  va_end(arg);

  /* Check success */
  if (n < 0 || n >= size) return;

  /* Construct strings array */
  strings[0] = message;
  strings[1] = 0;
    
  /* Open event log */
  HANDLE handle = RegisterEventSource(0, TEXT(NSSM));
  if (! handle) return;

  /* Log it */
  if (! ReportEvent(handle, type, 0, 0, 0, 1, 0, (const char **) strings, 0)) {
    printf("ReportEvent(): %s\n", error_string(GetLastError()));
  }

  /* Close event log */
  DeregisterEventSource(handle);
}
