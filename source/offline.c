#include "offline.h"
#include <errno.h>
#include <stddef.h>
int park_network_unavailable(void) { errno=ENETUNREACH; return -1; }
int park_getaddrinfo(const char *host,const char *service,const void *hints,void **result) {
    (void)host;(void)service;(void)hints;
    if(result) *result=NULL;
    errno=ENETUNREACH;
    return 2; /* Android/bionic EAI_AGAIN. No DNS request is attempted. */
}
void park_freeaddrinfo(void *p) { (void)p; }
unsigned park_alarm(unsigned seconds) { (void)seconds; return 0; }
unsigned park_geteuid(void) { return 0; }
void *park_getpwuid(unsigned uid) { (void)uid; errno=ENOENT; return NULL; }
