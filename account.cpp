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

  LSA_UNICODE_STRING lsa_username;
#ifdef UNICODE
  lsa_username.Buffer = (wchar_t *) username;
  lsa_username.Length = (unsigned short) _tcslen(username) * sizeof(TCHAR);
  lsa_username.MaximumLength = lsa_username.Length + sizeof(TCHAR);
#else
  size_t buflen;
  mbstowcs_s(&buflen, NULL, 0, username, _TRUNCATE);
  lsa_username.MaximumLength = (unsigned short) buflen * sizeof(wchar_t);
  lsa_username.Length = lsa_username.MaximumLength - sizeof(wchar_t);
  lsa_username.Buffer = (wchar_t *) HeapAlloc(GetProcessHeap(), 0, lsa_username.MaximumLength);
  if (lsa_username.Buffer) mbstowcs_s(&buflen, lsa_username.Buffer, lsa_username.MaximumLength, username, _TRUNCATE);
  else {
    if (policy == &handle) LsaClose(handle);
    print_message(stderr, NSSM_MESSAGE_OUT_OF_MEMORY, _T("LSA_UNICODE_STRING"), _T("username_sid()"));
    return 2;
  }
#endif

  LSA_REFERENCED_DOMAIN_LIST *translated_domains;
  LSA_TRANSLATED_SID *translated_sid;
  NTSTATUS status = LsaLookupNames(*policy, 1, &lsa_username, &translated_domains, &translated_sid);
#ifndef UNICODE
  HeapFree(GetProcessHeap(), 0, lsa_username.Buffer);
#endif
  if (policy == &handle) LsaClose(handle);
  if (status) {
    LsaFreeMemory(translated_domains);
    LsaFreeMemory(translated_sid);
    print_message(stderr, NSSM_MESSAGE_LSALOOKUPNAMES_FAILED, username, error_string(LsaNtStatusToWinError(status)));
    return 3;
  }

  if (translated_sid->Use != SidTypeUser && translated_sid->Use != SidTypeWellKnownGroup) {
    LsaFreeMemory(translated_domains);
    LsaFreeMemory(translated_sid);
    print_message(stderr, NSSM_GUI_INVALID_USERNAME, username);
    return 4;
  }

  LSA_TRUST_INFORMATION *trust = &translated_domains->Domains[translated_sid->DomainIndex];
  if (! trust || ! IsValidSid(trust->Sid)) {
    LsaFreeMemory(translated_domains);
    LsaFreeMemory(translated_sid);
    print_message(stderr, NSSM_GUI_INVALID_USERNAME, username);
    return 5;
  }

  /* GetSidSubAuthority*() return pointers! */
  unsigned char *n = GetSidSubAuthorityCount(trust->Sid);

  /* Convert translated SID to SID. */
  *sid = (SID *) HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, GetSidLengthRequired(*n + 1));
  if (! *sid) {
    LsaFreeMemory(translated_domains);
    LsaFreeMemory(translated_sid);
    print_message(stderr, NSSM_MESSAGE_OUT_OF_MEMORY, _T("SID"), _T("grant_logon_as_service"));
    return 6;
  }

  unsigned long error;
  if (! InitializeSid(*sid, GetSidIdentifierAuthority(trust->Sid), *n + 1)) {
    error = GetLastError();
    HeapFree(GetProcessHeap(), 0, *sid);
    LsaFreeMemory(translated_domains);
    LsaFreeMemory(translated_sid);
    print_message(stderr, NSSM_MESSAGE_INITIALIZESID_FAILED, username, error_string(error));
    return 7;
  }

  for (unsigned char i = 0; i <= *n; i++) {
    unsigned long *sub = GetSidSubAuthority(*sid, i);
    if (i < *n) *sub = *GetSidSubAuthority(trust->Sid, i);
    else *sub = translated_sid->RelativeId;
  }

  int ret = 0;
  if (translated_sid->Use == SidTypeWellKnownGroup && requires_password(*sid)) {
    print_message(stderr, NSSM_GUI_INVALID_USERNAME, username);
    ret = 8;
  }

  LsaFreeMemory(translated_domains);
  LsaFreeMemory(translated_sid);

  return ret;
}

