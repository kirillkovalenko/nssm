#include "nssm.h"

static HANDLE create_logging_thread(logger_t *logger) {
  HANDLE thread_handle = CreateThread(NULL, 0, log_and_rotate, (void *) logger, 0, logger->tid_ptr);
  if (! thread_handle) log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_CREATETHREAD_FAILED, error_string(GetLastError()), 0);
  return thread_handle;
}

static inline void write_bom(logger_t *logger) {
  wchar_t bom = L'\ufeff';
  unsigned long out;
  if (! WriteFile(logger->write_handle, (void *) &bom, sizeof(bom), &out, 0)) {
    log_event(EVENTLOG_WARNING_TYPE, NSSM_EVENT_SOMEBODY_SET_UP_US_THE_BOM, logger->service_name, logger->path, error_string(GetLastError()), 0);
  }
}

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

int delete_createfile_parameter(HKEY key, TCHAR *prefix, TCHAR *suffix) {
  TCHAR value[NSSM_STDIO_LENGTH];

  if (_sntprintf_s(value, _countof(value), _TRUNCATE, _T("%s%s"), prefix, suffix) < 0) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, suffix, _T("delete_createfile_parameter()"), 0);
    return 1;
  }

  if (RegDeleteValue(key, value)) return 0;
  return 1;
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

