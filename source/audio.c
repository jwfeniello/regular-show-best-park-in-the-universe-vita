/* Effect cache misses decode on a separate worker: they must never delay the
   thread feeding music/PCM to the device. The mixer owns cache entries, voices
   and pending plays; the decoder publishes immutable PCM through load jobs. */
#include "audio.h"
#include "assets.h"
#include "utils/logger.h"
#include "utils/dialog.h"
#include <psp2/audioout.h>
#include <psp2/kernel/threadmgr.h>
#include <sndfile.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdatomic.h>
#define FRAMES 1024
#define CHANNELS 16
#define SAMPLE_COUNT 96
#define LOAD_COUNT 32
#define PENDING_COUNT 32
#define CACHE_LIMIT (16*1024*1024)
typedef struct {unsigned char *data;sf_count_t size,pos;} Memory;
static sf_count_t mlength(void *v){return ((Memory *)v)->size;}
static sf_count_t mseek(sf_count_t off,int whence,void *v){Memory *m=v;sf_count_t p=(whence==SEEK_SET?0:whence==SEEK_CUR?m->pos:m->size)+off;if(p<0 || p>m->size)return -1;return m->pos=p;}
static sf_count_t mread(void *out,sf_count_t bytes,void *v){Memory *m=v;if(bytes>m->size-m->pos)bytes=m->size-m->pos;memcpy(out,m->data+m->pos,bytes);m->pos+=bytes;return bytes;}
static sf_count_t mtell(void *v){return ((Memory *)v)->pos;}
static SF_VIRTUAL_IO vio={mlength,mseek,mread,NULL,mtell};
typedef struct {int op,id;bool loop;float gain;char path[256];} Command;
static Command queue[256];static unsigned head,tail;
static SceKernelLwMutexWork lock;
static atomic_int next_id=1,music_playing;
static _Atomic(float) music_volume=1,effects_volume=1;
typedef struct {char path[256];int16_t *pcm;unsigned frames,used;int refs;} Sample;
static Sample samples[SAMPLE_COUNT];static unsigned use_clock,cache_bytes;
typedef struct {Sample *sample;double pos,step;float gain;int id;bool loop,paused;} Voice;
static Voice voices[CHANNELS];
enum { LOAD_FREE,LOAD_QUEUED,LOAD_WORKING,LOAD_READY };
typedef struct {
    atomic_int state,urgent;
    char path[256];
    int16_t *pcm;
    unsigned frames;
} Load;
static Load loads[LOAD_COUNT];
static SceUID decode_wakeup;
typedef struct {bool active,paused;Command command;} Pending;
static Pending pending[PENDING_COUNT];
static Memory music_mem;static SNDFILE *music;static bool music_loop,paused;
static short music_buf[2048*2];static unsigned music_frames,music_cursor,music_phase;
static void close_music(void){if(music)sf_close(music);music=NULL;free(music_mem.data);memset(&music_mem,0,sizeof(music_mem));music_playing=0;music_frames=music_cursor=music_phase=0;}
static void stop_voice(Voice *v){if(v->sample)--v->sample->refs;memset(v,0,sizeof(*v));}
static SNDFILE *open_sound(const char *path,Memory *mem,SF_INFO *info){size_t size=0;mem->data=park_asset_read(path,&size);mem->size=size;mem->pos=0;if(!mem->data)return NULL;SNDFILE *s=sf_open_virtual(&vio,SFM_READ,info,mem);if(!s || info->samplerate!=44100 || info->channels!=2){if(s)sf_close(s);free(mem->data);memset(mem,0,sizeof(*mem));l_warn("Unsupported audio file %s",path);return NULL;}return s;}
static Sample *find_sample(const char *path) {
    for(unsigned i=0;i<SAMPLE_COUNT;i++)if(samples[i].pcm && !strcmp(samples[i].path,path)){
        samples[i].used=++use_clock;return &samples[i];
    }
    return NULL;
}
static void request_sample(const char *path,bool urgent) {
    int empty=-1;
    for(unsigned i=0;i<LOAD_COUNT;i++) {
        int state=atomic_load_explicit(&loads[i].state,memory_order_acquire);
        if(state==LOAD_FREE) {if(empty<0)empty=i;continue;}
        if(!strcmp(loads[i].path,path)) {
            if(urgent)atomic_store(&loads[i].urgent,1);
            return; /* Share one decode across repeated plays and preloads. */
        }
    }
    if(empty<0)return; /* Pending plays retry after an outstanding job completes. */
    Load *job=&loads[empty];snprintf(job->path,sizeof(job->path),"%s",path);
    job->pcm=NULL;job->frames=0;atomic_store(&job->urgent,urgent);
    atomic_store_explicit(&job->state,LOAD_QUEUED,memory_order_release);
    sceKernelSignalSema(decode_wakeup,1);
}
/* Only the decode worker calls this; no cache/voice pointers cross threads. */
static void decode_sample(Load *job) {
    Memory mem={0};SF_INFO info={0};SNDFILE *f=open_sound(job->path,&mem,&info);
    if(f) {
        if(info.frames>0 && info.frames<=44100*30) {
            job->pcm=malloc((size_t)info.frames*4);
            if(job->pcm) {
                sf_count_t frames=sf_readf_short(f,job->pcm,info.frames);
                if(frames>0)job->frames=(unsigned)frames;
                else {free(job->pcm);job->pcm=NULL;}
            }
        }
        sf_close(f);free(mem.data);
    }
}
static int decoder(SceSize argc,void *arg) {
    (void)argc;(void)arg;unsigned cursor=0;
    for(;;) {
        Load *job=NULL;bool unconsumed=false;
        for(unsigned n=0;n<LOAD_COUNT;n++) {
            Load *candidate=&loads[(cursor+n)%LOAD_COUNT];
            int state=atomic_load_explicit(&candidate->state,memory_order_acquire);
            if(state==LOAD_READY)unconsumed=true;
            if(state==LOAD_QUEUED && (!job || atomic_load(&candidate->urgent))) {
                job=candidate;
                if(atomic_load(&candidate->urgent))break;
            }
        }
        /* At most one decoded result waits for the mixer, bounding extra PCM. */
        if(unconsumed || !job) {sceKernelWaitSema(decode_wakeup,1,NULL);continue;}
        atomic_store(&job->state,LOAD_WORKING);
        decode_sample(job);
        cursor=(unsigned)(job-loads+1)%LOAD_COUNT;
        atomic_store_explicit(&job->state,LOAD_READY,memory_order_release);
        /* Wait for consumption before even looking for another job. QUEUED
         * also means consumed: the mixer may already have reused this slot. */
        while(atomic_load_explicit(&job->state,memory_order_acquire)==LOAD_READY)
            sceKernelWaitSema(decode_wakeup,1,NULL);
    }
    return 0;
}
static int oldest_sample(void) {
    int old=-1;
    for(unsigned i=0;i<SAMPLE_COUNT;i++)
        if(samples[i].pcm && !samples[i].refs && (old<0 || samples[i].used<samples[old].used))old=i;
    return old;
}
static void evict_sample(unsigned slot) {
    Sample *s=&samples[slot];cache_bytes-=s->frames*4;free(s->pcm);memset(s,0,sizeof(*s));
}
static Sample *cache_result(Load *job) {
    if(!job->pcm || !job->frames)return NULL;
    unsigned bytes=job->frames*4;
    while(cache_bytes+bytes>CACHE_LIMIT) {
        int old=oldest_sample();if(old<0)return NULL;evict_sample(old);
    }
    int slot=-1;
    for(unsigned i=0;i<SAMPLE_COUNT;i++)if(!samples[i].pcm){slot=i;break;}
    if(slot<0){slot=oldest_sample();if(slot<0)return NULL;evict_sample(slot);}
    Sample *s=&samples[slot];s->pcm=job->pcm;job->pcm=NULL;s->frames=job->frames;
    s->used=++use_clock;snprintf(s->path,sizeof(s->path),"%s",job->path);cache_bytes+=bytes;
    return s;
}
static void start_voice(Sample *s,const Command *c,bool is_paused) {
    unsigned slot=0;for(unsigned i=0;i<CHANNELS;i++)if(!voices[i].sample){slot=i;break;}
    stop_voice(&voices[slot]);voices[slot]=(Voice){s,0,44100.0/48000.0,c->gain,c->id,c->loop,is_paused};s->refs++;
}
static void finish_loads(void) {
    for(unsigned i=0;i<LOAD_COUNT;i++) {
        Load *job=&loads[i];
        if(atomic_load_explicit(&job->state,memory_order_acquire)!=LOAD_READY)continue;
        Sample *s=cache_result(job);
        for(unsigned p=0;p<PENDING_COUNT;p++)if(pending[p].active && !strcmp(pending[p].command.path,job->path)) {
            if(s)start_voice(s,&pending[p].command,pending[p].paused);
            pending[p].active=false;
        }
        free(job->pcm);job->pcm=NULL;
        atomic_store_explicit(&job->state,LOAD_FREE,memory_order_release);
        sceKernelSignalSema(decode_wakeup,1);
    }
    for(unsigned p=0;p<PENDING_COUNT;p++)if(pending[p].active)request_sample(pending[p].command.path,true);
}
static void execute(const Command *c) {
    switch(c->op){
    case AUDIO_MUSIC:{close_music();SF_INFO info={0};music=open_sound(c->path,&music_mem,&info);music_loop=c->loop;paused=false;music_playing=music!=NULL;l_info("Music: %s opened=%u",c->path,music!=NULL);break;}
    case AUDIO_MUSIC_STOP:close_music();break;
    case AUDIO_MUSIC_PAUSE:paused=true;break;
    case AUDIO_MUSIC_RESUME:paused=false;break;
    case AUDIO_REWIND:if(music){sf_seek(music,0,SEEK_SET);music_frames=music_cursor=music_phase=0;}break;
    case AUDIO_PRELOAD:if(!find_sample(c->path))request_sample(c->path,false);break;
    case AUDIO_EFFECT:{
        Sample *s=find_sample(c->path);if(s){start_voice(s,c,false);break;}
        for(unsigned i=0;i<PENDING_COUNT;i++)if(!pending[i].active) {
            pending[i]=(Pending){.active=true,.command=*c};request_sample(c->path,true);break;
        }
        break;
    }
    default:
        for(unsigned i=0;i<CHANNELS;i++){Voice *v=&voices[i];if(!v->sample)continue;bool all=c->op>=AUDIO_STOP_ALL;if(!all && v->id!=c->id)continue;if(c->op==AUDIO_STOP || c->op==AUDIO_STOP_ALL)stop_voice(v);else if(c->op==AUDIO_PAUSE || c->op==AUDIO_PAUSE_ALL)v->paused=true;else if(c->op==AUDIO_RESUME || c->op==AUDIO_RESUME_ALL)v->paused=false;}
        /* A stop/pause arriving during decoding must also affect the later
         * completion; otherwise a stopped sound can unexpectedly start. */
        for(unsigned i=0;i<PENDING_COUNT;i++){Pending *p=&pending[i];if(!p->active)continue;bool all=c->op>=AUDIO_STOP_ALL;if(!all && p->command.id!=c->id)continue;if(c->op==AUDIO_STOP || c->op==AUDIO_STOP_ALL)p->active=false;else if(c->op==AUDIO_PAUSE || c->op==AUDIO_PAUSE_ALL)p->paused=true;else if(c->op==AUDIO_RESUME || c->op==AUDIO_RESUME_ALL)p->paused=false;}
        break;
    }
}
static bool music_frame(int *left,int *right) {
    if(!music || paused)return false;
    if(music_cursor>=music_frames){music_frames=sf_readf_short(music,music_buf,2048);music_cursor=0;if(!music_frames && music_loop){sf_seek(music,0,SEEK_SET);music_frames=sf_readf_short(music,music_buf,2048);}if(!music_frames){close_music();return false;}}
    *left=music_buf[music_cursor*2];*right=music_buf[music_cursor*2+1];music_phase+=44100;if(music_phase>=48000){music_phase-=48000;music_cursor++;}return true;
}
static int worker(SceSize argc,void *arg) {
    (void)argc;(void)arg;int port=sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN,FRAMES,48000,SCE_AUDIO_OUT_MODE_STEREO);
    if(port<0){l_error("Audio port failed %08x",port);return 0;}int vol[2]={SCE_AUDIO_VOLUME_0DB,SCE_AUDIO_VOLUME_0DB};sceAudioOutSetVolume(port,3,vol);
    short out[2][FRAMES*2] __attribute__((aligned(64)));unsigned slot=0;
    for(;;){
        /* Bounded command batches keep a preload burst from starving output. */
        for(unsigned i=0;i<8;i++){Command c;bool have;sceKernelLockLwMutex(&lock,1,NULL);have=head!=tail;if(have){c=queue[tail];tail=(tail+1)%256;}sceKernelUnlockLwMutex(&lock,1);if(!have)break;execute(&c);}
        finish_loads();
        float mv=music_volume,sv=effects_volume;
        for(unsigned frame=0;frame<FRAMES;frame++){int left=0,right=0;music_frame(&left,&right);float l=left*mv,r=right*mv;
            for(unsigned i=0;i<CHANNELS;i++){Voice *v=&voices[i];if(!v->sample || v->paused)continue;Sample *s=v->sample;unsigned at=(unsigned)v->pos;if(at>=s->frames){if(v->loop && s->frames){v->pos=fmod(v->pos,s->frames);at=(unsigned)v->pos;}else{stop_voice(v);continue;}}l+=s->pcm[at*2]*sv*v->gain;r+=s->pcm[at*2+1]*sv*v->gain;v->pos+=v->step;}
            out[slot][frame*2]=l>32767?32767:l<-32768?-32768:(short)l;out[slot][frame*2+1]=r>32767?32767:r<-32768?-32768:(short)r;
        }
        if(sceAudioOutOutput(port,out[slot])<0)break;slot^=1;
    }
    sceAudioOutReleasePort(port);return 0;
}
void park_audio_init(void) {
    if(sceKernelCreateLwMutex(&lock,"park_audio",0,0,NULL)<0)fatal_error("Audio mutex failed");
    decode_wakeup=sceKernelCreateSema("park_decode_wakeup",0,0,1,NULL);
    if(decode_wakeup<0)fatal_error("Audio decoder signal failed");
    SceUID decode_thread=sceKernelCreateThread("park_decode",decoder,0x10000100,256*1024,0,0,NULL);
    if(decode_thread<0 || sceKernelStartThread(decode_thread,0,NULL)<0)fatal_error("Audio decoder failed");
    SceUID thread=sceKernelCreateThread("park_audio",worker,0x40,256*1024,0,0,NULL);
    if(thread<0 || sceKernelStartThread(thread,0,NULL)<0)fatal_error("Audio worker failed");
}
void park_audio_command(int op,const char *path,int id,bool loop,float gain) {
    Command c={.op=op,.id=id,.loop=loop,.gain=gain};if(path)snprintf(c.path,sizeof(c.path),"%s",path);
    sceKernelLockLwMutex(&lock,1,NULL);unsigned next=(head+1)%256;
    if(next!=tail){queue[head]=c;head=next;}else l_warn("Audio command queue full");
    sceKernelUnlockLwMutex(&lock,1);
}
int park_audio_effect(const char *path,bool loop,float gain) {int id=atomic_fetch_add(&next_id,1);gain=fmaxf(0,fminf(gain,1));park_audio_command(AUDIO_EFFECT,path,id,loop,gain);return id;}
void park_audio_volume(bool music,float value) {value=fmaxf(0,fminf(value,1));if(music)music_volume=value;else effects_volume=value;}
float park_audio_get_volume(bool music){return music?music_volume:effects_volume;}
bool park_audio_playing(void){return music_playing!=0;}
