#ifndef PROCESS_H
#define PROCESS_H

#include <tlhelp32.h>

void kill_process_tree(char *, unsigned long, unsigned long, unsigned long);

#endif