static void rotated_filename(TCHAR *path, TCHAR *rotated, unsigned long rotated_len, SYSTEMTIME *st) {
  if (! st) {
    SYSTEMTIME now;
    st = &now;
    GetSystemTime(st);
  }

  TCHAR buffer[MAX_PATH];
  memmove(buffer, path, sizeof(buffer));
  TCHAR *ext = PathFindExtension(buffer);
  TCHAR extension[MAX_PATH];
  _sntprintf_s(extension, _countof(extension), _TRUNCATE, _T("-%04u%02u%02uT%02u%02u%02u.%03u%s"), st->wYear, st->wMonth, st->wDay, st->wHour, st->wMinute, st->wSecond, st->wMilliseconds, ext);
  *ext = _T('\0');
  _sntprintf_s(rotated, rotated_len, _TRUNCATE, _T("%s%s"), buffer, extension);
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

  TCHAR rotated[MAX_PATH];
  rotated_filename(path, rotated, _countof(rotated), &st);

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

  ULARGE_INTEGER size;
  size.LowPart = service->rotate_bytes_low;
  size.HighPart = service->rotate_bytes_high;

  /* stdout */
  if (get_createfile_parameters(key, NSSM_REG_STDOUT, service->stdout_path, &service->stdout_sharing, NSSM_STDOUT_SHARING, &service->stdout_disposition, NSSM_STDOUT_DISPOSITION, &service->stdout_flags, NSSM_STDOUT_FLAGS)) {
    service->stdout_sharing = service->stdout_disposition = service->stdout_flags = 0;
    ZeroMemory(service->stdout_path, _countof(service->stdout_path) * sizeof(TCHAR));
    return 3;
  }
  if (si && service->stdout_path[0]) {
    if (service->rotate_files) rotate_file(service->name, service->stdout_path, service->rotate_seconds, service->rotate_bytes_low, service->rotate_bytes_high);
    HANDLE stdout_handle = append_to_file(service->stdout_path, service->stdout_sharing, 0, service->stdout_disposition, service->stdout_flags);
    if (! stdout_handle) {
      log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_CREATEFILE_FAILED, service->stdout_path, error_string(GetLastError()), 0);
      return 4;
    }

    /* Try online rotation only if a size threshold is set. */
    logger_t *stdout_logger = 0;
    if (service->rotate_files && service->rotate_stdout_online && size.QuadPart) {
      stdout_logger = (logger_t *) HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(logger_t));
      if (stdout_logger) {
        /* Pipe between application's stdout and our logging handle. */
        if (CreatePipe(&service->stdout_pipe, &si->hStdOutput, &attributes, 0)) {
          stdout_logger->service_name = service->name;
          stdout_logger->path = service->stdout_path;
          stdout_logger->sharing = service->stdout_sharing;
          stdout_logger->disposition = service->stdout_disposition;
          stdout_logger->flags = service->stdout_flags;
          stdout_logger->read_handle = service->stdout_pipe;
          stdout_logger->write_handle = stdout_handle;
          stdout_logger->size = (__int64) size.QuadPart;
          stdout_logger->tid_ptr = &service->stdout_tid;

          /* Logging thread. */
          service->stdout_thread = create_logging_thread(stdout_logger);
          if (! service->stdout_thread) {
            log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_CREATEPIPE_FAILED, service->name, service->stdout_path, error_string(GetLastError()));
            CloseHandle(service->stdout_pipe);
            CloseHandle(si->hStdOutput);
            service->stdout_tid = 0;
          }
        }
        else service->stdout_tid = 0;
      }
      else {
        log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, _T("stdout_logger"), _T("get_output_handles()"), 0);
        service->stdout_tid = 0;
      }

      /* Fall through to try direct I/O. */
      if (! service->stdout_tid) log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_CREATEPIPE_FAILED, service->name, service->stdout_path, error_string(GetLastError()));
    }

    if (! service->stdout_tid) {
      if (stdout_logger) HeapFree(GetProcessHeap(), 0, stdout_logger);
      if (! DuplicateHandle(GetCurrentProcess(), stdout_handle, GetCurrentProcess(), &si->hStdOutput, 0, true, DUPLICATE_CLOSE_SOURCE | DUPLICATE_SAME_ACCESS)) {
        log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_DUPLICATEHANDLE_FAILED, NSSM_REG_STDOUT, error_string(GetLastError()), 0);
        return 4;
      }
      service->rotate_stdout_online = false;
    }

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
      service->rotate_stderr_online = false;

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
      HANDLE stderr_handle = append_to_file(service->stderr_path, service->stdout_sharing, 0, service->stdout_disposition, service->stdout_flags);
      if (! stderr_handle) {
        log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_CREATEFILE_FAILED, service->stderr_path, error_string(GetLastError()), 0);
        return 7;
      }

      /* Try online rotation only if a size threshold is set. */
      logger_t *stderr_logger = 0;
      if (service->rotate_files && service->rotate_stderr_online && size.QuadPart) {
        stderr_logger = (logger_t *) HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(logger_t));
        if (stderr_logger) {
          /* Pipe between application's stderr and our logging handle. */
          if (CreatePipe(&service->stderr_pipe, &si->hStdError, &attributes, 0)) {
            stderr_logger->service_name = service->name;
            stderr_logger->path = service->stderr_path;
            stderr_logger->sharing = service->stderr_sharing;
            stderr_logger->disposition = service->stderr_disposition;
            stderr_logger->flags = service->stderr_flags;
            stderr_logger->read_handle = service->stderr_pipe;
            stderr_logger->write_handle = stderr_handle;
            stderr_logger->size = (__int64) size.QuadPart;
            stderr_logger->tid_ptr = &service->stderr_tid;

            /* Logging thread. */
            service->stderr_thread = create_logging_thread(stderr_logger);
            if (! service->stderr_thread) {
              log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_CREATEPIPE_FAILED, service->name, service->stderr_path, error_string(GetLastError()));
              CloseHandle(service->stderr_pipe);
              CloseHandle(si->hStdError);
              service->stderr_tid = 0;
            }
          }
          else service->stderr_tid = 0;
        }
        else {
          log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, _T("stderr_logger"), _T("get_output_handles()"), 0);
          service->stderr_tid = 0;
        }

        /* Fall through to try direct I/O. */
        if (! service->stderr_tid) log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_CREATEPIPE_FAILED, service->name, service->stderr_path, error_string(GetLastError()));
      }

      if (! service->stderr_tid) {
        if (stderr_logger) HeapFree(GetProcessHeap(), 0, stderr_logger);
        if (! DuplicateHandle(GetCurrentProcess(), stderr_handle, GetCurrentProcess(), &si->hStdError, 0, true, DUPLICATE_CLOSE_SOURCE | DUPLICATE_SAME_ACCESS)) {
          log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_DUPLICATEHANDLE_FAILED, NSSM_REG_STDERR, error_string(GetLastError()), 0);
          return 7;
        }
        service->rotate_stderr_online = false;
      }
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