int username_sid(const TCHAR *username, SID **sid) {
  return username_sid(username, sid, 0);
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
  Find the canonical name for a well-known account name.
  MUST ONLY BE USED for well-known account names.
  Must call LocalFree() on result.
*/
TCHAR *canonical_username(const TCHAR *username) {
  SID *user_sid;
  TCHAR *canon;
  size_t len;

  if (is_localsystem(username)) {
    len = (_tcslen(NSSM_LOCALSYSTEM_ACCOUNT) + 1) * sizeof(TCHAR);
    canon = (TCHAR *) LocalAlloc(LPTR, len);
    if (! canon) {
      print_message(stderr, NSSM_MESSAGE_OUT_OF_MEMORY, NSSM_LOCALSYSTEM_ACCOUNT, _T("canonical_username"));
      return 0;
    }
    memmove(canon, NSSM_LOCALSYSTEM_ACCOUNT, len);
    _tprintf(_T("it's localsystem = %s!\n"), canon);
    return canon;
  }

  if (! imports.CreateWellKnownSid) return 0;

  if (username_sid(username, &user_sid)) return 0;

  /*
    LsaLookupSids will return the canonical username but the result won't
    include the NT Authority part.  Thus we must look that up as well.
  */
  unsigned long ntsidsize = SECURITY_MAX_SID_SIZE;
  SID *ntauth_sid = (SID *) HeapAlloc(GetProcessHeap(), 0, ntsidsize);
  if (! ntauth_sid) {
    HeapFree(GetProcessHeap(), 0, user_sid);
    print_message(stderr, NSSM_MESSAGE_OUT_OF_MEMORY, _T("NT Authority"), _T("canonical_username"));
    return 0;
  }

  if (! imports.CreateWellKnownSid(WinNtAuthoritySid, NULL, ntauth_sid, &ntsidsize)) {
    HeapFree(GetProcessHeap(), 0, ntauth_sid);
    print_message(stderr, NSSM_MESSAGE_CREATEWELLKNOWNSID_FAILED, _T("WinNtAuthoritySid"));
    return 0;
  }

  LSA_HANDLE policy;
  if (open_lsa_policy(&policy)) return 0;

  LSA_REFERENCED_DOMAIN_LIST *translated_domains;
  LSA_TRANSLATED_NAME *translated_names;

  unsigned long n = 2;
  PSID *sids = (PSID *) HeapAlloc(GetProcessHeap(), 0, n * sizeof(PSID));
  sids[0] = user_sid;
  sids[1] = ntauth_sid;

  NTSTATUS status = LsaLookupSids(policy, n, (PSID *) sids, &translated_domains, &translated_names);
  HeapFree(GetProcessHeap(), 0, user_sid);
  HeapFree(GetProcessHeap(), 0, ntauth_sid);
  HeapFree(GetProcessHeap(), 0, sids);
  LsaClose(policy);
  if (status) {
    print_message(stderr, NSSM_MESSAGE_LSALOOKUPSIDS_FAILED);
    return 0;
  }

  /* Look up the account name. */
  LSA_TRANSLATED_NAME *translated_name = &(translated_names[0]);
  if (translated_name->Use != SidTypeWellKnownGroup) {
    print_message(stderr, NSSM_GUI_INVALID_USERNAME);
    LsaFreeMemory(translated_domains);
    LsaFreeMemory(translated_names);
    return 0;
  }

  LSA_UNICODE_STRING *lsa_group = &translated_name->Name;

  /* Look up NT Authority. */
  translated_name = &(translated_names[1]);
  if (translated_name->Use != SidTypeDomain) {
    print_message(stderr, NSSM_GUI_INVALID_USERNAME);
    LsaFreeMemory(translated_domains);
    LsaFreeMemory(translated_names);
    return 0;
  }

  /* In theory these pointers should all be valid if we got this far... */
  LSA_TRUST_INFORMATION *trust = &translated_domains->Domains[translated_name->DomainIndex];
  LSA_UNICODE_STRING *lsa_domain = &trust->Name;

  TCHAR *domain, *group;
  unsigned long lsa_domain_len = lsa_domain->Length;
  unsigned long lsa_group_len = lsa_group->Length;
  len = lsa_domain_len + lsa_group_len + 2;

#ifdef UNICODE
  domain = lsa_domain->Buffer;
  group = lsa_group->Buffer;
#else
  size_t buflen;

  wcstombs_s(&buflen, NULL, 0, lsa_domain->Buffer, _TRUNCATE);
  domain = (TCHAR *) HeapAlloc(GetProcessHeap(), 0, buflen);
  if (! domain) {
    print_message(stderr, NSSM_MESSAGE_OUT_OF_MEMORY, _T("domain"), _T("canonical_username"));
    LsaFreeMemory(translated_domains);
    LsaFreeMemory(translated_names);
    return 0;
  }
  wcstombs_s(&buflen, (char *) domain, buflen, lsa_domain->Buffer, _TRUNCATE);

  wcstombs_s(&buflen, NULL, 0, lsa_group->Buffer, _TRUNCATE);
  group = (TCHAR *) HeapAlloc(GetProcessHeap(), 0, buflen);
  if (! group) {
    print_message(stderr, NSSM_MESSAGE_OUT_OF_MEMORY, _T("group"), _T("canonical_username"));
    LsaFreeMemory(translated_domains);
    LsaFreeMemory(translated_names);
    return 0;
  }
  wcstombs_s(&buflen, (char *) group, buflen, lsa_group->Buffer, _TRUNCATE);
#endif

  canon = (TCHAR *) HeapAlloc(GetProcessHeap(), 0, len * sizeof(TCHAR));
  if (! canon) {
    LsaFreeMemory(translated_domains);
    LsaFreeMemory(translated_names);
    print_message(stderr, NSSM_MESSAGE_OUT_OF_MEMORY, _T("canon"), _T("canonical_username"));
    return 0;
  }

  _sntprintf_s(canon, len, _TRUNCATE, _T("%s\\%s"), domain, group);

#ifndef UNICODE
  HeapFree(GetProcessHeap(), 0, domain);
  HeapFree(GetProcessHeap(), 0, group);
#endif

  LsaFreeMemory(translated_domains);
  LsaFreeMemory(translated_names);

  return canon;
}

/* Does the SID type require a password? */
int requires_password(SID *sid) {
  if (! imports.IsWellKnownSid) return -1;
  if (imports.IsWellKnownSid(sid, WinLocalSystemSid)) return 0;
  if (imports.IsWellKnownSid(sid, WinLocalServiceSid)) return 0;
  if (imports.IsWellKnownSid(sid, WinNetworkServiceSid)) return 0;;
  return 1;
}

/* Does the username require a password? */
int requires_password(const TCHAR *username) {
  if (str_equiv(username, NSSM_LOCALSYSTEM_ACCOUNT)) return 0;

  SID *sid;
  int r = username_sid(username, &sid);
  if (username_sid(username, &sid)) return 0;

  int ret = 0;
  ret = requires_password(sid);

  FreeSid(sid);

  return ret;
}

int grant_logon_as_service(const TCHAR *username) {
  if (! username) return 0;
  if (! requires_password(username)) return 0;

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
