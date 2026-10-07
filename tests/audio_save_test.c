#include <stdatomic.h>
#include <errno.h>
#include <stdio.h>
#include "audio.c"

static atomic_int fail_commit,slow_saves,save_started;
static FILE *test_save_open(const char *path,const char *mode){
    if(atomic_load(&slow_saves)){assert(host_role==3);atomic_store(&save_started,1);usleep(250000);}
    return fopen(path,mode);
}
static int test_save_rename(const char *from,const char *to){
    if(strstr(from,"preferences.tmp") && atomic_exchange(&fail_commit,0)){errno=EIO;return -1;}
    return rename(from,to);
}
#define fopen test_save_open
#define rename test_save_rename
#include "saves.c"
#undef fopen
#undef rename

static atomic_int asset_reads,slow_started,output_blocks,nonzero_blocks;
static atomic_ullong max_gap;
void *park_asset_read(const char *path,size_t *size){
    assert(host_role!=1 || !strcmp(path,"music.wav"));
    atomic_fetch_add(&asset_reads,1);
    if(!strcmp(path,"missing.wav")){*size=0;return NULL;}
    if(!strcmp(path,"slow.wav")){atomic_store(&slow_started,1);usleep(250000);}
    FILE *f=fopen(HOST_WAV,"rb");assert(f);fseek(f,0,SEEK_END);*size=ftell(f);rewind(f);
    void *data=malloc(*size);assert(data);assert(fread(data,1,*size,f)==*size);fclose(f);return data;
}
static int sceAudioOutOutput(int port,const void *data){
    (void)port;static uint64_t previous;uint64_t now=host_us();
    if(previous){uint64_t gap=now-previous;if(gap>atomic_load(&max_gap))atomic_store(&max_gap,gap);}previous=now;
    const short *pcm=data;if(pcm[0])atomic_fetch_add(&nonzero_blocks,1);
    atomic_fetch_add(&output_blocks,1);usleep(21333);return 0;
}
static void wait_for(atomic_int *value,int minimum){uint64_t until=host_us()+3000000;while(atomic_load(value)<minimum && host_us()<until)usleep(1000);assert(atomic_load(value)>=minimum);}
static Command effect(const char *path,int id){Command c={.op=AUDIO_EFFECT,.id=id,.gain=1};snprintf(c.path,sizeof(c.path),"%s",path);return c;}
static Load *job_for(const char *path){for(unsigned i=0;i<LOAD_COUNT;i++)if(atomic_load(&loads[i].state)!=LOAD_FREE && !strcmp(loads[i].path,path))return &loads[i];return NULL;}
static Voice *voice_for(int id){for(unsigned i=0;i<CHANNELS;i++)if(voices[i].sample && voices[i].id==id)return &voices[i];return NULL;}
static void complete(Load *job){assert(job);atomic_store(&job->state,LOAD_WORKING);decode_sample(job);atomic_store(&job->state,LOAD_READY);finish_loads();}
static void reset_audio(void){
    for(unsigned i=0;i<CHANNELS;i++)stop_voice(&voices[i]);
    for(unsigned i=0;i<SAMPLE_COUNT;i++)if(samples[i].pcm)evict_sample(i);
    for(unsigned i=0;i<LOAD_COUNT;i++){free(loads[i].pcm);memset(&loads[i],0,sizeof(loads[i]));}
    memset(pending,0,sizeof(pending));assert(cache_bytes==0);
}
static void assert_file(const char *path,const char *expected){char data[64]={0};FILE *f=fopen(path,"rb");assert(f);size_t n=fread(data,1,sizeof(data),f);fclose(f);assert(n==strlen(expected) && !memcmp(data,expected,n));}

