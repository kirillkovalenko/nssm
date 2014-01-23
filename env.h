#ifndef ENV_H
#define ENV_H

TCHAR *useful_environment(TCHAR *);
TCHAR *expand_environment_string(TCHAR *);
int set_environment_block(TCHAR *);
int clear_environment();
int duplicate_environment(TCHAR *);
int format_environment(TCHAR *, unsigned long, TCHAR **, unsigned long *);
int unformat_environment(TCHAR *, unsigned long, TCHAR **, unsigned long *);
int test_environment(TCHAR *);

#endif
