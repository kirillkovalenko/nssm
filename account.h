#ifndef ACCOUNT_H
#define ACCOUNT_H

#include <ntsecapi.h>

/* Not really an account.  The canonical name is NT Authority\System. */
#define NSSM_LOCALSYSTEM_ACCOUNT _T("LocalSystem")
/* Other well-known accounts which can start a service without a password. */
#define NSSM_LOCALSERVICE_ACCOUNT _T("NT Authority\\LocalService")
#define NSSM_NETWORKSERVICE_ACCOUNT _T("NT Authority\\NetworkService")
/* Virtual service accounts. */
#define NSSM_VIRTUAL_SERVICE_ACCOUNT_DOMAIN _T("NT Service")
/* This is explicitly a wide string. */
#define NSSM_LOGON_AS_SERVICE_RIGHT L"SeServiceLogonRight"

int open_lsa_policy(LSA_HANDLE *);
int username_sid(const TCHAR *, SID **, LSA_HANDLE *);
int username_sid(const TCHAR *, SID **);
int username_equiv(const TCHAR *, const TCHAR *);
int canonicalise_username(const TCHAR *, TCHAR **);
int is_localsystem(const TCHAR *);
TCHAR *virtual_account(const TCHAR *);
int is_virtual_account(const TCHAR *, const TCHAR *);
const TCHAR *well_known_sid(SID *);
const TCHAR *well_known_username(const TCHAR *);
int grant_logon_as_service(const TCHAR *);

#endif
