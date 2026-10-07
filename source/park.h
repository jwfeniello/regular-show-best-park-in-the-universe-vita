#ifndef PARK_H
#define PARK_H
#include <stdbool.h>
#include <stdint.h>
#include <falso_jni/FalsoJNI.h>
#include "reimpl/controls.h"
uintptr_t park_symbol(const char *name);
void park_log_init(void);
void park_java_init(void);
jobject park_renderer(void);
void park_check_data(void);
void park_start(void);
bool park_render(void);
void park_stop(void);
void park_touch(int id, float x, float y, ControlsAction action);
void park_key(int key, ControlsAction action);
void park_java_poll(void);
void park_java_flush(void);
#endif
