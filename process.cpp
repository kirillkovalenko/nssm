#include "nssm.h"

extern imports_t imports;

int get_process_creation_time(HANDLE process_handle, FILETIME *ft) {
  FILETIME creation_time, exit_time, kernel_time, user_time;

  if (! GetProcessTimes(process_handle, &creation_time, &exit_time, &kernel_time, &user_time)) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_GETPROCESSTIMES_FAILED, error_string(GetLastError()), 0);
    return 1;
  }

  memmove(ft, &creation_time, sizeof(creation_time));

  return 0;
}

int get_process_exit_time(HANDLE process_handle, FILETIME *ft) {
  FILETIME creation_time, exit_time, kernel_time, user_time;

  if (! GetProcessTimes(process_handle, &creation_time, &exit_time, &kernel_time, &user_time)) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_GETPROCESSTIMES_FAILED, error_string(GetLastError()), 0);
    return 1;
  }

  if (! (exit_time.dwLowDateTime || exit_time.dwHighDateTime)) return 2;
  memmove(ft, &exit_time, sizeof(exit_time));

  return 0;
}

int check_parent(nssm_service_t *service, PROCESSENTRY32 *pe, unsigned long ppid) {
  /* Check parent process ID matches. */
  if (pe->th32ParentProcessID != ppid) return 1;

  /*
    Process IDs can be reused so do a sanity check by making sure the child
    has been running for less time than the parent.
    Though unlikely, it's possible that the parent exited and its process ID
    was already reused, so we'll also compare against its exit time.
  */
  HANDLE process_handle = OpenProcess(PROCESS_QUERY_INFORMATION, false, pe->th32ProcessID);
  if (! process_handle) {
    TCHAR pid_string[16];
    _sntprintf_s(pid_string, _countof(pid_string), _TRUNCATE, _T("%lu"), pe->th32ProcessID);
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OPENPROCESS_FAILED, pid_string, service->name, error_string(GetLastError()), 0);
    return 2;
  }

  FILETIME ft;
  if (get_process_creation_time(process_handle, &ft)) {
    CloseHandle(process_handle);
    return 3;
  }

  CloseHandle(process_handle);

  /* Verify that the parent's creation time is not later. */
  if (CompareFileTime(&service->creation_time, &ft) > 0) return 4;

  /* Verify that the parent's exit time is not earlier. */
  if (CompareFileTime(&service->exit_time, &ft) < 0) return 5;

  return 0;
}

/* Send some window messages and hope the window respects one or more. */
int CALLBACK kill_window(HWND window, LPARAM arg) {
  kill_t *k = (kill_t *) arg;

  unsigned long pid;
  if (! GetWindowThreadProcessId(window, &pid)) return 1;
  if (pid != k->pid) return 1;

  /* First try sending WM_CLOSE to request that the window close. */
  k->signalled |= PostMessage(window, WM_CLOSE, k->exitcode, 0);

  /*
    Then tell the window that the user is logging off and it should exit
    without worrying about saving any data.
  */
  k->signalled |= PostMessage(window, WM_ENDSESSION, 1, ENDSESSION_CLOSEAPP | ENDSESSION_CRITICAL | ENDSESSION_LOGOFF);

  return 1;
}

