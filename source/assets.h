#pragma once
#include <stddef.h>
void park_assets_init(void);
void *park_asset_read(const char *name,size_t *size);
