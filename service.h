#ifndef SERVICE_H
#define SERVICE_H

#define ACTION_LEN 16

void WINAPI service_main(unsigned long, char **);
char *service_control_text(unsigned long);
void log_service_control(char *, unsigned long, bool);
unsigned long WINAPI service_control_handler(unsigned long, unsigned long, void *, void *);

SC_HANDLE open_service_manager();
int pre_install_service(int, char **);
int pre_remove_service(int, char **);
int install_service(char *, char *, char *);
int remove_service(char *);
void set_service_recovery(SC_HANDLE, char *);
int monitor_service();
int start_service();
int stop_service(unsigned long, bool, bool);
void CALLBACK end_service(void *, unsigned char);
void throttle_restart();
int await_shutdown(char *, char *, SERVICE_STATUS_HANDLE, SERVICE_STATUS *, HANDLE, unsigned long);

#endif
