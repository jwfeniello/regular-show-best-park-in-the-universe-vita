#pragma once
#include <stddef.h>

/* Returns a newly allocated replacement, or NULL to keep the original bytes. */
unsigned char *park_prompts_rewrite(const char *path, const unsigned char *data,
                                   size_t size, size_t *output_size);
const char *park_prompt_sprite(const char *name);
void park_prompts_install(void);
