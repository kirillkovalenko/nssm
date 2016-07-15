#ifndef SETTINGS_H
#define SETTINGS_H

#define NSSM_NATIVE_DEPENDONGROUP _T("DependOnGroup")
#define NSSM_NATIVE_DEPENDONSERVICE _T("DependOnService")
#define NSSM_NATIVE_DESCRIPTION _T("Description")
#define NSSM_NATIVE_DISPLAYNAME _T("DisplayName")
#define NSSM_NATIVE_ENVIRONMENT _T("Environment")
#define NSSM_NATIVE_IMAGEPATH _T("ImagePath")
#define NSSM_NATIVE_NAME _T("Name")
#define NSSM_NATIVE_OBJECTNAME _T("ObjectName")
#define NSSM_NATIVE_STARTUP _T("Start")
#define NSSM_NATIVE_TYPE _T("Type")

/* Are additional arguments needed? */
#define ADDITIONAL_GETTING (1 << 0)
#define ADDITIONAL_SETTING (1 << 1)
#define ADDITIONAL_RESETTING (1 << 2)
#define ADDITIONAL_CRLF (1 << 3)
#define ADDITIONAL_MANDATORY ADDITIONAL_GETTING|ADDITIONAL_SETTING|ADDITIONAL_RESETTING

#define DEPENDENCY_SERVICES (1 << 0)
#define DEPENDENCY_GROUPS (1 << 1)
#define DEPENDENCY_ALL (DEPENDENCY_SERVICES|DEPENDENCY_GROUPS)

typedef union {
  unsigned long numeric;
  TCHAR *string;
} value_t;

typedef int (*setting_function_t)(const TCHAR *, void *, const TCHAR *, void *, value_t *, const TCHAR *);

typedef struct {
  const TCHAR *name;
  unsigned long type;
  void *default_value;
  bool native;
  int additional;
  setting_function_t set;
  setting_function_t get;
  setting_function_t dump;
} settings_t;

int set_setting(const TCHAR *, HKEY, settings_t *, value_t *, const TCHAR *);
int set_setting(const TCHAR *, SC_HANDLE, settings_t *, value_t *, const TCHAR *);
int get_setting(const TCHAR *, HKEY, settings_t *, value_t *, const TCHAR *);
int get_setting(const TCHAR *, SC_HANDLE, settings_t *, value_t *, const TCHAR *);
int dump_setting(const TCHAR *, HKEY, SC_HANDLE, settings_t *);

#endif
