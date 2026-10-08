#include "prompts.h"
#include "park.h"
#include "utils/dialog.h"
#include "utils/logger.h"
#include <so_util/so_util.h>
#include <kubridge/kubridge.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

extern so_module so_mod;
static unsigned char *(*read_file)(void *,const char *,const char *,unsigned long *);
static void *(*sprite_file)(const char *), *(*sprite_frame)(void *);
static void *(*frame_cache)(void), *(*find_frame)(void *,const char *);

static unsigned char *read_with_prompts(void *self,const char *path,const char *mode,unsigned long *size) {
    /* Android's reader treats anything without a leading slash as a ZIP entry.
     * Read our bundled images through Vita stdio instead. Cocos may prefix its
     * search directory before this path, so locate the explicit app0 marker. */
    const char *bundled=path?strstr(path,"app0:prompts/"):NULL;
    if (bundled && size) {
        *size=0;
        FILE *f=fopen(bundled,"rb");
        if (!f) return NULL;
        if (fseek(f,0,SEEK_END)) { fclose(f); return NULL; }
        long bytes=ftell(f);
        if (bytes<1 || bytes>1024*1024 || fseek(f,0,SEEK_SET)) { fclose(f); return NULL; }
        unsigned char *data=malloc((size_t)bytes+1);
        if (!data) { fclose(f); return NULL; }
        size_t got=fread(data,1,(size_t)bytes,f); fclose(f);
        if (got!=(size_t)bytes) { free(data); return NULL; }
        data[got]=0; *size=got; return data;
    }
    unsigned char *original=read_file(self,path,mode,size);
    if (!original || !size) return original;
    size_t replacement_size=0;
    unsigned char *replacement=park_prompts_rewrite(path,original,*size,&replacement_size);
    if (!replacement) return original;
    free(original); *size=replacement_size;
    l_info("Vita prompts: %s",path);
    return replacement;
}
static void *sprite_with_prompts(const char *name) {
    const char *path=park_prompt_sprite(name);
    if (path) return sprite_file(path);
    /* Same two calls as the original factory; no temporary code unpatching. */
    return sprite_frame(find_frame(frame_cache(),name));
}
void park_prompts_install(void) {
    uintptr_t addr=park_symbol("_ZN7cocos2d18CCFileUtilsAndroid11getFileDataEPKcS2_Pm");
    uintptr_t base=addr&~(uintptr_t)1;
    /* Android 1.2.1: push {r4-r7,lr}; sub sp,#196; str r3,[sp,#4];
     * ldr r3,[pc,#488]. Relocate that literal load into a permanent bridge.
     * The original add r3,pc remains at its original address after the jump. */
    static const uint16_t expected[]={0xb5f0,0xb0b1,0x9301,0x4b7a};
    if (!(addr&1) || (base&3) || memcmp((void *)base,expected,sizeof(expected)))
        fatal_error("Unexpected Best Park file reader code");
    uintptr_t bridge=(so_mod.patch_head+3)&~(uintptr_t)3;
    if (bridge<so_mod.patch_base || bridge+20>so_mod.patch_base+so_mod.patch_size)
        fatal_error("No space for Best Park prompt bridge");
    so_mod.patch_head=bridge+20;
    uint32_t words[5]={0xb0b1b5f0,0x4b019301,0xf004f8df,0,(uint32_t)(base+8)|1};
    memcpy(&words[3],(void *)(base+496),4);
    memcpy((void *)bridge,words,sizeof(words));
    kuKernelFlushCaches((void *)bridge,sizeof(words));
    read_file=(void *)(bridge|1);
    hook_addr(addr,(uintptr_t)read_with_prompts);

    sprite_file=(void *)park_symbol("_ZN7cocos2d8CCSprite6createEPKc");
    sprite_frame=(void *)park_symbol("_ZN7cocos2d8CCSprite21createWithSpriteFrameEPNS_13CCSpriteFrameE");
    frame_cache=(void *)park_symbol("_ZN7cocos2d18CCSpriteFrameCache22sharedSpriteFrameCacheEv");
    find_frame=(void *)park_symbol("_ZN7cocos2d18CCSpriteFrameCache17spriteFrameByNameEPKc");
    hook_addr(park_symbol("_ZN7cocos2d8CCSprite25createWithSpriteFrameNameEPKc"),(uintptr_t)sprite_with_prompts);
}
