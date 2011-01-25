#include "nssm.h"

void kill_process_tree(char *service_name, unsigned long pid, unsigned long exitcode, unsigned long ppid) {
  log_event(EVENTLOG_INFORMATION_TYPE, NSSM_EVENT_KILLING, service_name, pid, exitcode, 0);

  /* Shouldn't happen. */
  if (! pid) return;

  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (! snapshot) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_CREATETOOLHELP32SNAPSHOT_FAILED, service_name, GetLastError(), 0);
    return;
  }

  PROCESSENTRY32 pe;
  ZeroMemory(&pe, sizeof(pe));
  pe.dwSize = sizeof(pe);

  if (! Process32First(snapshot, &pe)) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_PROCESS_ENUMERATE_FAILED, service_name, GetLastError(), 0);
    return;
  }

  if (pe.th32ParentProcessID == pid) kill_process_tree(service_name, pe.th32ProcessID, exitcode, ppid);

  while (true) {
    /* Try to get the next process. */
    if (! Process32Next(snapshot, &pe)) {
      unsigned long ret = GetLastError();
      if (ret == ERROR_NO_MORE_FILES) break;
      log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_PROCESS_ENUMERATE_FAILED, service_name, GetLastError(), 0);
      return;
    }

    if (pe.th32ParentProcessID == pid) kill_process_tree(service_name, pe.th32ProcessID, exitcode, ppid);
  }

  HANDLE process_handle = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_TERMINATE, false, pid);
  if (! process_handle) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_OPENPROCESS_FAILED, pid, service_name, GetLastError(), 0);
    return;
  }

  log_event(EVENTLOG_INFORMATION_TYPE, NSSM_EVENT_KILL_PROCESS_TREE, pid, ppid, service_name, 0);
  if (! TerminateProcess(process_handle, exitcode)) {
    log_event(EVENTLOG_ERROR_TYPE, NSSM_EVENT_TERMINATEPROCESS_FAILED, pid, service_name, GetLastError(), 0);
    return;
  }
}
