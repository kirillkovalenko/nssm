#ifndef EVENT_H
#define EVENT_H

char *error_string(unsigned long);
char *message_string(unsigned long);
void log_event(unsigned short, unsigned long, ...);
void print_message(FILE *, unsigned long, ...);
int popup_message(unsigned int, unsigned long, ...);

#endif
