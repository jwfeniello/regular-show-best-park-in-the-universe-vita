#include "park.h"
#include "gamepad.h"
#include "prompts.h"
#include "utils/logger.h"
#include "utils/dialog.h"
#include <so_util/so_util.h>
#include <string.h>
#include <stdio.h>
extern so_module so_mod;
/* ARM Linux kuser helpers referenced by this armeabi library's libgcc. Vita
   has no mapped kernel page at 0xffff0xxx. Preserve the helpers' real atomics. */
/* Preserve r1/r2 and the documented carry flag, not merely the C return value.
   https://www.kernel.org/doc/html/latest/arch/arm/kernel_user_helpers.html */
__attribute__((naked)) static int park_cmpxchg(int expected,int desired,volatile int *ptr) {
    __asm__ volatile(
        "dmb ish\n"
        "1: ldrex r3, [r2]\n"
        "cmp r3, r0\n"
        "bne 2f\n"
        "strex ip, r1, [r2]\n"
        "cmp ip, #0\n"
        "bne 1b\n"
        "movs r0, #0\n"
        "dmb ish\n"
        "cmp r0, #0\n" /* Success: C=1. */
        "bx lr\n"
        "2: clrex\n"
        "movs r0, #1\n"
        "dmb ish\n"
        "cmp r0, #2\n" /* Failure: C=0. */
        "bx lr\n");
}
__attribute__((naked)) static void park_memory_barrier(void) {
    __asm__ volatile("dmb ish\nbx lr\n");
}
static void patch_kuser(void) {
    static const uint32_t cas[]={0x41f778,0x41f7b0,0x41f7e8,0x41f820,0x41f858,0x41f894,0x41f8f8,0x41f95c,0x41f9c0,0x41fa24,0x41fa88,0x41faf0,0x41fb4c,0x41fba8,0x41fc04,0x41fc60,0x41fcbc,0x41fd1c,0x41fd54,0x41fd8c,0x41fdc4,0x41fdfc,0x41fe34,0x41fe70,0x41fedc,0x41ff48,0x41ffb4,0x420020,0x42008c,0x4200fc,0x420160,0x4201c4,0x420228,0x42028c,0x4202f0,0x420358,0x42039c,0x420408,0x42046c,0x420498,0x42051c,0x42057c,0x4205d4};
    for(unsigned i=0;i<sizeof(cas)/sizeof(*cas);i++) {
        uint32_t *p=(void *)(so_mod.text_base+cas[i]);
        if(*p!=0xffff0fc0)fatal_error("Unexpected Android atomic-helper code");
        *p=(uintptr_t)park_cmpxchg;
    }
    static const uint32_t barrier[]={0x4204e4,0x4205f8,0x420618,0x420638,0x420658};
    for(unsigned i=0;i<sizeof(barrier)/sizeof(*barrier);i++) {
        uint32_t *p=(void *)(so_mod.text_base+barrier[i]);
        if(*p!=0xffff0fa0)fatal_error("Unexpected Android atomic-helper code");
        *p=(uintptr_t)park_memory_barrier;
    }
    l_info("Replaced ARM Linux atomic and memory-barrier helpers");
}
static void *(*add_image)(void *,const char *);
static void *pvr_image(void *cache,const char *path) {
    /* Every PVR used by this build has a PNG equivalent. Preserve the engine's
       selected pixel format while avoiding the truncated PVR payloads. */
    char png[768];snprintf(png,sizeof(png),"%s",path);
    char *ext=strrchr(png,'.');
    if(ext && !strcmp(ext,".pvr"))memcpy(ext,".png",5);
    l_debug("Texture PNG fallback: %s",png);
    return add_image(cache,png);
}
void so_patch(void) {
    patch_kuser();
    park_gamepad_install();
    park_prompts_install();
    add_image=(void *)park_symbol("_ZN7cocos2d14CCTextureCache8addImageEPKc");
    hook_addr(park_symbol("_ZN7cocos2d14CCTextureCache11addPVRImageEPKc"),(uintptr_t)pvr_image);
    l_info("Texture PNG fallback enabled; online services return unavailable");
}
