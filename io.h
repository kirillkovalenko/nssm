#ifndef IO_H
#define IO_H

int get_createfile_parameters(HKEY, char *, char *, unsigned long *, unsigned long, unsigned long *, unsigned long, unsigned long *, unsigned long);
HANDLE append_to_file(char *, unsigned long, SECURITY_ATTRIBUTES *, unsigned long, unsigned long);
int get_output_handles(HKEY, STARTUPINFO *);
void close_output_handles(STARTUPINFO *);

#endif