/*
  Try to post a message to the message queues of threads associated with the
  given process ID.  Not all threads have message queues so there's no
  guarantee of success, and we don't want to be left waiting for unsignalled
  processes so this function returns only true if at least one thread was
  successfully prodded.
*/
int kill_threads(TCHAR *service_name, kill_t *k) {
  int ret = 0;

  /* Get a snapshot of all threads in the system. */
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  if (! snapshot) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_CREATETOOLHELP32SNAPSHOT_THREAD_FAILED, service_name, error_string(GetLastError()), 0);
    return 0;
  }

  THREADENTRY32 te;
  ZeroMemory(&te, sizeof(te));
  te.dwSize = sizeof(te);

  if (! Thread32First(snapshot, &te)) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_THREAD_ENUMERATE_FAILED, service_name, error_string(GetLastError()), 0);
    CloseHandle(snapshot);
    return 0;
  }

  /* This thread belongs to the doomed process so signal it. */
  if (te.th32OwnerProcessID == k->pid) {
    ret |= PostThreadMessage(te.th32ThreadID, WM_QUIT, k->exitcode, 0);
  }

  while (true) {
    /* Try to get the next thread. */
    if (! Thread32Next(snapshot, &te)) {
      unsigned long error = GetLastError();
      if (error == ERROR_NO_MORE_FILES) break;
      log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_THREAD_ENUMERATE_FAILED, service_name, error_string(GetLastError()), 0);
      CloseHandle(snapshot);
      return ret;
    }

    if (te.th32OwnerProcessID == k->pid) {
      ret |= PostThreadMessage(te.th32ThreadID, WM_QUIT, k->exitcode, 0);
    }
  }

  CloseHandle(snapshot);

  return ret;
}

/* Give the process a chance to die gracefully. */
int kill_process(nssm_service_t *service, HANDLE process_handle, unsigned long pid, unsigned long exitcode) {
  /* Shouldn't happen. */
  if (! service) return 1;
  if (! pid) return 1;
  if (! process_handle) return 1;

  unsigned long ret;
  if (GetExitCodeProcess(process_handle, &ret)) {
    if (ret != STILL_ACTIVE) return 1;
  }

  kill_t k = { pid, exitcode, 0 };

  /* Try to send a Control-C event to the console. */
  if (service->stop_method & NSSM_STOP_METHOD_CONSOLE) {
    if (! kill_console(service, &k)) return 1;
  }

  /*
    Try to post messages to the windows belonging to the given process ID.
    If the process is a console application it won't have any windows so there's
    no guarantee of success.
  */
  if (service->stop_method & NSSM_STOP_METHOD_WINDOW) {
    EnumWindows((WNDENUMPROC) kill_window, (LPARAM) &k);
    if (k.signalled) {
      if (! await_shutdown(service, _T(__FUNCTION__), service->kill_window_delay)) return 1;
    }
  }

  /*
    Try to post messages to any thread message queues associated with the
    process.  Console applications might have them (but probably won't) so
    there's still no guarantee of success.
  */
  if (service->stop_method & NSSM_STOP_METHOD_THREADS) {
    if (kill_threads(service->name, &k)) {
      if (! await_shutdown(service, _T(__FUNCTION__), service->kill_threads_delay)) return 1;
    }
  }

  /* We tried being nice.  Time for extreme prejudice. */
  if (service->stop_method & NSSM_STOP_METHOD_TERMINATE) {
    return TerminateProcess(process_handle, exitcode);
  }

  return 0;
}

/* Simulate a Control-C event to our console (shared with the app). */
int kill_console(nssm_service_t *service, kill_t *k) {
  unsigned long ret;

  if (! service) return 1;

  /* Check we loaded AttachConsole(). */
  if (! imports.AttachConsole) return 4;

  /* Try to attach to the process's console. */
  if (! imports.AttachConsole(k->pid)) {
    ret = GetLastError();

    switch (ret) {
      case ERROR_INVALID_HANDLE:
        /* The app doesn't have a console. */
        return 1;

      case ERROR_GEN_FAILURE:
        /* The app already exited. */
        return 2;

      case ERROR_ACCESS_DENIED:
      default:
        /* We already have a console. */
        log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_ATTACHCONSOLE_FAILED, service->name, error_string(ret), 0);
        return 3;
    }
  }

  /* Ignore the event ourselves. */
  ret = 0;
  if (! SetConsoleCtrlHandler(0, TRUE)) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_SETCONSOLECTRLHANDLER_FAILED, service->name, error_string(GetLastError()), 0);
    ret = 4;
  }

  /* Send the event. */
  if (! ret) {
    if (! GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0)) {
      log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_GENERATECONSOLECTRLEVENT_FAILED, service->name, error_string(GetLastError()), 0);
      ret = 5;
    }
  }

  /* Detach from the console. */
  if (! FreeConsole()) {
    log_event(EVENTLOG_WARNING_TYPE, NSSM_EVENT_FREECONSOLE_FAILED, service->name, error_string(GetLastError()), 0);
  }

  /* Wait for process to exit. */
  if (await_shutdown(service, _T(__FUNCTION__), service->kill_console_delay)) ret = 6;

  return ret;
}

