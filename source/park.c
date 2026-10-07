#include "park.h"
#include "gamepad.h"
#include "utils/logger.h"
#include "utils/dialog.h"
#include <so_util/so_util.h>
#include <psp2/io/stat.h>
#include <stdio.h>
extern so_module so_mod;
static void (*render_fn)(JNIEnv *,jobject);
static void (*begin_fn)(JNIEnv *,jobject,int,float,float);
static void (*end_fn)(JNIEnv *,jobject,int,float,float);
static void (*move_fn)(JNIEnv *,jobject,jintArray,jfloatArray,jfloatArray);
static jboolean (*key_fn)(JNIEnv *,jobject,int);
uintptr_t park_symbol(const char *name) {
    uintptr_t p=so_symbol(&so_mod,name);
    if(!p) fatal_error("Missing game function: %s",name);
    return p;
}
void park_check_data(void) {
    SceIoStat st;
    if(sceIoGetstat(SO_PATH,&st)<0 || st.st_size!=5359068)
        fatal_error("Copy the Best Park 1.2.1 ARM library to " SO_PATH);
    if(sceIoGetstat(DATA_PATH "main.16.com.turner.bestparkintheuniverse.obb",&st)<0 || st.st_size!=388494611)
        fatal_error("Missing matching Best Park expansion data in " DATA_PATH);
    sceIoMkdir(DATA_PATH "saves",0777);
}
void park_start(void) {
    void (*set_apk)(JNIEnv *,jobject,jstring)=(void *)park_symbol("Java_org_cocos2dx_lib_Cocos2dxHelper_nativeSetApkPath");
    void (*init)(JNIEnv *,jobject,int,int)=(void *)park_symbol("Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeInit");
    render_fn=(void *)park_symbol("Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeRender");
    begin_fn=(void *)park_symbol("Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeTouchesBegin");
    end_fn=(void *)park_symbol("Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeTouchesEnd");
    move_fn=(void *)park_symbol("Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeTouchesMove");
    key_fn=(void *)park_symbol("Java_org_cocos2dx_lib_Cocos2dxRenderer_nativeKeyDown");
    jstring path=(*jni).NewStringUTF(&jni,DATA_PATH "game.apk");
    set_apk(&jni,NULL,path);(*jni).DeleteLocalRef(&jni,path);
    l_info("Starting Cocos2d-x at 960x544"); init(&jni,NULL,960,544);
    l_info("Native initialization returned");
}
bool park_render(void) { render_fn(&jni,NULL);return true; }
void park_touch(int id,float x,float y,ControlsAction a) {
    if(a==CONTROLS_ACTION_DOWN) begin_fn(&jni,NULL,id,x,y);
    else if(a==CONTROLS_ACTION_UP) end_fn(&jni,NULL,id,x,y);
    else {
        jintArray ids=(*jni).NewIntArray(&jni,1);
        jfloatArray xs=(*jni).NewFloatArray(&jni,1),ys=(*jni).NewFloatArray(&jni,1);
        (*jni).SetIntArrayRegion(&jni,ids,0,1,&id);
        (*jni).SetFloatArrayRegion(&jni,xs,0,1,&x);(*jni).SetFloatArrayRegion(&jni,ys,0,1,&y);
        move_fn(&jni,NULL,ids,xs,ys);
        (*jni).DeleteLocalRef(&jni,ids);(*jni).DeleteLocalRef(&jni,xs);(*jni).DeleteLocalRef(&jni,ys);
    }
}
void park_key(int key,ControlsAction a) {
    park_gamepad_key(key,a);
    if(a==CONTROLS_ACTION_DOWN && (key==AKEYCODE_BUTTON_START || key==AKEYCODE_BACK))key_fn(&jni,NULL,AKEYCODE_BACK);
}
void park_stop(void) {park_java_flush();}
