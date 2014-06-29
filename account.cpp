#include "nssm.h"

#include <sddl.h>

extern imports_t imports;

/* Open Policy object. */
int open_lsa_policy(LSA_HANDLE *policy) {
  LSA_OBJECT_ATTRIBUTES attributes;
  ZeroMemory(&attributes, sizeof(attributes));

  NTSTATUS status = LsaOpenPolicy(0, &attributes, POLICY_ALL_ACCESS, policy);
  if (status) {
    print_message(stderr, NSSM_MESSAGE_LSAOPENPOLICY_FAILED, error_string(LsaNtStatusToWinError(status)));
    return 1;
  }

  return 0;
}

/* Look up SID for an account. */
int username_sid(const TCHAR *username, SID **sid, LSA_HANDLE *policy) {
  LSA_HANDLE handle;
  if (! policy) {
    policy = &handle;
    if (open_lsa_policy(policy)) return 1;
  }

  /*
    LsaLookupNames() can't look up .\username but can look up
    %COMPUTERNAME%\username.  ChangeServiceConfig() writes .\username to the
    registry when %COMPUTERNAME%\username is a passed as a parameter.  We
    need to preserve .\username when calling ChangeServiceConfig() without
    changing the username, but expand to %COMPUTERNAME%\username when calling
    LsaLookupNames().
  */
  TCHAR *expanded;
  unsigned long expandedlen;
  if (_tcsnicmp(_T(".\\"), username, 2)) {
    expandedlen = (unsigned long) (_tcslen(username) + 1) * sizeof(TCHAR);
    expanded = (TCHAR *) HeapAlloc(GetProcessHeap(), 0, expandedlen);
    if (! expanded) {
      print_message(stderr, NSSM_MESSAGE_OUT_OF_MEMORY, _T("expanded"), _T("username_sid"));
      if (policy == &handle) LsaClose(handle);
      return 2;
    }
    memmove(expanded, username, expandedlen);
  }
  else {
    TCHAR computername[MAX_COMPUTERNAME_LENGTH + 1];
    expandedlen = _countof(computername);
    GetComputerName(computername, &expandedlen);
    expandedlen += (unsigned long) _tcslen(username);

    expanded = (TCHAR *) HeapAlloc(GetProcessHeap(), 0, expandedlen * sizeof(TCHAR));
    if (! expanded) {
      print_message(stderr, NSSM_MESSAGE_OUT_OF_MEMORY, _T("expanded"), _T("username_sid"));
      if (policy == &handle) LsaClose(handle);
      return 2;
    }
    _sntprintf_s(expanded, expandedlen, _TRUNCATE, _T("%s\\%s"), computername, username + 2);
  }

  LSA_UNICODE_STRING lsa_username;
#ifdef UNICODE
  lsa_username.Buffer = (wchar_t *) expanded;
  lsa_username.Length = (unsigned short) _tcslen(expanded) * sizeof(TCHAR);
  lsa_username.MaximumLength = lsa_username.Length + sizeof(TCHAR);
#else
  size_t buflen;
  mbstowcs_s(&buflen, NULL, 0, expanded, _TRUNCATE);
  lsa_username.MaximumLength = (unsigned short) buflen * sizeof(wchar_t);
  lsa_username.Length = lsa_username.MaximumLength - sizeof(wchar_t);
  lsa_username.Buffer = (wchar_t *) HeapAlloc(GetProcessHeap(), 0, lsa_username.MaximumLength);
  if (lsa_username.Buffer) mbstowcs_s(&buflen, lsa_username.Buffer, lsa_username.MaximumLength, expanded, _TRUNCATE);
  else {
    if (policy == &handle) LsaClose(handle);
    HeapFree(GetProcessHeap(), 0, expanded);
    print_message(stderr, NSSM_MESSAGE_OUT_OF_MEMORY, _T("LSA_UNICODE_STRING"), _T("username_sid()"));
    return 4;
  }
#endif

  LSA_REFERENCED_DOMAIN_LIST *translated_domains;
  LSA_TRANSLATED_SID *translated_sid;
  NTSTATUS status = LsaLookupNames(*policy, 1, &lsa_username, &translated_domains, &translated_sid);
#ifndef UNICODE
  HeapFree(GetProcessHeap(), 0, lsa_username.Buffer);
#endif
  HeapFree(GetProcessHeap(), 0, expanded);
  if (policy == &handle) LsaClose(handle);
  if (status) {
    LsaFreeMemory(translated_domains);
    LsaFreeMemory(translated_sid);
    print_message(stderr, NSSM_MESSAGE_LSALOOKUPNAMES_FAILED, username, error_string(LsaNtStatusToWinError(status)));
    return 5;
  }

  if (translated_sid->Use != SidTypeUser && translated_sid->Use != SidTypeWellKnownGroup) {
    LsaFreeMemory(translated_domains);
    LsaFreeMemory(translated_sid);
    print_message(stderr, NSSM_GUI_INVALID_USERNAME, username);
    return 6;
  }

  LSA_TRUST_INFORMATION *trust = &translated_domains->Domains[translated_sid->DomainIndex];
  if (! trust || ! IsValidSid(trust->Sid)) {
    LsaFreeMemory(translated_domains);
    LsaFreeMemory(translated_sid);
    print_message(stderr, NSSM_GUI_INVALID_USERNAME, username);
    return 7;
  }

  /* GetSidSubAuthority*() return pointers! */
  unsigned char *n = GetSidSubAuthorityCount(trust->Sid);

  /* Convert translated SID to SID. */
  *sid = (SID *) HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, GetSidLengthRequired(*n + 1));
  if (! *sid) {
    LsaFreeMemory(translated_domains);
    LsaFreeMemory(translated_sid);
    print_message(stderr, NSSM_MESSAGE_OUT_OF_MEMORY, _T("SID"), _T("username_sid"));
    return 8;
  }

  unsigned long error;
  if (! InitializeSid(*sid, GetSidIdentifierAuthority(trust->Sid), *n + 1)) {
    error = GetLastError();
    HeapFree(GetProcessHeap(), 0, *sid);
    LsaFreeMemory(translated_domains);
    LsaFreeMemory(translated_sid);
    print_message(stderr, NSSM_MESSAGE_INITIALIZESID_FAILED, username, error_string(error));
    return 9;
  }

  for (unsigned char i = 0; i <= *n; i++) {
    unsigned long *sub = GetSidSubAuthority(*sid, i);
    if (i < *n) *sub = *GetSidSubAuthority(trust->Sid, i);
    else *sub = translated_sid->RelativeId;
  }

  int ret = 0;
  if (translated_sid->Use == SidTypeWellKnownGroup && ! well_known_sid(*sid)) {
    print_message(stderr, NSSM_GUI_INVALID_USERNAME, username);
    ret = 10;
  }

  LsaFreeMemory(translated_domains);
  LsaFreeMemory(translated_sid);

  return ret;
}