void kill_process_tree(nssm_service_t *service, unsigned long pid, unsigned long exitcode, unsigned long ppid) {
  /* Shouldn't happen unless the service failed to start. */
  if (! pid) return;

  TCHAR pid_string[16], code[16];
  _sntprintf_s(pid_string, _countof(pid_string), _TRUNCATE, _T("%lu"), pid);
  _sntprintf_s(code, _countof(code), _TRUNCATE, _T("%lu"), exitcode);
  log_event(EVENTLOG_INFORMATION_TYPE, NSSM_EVENT_KILLING, service->name, pid_string, code, 0);

  /* We will need a process handle in order to call TerminateProcess() later. */
  HANDLE process_handle = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_TERMINATE, false, pid);
  if (process_handle) {
    /* Kill this process first, then its descendents. */
    TCHAR ppid_string[16];
    _sntprintf_s(ppid_string, _countof(ppid_string), _TRUNCATE, _T("%lu"), ppid);
    log_event(EVENTLOG_INFORMATION_TYPE, NSSM_EVENT_KILL_PROCESS_TREE, pid_string, ppid_string, service->name, 0);
    if (! kill_process(service, process_handle, pid, exitcode)) {
      /* Maybe it already died. */
      unsigned long ret;
      if (! GetExitCodeProcess(process_handle, &ret) || ret == STILL_ACTIVE) {
        if (service->stop_method & NSSM_STOP_METHOD_TERMINATE) log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_TERMINATEPROCESS_FAILED, pid_string, service->name, error_string(GetLastError()), 0);
        else log_event(EVENTLOG_WARNING_TYPE, NSSM_EVENT_PROCESS_STILL_ACTIVE, service->name, pid_string, NSSM, NSSM_REG_STOP_METHOD_SKIP, 0);
      }
    }

    CloseHandle(process_handle);
  }
  else log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OPENPROCESS_FAILED, pid_string, service->name, error_string(GetLastError()), 0);

  /* Get a snapshot of all processes in the system. */
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (! snapshot) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_CREATETOOLHELP32SNAPSHOT_PROCESS_FAILED, service->name, error_string(GetLastError()), 0);
    return;
  }

  PROCESSENTRY32 pe;
  ZeroMemory(&pe, sizeof(pe));
  pe.dwSize = sizeof(pe);

  if (! Process32First(snapshot, &pe)) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_PROCESS_ENUMERATE_FAILED, service->name, error_string(GetLastError()), 0);
    CloseHandle(snapshot);
    return;
  }

  /* This is a child of the doomed process so kill it. */
  if (! check_parent(service, &pe, pid)) kill_process_tree(service, pe.th32ProcessID, exitcode, ppid);

  while (true) {
    /* Try to get the next process. */
    if (! Process32Next(snapshot, &pe)) {
      unsigned long ret = GetLastError();
      if (ret == ERROR_NO_MORE_FILES) break;
      log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_PROCESS_ENUMERATE_FAILED, service->name, error_string(GetLastError()), 0);
      CloseHandle(snapshot);
      return;
    }

    if (! check_parent(service, &pe, pid)) kill_process_tree(service, pe.th32ProcessID, exitcode, ppid);
  }

  CloseHandle(snapshot);
}
