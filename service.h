#ifndef SERVICE_H
#define SERVICE_H

#define ACTION_LEN 16

void WINAPI service_main(unsigned long, char **);
unsigned long WINAPI service_control_handler(unsigned long, unsigned long, void *, void *);

SC_HANDLE open_service_manager();
int pre_install_service(int, char **);
int pre_remove_service(int, char **);
int install_service(char *, char *, char *);
int remove_service(char *);
void set_service_recovery(char *);
int monitor_service();
int start_service();
int stop_service(unsigned long, bool, bool);
void CALLBACK end_service(void *, unsigned char);
void throttle_restart();

#endif
