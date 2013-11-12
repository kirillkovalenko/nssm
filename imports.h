#ifndef IMPORTS_H
#define IMPORTS_H

typedef BOOL (WINAPI *AttachConsole_ptr)(DWORD);

typedef struct {
  HMODULE kernel32;
  AttachConsole_ptr AttachConsole;
} imports_t;

HMODULE get_dll(const char *, unsigned long *);
FARPROC get_import(HMODULE, const char *, unsigned long *);
int get_imports();
void free_imports();

#endif
