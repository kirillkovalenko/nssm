#ifndef ACCOUNT_H
#define ACCOUNT_H

#include <ntsecapi.h>

/* Not really an account.  The canonical name is NT Authority\System. */
#define NSSM_LOCALSYSTEM_ACCOUNT _T("LocalSystem")
/* This is explicitly a wide string. */
#define NSSM_LOGON_AS_SERVICE_RIGHT L"SeServiceLogonRight"


int open_lsa_policy(LSA_HANDLE *);
int username_sid(const TCHAR *, SID **, LSA_HANDLE *);
int username_sid(const TCHAR *, SID **);
int username_equiv(const TCHAR *, const TCHAR *);
int is_localsystem(const TCHAR *);
TCHAR *canonical_username(const TCHAR *);
int requires_password(SID *);
int requires_password(const TCHAR *);
int grant_logon_as_service(const TCHAR *);

#endif