int username_sid(const TCHAR *username, SID **sid) {
  return username_sid(username, sid, 0);
}

int canonicalise_username(const TCHAR *username, TCHAR **canon) {
  LSA_HANDLE policy;
  if (open_lsa_policy(&policy)) return 1;

  SID *sid;
  if (username_sid(username, &sid, &policy)) return 2;
  PSID sids = { sid };

  LSA_REFERENCED_DOMAIN_LIST *translated_domains;
  LSA_TRANSLATED_NAME *translated_name;
  NTSTATUS status = LsaLookupSids(policy, 1, &sids, &translated_domains, &translated_name);
  if (status) {
    LsaFreeMemory(translated_domains);
    LsaFreeMemory(translated_name);
    print_message(stderr, NSSM_MESSAGE_LSALOOKUPSIDS_FAILED, error_string(LsaNtStatusToWinError(status)));
    return 3;
  }

  LSA_TRUST_INFORMATION *trust = &translated_domains->Domains[translated_name->DomainIndex];
  LSA_UNICODE_STRING lsa_canon;
  lsa_canon.Length = translated_name->Name.Length + trust->Name.Length + sizeof(wchar_t);
  lsa_canon.MaximumLength = lsa_canon.Length + sizeof(wchar_t);
  lsa_canon.Buffer = (wchar_t *) HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, lsa_canon.MaximumLength);
  if (! lsa_canon.Buffer) {
    LsaFreeMemory(translated_domains);
    LsaFreeMemory(translated_name);
    print_message(stderr, NSSM_MESSAGE_OUT_OF_MEMORY, _T("lsa_canon"), _T("username_sid"));
    return 9;
  }

  /* Buffer is wchar_t but Length is in bytes. */
  memmove((char *) lsa_canon.Buffer, trust->Name.Buffer, trust->Name.Length);
  memmove((char *) lsa_canon.Buffer + trust->Name.Length, L"\\", sizeof(wchar_t));
  memmove((char *) lsa_canon.Buffer + trust->Name.Length + sizeof(wchar_t), translated_name->Name.Buffer, translated_name->Name.Length);

#ifdef UNICODE
  *canon = lsa_canon.Buffer;
#else
  size_t buflen;
  wcstombs_s(&buflen, NULL, 0, lsa_canon.Buffer, _TRUNCATE);
  *canon = (TCHAR *) HeapAlloc(GetProcessHeap(), 0, buflen);
  if (! *canon) {
    LsaFreeMemory(translated_domains);
    LsaFreeMemory(translated_name);
    print_message(stderr, NSSM_MESSAGE_OUT_OF_MEMORY, _T("canon"), _T("username_sid"));
    return 10;
  }
  wcstombs_s(&buflen, *canon, buflen, lsa_canon.Buffer, _TRUNCATE);
  HeapFree(GetProcessHeap(), 0, lsa_canon.Buffer);
