#ifndef PARK_OFFLINE_H
#define PARK_OFFLINE_H
#include <sys/types.h>
int park_network_unavailable(void);
int park_getaddrinfo(const char *,const char *,const void *,void **);
void park_freeaddrinfo(void *);
unsigned park_alarm(unsigned);
unsigned park_geteuid(void);
void *park_getpwuid(unsigned);
#endif
