#ifndef PARK_GAMEPAD_H
#define PARK_GAMEPAD_H
#include "reimpl/controls.h"
void park_gamepad_install(void);
void park_gamepad_begin_frame(void);
void park_gamepad_key(int key, ControlsAction action);
void park_gamepad_axis(ControlsStickId stick, float x, float y);
#endif