int main(void){
    decode_wakeup=sceKernelCreateSema("test",0,0,1,NULL);
    Command a=effect("hit.wav",100),b=effect("hit.wav",101);execute(&a);execute(&b);
    assert(atomic_load(&asset_reads)==0);unsigned jobs=0;
    for(unsigned i=0;i<LOAD_COUNT;i++)jobs+=atomic_load(&loads[i].state)!=LOAD_FREE;
    assert(jobs==1);execute(&(Command){.op=AUDIO_STOP,.id=100});execute(&(Command){.op=AUDIO_PAUSE,.id=101});
    complete(job_for("hit.wav"));assert(!voice_for(100));assert(voice_for(101) && voice_for(101)->paused);
    execute(&(Command){.op=AUDIO_RESUME,.id=101});assert(!voice_for(101)->paused);
    execute(&(Command){.op=AUDIO_STOP_ALL});int reads=atomic_load(&asset_reads);execute(&a);assert(voice_for(100) && atomic_load(&asset_reads)==reads);
    b=effect("missing.wav",102);execute(&b);complete(job_for("missing.wav"));assert(!voice_for(102) && !job_for("missing.wav"));
    b=effect("cancel.wav",103);execute(&b);execute(&(Command){.op=AUDIO_STOP_ALL});complete(job_for("cancel.wav"));assert(!voice_for(103));
    /* Cache pressure must never evict a currently referenced sample. */
    execute(&a);Sample *protected=voice_for(100)->sample;int16_t *protected_pcm=protected->pcm;
    for(unsigned i=0;i<12;i++){Load result={.frames=512*1024};snprintf(result.path,sizeof(result.path),"pressure%u",i);result.pcm=calloc(result.frames,4);assert(cache_result(&result));assert(!result.pcm && cache_bytes<=CACHE_LIMIT);}
    assert(protected->pcm==protected_pcm && protected->refs==1);
    reset_audio();
    /* A full preload queue must not lose a later actual play request. */
    for(unsigned i=0;i<LOAD_COUNT;i++){char path[32];snprintf(path,sizeof(path),"preload%u",i);request_sample(path,false);}
    b=effect("after-full.wav",104);execute(&b);assert(!job_for("after-full.wav"));complete(&loads[0]);assert(job_for("after-full.wav"));complete(job_for("after-full.wav"));assert(voice_for(104));reset_audio();
    puts("PASS: async cache miss, deduplication, stop/pause before completion, cache budget/live references, missing clips, saturated preload queue");

    /* Real mixer/decoder threads with a quarter-second storage stall. */
    park_audio_init();park_audio_command(AUDIO_MUSIC,"music.wav",0,true,1);wait_for(&nonzero_blocks,4);
    park_audio_effect("slow.wav",false,1);wait_for(&slow_started,1);
    int before=atomic_load(&nonzero_blocks);usleep(180000);
    assert(atomic_load(&nonzero_blocks)-before>=5);assert(atomic_load(&max_gap)<150000);
    printf("PASS: music kept playing during 250 ms effect read; largest output gap %.1f ms\n",atomic_load(&max_gap)/1000.0);

    Snapshot initial={"original",8},next={"replacement",11};assert(write_snapshot(&initial));atomic_store(&fail_commit,1);
    assert(!write_snapshot(&next));assert_file(DATA_PATH "saves/preferences.bin","original");
    assert(write_snapshot(&next));assert_file(DATA_PATH "saves/preferences.previous","original");
    puts("PASS: failed preference commit restores original; successful commit retains previous snapshot");

    park_saves_init();atomic_store(&slow_saves,1);park_saves_submit(strdup("first"),5);wait_for(&save_started,1);
    uint64_t start=host_us();park_saves_submit(strdup("superseded"),10);park_saves_submit(strdup("latest"),6);
    assert(host_us()-start<50000);before=atomic_load(&nonzero_blocks);usleep(180000);assert(atomic_load(&nonzero_blocks)-before>=5);
    assert(park_saves_flush());assert_file(DATA_PATH "saves/preferences.bin","latest");assert_file(DATA_PATH "saves/preferences.previous","first");
    /* Retry a failed background commit without losing the submitted data. */
    atomic_store(&slow_saves,0);atomic_store(&fail_commit,1);park_saves_submit(strdup("retry"),5);assert(park_saves_flush());assert_file(DATA_PATH "saves/preferences.bin","retry");
    printf("PASS: nonblocking/coalesced saves, background retry, exit flush; largest audio gap %.1f ms\n",atomic_load(&max_gap)/1000.0);
    return 0;
}
