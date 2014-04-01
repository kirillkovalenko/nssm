#ifndef ENV_H
#define ENV_H

TCHAR *copy_environment_block(TCHAR *);
TCHAR *useful_environment(TCHAR *);
TCHAR *expand_environment_string(TCHAR *);
int set_environment_block(TCHAR *);
int clear_environment();
int duplicate_environment(TCHAR *);
int test_environment(TCHAR *);
void duplicate_environment_strings(TCHAR *);

#endif
