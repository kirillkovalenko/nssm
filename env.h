#ifndef ENV_H
#define ENV_H

int format_environment(TCHAR *, unsigned long, TCHAR **, unsigned long *);
int unformat_environment(TCHAR *, unsigned long, TCHAR **, unsigned long *);
int test_environment(TCHAR *);

#endif
