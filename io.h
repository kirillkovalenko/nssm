#ifndef IO_H
#define IO_H

#define NSSM_STDIN_SHARING FILE_SHARE_WRITE
#define NSSM_STDIN_DISPOSITION OPEN_EXISTING
#define NSSM_STDIN_FLAGS FILE_ATTRIBUTE_NORMAL
#define NSSM_STDOUT_SHARING (FILE_SHARE_READ | FILE_SHARE_WRITE)
#define NSSM_STDOUT_DISPOSITION OPEN_ALWAYS
#define NSSM_STDOUT_FLAGS FILE_ATTRIBUTE_NORMAL
#define NSSM_STDERR_SHARING (FILE_SHARE_READ | FILE_SHARE_WRITE)
#define NSSM_STDERR_DISPOSITION OPEN_ALWAYS
#define NSSM_STDERR_FLAGS FILE_ATTRIBUTE_NORMAL

int get_createfile_parameters(HKEY, TCHAR *, TCHAR *, unsigned long *, unsigned long, unsigned long *, unsigned long, unsigned long *, unsigned long);
int set_createfile_parameter(HKEY, TCHAR *, TCHAR *, unsigned long);
HANDLE append_to_file(TCHAR *, unsigned long, SECURITY_ATTRIBUTES *, unsigned long, unsigned long);
int get_output_handles(HKEY, STARTUPINFO *);
void close_output_handles(STARTUPINFO *);

#endif
