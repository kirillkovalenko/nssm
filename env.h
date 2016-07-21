#ifndef ENV_H
#define ENV_H

size_t environment_length(TCHAR *);
TCHAR *copy_environment_block(TCHAR *);
TCHAR *useful_environment(TCHAR *);
TCHAR *expand_environment_string(TCHAR *);
int set_environment_block(TCHAR *);
int clear_environment();
int duplicate_environment(TCHAR *);
int test_environment(TCHAR *);
void duplicate_environment_strings(TCHAR *);
TCHAR *copy_environment();
int append_to_environment_block(TCHAR *, unsigned long, TCHAR *, TCHAR **, unsigned long *);
int remove_from_environment_block(TCHAR *, unsigned long, TCHAR *, TCHAR **, unsigned long *);

#endif
