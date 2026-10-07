#include "java_refs.h"
#include "utils/dialog.h"
#include "utils/logger.h"
#include <falso_jni/FalsoJNI_ImplBridge.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

typedef struct Ref {
    void *object;
    enum ParkRefKind kind;
    const char *class_name;
    unsigned local,global;
    void (*destroy)(void *);
    struct Ref *next;
} Ref;
static Ref *refs;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static struct JNINativeInterface original;

jobject park_ref_track(void *object,enum ParkRefKind kind,const char *cls,void (*destroy)(void *)) {
    if(!object) return NULL;
    Ref *r=calloc(1,sizeof(*r));
    if(!r) fatal_error("Out of memory tracking Java object.");
    *r=(Ref){object,kind,cls,1,0,destroy,NULL};
    pthread_mutex_lock(&lock);r->next=refs;refs=r;pthread_mutex_unlock(&lock);
    return object;
}
static void retain(void *p,bool global) {
    if(!p) return;
    pthread_mutex_lock(&lock);
    for(Ref *r=refs;r;r=r->next) if(r->object==p) {
        if(global) ++r->global; else ++r->local;
        break;
    }
    pthread_mutex_unlock(&lock);
}
void park_ref_keep(jobject p) { retain(p,true); }
static void release(void *p,bool global) {
    if(!p) return;
    Ref *dead=NULL;
    pthread_mutex_lock(&lock);
    for(Ref **link=&refs;*link;link=&(*link)->next) {
        Ref *r=*link;
        if(r->object!=p) continue;
        unsigned *n=global ? &r->global : &r->local;
        if(*n) --*n;
        if(!r->global && !r->local) { *link=r->next;dead=r; }
        break;
    }
    pthread_mutex_unlock(&lock);
    if(!dead) return;
    if(dead->destroy) dead->destroy(p);
    else if(dead->kind==PARK_REF_STRING) {
        JavaString *s=p;jda_free(s->utf8);jda_free(s->utf16);free(s);
    } else if(dead->kind==PARK_REF_ARRAY) jda_free(p);
    else free(p);
    free(dead);
}
const char *park_ref_class(jobject p) {
    const char *cls="java/lang/Object";
    pthread_mutex_lock(&lock);
    for(Ref *r=refs;r;r=r->next) if(r->object==p) {cls=r->class_name;break;}
    pthread_mutex_unlock(&lock);return cls;
}
static jclass find_class(JNIEnv *env,const char *name) {
    (void)env;return park_ref_track(strdup(name),PARK_REF_CLASS,"java/lang/Class",NULL);
}
static jclass object_class(JNIEnv *env,jobject object) { return find_class(env,park_ref_class(object)); }
static jobject global_ref(JNIEnv *env,jobject p) { (void)env;retain(p,true);return p; }
static jobject local_ref(JNIEnv *env,jobject p) { (void)env;retain(p,false);return p; }
static void del_global(JNIEnv *env,jobject p) { (void)env;release(p,true); }
static void del_local(JNIEnv *env,jobject p) { (void)env;release(p,false); }
static jstring string_utf(JNIEnv *env,const char *text) {
    return park_ref_track(original.NewStringUTF(env,text ? text : ""),PARK_REF_STRING,"java/lang/String",NULL);
}
static jstring string_utf16(JNIEnv *env,const jchar *text,jsize len) {
    return park_ref_track(original.NewString(env,text,len),PARK_REF_STRING,"java/lang/String",NULL);
}
static jbyteArray byte_array(JNIEnv *env,jsize len) {
    if(len<0 || len>32*1024*1024) return NULL;
    return park_ref_track(original.NewByteArray(env,len),PARK_REF_ARRAY,"[B",NULL);
}
static jintArray int_array(JNIEnv *env,jsize len) {
    if(len<0 || len>1048576)return NULL;
    return park_ref_track(original.NewIntArray(env,len),PARK_REF_ARRAY,"[I",NULL);
}
static jfloatArray float_array(JNIEnv *env,jsize len) {
    if(len<0 || len>1048576)return NULL;
    return park_ref_track(original.NewFloatArray(env,len),PARK_REF_ARRAY,"[F",NULL);
}
void park_refs_install(void) {
    struct JNINativeInterface *t=(void *)jni;original=*t;
    t->FindClass=find_class;t->GetObjectClass=object_class;
    t->NewGlobalRef=global_ref;t->NewLocalRef=local_ref;
    t->DeleteGlobalRef=del_global;t->DeleteLocalRef=del_local;
    t->NewStringUTF=string_utf;t->NewString=string_utf16;t->NewByteArray=byte_array;
    t->NewIntArray=int_array;t->NewFloatArray=float_array;
}