void close_output_handles(STARTUPINFO *si, bool close_stdout, bool close_stderr) {
  if (si->hStdInput) CloseHandle(si->hStdInput);
  if (si->hStdOutput && close_stdout) CloseHandle(si->hStdOutput);
  if (si->hStdError && close_stderr) CloseHandle(si->hStdError);
}

void close_output_handles(STARTUPINFO *si) {
  return close_output_handles(si, true, true);
}

/* Wrapper to be called in a new thread for logging. */
unsigned long WINAPI log_and_rotate(void *arg) {
  logger_t *logger = (logger_t *) arg;
  if (! logger) return 1;

  __int64 size;
  BY_HANDLE_FILE_INFORMATION info;

  /* Find initial file size. */
  if (! GetFileInformationByHandle(logger->write_handle, &info)) logger->size = 0LL;
  else {
    ULARGE_INTEGER l;
    l.HighPart = info.nFileSizeHigh;
    l.LowPart = info.nFileSizeLow;
    size = l.QuadPart;
  }

  char buffer[1024];
  void *address;
  unsigned long in, out;
  while (true) {
    /* Read data from the pipe. */
    address = &buffer;
    if (! ReadFile(logger->read_handle, address, sizeof(buffer), &in, 0)) {
      log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_READFILE_FAILED, logger->service_name, logger->path, error_string(GetLastError()), 0);
      return 2;
    }

    if (size + (__int64) in >= logger->size) {
      /* Look for newline. */
      unsigned long i;
      for (i = 0; i < in; i++) {
        if (buffer[i] == '\n') {
          unsigned char unicode = IsTextUnicode(address, sizeof(buffer), 0);
          if (unicode) i += sizeof(wchar_t);
          else i += sizeof(char);

          /* Write up to the newline. */
          if (! WriteFile(logger->write_handle, address, i, &out, 0)) {
            log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_WRITEFILE_FAILED, logger->service_name, logger->path, error_string(GetLastError()), 0);
            return 3;
          }

          /* Rotate. */
          TCHAR rotated[MAX_PATH];
          rotated_filename(logger->path, rotated, _countof(rotated), 0);

          /*
            Ideally we'd try the rename first then close the handle but
            MoveFile() will fail if the handle is still open so we must
            risk losing everything.
          */
          CloseHandle(logger->write_handle);
          if (! MoveFile(logger->path, rotated)) {
            unsigned long error = GetLastError();
            if (error != ERROR_FILE_NOT_FOUND) {
              log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_ROTATE_FILE_FAILED, logger->service_name, logger->path, _T("MoveFile()"), rotated, error_string(error), 0);
              /* We can at least try to re-open the existing file. */
              logger->disposition = OPEN_ALWAYS;
            }
          }

          /* Reopen. */
          logger->write_handle = append_to_file(logger->path, logger->sharing, 0, logger->disposition, logger->flags);
          if (! logger->write_handle) {
            log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_CREATEFILE_FAILED, logger->path, error_string(GetLastError()), 0);
            /* Oh dear.  Now we can't log anything further. */
            return 4;
          }

          /* Unicode files need a new BOM. */
          if (unicode) write_bom(logger);

          /* Resume writing after the newline. */
          size = 0LL;
          address = (void *) ((char *) address + i);
          in -= i;

          break;
        }
      }
    }
    else if (! size) {
      /* Write a BOM to the new file. */
      if (IsTextUnicode(address, sizeof(buffer), 0)) write_bom(logger);
    }

    /* Write the data. */
    if (! WriteFile(logger->write_handle, address, in, &out, 0)) {
      log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_WRITEFILE_FAILED, logger->service_name, logger->path, error_string(GetLastError()), 0);
      return 3;
    }

    size += (__int64) out;
  }

  return 0;
}
