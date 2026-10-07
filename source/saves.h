#pragma once
#include <stddef.h>
#include <stdbool.h>
void park_saves_init(void);
/* Takes ownership of a complete preferences snapshot. Never waits for disk. */
void park_saves_submit(void *data,size_t size);
bool park_saves_flush(void);
