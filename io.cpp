#include "nssm.h"

/* Get path, share mode, creation disposition and flags for a stream. */
int get_createfile_parameters(HKEY key, char *prefix, char *path, unsigned long *sharing, unsigned long default_sharing, unsigned long *disposition, unsigned long default_disposition, unsigned long *flags, unsigned long default_flags) {
  char value[NSSM_STDIO_LENGTH];

  /* Path. */
  if (_snprintf(value, sizeof(value), "%s", prefix) < 0) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, prefix, "get_createfile_parameters()", 0);
    return 1;
  }
  switch (expand_parameter(key, value, path, MAX_PATH, true, false)) {
    case 0: if (! path[0]) return 0; break; /* OK. */
    default: return 2; /* Error. */
  }

  /* ShareMode. */
  if (_snprintf(value, sizeof(value), "%s%s", prefix, NSSM_REG_STDIO_SHARING) < 0) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, NSSM_REG_STDIO_SHARING, "get_createfile_parameters()", 0);
    return 3;
  }
  switch (get_number(key, value, sharing, false)) {
    case 0: *sharing = default_sharing; break; /* Missing. */
    case 1: break; /* Found. */
    case -2: return 4; break; /* Error. */
  }

  /* CreationDisposition. */
  if (_snprintf(value, sizeof(value), "%s%s", prefix, NSSM_REG_STDIO_DISPOSITION) < 0) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, NSSM_REG_STDIO_DISPOSITION, "get_createfile_parameters()", 0);
    return 5;
  }
  switch (get_number(key, value, disposition, false)) {
    case 0: *disposition = default_disposition; break; /* Missing. */
    case 1: break; /* Found. */
    case -2: return 6; break; /* Error. */
  }

  /* Flags. */
  if (_snprintf(value, sizeof(value), "%s%s", prefix, NSSM_REG_STDIO_FLAGS) < 0) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, NSSM_REG_STDIO_FLAGS, "get_createfile_parameters()", 0);
    return 7;
  }
  switch (get_number(key, value, flags, false)) {
    case 0: *flags = default_flags; break; /* Missing. */
    case 1: break; /* Found. */
    case -2: return 8; break; /* Error. */
  }

  return 0;
}

HANDLE append_to_file(char *path, unsigned long sharing, SECURITY_ATTRIBUTES *attributes, unsigned long disposition, unsigned long flags) {
  HANDLE ret;

  /* Try to append to the file first. */
  ret = CreateFile(path, FILE_APPEND_DATA, sharing, attributes, disposition, flags, 0);
  if (ret) {
    SetEndOfFile(ret);
    return ret;
  }

  unsigned long error = GetLastError();
  if (error != ERROR_FILE_NOT_FOUND) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_CREATEFILE_FAILED, path, error_string(error));
    return (HANDLE) 0;
  }

  /* It didn't exist.  Create it. */
  return CreateFile(path, FILE_WRITE_DATA, sharing, attributes, disposition, flags, 0);
}

int get_output_handles(HKEY key, STARTUPINFO *si) {
  char path[MAX_PATH];
  char stdout_path[MAX_PATH];
  unsigned long sharing, disposition, flags;
  bool set_flags = false;

  /* Standard security attributes allowing inheritance. */
  SECURITY_ATTRIBUTES attributes;
  ZeroMemory(&attributes, sizeof(attributes));
  attributes.bInheritHandle = true;

  /* stdin */
  if (get_createfile_parameters(key, NSSM_REG_STDIN, path, &sharing, FILE_SHARE_WRITE, &disposition, OPEN_EXISTING, &flags, FILE_ATTRIBUTE_NORMAL)) return 1;
  if (path[0]) {
    si->hStdInput = CreateFile(path, FILE_READ_DATA, sharing, &attributes, disposition, flags, 0);
    if (! si->hStdInput) {
      log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_CREATEFILE_FAILED, path, error_string(GetLastError()));
      return 2;
    }
    set_flags = true;
  }

  /* stdout */
  if (get_createfile_parameters(key, NSSM_REG_STDOUT, path, &sharing, FILE_SHARE_READ | FILE_SHARE_WRITE, &disposition, OPEN_ALWAYS, &flags, FILE_ATTRIBUTE_NORMAL)) return 3;
  if (path[0]) {
    /* Remember path for comparison with stderr. */
    if (_snprintf(stdout_path, sizeof(stdout_path), "%s", path) < 0) {
      log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OUT_OF_MEMORY, "stdout_path", "get_output_handles", 0);
      return 4;
    }

    si->hStdOutput = append_to_file(path, sharing, &attributes, disposition, flags);
    if (! si->hStdOutput) return 5;
    set_flags = true;
  }
  else ZeroMemory(stdout_path, sizeof(stdout_path));

  /* stderr */
  if (get_createfile_parameters(key, NSSM_REG_STDERR, path, &sharing, FILE_SHARE_READ | FILE_SHARE_WRITE, &disposition, OPEN_ALWAYS, &flags, FILE_ATTRIBUTE_NORMAL)) return 6;
  if (path[0]) {
    /* Same as stdin? */
    if (str_equiv(path, stdout_path)) {
      /* Two handles to the same file will create a race. */
      if (! DuplicateHandle(GetCurrentProcess(), si->hStdOutput, GetCurrentProcess(), &si->hStdError, 0, true, DUPLICATE_SAME_ACCESS)) {
        log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_DUPLICATEHANDLE_FAILED, NSSM_REG_STDOUT, error_string(GetLastError()));
        return 7;
      }
    }
    else {
      si->hStdError = append_to_file(path, sharing, &attributes, disposition, flags);
      if (! si->hStdError) {
        log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_CREATEFILE_FAILED, path, error_string(GetLastError()));
        return 8;
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
  si->dwFlags |= STARTF_USESTDHANDLES;

  return 0;
}

void close_output_handles(STARTUPINFO *si) {
  if (si->hStdInput) CloseHandle(si->hStdInput);
  if (si->hStdOutput) CloseHandle(si->hStdOutput);
  if (si->hStdError) CloseHandle(si->hStdError);
}
