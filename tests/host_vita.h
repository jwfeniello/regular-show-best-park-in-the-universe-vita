#pragma once
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <time.h>
typedef int SceUID;
typedef unsigned SceSize;
typedef pthread_mutex_t SceKernelLwMutexWork;
static _Thread_local int host_role;
static uint64_t host_us(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000+t.tv_nsec/1000;}
typedef struct {pthread_mutex_t mutex;pthread_cond_t cond;int count,max;} HostSema;
static HostSema host_semas[16];static int host_sema_count;
static int sceKernelCreateSema(const char *name,int attr,int initial,int max,void *opt){(void)name;(void)attr;(void)opt;int id=++host_sema_count;assert(id<16);HostSema *s=&host_semas[id];pthread_mutex_init(&s->mutex,NULL);pthread_cond_init(&s->cond,NULL);s->count=initial;s->max=max;return id;}
static int sceKernelSignalSema(int id,int count){HostSema *s=&host_semas[id];pthread_mutex_lock(&s->mutex);int rc=0;if(s->count+count>s->max)rc=-1;else{s->count+=count;pthread_cond_signal(&s->cond);}pthread_mutex_unlock(&s->mutex);return rc;}
static int sceKernelWaitSema(int id,int count,void *timeout){(void)timeout;HostSema *s=&host_semas[id];pthread_mutex_lock(&s->mutex);while(s->count<count)pthread_cond_wait(&s->cond,&s->mutex);s->count-=count;pthread_mutex_unlock(&s->mutex);return 0;}
static int sceKernelCreateLwMutex(SceKernelLwMutexWork *m,const char *name,int a,int b,void *opt){(void)name;(void)a;(void)b;(void)opt;return pthread_mutex_init(m,NULL);}
static int sceKernelLockLwMutex(SceKernelLwMutexWork *m,int count,void *opt){(void)count;(void)opt;return pthread_mutex_lock(m);}
static int sceKernelUnlockLwMutex(SceKernelLwMutexWork *m,int count){(void)count;return pthread_mutex_unlock(m);}
static int sceKernelDelayThread(unsigned us){usleep(us);return 0;}
typedef struct {int (*entry)(SceSize,void *);int role;pthread_t thread;} HostThread;
static HostThread host_threads[8];static int host_thread_count;
static void *host_start(void *arg){HostThread *t=arg;host_role=t->role;t->entry(0,NULL);return NULL;}
static int sceKernelCreateThread(const char *name,int (*entry)(SceSize,void *),int priority,unsigned stack,int attr,int cpu,void *opt){(void)stack;(void)attr;(void)cpu;(void)opt;int id=++host_thread_count;assert(id<8);host_threads[id].entry=entry;host_threads[id].role=!strcmp(name,"park_audio")?1:!strcmp(name,"park_decode")?2:3;if(host_threads[id].role==1)assert(priority==0x40);return id;}
static int sceKernelStartThread(int id,SceSize size,void *arg){(void)size;(void)arg;int rc=pthread_create(&host_threads[id].thread,NULL,host_start,&host_threads[id]);pthread_detach(host_threads[id].thread);return rc;}
#define SCE_AUDIO_OUT_PORT_TYPE_MAIN 0
#define SCE_AUDIO_OUT_MODE_STEREO 0
#define SCE_AUDIO_VOLUME_0DB 32768
static int sceAudioOutOpenPort(int type,int frames,int rate,int mode){(void)type;(void)mode;assert(frames==1024 && rate==48000);return 1;}
static int sceAudioOutSetVolume(int port,int mask,int *v){(void)port;(void)mask;(void)v;return 0;}
static int sceAudioOutReleasePort(int port){(void)port;return 0;}
static int sceAudioOutOutput(int port,const void *data);
