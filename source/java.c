#include "park.h"
#include "java_refs.h"
#include "audio.h"
#include "text.h"
#include "saves.h"
#include "utils/logger.h"
#include "utils/dialog.h"
#include <falso_jni/FalsoJNI_Impl.h>
#include <psp2/kernel/processmgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
typedef struct {char name[100],sig[200];} Method;
static Method methods[192];static unsigned method_count;
static pthread_mutex_t method_lock=PTHREAD_MUTEX_INITIALIZER;
typedef struct {char *key,*value;} Setting;
static Setting settings[512];static unsigned setting_count;static bool dirty;
static const char *str(jstring s){return s?(*jni).GetStringUTFChars(&jni,s,NULL):"";}
static void unstr(jstring s,const char *p){if(s)(*jni).ReleaseStringUTFChars(&jni,s,(char *)p);}
static const char *get(const char *key){for(unsigned i=0;i<setting_count;i++)if(!strcmp(settings[i].key,key))return settings[i].value;return NULL;}
static void put(const char *key,const char *value){
    if(strlen(key)>255 || strlen(value)>131072)return;
    unsigned i=0;for(;i<setting_count;i++)if(!strcmp(settings[i].key,key))break;
    if(i<setting_count && !strcmp(settings[i].value,value))return;
    char *v=strdup(value);if(!v)return;
    if(i==setting_count){if(setting_count==512){free(v);return;}char *k=strdup(key);if(!k){free(v);return;}settings[i].key=k;settings[i].value=NULL;setting_count++;}
    free(settings[i].value);settings[i].value=v;dirty=true;
}
static void load_settings(void){
    FILE *f=fopen(DATA_PATH "saves/preferences.bin","rb");if(!f)return;
    unsigned h[2];if(fread(h,1,8,f)!=8 || h[0]!=0x31535042 || h[1]>512){fclose(f);return;}
    for(unsigned i=0;i<h[1];i++){
        unsigned lengths[2];if(fread(lengths,1,8,f)!=8 || lengths[0]>255 || lengths[1]>131072)break;
        char *k=calloc(1,lengths[0]+1),*v=calloc(1,lengths[1]+1);
        if(!k || !v){free(k);free(v);break;}
        if(fread(k,1,lengths[0],f)!=lengths[0] || fread(v,1,lengths[1],f)!=lengths[1]){free(k);free(v);break;}
        put(k,v);free(k);free(v);
    }
    fclose(f);dirty=false;
}
static void submit_settings(void){
    if(!dirty)return;
    size_t bytes=8;
    for(unsigned i=0;i<setting_count;i++)bytes+=8+strlen(settings[i].key)+strlen(settings[i].value);
    unsigned char *snapshot=malloc(bytes);if(!snapshot)return;
    unsigned h[2]={0x31535042,setting_count};memcpy(snapshot,h,8);size_t at=8;
    for(unsigned i=0;i<setting_count;i++){
        unsigned sizes[2]={strlen(settings[i].key),strlen(settings[i].value)};
        memcpy(snapshot+at,sizes,8);at+=8;
        memcpy(snapshot+at,settings[i].key,sizes[0]);at+=sizes[0];
        memcpy(snapshot+at,settings[i].value,sizes[1]);at+=sizes[1];
    }
    park_saves_submit(snapshot,bytes);dirty=false;
}
void park_java_poll(void){
    static uint64_t last;uint64_t now=sceKernelGetProcessTimeWide();
    if(!dirty || now-last<1000000)return;last=now;submit_settings();
}
void park_java_flush(void){submit_settings();park_saves_flush();}
static jmethodID method_id(JNIEnv *env,jclass cls,const char *name,const char *sig){
    (void)env;(void)cls;pthread_mutex_lock(&method_lock);
    for(unsigned i=0;i<method_count;i++)if(!strcmp(name,methods[i].name) && !strcmp(sig,methods[i].sig)){pthread_mutex_unlock(&method_lock);return (jmethodID)&methods[i];}
    if(method_count>=192){pthread_mutex_unlock(&method_lock);fatal_error("JNI method table exhausted");}
    Method *m=&methods[method_count++];snprintf(m->name,sizeof(m->name),"%s",name);snprintf(m->sig,sizeof(m->sig),"%s",sig);pthread_mutex_unlock(&method_lock);
    l_info("JNI method: %s %s",name,sig);return (jmethodID)m;
}
#define IS(s) (!strcmp(m->name,s))
static jobject object_v(JNIEnv *env,jobject obj,jmethodID id,va_list a){
    (void)obj;Method *m=(Method *)id;if(!m)return NULL;
    const char *value="";
    if(IS("getAPKExpansionFileName"))value=DATA_PATH "main.16.com.turner.bestparkintheuniverse.obb";
    else if(IS("getCocos2dxWritablePath"))value=DATA_PATH "saves";
    else if(IS("getCocos2dxPackageName"))value="com.turner.bestparkintheuniverse";
    else if(IS("getCurrentLanguage"))value="en";
    else if(IS("getDeviceModel"))value="PlayStation Vita";
    else if(IS("getStringForKey") || IS("getItem")){
        jstring key=va_arg(a,jstring);const char *k=str(key),*v=get(k);unstr(key,k);
        if(v)return (*jni).NewStringUTF(env,v);
        if(IS("getStringForKey")){jstring def=va_arg(a,jstring);return (*jni).NewLocalRef(env,def);}
        return NULL;
    }else if(IS("getStringWithEllipsis")){jstring original=va_arg(a,jstring);return (*jni).NewLocalRef(env,original);}
    else if(IS("getAssetManager"))return (jobject)1;
    else l_warn("Unavailable Java object method: %s",m->name);
    return (*jni).NewStringUTF(env,value);
}
static jboolean boolean_v(JNIEnv *env,jobject obj,jmethodID id,va_list a){
    (void)env;(void)obj;Method *m=(Method *)id;if(!m)return false;
    if(IS("getBoolForKey")){jstring key=va_arg(a,jstring);int def=va_arg(a,int);const char *k=str(key),*v=get(k);bool result=v?atoi(v)!=0:def;unstr(key,k);return result;}
    if(IS("isBackgroundMusicPlaying"))return park_audio_playing();
    if(IS("init"))return true; /* Local storage is available. */
    return false; /* No tablet UI, Kindle mode, sign-in or online services. */
}
static jint int_v(JNIEnv *env,jobject obj,jmethodID id,va_list a){
    (void)env;(void)obj;Method *m=(Method *)id;if(!m)return 0;
    if(IS("getDPI"))return 220;
    if(IS("getIntegerForKey")){jstring key=va_arg(a,jstring);int def=va_arg(a,int);const char *k=str(key),*v=get(k);int result=v?atoi(v):def;unstr(key,k);return result;}
    if(IS("getFontSizeAccordingHeight"))return va_arg(a,int);
    if(IS("playEffect")){jstring path=va_arg(a,jstring);int loop=va_arg(a,int);float gain=va_arg(a,double);const char *s=str(path);int sound=park_audio_effect(s,loop,gain);unstr(path,s);return sound;}
    return 0;
}
static jfloat float_v(JNIEnv *env,jobject obj,jmethodID id,va_list a){
    (void)env;(void)obj;Method *m=(Method *)id;if(!m)return 0;
    if(IS("getBackgroundMusicVolume"))return park_audio_get_volume(true);
    if(IS("getEffectsVolume"))return park_audio_get_volume(false);
    if(IS("getFloatForKey")){jstring key=va_arg(a,jstring);double def=va_arg(a,double);const char *k=str(key),*v=get(k);float result=v?strtod(v,NULL):def;unstr(key,k);return result;}
    return 0;
}
static jdouble double_v(JNIEnv *env,jobject obj,jmethodID id,va_list a){
    (void)env;(void)obj;Method *m=(Method *)id;if(!m)return 0;
    if(IS("getDoubleForKey")){jstring key=va_arg(a,jstring);double def=va_arg(a,double);const char *k=str(key),*v=get(k);double result=v?strtod(v,NULL):def;unstr(key,k);return result;}
    return 0;
}
static void void_v(JNIEnv *env,jobject obj,jmethodID id,va_list a){
    (void)env;(void)obj;Method *m=(Method *)id;if(!m)return;
    if(IS("createTextBitmap")){
        jstring text=va_arg(a,jstring),font=va_arg(a,jstring);int size=va_arg(a,int),align=va_arg(a,int),width=va_arg(a,int),height=va_arg(a,int);
        const char *t=str(text),*f=str(font);park_text_bitmap(t,f,size,align,width,height);unstr(text,t);unstr(font,f);return;
    }
    if(IS("setBoolForKey") || IS("setIntegerForKey") || IS("setFloatForKey") || IS("setDoubleForKey") || IS("setStringForKey") || IS("setItem")){
        jstring key=va_arg(a,jstring);const char *k=str(key);char buf[64];
        if(IS("setStringForKey") || IS("setItem")){jstring value=va_arg(a,jstring);const char *v=str(value);put(k,v);unstr(value,v);}
        else {if(IS("setBoolForKey") || IS("setIntegerForKey"))snprintf(buf,sizeof(buf),"%d",va_arg(a,int));else snprintf(buf,sizeof(buf),"%.17g",va_arg(a,double));put(k,buf);}
        unstr(key,k);return;
    }
    if(IS("playBackgroundMusic") || IS("preloadEffect")){
        jstring path=va_arg(a,jstring);bool loop=IS("playBackgroundMusic")?va_arg(a,int):false;const char *s=str(path);
        park_audio_command(IS("playBackgroundMusic")?AUDIO_MUSIC:AUDIO_PRELOAD,s,0,loop,1);unstr(path,s);return;
    }
    if(IS("setBackgroundMusicVolume") || IS("setEffectsVolume")){park_audio_volume(IS("setBackgroundMusicVolume"),va_arg(a,double));return;}
    int op=0,sound=0;
    if(IS("stopBackgroundMusic"))op=AUDIO_MUSIC_STOP;else if(IS("pauseBackgroundMusic"))op=AUDIO_MUSIC_PAUSE;
    else if(IS("resumeBackgroundMusic"))op=AUDIO_MUSIC_RESUME;else if(IS("rewindBackgroundMusic"))op=AUDIO_REWIND;
    else if(IS("stopAllEffects"))op=AUDIO_STOP_ALL;else if(IS("pauseAllEffects"))op=AUDIO_PAUSE_ALL;else if(IS("resumeAllEffects"))op=AUDIO_RESUME_ALL;
    else if(IS("stopEffect")){op=AUDIO_STOP;sound=va_arg(a,int);}else if(IS("pauseEffect")){op=AUDIO_PAUSE;sound=va_arg(a,int);}else if(IS("resumeEffect")){op=AUDIO_RESUME;sound=va_arg(a,int);}
    if(op){park_audio_command(op,NULL,sound,false,1);return;}
    if(IS("terminateProcess")){park_java_flush();sceKernelExitProcess(0);}
    /* Preload/unload music/effects are cache hints. External UI, telemetry,
       achievements, sign-in and accelerometer requests remain unavailable. */
}
#define CALL(TYPE,NAME) static TYPE NAME##_call(JNIEnv *e,jobject o,jmethodID m,...) {va_list a;va_start(a,m);TYPE r=NAME##_v(e,o,m,a);va_end(a);return r;}
CALL(jobject,object) CALL(jboolean,boolean) CALL(jint,int) CALL(jfloat,float) CALL(jdouble,double)
static void void_call(JNIEnv *e,jobject o,jmethodID m,...){va_list a;va_start(a,m);void_v(e,o,m,a);va_end(a);}
jobject park_renderer(void){return NULL;}
void park_java_init(void){
    park_refs_install();load_settings();park_saves_init();struct JNINativeInterface *t=(void *)jni;
    t->GetMethodID=method_id;t->GetStaticMethodID=method_id;
#define INSTALL(T,N) t->Call##T##Method=N##_call;t->Call##T##MethodV=N##_v;t->CallStatic##T##Method=N##_call;t->CallStatic##T##MethodV=N##_v;
    INSTALL(Object,object) INSTALL(Boolean,boolean) INSTALL(Int,int) INSTALL(Float,float) INSTALL(Double,double) INSTALL(Void,void)
}
NameToMethodID nameToMethodId[]={};NameToFieldID nameToFieldId[]={};
#define EMPTY(TYPE,NAME) TYPE NAME[]={};
EMPTY(MethodsBoolean,methodsBoolean) EMPTY(MethodsByte,methodsByte) EMPTY(MethodsChar,methodsChar) EMPTY(MethodsDouble,methodsDouble)
EMPTY(MethodsFloat,methodsFloat) EMPTY(MethodsInt,methodsInt) EMPTY(MethodsLong,methodsLong) EMPTY(MethodsObject,methodsObject) EMPTY(MethodsShort,methodsShort) EMPTY(MethodsVoid,methodsVoid)
EMPTY(FieldsBoolean,fieldsBoolean) EMPTY(FieldsByte,fieldsByte) EMPTY(FieldsChar,fieldsChar) EMPTY(FieldsDouble,fieldsDouble) EMPTY(FieldsFloat,fieldsFloat)
EMPTY(FieldsInt,fieldsInt) EMPTY(FieldsLong,fieldsLong) EMPTY(FieldsObject,fieldsObject) EMPTY(FieldsShort,fieldsShort)
__FALSOJNI_IMPL_CONTAINER_SIZES
