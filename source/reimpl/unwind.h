#ifndef PARK_UNWIND_H
#define PARK_UNWIND_H

#include <stdint.h>

uintptr_t park_dl_unwind_find_exidx(uintptr_t pc, int *count);

#endif
