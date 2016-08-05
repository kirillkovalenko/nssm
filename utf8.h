#ifndef UTF8_H
#define UTF8_H

void setup_utf8();
void unsetup_utf8();
int to_utf8(const wchar_t *, char **, unsigned long *);
int to_utf8(const char *, char **, unsigned long *);
int to_utf16(const char *, wchar_t **utf16, unsigned long *);
int to_utf16(const wchar_t *, wchar_t **utf16, unsigned long *);
int from_utf8(const char *, TCHAR **, unsigned long *);
int from_utf16(const wchar_t *, TCHAR **, unsigned long *);

#endif
