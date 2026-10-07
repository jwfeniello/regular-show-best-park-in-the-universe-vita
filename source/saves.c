/* Only this worker owns the preference files. Game-thread settings remain in
 * memory; immutable snapshots make disk latency independent of frame timing. */
#include "saves.h"
#include "utils/dialog.h"
#include <psp2/kernel/threadmgr.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

typedef struct {void *data;size_t size;} Snapshot;
static pthread_mutex_t save_lock=PTHREAD_MUTEX_INITIALIZER;
static Snapshot pending_save;
static bool saving,initialized;
static SceUID wakeup;

static bool write_snapshot(const Snapshot *save) {
    FILE *f=fopen(DATA_PATH "saves/preferences.tmp","wb");
    if(!f)return false;
    bool ok=fwrite(save->data,1,save->size,f)==save->size;
    if(fclose(f))ok=false;
    if(!ok)return false;
    if(remove(DATA_PATH "saves/preferences.previous") && errno!=ENOENT)return false;
    bool had_original=rename(DATA_PATH "saves/preferences.bin",DATA_PATH "saves/preferences.previous")==0;
    if(!had_original && errno!=ENOENT)return false;
    if(rename(DATA_PATH "saves/preferences.tmp",DATA_PATH "saves/preferences.bin")==0)return true;
    if(had_original)rename(DATA_PATH "saves/preferences.previous",DATA_PATH "saves/preferences.bin");
    return false;
}

static int save_worker(SceSize argc,void *arg) {
    (void)argc;(void)arg;
    for(;;) {
        pthread_mutex_lock(&save_lock);
        Snapshot save=pending_save;pending_save=(Snapshot){0};saving=save.data!=NULL;
        pthread_mutex_unlock(&save_lock);
        if(!save.data){sceKernelWaitSema(wakeup,1,NULL);continue;}
        bool ok=write_snapshot(&save);
        pthread_mutex_lock(&save_lock);
        /* Keep a failed snapshot for retry unless a newer one supersedes it. */
        if(!ok && !pending_save.data){pending_save=save;save=(Snapshot){0};}
        saving=false;
        pthread_mutex_unlock(&save_lock);
        free(save.data);
        if(!ok)sceKernelDelayThread(1000000);
    }
    return 0;
}

void park_saves_init(void) {
    wakeup=sceKernelCreateSema("park_save_wakeup",0,0,1,NULL);
    if(wakeup<0)fatal_error("Save signal failed");
    SceUID thread=sceKernelCreateThread("park_save",save_worker,0x10000100,64*1024,0,0,NULL);
    if(thread<0 || sceKernelStartThread(thread,0,NULL)<0)fatal_error("Save worker failed");
    initialized=true;
}

void park_saves_submit(void *data,size_t size) {
    pthread_mutex_lock(&save_lock);
    Snapshot old=pending_save;pending_save=(Snapshot){data,size};
    pthread_mutex_unlock(&save_lock);
    free(old.data);
    sceKernelSignalSema(wakeup,1);
}

bool park_saves_flush(void) {
    if(!initialized)return true;
    /* An explicit application exit may wait for saves; gameplay never does. */
    for(unsigned i=0;i<2000;i++) {
        pthread_mutex_lock(&save_lock);
        bool done=!saving && !pending_save.data;
        pthread_mutex_unlock(&save_lock);
        if(done)return true;
        sceKernelDelayThread(1000);
    }
    return false;
}
