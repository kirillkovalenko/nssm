#include "nssm.h"

#define NSSM_ERROR_BUFSIZE 65535
#define NSSM_NUM_EVENT_STRINGS 16
unsigned long tls_index;

/* Convert error code to error string - must call LocalFree() on return value */
TCHAR *error_string(unsigned long error) {
  /* Thread-safe buffer */
  TCHAR *error_message = (TCHAR *) TlsGetValue(tls_index);
  if (! error_message) {
    error_message = (TCHAR *) LocalAlloc(LPTR, NSSM_ERROR_BUFSIZE);
    if (! error_message) return _T("<out of memory for error message>");
    TlsSetValue(tls_index, (void *) error_message);
  }

  if (! FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, 0, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (TCHAR *) error_message, NSSM_ERROR_BUFSIZE, 0)) {
    if (_sntprintf_s(error_message, NSSM_ERROR_BUFSIZE, _TRUNCATE, _T("system error %lu"), error) < 0) return 0;
  }
  return error_message;
}

/* Convert message code to format string */
TCHAR *message_string(unsigned long error) {
  TCHAR *ret;
  if (! FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_IGNORE_INSERTS, 0, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR) &ret, NSSM_ERROR_BUFSIZE, 0)) {
    ret = (TCHAR *) HeapAlloc(GetProcessHeap(), 0, 32 * sizeof(TCHAR));
    if (_sntprintf_s(ret, NSSM_ERROR_BUFSIZE, _TRUNCATE, _T("system error %lu"), error) < 0) return 0;
  }
  return ret;
}

/* Log a message to the Event Log */
void log_event(unsigned short type, unsigned long id, ...) {
  va_list arg;
  int count;
  TCHAR *s;
  TCHAR *strings[NSSM_NUM_EVENT_STRINGS];

  /* Open event log */
  HANDLE handle = RegisterEventSource(0, NSSM);
  if (! handle) return;

  /* Log it */
  count = 0;
  va_start(arg, id);
  while ((s = va_arg(arg, TCHAR *)) && count < NSSM_NUM_EVENT_STRINGS - 1) strings[count++] = s;
  strings[count] = 0;
  va_end(arg);
  ReportEvent(handle, type, 0, id, 0, count, 0, (const TCHAR **) strings, 0);

  /* Close event log */
  DeregisterEventSource(handle);
}

/* Log a message to the console */
void print_message(FILE *file, unsigned long id, ...) {
  va_list arg;

  TCHAR *format = message_string(id);
  if (! format) return;

  va_start(arg, id);
  _vftprintf(file, format, arg);
  va_end(arg);

  LocalFree(format);
}

/* Show a GUI dialogue */
int popup_message(unsigned int type, unsigned long id, ...) {
  va_list arg;

  TCHAR *format = message_string(id);
  if (! format) {
    return MessageBox(0, _T("The message which was supposed to go here is missing!"), NSSM, MB_OK | MB_ICONEXCLAMATION);
  }

  TCHAR blurb[512];
  va_start(arg, id);
  if (_vsntprintf_s(blurb, _countof(blurb), _TRUNCATE, format, arg) < 0) {
    va_end(arg);
    LocalFree(format);
    return MessageBox(0, _T("the message which was supposed to go here is too big!"), NSSM, MB_OK | MB_ICONEXCLAMATION);
  }
  va_end(arg);

  int ret = MessageBox(0, blurb, NSSM, type);

  LocalFree(format);

  return ret;
}