#endif

  LsaFreeMemory(translated_domains);
  LsaFreeMemory(translated_name);

  return 0;
}

/* Do two usernames map to the same SID? */
int username_equiv(const TCHAR *a, const TCHAR *b) {
  SID *sid_a, *sid_b;
  if (username_sid(a, &sid_a)) return 0;

  if (username_sid(b, &sid_b)) {
    FreeSid(sid_a);
    return 0;
  }

  int ret = 0;
  if (EqualSid(sid_a, sid_b)) ret = 1;

  FreeSid(sid_a);
  FreeSid(sid_b);

  return ret;
}

/* Does the username represent the LocalSystem account? */
int is_localsystem(const TCHAR *username) {
  if (str_equiv(username, NSSM_LOCALSYSTEM_ACCOUNT)) return 1;
  if (! imports.IsWellKnownSid) return 0;

  SID *sid;
  if (username_sid(username, &sid)) return 0;

  int ret = 0;
  if (imports.IsWellKnownSid(sid, WinLocalSystemSid)) ret = 1;

  FreeSid(sid);

  return ret;
}

/*
  Get well-known alias for LocalSystem and friends.
  Returns a pointer to a static string.  DO NOT try to free it.
*/
const TCHAR *well_known_sid(SID *sid) {
  if (! imports.IsWellKnownSid) return 0;
  if (imports.IsWellKnownSid(sid, WinLocalSystemSid)) return NSSM_LOCALSYSTEM_ACCOUNT;
  if (imports.IsWellKnownSid(sid, WinLocalServiceSid)) return NSSM_LOCALSERVICE_ACCOUNT;
  if (imports.IsWellKnownSid(sid, WinNetworkServiceSid)) return NSSM_NETWORKSERVICE_ACCOUNT;
  return 0;
}

const TCHAR *well_known_username(const TCHAR *username) {
  if (! username) return NSSM_LOCALSYSTEM_ACCOUNT;
  if (str_equiv(username, NSSM_LOCALSYSTEM_ACCOUNT)) return NSSM_LOCALSYSTEM_ACCOUNT;
  SID *sid;
  if (username_sid(username, &sid)) return 0;

  const TCHAR *well_known = well_known_sid(sid);
  FreeSid(sid);

  return well_known;
}

int grant_logon_as_service(const TCHAR *username) {
  if (! username) return 0;

  /* Open Policy object. */
  LSA_OBJECT_ATTRIBUTES attributes;
  ZeroMemory(&attributes, sizeof(attributes));

  LSA_HANDLE policy;
  NTSTATUS status;

  if (open_lsa_policy(&policy)) return 1;

  /* Look up SID for the account. */
  SID *sid;
  if (username_sid(username, &sid, &policy)) {
    LsaClose(policy);
    return 2;
  }

  /*
    Shouldn't happen because it should have been checked before callling this function.
  */
  if (well_known_sid(sid)) {
    LsaClose(policy);
    return 3;
  }

  /* Check if the SID has the "Log on as a service" right. */
  LSA_UNICODE_STRING lsa_right;
  lsa_right.Buffer = NSSM_LOGON_AS_SERVICE_RIGHT;
  lsa_right.Length = (unsigned short) wcslen(lsa_right.Buffer) * sizeof(wchar_t);
  lsa_right.MaximumLength = lsa_right.Length + sizeof(wchar_t);

  LSA_UNICODE_STRING *rights;
  unsigned long count = ~0;
  status = LsaEnumerateAccountRights(policy, sid, &rights, &count);
  if (status) {
    /*
      If the account has no rights set LsaEnumerateAccountRights() will return
      STATUS_OBJECT_NAME_NOT_FOUND and set count to 0.
    */
    unsigned long error = LsaNtStatusToWinError(status);
    if (error != ERROR_FILE_NOT_FOUND) {
      FreeSid(sid);
      LsaClose(policy);
      print_message(stderr, NSSM_MESSAGE_LSAENUMERATEACCOUNTRIGHTS_FAILED, username, error_string(error));
      return 4;
    }
  }

  for (unsigned long i = 0; i < count; i++) {
    if (rights[i].Length != lsa_right.Length) continue;
    if (_wcsnicmp(rights[i].Buffer, lsa_right.Buffer, lsa_right.MaximumLength)) continue;
    /* The SID has the right. */
    FreeSid(sid);
    LsaFreeMemory(rights);
    LsaClose(policy);
    return 0;
  }
  LsaFreeMemory(rights);

  /* Add the right. */
  status = LsaAddAccountRights(policy, sid, &lsa_right, 1);
  FreeSid(sid);
  LsaClose(policy);
  if (status) {
    print_message(stderr, NSSM_MESSAGE_LSAADDACCOUNTRIGHTS_FAILED, error_string(LsaNtStatusToWinError(status)));
    return 5;
  }

  print_message(stdout, NSSM_MESSAGE_GRANTED_LOGON_AS_SERVICE, username);
  return 0;
}
