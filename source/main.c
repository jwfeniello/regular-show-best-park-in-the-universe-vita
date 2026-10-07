#include "park.h"
#include "assets.h"
#include "audio.h"
#include "gamepad.h"
#include "utils/init.h"
#include "utils/glutil.h"
#include "utils/logger.h"
#include "utils/dialog.h"
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <so_util/so_util.h>
int _newlib_heap_size_user=256*1024*1024;
int sceLibcHeapSize=4*1024*1024;
/* Reserve headroom for native startup, file decoding and C++ calls. */
const SceSize sceUserMainThreadStackSize=2*1024*1024;
so_module so_mod;
int main(void) {
    park_log_init();
#ifdef PARK_DIAGNOSTICS
    l_info("Best Park in the Universe Vita build 0.8");
    SceKernelThreadInfo thread_info={.size=sizeof(thread_info)};
    if(sceKernelGetThreadInfo(sceKernelGetThreadId(),&thread_info)==0)
        l_info("Main thread stack: %p, %u bytes",thread_info.stack,
               (unsigned)thread_info.stackSize);
#endif
    soloader_init_clocks(); park_check_data(); soloader_init_all();
    park_assets_init(); park_audio_init(); park_java_init();
    jint (*onload)(JavaVM *,void *)=(void *)park_symbol("JNI_OnLoad");
    if(onload(&jvm,NULL)<0) fatal_error("JNI_OnLoad failed.");
    gl_init(); park_start();
#ifdef PARK_DIAGNOSTICS
    unsigned frame=0;
#endif
    for(;;) {
        uint64_t start=sceKernelGetProcessTimeWide();
        park_gamepad_begin_frame(); controls_poll(); park_java_poll();
        if(!park_render()) break;
        gl_swap();
#ifdef PARK_DIAGNOSTICS
        if(frame==0 || frame%300==0) l_info("Rendered frame %u",frame);
        ++frame;
#endif
        uint64_t elapsed=sceKernelGetProcessTimeWide()-start;
        if(elapsed<33333) sceKernelDelayThread(33333-elapsed);
    }
    park_stop(); sceKernelExitProcess(0); return 0;
}
void controls_handler_key(int32_t key,ControlsAction action) { park_key(key,action); }
void controls_handler_touch(int32_t id,float x,float y,ControlsAction action) { park_touch(id,x,y,action); }
void controls_handler_analog(ControlsStickId stick,float x,float y,ControlsAction action) {
    (void)action; park_gamepad_axis(stick,x,y);
}
