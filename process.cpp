#include "nssm.h"

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

  memmove(ft, &exit_time, sizeof(exit_time));

  return 0;
}

int check_parent(char *service_name, PROCESSENTRY32 *pe, unsigned long ppid, FILETIME *pft, FILETIME *exit_time) {
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
    char pid_string[16];
    _snprintf(pid_string, sizeof(pid_string), "%d", pe->th32ProcessID);
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OPENPROCESS_FAILED, pid_string, service_name, error_string(GetLastError()), 0);
    return 2;
  }

  FILETIME ft;
  if (get_process_creation_time(process_handle, &ft)) {
    CloseHandle(process_handle);
    return 3;
  }

  CloseHandle(process_handle);

  /* Verify that the parent's creation time is not later. */
  if (CompareFileTime(pft, &ft) > 0) return 4;

  /* Verify that the parent's exit time is not earlier. */
  if (CompareFileTime(exit_time, &ft) < 0) return 5;

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
int kill_threads(char *service_name, kill_t *k) {
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
int kill_process(char *service_name, HANDLE process_handle, unsigned long pid, unsigned long exitcode) {
  /* Shouldn't happen. */
  if (! pid) return 1;
  if (! process_handle) return 1;

  unsigned long ret;
  if (GetExitCodeProcess(process_handle, &ret)) {
    if (ret != STILL_ACTIVE) return 1;
  }

  kill_t k = { pid, exitcode, 0 };

  /*
    Try to post messages to the windows belonging to the given process ID.
    If the process is a console application it won't have any windows so there's
    no guarantee of success.
  */
  EnumWindows((WNDENUMPROC) kill_window, (LPARAM) &k);
  if (k.signalled) {
    if (! WaitForSingleObject(process_handle, NSSM_KILL_WINDOW_GRACE_PERIOD)) return 1;
  }

  /*
    Try to post messages to any thread message queues associated with the
    process.  Console applications might have them (but probably won't) so
    there's still no guarantee of success.
  */
  if (kill_threads(service_name, &k)) {
    if (! WaitForSingleObject(process_handle, NSSM_KILL_THREADS_GRACE_PERIOD)) return 1;
  }

  /* We tried being nice.  Time for extreme prejudice. */
  return TerminateProcess(process_handle, exitcode);
}

void kill_process_tree(char *service_name, unsigned long pid, unsigned long exitcode, unsigned long ppid, FILETIME *parent_creation_time, FILETIME *parent_exit_time) {
  /* Shouldn't happen unless the service failed to start. */
  if (! pid) return;

  char pid_string[16], code[16];
  _snprintf(pid_string, sizeof(pid_string), "%d", pid);
  _snprintf(code, sizeof(code), "%d", exitcode);
  log_event(EVENTLOG_INFORMATION_TYPE, NSSM_EVENT_KILLING, service_name, pid_string, code, 0);

  /* Get a snapshot of all processes in the system. */
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (! snapshot) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_CREATETOOLHELP32SNAPSHOT_PROCESS_FAILED, service_name, error_string(GetLastError()), 0);
    return;
  }

  PROCESSENTRY32 pe;
  ZeroMemory(&pe, sizeof(pe));
  pe.dwSize = sizeof(pe);

  if (! Process32First(snapshot, &pe)) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_PROCESS_ENUMERATE_FAILED, service_name, error_string(GetLastError()), 0);
    CloseHandle(snapshot);
    return;
  }

  /* This is a child of the doomed process so kill it. */
  if (! check_parent(service_name, &pe, pid, parent_creation_time, parent_exit_time)) kill_process_tree(service_name, pe.th32ProcessID, exitcode, ppid, parent_creation_time, parent_exit_time);

  while (true) {
    /* Try to get the next process. */
    if (! Process32Next(snapshot, &pe)) {
      unsigned long ret = GetLastError();
      if (ret == ERROR_NO_MORE_FILES) break;
      log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_PROCESS_ENUMERATE_FAILED, service_name, error_string(GetLastError()), 0);
      CloseHandle(snapshot);
      return;
    }

    if (! check_parent(service_name, &pe, pid, parent_creation_time, parent_exit_time)) kill_process_tree(service_name, pe.th32ProcessID, exitcode, ppid, parent_creation_time, parent_exit_time);
  }

  CloseHandle(snapshot);

  /* We will need a process handle in order to call TerminateProcess() later. */
  HANDLE process_handle = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_TERMINATE, false, pid);
  if (! process_handle) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OPENPROCESS_FAILED, pid_string, service_name, error_string(GetLastError()), 0);
    return;
  }

  char ppid_string[16];
  _snprintf(ppid_string, sizeof(ppid_string), "%d", ppid);
  log_event(EVENTLOG_INFORMATION_TYPE, NSSM_EVENT_KILL_PROCESS_TREE, pid_string, ppid_string, service_name, 0);
  if (! kill_process(service_name, process_handle, pid, exitcode)) {
    /* Maybe it already died. */
    unsigned long ret;
    if (! GetExitCodeProcess(process_handle, &ret) || ret == STILL_ACTIVE) log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_TERMINATEPROCESS_FAILED, pid_string, service_name, error_string(GetLastError()), 0);
  }

  CloseHandle(process_handle);
}
