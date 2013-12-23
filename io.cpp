#include "nssm.h"

/* Get path, share mode, creation disposition and flags for a stream. */
int get_createfile_parameters(HKEY key, TCHAR *prefix, TCHAR *path, unsigned long *sharing, unsigned long default_sharing, unsigned long *disposition, unsigned long default_disposition, unsigned long *flags, unsigned long default_flags) {
  TCHAR value[NSSM_STDIO_LENGTH];

  /* Path. */
  if (_sntprintf_s(value, _countof(value), _TRUNCATE, _T("%s"), prefix) < 0) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, prefix, _T("get_createfile_parameters()"), 0);
    return 1;
  }
  switch (expand_parameter(key, value, path, MAX_PATH, true, false)) {
    case 0: if (! path[0]) return 0; break; /* OK. */
    default: return 2; /* Error. */
  }

  /* ShareMode. */
  if (_sntprintf_s(value, _countof(value), _TRUNCATE, _T("%s%s"), prefix, NSSM_REG_STDIO_SHARING) < 0) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, NSSM_REG_STDIO_SHARING, _T("get_createfile_parameters()"), 0);
    return 3;
  }
  switch (get_number(key, value, sharing, false)) {
    case 0: *sharing = default_sharing; break; /* Missing. */
    case 1: break; /* Found. */
    case -2: return 4; break; /* Error. */
  }

  /* CreationDisposition. */
  if (_sntprintf_s(value, _countof(value), _TRUNCATE, _T("%s%s"), prefix, NSSM_REG_STDIO_DISPOSITION) < 0) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, NSSM_REG_STDIO_DISPOSITION, _T("get_createfile_parameters()"), 0);
    return 5;
  }
  switch (get_number(key, value, disposition, false)) {
    case 0: *disposition = default_disposition; break; /* Missing. */
    case 1: break; /* Found. */
    case -2: return 6; break; /* Error. */
  }

  /* Flags. */
  if (_sntprintf_s(value, _countof(value), _TRUNCATE, _T("%s%s"), prefix, NSSM_REG_STDIO_FLAGS) < 0) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, NSSM_REG_STDIO_FLAGS, _T("get_createfile_parameters()"), 0);
    return 7;
  }
  switch (get_number(key, value, flags, false)) {
    case 0: *flags = default_flags; break; /* Missing. */
    case 1: break; /* Found. */
    case -2: return 8; break; /* Error. */
  }

  return 0;
}

int set_createfile_parameter(HKEY key, TCHAR *prefix, TCHAR *suffix, unsigned long number) {
  TCHAR value[NSSM_STDIO_LENGTH];

  if (_sntprintf_s(value, _countof(value), _TRUNCATE, _T("%s%s"), prefix, suffix) < 0) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, suffix, _T("set_createfile_parameter()"), 0);
    return 1;
  }

  return set_number(key, value, number);
}

HANDLE append_to_file(TCHAR *path, unsigned long sharing, SECURITY_ATTRIBUTES *attributes, unsigned long disposition, unsigned long flags) {
  HANDLE ret;

  /* Try to append to the file first. */
  ret = CreateFile(path, FILE_APPEND_DATA, sharing, attributes, disposition, flags, 0);
  if (ret) {
    SetEndOfFile(ret);
    return ret;
  }

  unsigned long error = GetLastError();
  if (error != ERROR_FILE_NOT_FOUND) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_CREATEFILE_FAILED, path, error_string(error), 0);
    return (HANDLE) 0;
  }

  /* It didn't exist.  Create it. */
  return CreateFile(path, FILE_WRITE_DATA, sharing, attributes, disposition, flags, 0);
}

void rotate_file(TCHAR *service_name, TCHAR *path, unsigned long seconds, unsigned long low, unsigned long high) {
  unsigned long error;

  /* Now. */
  SYSTEMTIME st;
  GetSystemTime(&st);

  BY_HANDLE_FILE_INFORMATION info;

  /* Try to open the file to check if it exists and to get attributes. */
  HANDLE file = CreateFile(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
  if (file) {
    /* Get file attributes. */
    if (! GetFileInformationByHandle(file, &info)) {
      /* Reuse current time for rotation timestamp. */
      seconds = low = high = 0;
      SystemTimeToFileTime(&st, &info.ftLastWriteTime);
    }

    CloseHandle(file);
  }
  else {
    error = GetLastError();
    if (error == ERROR_FILE_NOT_FOUND) return;
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_ROTATE_FILE_FAILED, service_name, path, _T("CreateFile()"), path, error_string(error), 0);
    /* Reuse current time for rotation timestamp. */
    seconds = low = high = 0;
    SystemTimeToFileTime(&st, &info.ftLastWriteTime);
  }

  /* Check file age. */
  if (seconds) {
    FILETIME ft;
    SystemTimeToFileTime(&st, &ft);

    ULARGE_INTEGER s;
    s.LowPart = ft.dwLowDateTime;
    s.HighPart = ft.dwHighDateTime;
    s.QuadPart -= seconds * 10000000LL;
    ft.dwLowDateTime = s.LowPart;
    ft.dwHighDateTime = s.HighPart;
    if (CompareFileTime(&info.ftLastWriteTime, &ft) > 0) return;
  }

  /* Check file size. */
  if (low || high) {
    if (info.nFileSizeHigh < high) return;
    if (info.nFileSizeHigh == high && info.nFileSizeLow < low) return;
  }

  /* Get new filename. */
  FileTimeToSystemTime(&info.ftLastWriteTime, &st);

  TCHAR buffer[MAX_PATH];
  memmove(buffer, path, sizeof(buffer));
  TCHAR *ext = PathFindExtension(buffer);
  TCHAR extension[MAX_PATH];
  _sntprintf_s(extension, _countof(extension), _TRUNCATE, _T("-%04u%02u%02uT%02u%02u%02u.%03u%s"), st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, ext);
  *ext = _T('\0');
  TCHAR rotated[MAX_PATH];
  _sntprintf_s(rotated, _countof(rotated), _TRUNCATE, _T("%s%s"), buffer, extension);

  /* Rotate. */
  if (MoveFile(path, rotated)) return;
  error = GetLastError();

  log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_ROTATE_FILE_FAILED, service_name, path, _T("MoveFile()"), rotated, error_string(error), 0);
  return;
}

