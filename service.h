#ifndef SERVICE_H
#define SERVICE_H

void WINAPI service_main(unsigned long, char **);
unsigned long WINAPI service_control_handler(unsigned long, unsigned long, void *, void *);

SC_HANDLE open_service_manager();
int install_service(char *);
int remove_service(char *);
int monitor_service();
int start_service();
int stop_service(unsigned long);
void CALLBACK end_service(void *, unsigned char);

#endif
