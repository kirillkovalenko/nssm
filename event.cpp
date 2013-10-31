#include "nssm.h"

#define NSSM_ERROR_BUFSIZE 65535
unsigned long tls_index;

/* Convert error code to error string - must call LocalFree() on return value */
char *error_string(unsigned long error) {
  /* Thread-safe buffer */
  char *error_message = (char *) TlsGetValue(tls_index);
  if (! error_message) {
    error_message = (char *) LocalAlloc(LPTR, NSSM_ERROR_BUFSIZE);
    if (! error_message) return "<out of memory for error message>";
    TlsSetValue(tls_index, (void *) error_message);
  }

  if (! FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, 0, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (char *) error_message, NSSM_ERROR_BUFSIZE, 0)) {
    if (_snprintf_s(error_message, NSSM_ERROR_BUFSIZE, _TRUNCATE, "system error %lu", error) < 0) return 0;
  }
  return error_message;
}

/* Convert message code to format string */
char *message_string(unsigned long error) {
  char *ret;
  if (! FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_IGNORE_INSERTS, 0, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR) &ret, NSSM_ERROR_BUFSIZE, 0)) {
    ret = (char *) HeapAlloc(GetProcessHeap(), 0, 32);
    if (_snprintf_s(ret, NSSM_ERROR_BUFSIZE, _TRUNCATE, "system error %lu", error) < 0) return 0;
  }
  return ret;
}

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

/* Log a message to the console */
void print_message(FILE *file, unsigned long id, ...) {
  va_list arg;

  char *format = message_string(id);
  if (! format) return;

  va_start(arg, id);
  vfprintf(file, format, arg);
  va_end(arg);

  LocalFree(format);
}

/* Show a GUI dialogue */
int popup_message(unsigned int type, unsigned long id, ...) {
  va_list arg;

  char *format = message_string(id);
  if (! format) {
    return MessageBox(0, "Message %lu was supposed to go here!", NSSM, MB_OK | MB_ICONEXCLAMATION);
  }

  char blurb[256];
  va_start(arg, id);
  if (vsnprintf_s(blurb, sizeof(blurb), _TRUNCATE, format, arg) < 0) {
    va_end(arg);
    LocalFree(format);
    return MessageBox(0, "Message %lu was supposed to go here!", NSSM, MB_OK | MB_ICONEXCLAMATION);
  }
  va_end(arg);

  int ret = MessageBox(0, blurb, NSSM, type);

  LocalFree(format);

  return ret;
}