int get_output_handles(nssm_service_t *service, HKEY key, STARTUPINFO *si) {
  bool set_flags = false;

  /* Standard security attributes allowing inheritance. */
  SECURITY_ATTRIBUTES attributes;
  ZeroMemory(&attributes, sizeof(attributes));
  attributes.bInheritHandle = true;

  /* stdin */
  if (get_createfile_parameters(key, NSSM_REG_STDIN, service->stdin_path, &service->stdin_sharing, NSSM_STDIN_SHARING, &service->stdin_disposition, NSSM_STDIN_DISPOSITION, &service->stdin_flags, NSSM_STDIN_FLAGS)) {
    service->stdin_sharing = service->stdin_disposition = service->stdin_flags = 0;
    ZeroMemory(service->stdin_path, _countof(service->stdin_path) * sizeof(TCHAR));
    return 1;
  }
  if (si && service->stdin_path[0]) {
    si->hStdInput = CreateFile(service->stdin_path, FILE_READ_DATA, service->stdin_sharing, &attributes, service->stdin_disposition, service->stdin_flags, 0);
    if (! si->hStdInput) {
      log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_CREATEFILE_FAILED, service->stdin_path, error_string(GetLastError()), 0);
      return 2;
    }
    set_flags = true;
  }

  /* stdout */
  if (get_createfile_parameters(key, NSSM_REG_STDOUT, service->stdout_path, &service->stdout_sharing, NSSM_STDOUT_SHARING, &service->stdout_disposition, NSSM_STDOUT_DISPOSITION, &service->stdout_flags, NSSM_STDOUT_FLAGS)) {
    service->stdout_sharing = service->stdout_disposition = service->stdout_flags = 0;
    ZeroMemory(service->stdout_path, _countof(service->stdout_path) * sizeof(TCHAR));
    return 3;
  }
  if (si && service->stdout_path[0]) {
    if (service->rotate_files) rotate_file(service->name, service->stdout_path, service->rotate_seconds, service->rotate_bytes_low, service->rotate_bytes_high);
    si->hStdOutput = append_to_file(service->stdout_path, service->stdout_sharing, &attributes, service->stdout_disposition, service->stdout_flags);
    if (! si->hStdOutput) return 4;
    set_flags = true;
  }

  /* stderr */
  if (get_createfile_parameters(key, NSSM_REG_STDERR, service->stderr_path, &service->stdout_sharing, NSSM_STDERR_SHARING, &service->stdout_disposition, NSSM_STDERR_DISPOSITION, &service->stdout_flags, NSSM_STDERR_FLAGS)) {
    service->stderr_sharing = service->stderr_disposition = service->stderr_flags = 0;
    ZeroMemory(service->stderr_path, _countof(service->stderr_path) * sizeof(TCHAR));
    return 5;
  }
  if (service->stderr_path[0]) {
    /* Same as stdout? */
    if (str_equiv(service->stderr_path, service->stdout_path)) {
      service->stderr_sharing = service->stdout_sharing;
      service->stderr_disposition = service->stdout_disposition;
      service->stderr_flags = service->stdout_flags;

      if (si) {
        /* Two handles to the same file will create a race. */
        if (! DuplicateHandle(GetCurrentProcess(), si->hStdOutput, GetCurrentProcess(), &si->hStdError, 0, true, DUPLICATE_SAME_ACCESS)) {
          log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_DUPLICATEHANDLE_FAILED, NSSM_REG_STDOUT, error_string(GetLastError()), 0);
          return 6;
        }
      }
    }
    else if (si) {
      if (service->rotate_files) rotate_file(service->name, service->stderr_path, service->rotate_seconds, service->rotate_bytes_low, service->rotate_bytes_high);
      si->hStdError = append_to_file(service->stderr_path, service->stdout_sharing, &attributes, service->stdout_disposition, service->stdout_flags);
      if (! si->hStdError) {
        log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_CREATEFILE_FAILED, service->stderr_path, error_string(GetLastError()), 0);
        return 7;
      }
      SetEndOfFile(si->hStdError);
    }
    set_flags = true;
  }

  if (! set_flags) return 0;

  /*
    We need to set the startup_info flags to make the new handles
    inheritable by the new process.
  */
  if (si) si->dwFlags |= STARTF_USESTDHANDLES;

  return 0;
}

void close_output_handles(STARTUPINFO *si) {
  if (si->hStdInput) CloseHandle(si->hStdInput);
  if (si->hStdOutput) CloseHandle(si->hStdOutput);
  if (si->hStdError) CloseHandle(si->hStdError);
}
